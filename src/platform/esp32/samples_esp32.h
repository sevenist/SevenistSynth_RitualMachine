#pragma once
#include "hal/hal_audio.h"
// The sample library on the TF card: mounts the card, reads the files below SD_SAMPLE_DIR into the engine's catalog when they are asked
// for, lists a folder for the browser and runs the sample loader.
void samples_esp32_start();      // after engine_synth_init(): starts the I/O task (core 0, low priority) that mounts the card and pumps the loader
bool samples_esp32_ready(int index);   // the entry can be played (.smp files are always ready once in the catalog)
void samples_esp32_hide(int index);    // hal_audio.h audio_sample_hide
// hal_audio.h audio_sample_find / _use / audio_dir_open / _read. use and dir_open wait for the I/O task, which owns the card (a header:
// a few ms; a folder of 300 files: about 0.1 s; at most 1.5 s, then they fail).
int  samples_esp32_find(const char *name);
int  samples_esp32_use(const char *name);
int  samples_esp32_dir_open(const char *dir);
int  samples_esp32_dir_read(int first, audio_dir_entry_t *out, int max);
sd_state_t samples_esp32_sd_state();      // the card: none / usable / inserted but too slow (hal_audio.h)
uint32_t samples_esp32_sd_read_us();     // SD_SLOW: the measured time of a sector read
uint32_t samples_esp32_sd_generation();  // +1 on every insertion, removal and when the card's folders were made
