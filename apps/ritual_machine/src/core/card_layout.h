#pragma once
// The Ritual Machine's TF card: its folders and the one buffer its settings files go through. The card itself (small files, card
// events, making folders) is the library's hal/hal_storage.h, which knows no layout: app_init hands it this list.
// The simulator's card is the folder sdcard/ (next to samples/; F12 pulls it out and puts it back).
#include "hal/hal_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

// The one text buffer of the settings files (core/storage_buf.c): keys.cfg, curves.cfg, ui.cfg, pads.cfg and history.cfg are each read /
// written through it, one at a time, on the UI thread (each had its own 4 KB before: 20 KB of internal RAM). Never keep a pointer into it
// across another load / save.
extern char g_storage_buf[STORAGE_FILE_MAX];

// The folders of a Ritual Machine card, relative to its root. Parents come before their children (they are created in this order).
#define STORAGE_DIR_SAMPLES "system/samples"         // the sample library (*.smp)
#define STORAGE_DIR_CONFIG  "system/config"          // settings files (keys.cfg ...)
#define STORAGE_DIR_IMPORT  "import"                 // .wav / .mp3 dropped here to be imported
#define STORAGE_DIR_PRESETS "presets"                // racks and patches
#define STORAGE_FOLDERS     {"system", STORAGE_DIR_SAMPLES, STORAGE_DIR_CONFIG, STORAGE_DIR_IMPORT, STORAGE_DIR_PRESETS}
#define STORAGE_FOLDER_COUNT 5

// Hands the list above to the storage HAL (storage_set_folders). Called once, before the card is first looked at.
void card_layout_apply(void);

#ifdef __cplusplus
}
#endif
