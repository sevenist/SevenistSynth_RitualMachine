#pragma once
// C++ side of the sample library, used by the platform code that owns the storage (desktop folder, TF card).
#include "engine/sampler/storage.h"
#include "hal/hal_audio.h"

// After engine_synth_init(): hands the storage device (must outlive the engine) to the sample bank.
bool engine_synth_attach_storage(sc::StorageDevice *dev);
// The platform's converter for pending entries (.wav / .mp3 -> .smp): called from the control thread, before a rack that uses such a file is built.
// It must update the catalog (engine_synth_catalog_set) and return true when the file is ready.
void engine_synth_set_importer(bool (*convert)(int index));
// Replaces / grows the catalog (the first `n` entries; entries already present must keep their index).
void engine_synth_catalog_set(const audio_sample_info_t *infos, int n);
