// The TF card on the second prototype (boards/esp32/sd_card.h): 4 bits on the chip's SDMMC host (ESP-IDF), pins in board_pins.h. The host does the
// CRCs and retries itself, and its transfers use DMA: the task sleeps through them. Its DMA reaches internal RAM only: the sample streams go through
// SdStorage's bounce buffer as on any board, and the driver handles the other buffers by itself, slowly.
#if defined(ARDUINO_ARCH_ESP32) && defined(HWV2)
#include <Arduino.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include "boards/esp32/board_pins.h"
#include "boards/esp32/sd_card.h"

// The card's clock in kHz. 20 MHz on 4 bits is 10 MB/s at most, 4 times the first prototype's SPI card; 40000 (SDMMC_FREQ_HIGHSPEED) after a test on this wiring.
#ifndef HWV2_SD_FREQ_KHZ
#define HWV2_SD_FREQ_KHZ 20000
#endif

namespace {
constexpr int kMaxOpenFiles = 8;               // FATFS file objects held by the VFS (each owns a 512 byte sector buffer): SdStorage keeps 4 open, the scan 1-2
sdmmc_card_t *g_card = nullptr;                // the volume is mounted
sdmmc_card_t *g_raw = nullptr;                 // the card is set up without a file system (the USB card reader)
bool g_said_none = false;                      // the failed mount was logged: a poll every second must not fill the log

sdmmc_host_t host_config() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = HWV2_SD_FREQ_KHZ;
    return host;
}

sdmmc_slot_config_t slot_config() {
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = static_cast<gpio_num_t>(PIN_SD_CLK);
    slot.cmd = static_cast<gpio_num_t>(PIN_SD_CMD);
    slot.d0 = static_cast<gpio_num_t>(PIN_SD_D0);
    slot.d1 = static_cast<gpio_num_t>(PIN_SD_D1);
    slot.d2 = static_cast<gpio_num_t>(PIN_SD_D2);
    slot.d3 = static_cast<gpio_num_t>(PIN_SD_D3);
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;   // (the board has its own: these do no harm, and a part not soldered yet costs nothing)
    return slot;
}

void raw_end() {
    if (!g_raw) return;
    sdmmc_host_deinit();
    heap_caps_free(g_raw);
    g_raw = nullptr;
}
}  // namespace

bool sd_card_mounted() { return g_card != nullptr; }

bool sd_card_mount() {
    if (g_card) return true;
    raw_end();
    sdmmc_host_t host = host_config();
    sdmmc_slot_config_t slot = slot_config();
    esp_vfs_fat_mount_config_t mc = {};
    mc.format_if_mount_failed = false;         // never erase a user's card
    mc.max_files = kMaxOpenFiles;
    mc.allocation_unit_size = 0;
    const esp_err_t e = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mc, &g_card);
    if (e != ESP_OK) {
        g_card = nullptr;
        if (!g_said_none) Serial.printf("[SD] no card / mount failed: %s\n", esp_err_to_name(e));
        g_said_none = true;
        return false;
    }
    g_said_none = false;
    Serial.printf("[SD] mounted: %s, %llu MB, 4 bits at %d kHz\n", g_card->cid.name, (unsigned long long)g_card->csd.capacity * g_card->csd.sector_size / (1024 * 1024),
                  g_card->real_freq_khz);
    return true;
}

void sd_card_unmount() {
    if (g_card) { esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_card); g_card = nullptr; }
}

// A few single-sector reads, timed: tells a card that can stream (a read takes 0.3-2 ms) from one that cannot. The first read is not counted: it may still carry the mount.
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
uint32_t sd_card_crc_errors() { return 0; }    // the host checks CRCs itself
void sd_card_verify(bool, uint32_t *sectors, uint32_t *bad) { *sectors = 0; *bad = 0; }   // (dev: the first prototype's own driver only)
void sd_card_bench() {}                        // (dev: the same)

bool sd_card_raw_begin() {
    sd_card_unmount();
    raw_end();
    sdmmc_host_t host = host_config();
    sdmmc_slot_config_t slot = slot_config();
    if (sdmmc_host_init() != ESP_OK) return false;
    if (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) { sdmmc_host_deinit(); return false; }
    sdmmc_card_t *card = static_cast<sdmmc_card_t *>(heap_caps_calloc(1, sizeof(sdmmc_card_t), MALLOC_CAP_INTERNAL));
    if (!card) { sdmmc_host_deinit(); return false; }
    if (sdmmc_card_init(&host, card) != ESP_OK) {
        sdmmc_host_deinit();
        heap_caps_free(card);
        return false;
    }
    g_raw = card;
    return true;
}

uint32_t sd_card_raw_sectors() { return g_raw ? static_cast<uint32_t>(g_raw->csd.capacity) : 0; }
bool sd_card_raw_read(uint32_t sector, uint8_t *dst, uint32_t count) { return g_raw && sdmmc_read_sectors(g_raw, dst, sector, count) == ESP_OK; }
bool sd_card_raw_write(uint32_t sector, const uint8_t *src, uint32_t count) { return g_raw && sdmmc_write_sectors(g_raw, src, sector, count) == ESP_OK; }
#endif
