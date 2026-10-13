#pragma once
#include "core/audio.h"
// The sample library on the TF card: mounts the card, reads the files below SD_SAMPLE_DIR into the engine's catalog when they are asked
// for, lists a folder for the browser and runs the sample loader.
void samples_esp32_start();      // after engine_synth_init(): starts the I/O task (core 0, low priority) that mounts the card and pumps the loader
bool samples_esp32_ready(int index);   // the entry can be played (.smp files are always ready once in the catalog)
void samples_esp32_hide(int index);    // core/audio.h audio_sample_hide
// core/audio.h audio_sample_find / _use / audio_dir_open / _read. use and dir_open wait for the I/O task, which owns the card (a header:
// about 20 ms; a folder: about 0.5 ms per file it holds, kept until another folder or dir_close; they fail after 1.5 s / 15 s).
int  samples_esp32_find(const char *name);
int  samples_esp32_use(const char *name);
int  samples_esp32_request(const char *name);   // core/audio.h audio_sample_request: never waits (-3 while the I/O task reads the header)
int  samples_esp32_dir_open(const char *dir);
int  samples_esp32_dir_read(int first, audio_dir_entry_t *out, int max);
void samples_esp32_dir_close();
sd_state_t samples_esp32_sd_state();      // the card: none / usable / inserted but too slow (core/audio.h)
uint32_t samples_esp32_sd_read_us();     // SD_SLOW: the measured time of a sector read
uint32_t samples_esp32_sd_generation();  // +1 on every insertion, removal and when the card's folders were made
