#pragma once
// The sample library on the TF card: mounts the card, lists SD_SAMPLE_DIR/*.smp into the engine's catalog and runs the sample loader.
void samples_esp32_start();      // after engine_synth_init(): starts the I/O task (core 0, low priority) that mounts, scans and pumps the loader
int  samples_esp32_rescan();     // asks the I/O task to look at the card again (a card inserted later is mounted then); returns the files known so far
bool samples_esp32_ready(int index);   // the entry can be played (.smp files are always ready once listed)
