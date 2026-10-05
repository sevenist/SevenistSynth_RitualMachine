#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "platform/esp32/sd_card.h"
#include "board_pins.h"

#if defined(HWV1) && defined(HWV1_SD_ARDUINO)
// Experiment: the Arduino core's own SPI card driver instead of ESP-IDF's sdspi host, on the same pins, mounted at the same place.
#include <SD.h>
#include <SPI.h>

#ifndef HWV1_SD_FREQ_KHZ
#define HWV1_SD_FREQ_KHZ 20000
#endif

namespace {
SPIClass g_spi(HSPI);
bool g_up = false;
}  // namespace

bool sd_card_mounted() { return g_up; }

bool sd_card_mount() {
    if (g_up) return true;
    g_spi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, g_spi, HWV1_SD_FREQ_KHZ * 1000u, SD_MOUNT_POINT, 8)) { Serial.println("[SD] Arduino SD: no card / mount failed"); return false; }
    g_up = true;
    Serial.printf("[SD] mounted (Arduino SD driver): type %d, %llu MB, SPI %d kHz\n", (int)SD.cardType(), (unsigned long long)(SD.cardSize() / (1024 * 1024)), (int)HWV1_SD_FREQ_KHZ);
    return true;
}

void sd_card_bench() {}
void sd_card_unmount() { if (g_up) { SD.end(); g_spi.end(); g_up = false; } }

#elif defined(HWV1)
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
        const esp_err_t e = spi_bus_initialize(kHost, &bus, SDSPI_DEFAULT_DMA);
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
    heap_caps_free(buf);
}

void sd_card_unmount() {
    if (g_card) { esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_card); g_card = nullptr; }
    if (g_bus) { spi_bus_free(kHost); g_bus = false; }
}

#else   // a revision without a card slot
bool sd_card_mount() { return false; }
void sd_card_bench() {}
void sd_card_unmount() {}
bool sd_card_mounted() { return false; }
#endif  // HWV1
#endif  // ARDUINO_ARCH_ESP32
