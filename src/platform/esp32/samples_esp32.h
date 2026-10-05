#pragma once
#include "hal/hal_audio.h"
// The sample library on the TF card: mounts the card, lists SD_SAMPLE_DIR/*.smp into the engine's catalog and runs the sample loader.
void samples_esp32_start();      // after engine_synth_init(): starts the I/O task (core 0, low priority) that mounts, scans and pumps the loader
int  samples_esp32_rescan();     // asks the I/O task to look at the card again (a card inserted later is mounted then); returns the files known so far
bool samples_esp32_ready(int index);   // the entry can be played (.smp files are always ready once listed)
sd_state_t samples_esp32_sd_state();      // the card: none / usable / inserted but too slow (hal_audio.h)
uint32_t samples_esp32_sd_read_us();     // SD_SLOW: the measured time of a sector read
uint32_t samples_esp32_sd_generation();  // +1 on every insertion, removal and finished scan
