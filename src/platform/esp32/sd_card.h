#pragma once
// The TF card as a FAT volume mounted at SD_MOUNT_POINT. This is the only place that knows how the card is wired: HWV1 uses an SPI
// card (ESP-IDF sdspi host); a prototype with a 4-bit SDMMC slot replaces the body of sd_card_mount() and nothing else.
#define SD_MOUNT_POINT "/sdcard"
#define SD_SAMPLE_DIR  SD_MOUNT_POINT "/system/samples"   // the *.smp files of the sample library (STORAGE_DIR_SAMPLES of hal_storage.h)

bool sd_card_mount();           // idempotent; false when there is no card (or no wiring on this board)
void sd_card_unmount();
bool sd_card_mounted();
uint32_t sd_card_probe_us();    // average time of a few single-sector reads in microseconds (a healthy card: 300-2000); 0 = the reads failed
bool sd_card_alive();           // the mounted card still answers (a status command): false once it is pulled out
uint32_t sd_card_id();          // identifies the card (serial number), to tell "the same card again" from "another card"
uint32_t sd_card_crc_errors();  // data blocks that arrived with a wrong CRC and were read again (0 when the driver does not check)
void sd_card_bench();           // dev (HWV1_SD_BENCH): raw sector reads of 1 and 8 sectors, timed, printed as [SD] bench lines
