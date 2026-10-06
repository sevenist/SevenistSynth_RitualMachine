#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "platform/esp32/sd_card.h"
#include "board_pins.h"

#if defined(HWV1) && defined(HWV1_SD_IDF)
// ESP-IDF's sdspi host, kept for comparison (build flag HWV1_SD_IDF). On the first prototype it spends about 40 ms before EVERY command (a CMD13 status
// takes 40 ms, while the pins show the 55 us transfer only at the end), so the default is the own driver in sd_card_spi.cpp.
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

// SPI clock of the card in kHz. 20 MHz is the SPI-mode limit of a default-speed card on a prototype's wiring; raise it (up to 40000) after a test.
#ifndef HWV1_SD_FREQ_KHZ
#define HWV1_SD_FREQ_KHZ 20000
#endif

namespace {
constexpr spi_host_device_t kHost = SPI3_HOST;      // SPI2 is the Arduino default bus, left free
constexpr spi_dma_chan_t kDma = SPI_DMA_CH_AUTO;
constexpr int kMaxOpenFiles = 8;                    // FATFS file objects held by the VFS (each owns a 512 byte sector buffer): SdStorage keeps 4 open, the scan 1-2
sdmmc_card_t *g_card = nullptr;
bool g_bus = false;
}  // namespace

bool sd_card_mounted() { return g_card != nullptr; }

bool sd_card_mount() {
    if (g_card) return true;
    if (!g_bus) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = PIN_SD_MOSI;
        bus.miso_io_num = PIN_SD_MISO;
        bus.sclk_io_num = PIN_SD_SCLK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 16 * 1024;
        const esp_err_t e = spi_bus_initialize(kHost, &bus, kDma);
        if (e != ESP_OK) { Serial.printf("[SD] spi_bus_initialize failed: %s\n", esp_err_to_name(e)); return false; }
        g_bus = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = kHost;
    host.max_freq_khz = HWV1_SD_FREQ_KHZ;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = static_cast<gpio_num_t>(PIN_SD_CS);
    slot.host_id = kHost;
    esp_vfs_fat_mount_config_t mc = {};
    mc.format_if_mount_failed = false;                // never erase a user's card
    mc.max_files = kMaxOpenFiles;
    mc.allocation_unit_size = 0;
    const esp_err_t e = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mc, &g_card);
    if (e != ESP_OK) {
        g_card = nullptr;
        Serial.printf("[SD] no card / mount failed: %s\n", esp_err_to_name(e));
        return false;
    }
    Serial.printf("[SD] mounted: %s, %llu MB, SPI %d kHz\n", g_card->cid.name, (unsigned long long)g_card->csd.capacity * g_card->csd.sector_size / (1024 * 1024),
                  (int)HWV1_SD_FREQ_KHZ);
    return true;
}

// Raw reads below the filesystem: tells the card (and the SPI link) apart from FATFS and the access pattern.
void sd_card_bench() {
    if (!g_card) return;
    uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(32 * 512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!buf) return;
    Serial.printf("[SD] bench card: sector size %d, capacity %u sectors, max freq %d kHz, real freq %d kHz\n", g_card->csd.sector_size, (unsigned)g_card->csd.capacity, g_card->max_freq_khz, g_card->real_freq_khz);
    for (int n : {1, 8, 32}) {                           // contiguous reads of n sectors: the first few timings one by one, then the average
        char line[128];
        int len = snprintf(line, sizeof line, "[SD] bench raw %2d sector(s):", n);
        uint32_t total = 0;
        int fails = 0;
        const int reps = n == 1 ? 8 : 4;
        for (int i = 0; i < reps; i++) {
            const int64_t a = esp_timer_get_time();
            if (sdmmc_read_sectors(g_card, buf, 20000 + i * n, n) != ESP_OK) fails++;
            const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - a);
            total += us;
            if (len < (int)sizeof line - 12) len += snprintf(line + len, sizeof line - len, " %u", (unsigned)us);
        }
        Serial.printf("%s us (avg %u us, %d failed)\n", line, (unsigned)(total / reps), fails);
    }
    constexpr uint32_t kReadBlockSingle = 17, kOcrSdhc = 1u << 30, kCmdTimeoutMs = 1000;     // (sdmmc_defs.h is a private header)
    // below the helper: the same single-sector read as one host transaction (what sdmmc_read_sectors sends), and a plain status command
    for (int rep = 0; rep < 3; rep++) {
        sdmmc_command_t cmd = {};
        cmd.opcode = kReadBlockSingle;
        cmd.arg = (g_card->ocr & kOcrSdhc) ? 20000u + rep : (20000u + rep) * 512u;
        cmd.flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1;
        cmd.data = buf;
        cmd.datalen = 512;
        cmd.blklen = 512;
        cmd.timeout_ms = kCmdTimeoutMs;
        const int64_t a = esp_timer_get_time();
        const esp_err_t e1 = g_card->host.do_transaction(g_card->host.slot, &cmd);
        const uint32_t t_read = static_cast<uint32_t>(esp_timer_get_time() - a);
        const int64_t b = esp_timer_get_time();
        const esp_err_t e2 = sdmmc_get_status(g_card);
        const uint32_t t_stat = static_cast<uint32_t>(esp_timer_get_time() - b);
        Serial.printf("[SD] bench direct: CMD17 %u us (%s), CMD13 %u us (%s)\n", (unsigned)t_read, esp_err_to_name(e1), (unsigned)t_stat, esp_err_to_name(e2));
    }
    heap_caps_free(buf);
}

// A few single-sector reads, timed: tells a card that can stream (a read takes 0.3-2 ms) from one that cannot (the first prototype's old 1 GB card
// takes 81 ms per read command whatever the size, 50 KB/s at 4 KB per read). The first read is not counted: it may still carry the mount.
uint32_t sd_card_probe_us() {
    if (!g_card) return 0;
    uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!buf) return 0;
    uint32_t total = 0;
    bool ok = sdmmc_read_sectors(g_card, buf, 2048, 1) == ESP_OK;
    for (int i = 1; i <= 3 && ok; i++) {
        const int64_t t0 = esp_timer_get_time();
        ok = sdmmc_read_sectors(g_card, buf, 2048 + 64 * i, 1) == ESP_OK;
        total += static_cast<uint32_t>(esp_timer_get_time() - t0);
    }
    heap_caps_free(buf);
    return ok ? total / 3 : 0;
}

bool sd_card_alive() { return g_card && sdmmc_get_status(g_card) == ESP_OK; }

uint32_t sd_card_id() { return g_card ? g_card->cid.serial : 0; }
uint32_t sd_card_crc_errors() { return 0; }                     // the IDF host checks CRCs itself

void sd_card_unmount() {
    if (g_card) { esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_card); g_card = nullptr; }
    if (g_bus) { spi_bus_free(kHost); g_bus = false; }
}

#elif !defined(HWV1)   // a revision without a card slot
bool sd_card_mount() { return false; }
void sd_card_bench() {}
uint32_t sd_card_probe_us() { return 0; }
bool sd_card_alive() { return false; }
uint32_t sd_card_id() { return 0; }
uint32_t sd_card_crc_errors() { return 0; }
void sd_card_unmount() {}
bool sd_card_mounted() { return false; }
#endif  // HWV1
#endif  // ARDUINO_ARCH_ESP32
