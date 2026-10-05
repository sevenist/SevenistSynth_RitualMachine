// The TF card of the first prototype (HWV1): a small SD driver in SPI mode, read-only, registered with FATFS at SD_MOUNT_POINT.
//
// Why not ESP-IDF's sdspi host: on this board it spends about 40 ms before EVERY command (a plain CMD13 status takes 40 ms, with both pins
// quiet until the 55 us transfer at the very end; same for two different cards, both SPI hosts, any clock), which made a 4 KB read cost
// 80 ms. A bare SPI transfer on the same bus takes the time the clock says, so this driver uses only the SPI master layer and does the
// SD protocol itself: initialisation (CMD0 / CMD8 / ACMD41 / CMD58), CMD17 / CMD18 reads, CMD9 / CMD10 for capacity and identity.
// (The IDF host stays available with -DHWV1_SD_IDF, see sd_card.cpp.)
//
// Everything runs on the caller's task (the sample I/O task), polling transfers, no locks. Loops that can wait on a slow card give the
// CPU away after a millisecond, so the idle task keeps its watchdog fed.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV1) && !defined(HWV1_SD_IDF)
#include <Arduino.h>
#include <diskio_impl.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <string.h>
#include "board_pins.h"
#include "platform/esp32/sd_card.h"

// SPI clock of the card in kHz once it is initialised (initialisation itself runs at 400 kHz, as the SD spec requires).
#ifndef HWV1_SD_FREQ_KHZ
#define HWV1_SD_FREQ_KHZ 20000
#endif

