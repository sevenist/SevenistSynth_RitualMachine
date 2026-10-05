#pragma once
// The TF card as a FAT volume mounted at SD_MOUNT_POINT. This is the only place that knows how the card is wired: HWV1 uses an SPI
// card (ESP-IDF sdspi host); a prototype with a 4-bit SDMMC slot replaces the body of sd_card_mount() and nothing else.
#define SD_MOUNT_POINT "/sdcard"
#define SD_SAMPLE_DIR  SD_MOUNT_POINT "/samples"      // the *.smp files of the sample library

bool sd_card_mount();           // idempotent; false when there is no card (or no wiring on this board)
void sd_card_unmount();
bool sd_card_mounted();
void sd_card_bench();           // dev (HWV1_SD_BENCH): raw sector reads of 1 and 8 sectors, timed, printed as [SD] bench lines
