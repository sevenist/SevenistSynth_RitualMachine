#pragma once
// C++ side of the sample library, used by the platform code that owns the storage (desktop folder, TF card).
#include "engine/sampler/storage.h"
#include "hal/hal_audio.h"

// After engine_synth_init(): hands the storage device (must outlive the engine) to the sample bank.
bool engine_synth_attach_storage(sc::StorageDevice *dev);
// The platform's converter for pending entries (.wav / .mp3 -> .smp): called from the control thread, before a rack that uses such a file is built.
// It must update the catalog (engine_synth_catalog_set) and return true when the file is ready.
void engine_synth_set_importer(bool (*convert)(int index));
// Replaces / grows the catalog (the first `n` entries; entries already present must keep their index). (The tests fill it this way.)
void engine_synth_catalog_set(const audio_sample_info_t *infos, int n);
// The catalog holds the files in use, added when a pad, a sampler or the browser asks for one (hal_audio.h audio_sample_use):
//   find    the index of the entry called `name`, -1 when it is not there
//   add     a file (or new facts about the entry of that name); its index: that entry's, else a removed entry's whose sample is
//           unloaded, else a new one; -1 when the catalog is full (AUDIO_SAMPLES_MAX)
//   remove  the entry's name goes (no list shows it, nothing finds it) and its sample is unloaded as soon as nothing reads it
//           (engine_synth_housekeeping); the caller makes sure no pad and no rack module uses the entry any more
int  engine_synth_catalog_find(const char *name);
int  engine_synth_catalog_add(const audio_sample_info_t *info);
void engine_synth_catalog_remove(int index);