namespace {
constexpr spi_host_device_t kHost = SPI3_HOST;      // SPI2 is the Arduino default bus, left free
constexpr int kMaxOpenFiles = 8;                    // FATFS file objects held by the VFS (each owns a 512 byte sector buffer)
constexpr uint32_t kSector = 512;

spi_device_handle_t g_dev = nullptr;
bool g_bus = false, g_up = false, g_sdhc = false;
uint32_t g_sectors = 0, g_serial = 0;
char g_name[8] = {};
uint8_t g_pdrv = 0xFF;
FATFS *g_fs = nullptr;

// Transfer buffers in internal RAM (DMA capable): the answer / data of a block, and the 0xFF the host clocks out while the card talks.
alignas(4) uint8_t g_rx[kSector + 4];
alignas(4) uint8_t g_ff[kSector + 4];

int64_t now_us() { return esp_timer_get_time(); }

bool xfer(const uint8_t *tx, uint8_t *rx, size_t n) {
    spi_transaction_t t = {};
    t.length = n * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(g_dev, &t) == ESP_OK;
}

uint8_t xfer_byte(uint8_t out = 0xFF) {
    uint8_t in = 0xFF;
    xfer(&out, &in, 1);
    return in;
}

void cs(bool selected) { gpio_set_level(static_cast<gpio_num_t>(PIN_SD_CS), selected ? 0 : 1); }

// Ends a transaction: CS high, then 8 more clocks so the card releases the data line.
void deselect() { cs(false); xfer_byte(); }

bool add_device(int khz) {
    spi_device_interface_config_t dc = {};
    dc.clock_speed_hz = khz * 1000;
    dc.mode = 0;
    dc.spics_io_num = -1;                           // chip select by hand: it stays low over a whole command + data
    dc.queue_size = 1;
    return spi_bus_add_device(kHost, &dc, &g_dev) == ESP_OK;
}

// The card pulls its data line low while it is busy: wait until it reads 0xFF. Gives the CPU away after the first millisecond.
bool wait_ready(int timeout_ms) {
    const int64_t t0 = now_us();
    for (;;) {
        if (xfer_byte() == 0xFF) return true;
        const int64_t dt = now_us() - t0;
        if (dt > timeout_ms * 1000) return false;
        if (dt > 1000) vTaskDelay(1);
    }
}

// Sends command `idx` and returns its R1 response (0xFF = no answer). The CS stays low; the caller reads the rest and calls deselect().
uint8_t command(uint8_t idx, uint32_t arg, uint8_t crc, bool check_busy = true) {
    if (check_busy && !wait_ready(500)) return 0xFF;
    const uint8_t frame[6] = {static_cast<uint8_t>(0x40 | idx), static_cast<uint8_t>(arg >> 24), static_cast<uint8_t>(arg >> 16), static_cast<uint8_t>(arg >> 8),
                              static_cast<uint8_t>(arg), crc};
    uint8_t ignore[6];
    xfer(frame, ignore, 6);
    for (int i = 0; i < 16; i++) {                  // the answer comes within 8 bytes: the first one with the top bit clear
        const uint8_t r = xfer_byte();
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

uint8_t app_command(uint8_t idx, uint32_t arg) {    // ACMD = CMD55 + CMD
    cs(true);
    const uint8_t r = command(55, 0, 0x65);
    if (r > 1) { deselect(); return r; }
    const uint8_t r2 = command(idx, arg, 0x77);
    return r2;
}

// Another block of a multi-block read: the card normally sends the next token right behind the previous block, so the token, the data and the
// CRC are clocked in one transfer (n + 3 bytes). The token can sit anywhere in it (a slow card answers late), so it is looked up; whatever of the
// block did not fit is fetched with a second transfer. Everything before the token is 0xFF filler, so a transfer that holds no token loses nothing.
// Returns 1 = done, 0 = failed, -1 = no token yet (try again, or poll).
int read_next_block(uint8_t *dst, size_t n) {
    if (!xfer(g_ff, g_rx, n + 3)) return 0;
    size_t k = 0;
    while (k < n + 3 && g_rx[k] == 0xFF) k++;
    if (k == n + 3) return -1;
    if (g_rx[k] != 0xFE) return 0;
    const size_t have = n + 2 - k;                   // bytes of data + CRC that came with the token
    const size_t first = have < n ? have : n;
    memcpy(dst, g_rx + k + 1, first);
    if (have < n + 2) {                              // the rest: (n - first) data bytes and the CRC bytes that are still missing
        const size_t more = n + 2 - have;
        if (!xfer(g_ff, g_rx, more)) return 0;
        if (first < n) memcpy(dst + first, g_rx, n - first);
    }
    return 1;
}

// A data block that follows a command: wait for the start token 0xFE (the card may take a while on its first access), then `n` bytes + CRC.
bool read_block(uint8_t *dst, size_t n, bool first = true) {
    if (!first) {
        const int r = read_next_block(dst, n);
        if (r >= 0) return r == 1;
    }
    const int64_t t0 = now_us();
    uint8_t token;
    for (;;) {
        token = xfer_byte();
        if (token != 0xFF) break;
        const int64_t dt = now_us() - t0;
        if (dt > 400 * 1000) return false;
        if (dt > 1000) vTaskDelay(1);
    }
    if (token != 0xFE) return false;
    if (!xfer(g_ff, g_rx, n + 2)) return false;      // data + 2 CRC bytes (not checked)
    memcpy(dst, g_rx, n);
    return true;
}

// 16-byte register (CSD / CID) through CMD9 / CMD10.
bool read_register(uint8_t idx, uint8_t out[16]) {
    cs(true);
    const uint8_t r = command(idx, 0, 0xFF);
    const bool ok = r == 0x00 && read_block(out, 16);
    deselect();
    return ok;
}

bool read_sectors(uint32_t sector, uint8_t *dst, uint32_t count) {
    if (!g_up || count == 0) return false;
    const uint32_t addr = g_sdhc ? sector : sector * kSector;
    cs(true);
    bool ok = command(count == 1 ? 17 : 18, addr, 0xFF) == 0x00;
    for (uint32_t i = 0; ok && i < count; i++) ok = read_block(dst + i * kSector, kSector, i == 0);
    if (count > 1) {                                // stop the transfer (CMD12: one stuff byte, then busy until done)
        command(12, 0, 0xFF, false);
        xfer_byte();
    }
    deselect();
    return ok;
}

/* ---------------- FATFS glue (read only) ---------------- */

DSTATUS ff_status(BYTE) { return 0; }
DSTATUS ff_init(BYTE) { return 0; }
DRESULT ff_read(BYTE, BYTE *buff, LBA_t sector, UINT count) {
    uint32_t done = 0;
    while (done < count) {                           // at most 16 sectors (8 KB) per command: the card keeps the data line busy meanwhile
        const uint32_t n = count - done > 16 ? 16 : count - done;
        if (!read_sectors(static_cast<uint32_t>(sector) + done, buff + done * kSector, n)) return RES_ERROR;
        done += n;
    }
    return RES_OK;
}
DRESULT ff_write(BYTE, const BYTE *, LBA_t, UINT) { return RES_WRPRT; }
DRESULT ff_ioctl(BYTE, BYTE cmd, void *buf) {
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *static_cast<LBA_t *>(buf) = g_sectors; return RES_OK;
    case GET_SECTOR_SIZE: *static_cast<WORD *>(buf) = kSector; return RES_OK;
    case GET_BLOCK_SIZE: *static_cast<DWORD *>(buf) = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

/* ---------------- card initialisation ---------------- */

bool init_card() {
    uint8_t reg[16];
    cs(false);
    for (int i = 0; i < 10; i++) xfer_byte();       // 80 clocks with CS high: the card wakes up in SPI-mode capable state
    cs(true);
    int idle = 0;
    for (int i = 0; i < 10 && idle != 1; i++) { idle = command(0, 0, 0x95, false); if (idle != 1) { deselect(); cs(true); } }   // CMD0: into idle (R1 = 0x01)
    deselect();
    if (idle != 1) return false;                     // no card (nothing answers)

    cs(true);
    bool v2 = false;
    const uint8_t r8 = command(8, 0x1AA, 0x87);      // CMD8: voltage check (cards from 2006 on)
    if (r8 == 1) {
        uint8_t r7[4];
        for (auto &b : r7) b = xfer_byte();
        v2 = r7[2] == 0x01 && r7[3] == 0xAA;
    }
    deselect();

    const int64_t t0 = now_us();                     // ACMD41 until the card leaves idle (up to a second)
    uint8_t r = 0xFF;
    for (;;) {
        r = app_command(41, v2 ? 0x40000000u : 0);
        deselect();
        if (r == 0x00) break;
        if (now_us() - t0 > 1500 * 1000) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    g_sdhc = false;
    if (v2) {                                        // CMD58: OCR, bit 30 = block addressing (SDHC / SDXC)
        cs(true);
        if (command(58, 0, 0xFF) == 0x00) {
            uint8_t ocr[4];
            for (auto &b : ocr) b = xfer_byte();
            g_sdhc = (ocr[0] & 0x40) != 0;
        }
        deselect();
    }
    if (!g_sdhc) {                                   // byte-addressed card: sectors of 512 bytes
        cs(true);
        const uint8_t r16 = command(16, kSector, 0xFF);
        deselect();
        if (r16 != 0x00) return false;
    }

    // the card is ready: now the real clock
    spi_bus_remove_device(g_dev);
    g_dev = nullptr;
    if (!add_device(HWV1_SD_FREQ_KHZ)) return false;

    if (!read_register(9, reg)) return false;        // CSD: capacity
    if ((reg[0] >> 6) == 1) {
        g_sectors = ((static_cast<uint32_t>(reg[7] & 0x3F) << 16) | (static_cast<uint32_t>(reg[8]) << 8) | reg[9]) * 1024u + 1024u;
    } else {
        const uint32_t read_bl_len = reg[5] & 0x0F;
        const uint32_t c_size = (static_cast<uint32_t>(reg[6] & 3) << 10) | (static_cast<uint32_t>(reg[7]) << 2) | (reg[8] >> 6);
        const uint32_t c_mult = ((reg[9] & 3) << 1) | (reg[10] >> 7);
        g_sectors = ((c_size + 1) << (c_mult + 2)) << read_bl_len >> 9;
    }
    if (!read_register(10, reg)) return false;       // CID: name and serial number
    memcpy(g_name, reg + 3, 5);
    g_name[5] = 0;
    g_serial = (static_cast<uint32_t>(reg[9]) << 24) | (static_cast<uint32_t>(reg[10]) << 16) | (static_cast<uint32_t>(reg[11]) << 8) | reg[12];
    return true;
}
}  // namespace

bool sd_card_mounted() { return g_up; }

bool sd_card_mount() {
    if (g_up) return true;
    if (!g_bus) {
        memset(g_ff, 0xFF, sizeof g_ff);
        spi_bus_config_t bus = {};
        bus.mosi_io_num = PIN_SD_MOSI;
        bus.miso_io_num = PIN_SD_MISO;
        bus.sclk_io_num = PIN_SD_SCLK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 1024;
        const esp_err_t e = spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO);
        if (e != ESP_OK) { Serial.printf("[SD] spi_bus_initialize failed: %s\n", esp_err_to_name(e)); return false; }
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << PIN_SD_CS;
        cfg.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cfg);
        cs(false);
        g_bus = true;
    }
    if (g_dev) { spi_bus_remove_device(g_dev); g_dev = nullptr; }
    if (!add_device(400)) return false;
    g_up = true;                                     // (read_sectors needs it while the card is being set up)
    if (!init_card()) {
        g_up = false;
        if (g_dev) { spi_bus_remove_device(g_dev); g_dev = nullptr; }
        return false;                                // no card: quiet, the caller polls again
    }

    ff_diskio_impl_t impl = {};
    impl.init = &ff_init;
    impl.status = &ff_status;
    impl.read = &ff_read;
    impl.write = &ff_write;
    impl.ioctl = &ff_ioctl;
    if (ff_diskio_get_drive(&g_pdrv) != ESP_OK) { Serial.println("[SD] no free FATFS drive"); sd_card_unmount(); return false; }
    ff_diskio_register(g_pdrv, &impl);
    char drv[3] = {static_cast<char>('0' + g_pdrv), ':', 0};
    if (esp_vfs_fat_register(SD_MOUNT_POINT, drv, kMaxOpenFiles, &g_fs) != ESP_OK) { Serial.println("[SD] esp_vfs_fat_register failed"); sd_card_unmount(); return false; }
    if (f_mount(g_fs, drv, 1) != FR_OK) { Serial.println("[SD] no FAT file system on the card"); sd_card_unmount(); return false; }
    Serial.printf("[SD] mounted: %s, %u MB, SPI %d kHz (own driver, %s)\n", g_name, (unsigned)(g_sectors / 2048), (int)HWV1_SD_FREQ_KHZ, g_sdhc ? "SDHC" : "SDSC");
    return true;
}

void sd_card_unmount() {
    if (g_pdrv != 0xFF) {
        char drv[3] = {static_cast<char>('0' + g_pdrv), ':', 0};
        if (g_fs) { f_mount(nullptr, drv, 0); esp_vfs_fat_unregister_path(SD_MOUNT_POINT); g_fs = nullptr; }
        ff_diskio_unregister(g_pdrv);
        g_pdrv = 0xFF;
    }
    g_up = false;
    if (g_dev) { spi_bus_remove_device(g_dev); g_dev = nullptr; }
}

// A few single-sector reads, timed: tells a card that can stream (a read takes 0.3-2 ms) from one that cannot. The first read is not counted.
uint32_t sd_card_probe_us() {
    if (!g_up) return 0;
    static uint8_t buf[kSector];
    bool ok = read_sectors(2048, buf, 1);
    uint32_t total = 0;
    for (int i = 1; i <= 3 && ok; i++) {
        const int64_t t0 = now_us();
        ok = read_sectors(2048 + 64 * i, buf, 1);
        total += static_cast<uint32_t>(now_us() - t0);
    }
    return ok ? total / 3 : 0;
}

// The card still answers, and is the same one: its CID (a 16-byte register) reads back with the serial number seen at mount.
bool sd_card_alive() {
    uint8_t reg[16];
    if (!g_up || !read_register(10, reg)) return false;
    const uint32_t serial = (static_cast<uint32_t>(reg[9]) << 24) | (static_cast<uint32_t>(reg[10]) << 16) | (static_cast<uint32_t>(reg[11]) << 8) | reg[12];
    return serial == g_serial;
}

uint32_t sd_card_id() { return g_up ? g_serial : 0; }

// Raw reads below the filesystem, one command each: the cost of a command and of a sector (dev, HWV1_SD_BENCH).
void sd_card_bench() {
    if (!g_up) return;
    static uint8_t buf[32 * kSector];
    Serial.printf("[SD] bench card: %s, %u sectors, %s, SPI %d kHz\n", g_name, (unsigned)g_sectors, g_sdhc ? "SDHC" : "SDSC", (int)HWV1_SD_FREQ_KHZ);
    for (uint32_t n : {1u, 8u, 16u}) {
        uint32_t total = 0, worst = 0;
        int fails = 0;
        for (int i = 0; i < 8; i++) {
            const int64_t t0 = now_us();
            if (!read_sectors(20000 + i * n, buf, n)) fails++;
            const uint32_t us = static_cast<uint32_t>(now_us() - t0);
            total += us;
            if (us > worst) worst = us;
        }
        Serial.printf("[SD] bench raw %2u sector(s) per command: avg %u us, worst %u us, %d failed of 8\n", (unsigned)n, (unsigned)(total / 8), (unsigned)worst, fails);
    }
}
#endif  // ARDUINO_ARCH_ESP32 && HWV1 && !HWV1_SD_IDF
