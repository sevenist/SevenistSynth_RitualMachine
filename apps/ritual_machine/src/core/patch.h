#pragma once
// A patch as text: the sound of the synth with the macros and the joystick's bindings, which point at its modules.
//
// What it holds: the rack (modules, their values, rows, Para, targets, the motion sequences), the MIX, the synth type and its voice
// settings (voices, glide, legato), the amp envelope and the other global sound values, the Strings settings, the FM patch being
// edited (FM type only), a sampler's file by NAME (the catalog index changes from one start to the next), the macros and the joystick's
// X / Y bindings. What it does not: the machine's settings (volume, output, speaker, knob mode, key layout, LED colours, the Shift /
// Mod layers: ui.cfg and keys.cfg).
//
// Two kinds of file use it:
//   the working state   system/config/synth.cfg: what the machine plays, written by itself a moment after a change and read at
//                       power-on, so the synth comes back as it was. It also holds the sequencer (pattern, length, tempo...) and
//                       which preset the sound came from.
//   a preset            presets/<name>.preset (sub-folders allowed): written by Save / Save as, read by Load. No sequencer.
//
// Lines (one thing per line, numbers as %g; unknown lines are skipped, a missing line keeps its default):
//   patch 1
//   from <edited 0|1> <preset>               state only: the preset the sound came from, and whether it was changed since
//   type T voices V strvoices S glide G legato L fmpatch F
//   mix <level 1> <pan 1> <level 2> <pan 2>
//   par <wave> <pw> <volume> <filter type> <cutoff> <resonance> <filter env amount>
//   amp  A D S R Hold ACurve DCurve RCurve          fenv ... (the same)
//   str <wave> <osc> <lp> <ftype> <detune> <mix> <pw> <level> <lpcut> <lpenv> <lpkey> <fcut> <fres>
//   fm <algorithm> <feedback> <release ms> <name>      op K <level> <coarse> <fine> <fixed Hz> <4 levels> <4 times ms>
//   mod <id> <CODE> <row> <para> <penv> <target id> <target param> : <its values>
//   smp <id> <sample name>                   the file of sampler <id>
//   ms <pool> <lane> <target id> <target param> <linear> <bipolar> <depth> <active steps, hex> : <16 values>
//   macro K <mapping>                        joy x|y <mapping>            (core/modifiers.h mapping_to_text)
// (The sequencer is not in it: its patterns have their own files, core/seq.h.)
#include <stdbool.h>
#include <stdint.h>
#include "core/rack.h"
#include "core/seq.h"
#include "core/synth_params.h"
#include "core/synth_ui.h"
#include "hal/hal_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PATCH_STATE_FILE STORAGE_DIR_CONFIG "/synth.cfg"
#define PRESET_EXT       ".preset"
#define PRESET_NAME_MAX  PAD_NAME_MAX            // a preset's name with its 0: its path below presets/, without the extension

// The text of the sound in `rack`, `params` and `ui` (its macros and joystick bindings). `state`: a working state, with its "from"
// line (`preset`, `edited`); else a preset. Returns the length, or -1 when it does not fit in `cap` (nothing usable is in buf then).
int patch_to_text(const rack_t *rack, const synth_params_t *params, const synth_ui_t *ui, bool state, const char *preset,
                  bool edited, char *buf, int cap);

// Reads such a text into the same places. False when it is not a patch: nothing is changed then. The machine's settings in rack->cfg
// are kept. preset NULL: the "from" line is skipped. A sampler's file is asked from the library
// by name (the card may be read: audio_sample_use). The caller rebuilds the UI's pages and the audio afterwards.
bool patch_from_text(rack_t *rack, synth_params_t *params, synth_ui_t *ui, char *preset, bool *edited, const char *txt);

// A number that changes when anything a patch text holds changes. It is of
// the values in memory: a sound read back from its text may differ in the last digits (the text rounds), so it is not kept in files.
uint32_t patch_hash(const rack_t *rack, const synth_params_t *params, const synth_ui_t *ui);

// The same through a file on the card (the shared settings buffer). save: false when there is no card, the write failed or the text
// does not fit (*too_big says which, when given).
bool patch_save(const char *file, const rack_t *rack, const synth_params_t *params, const synth_ui_t *ui, bool state, const char *preset,
                bool edited, bool *too_big);
bool patch_load(const char *file, rack_t *rack, synth_params_t *params, synth_ui_t *ui, char *preset, bool *edited);

// "presets/<name>.preset". False when it does not fit in n.
bool preset_file(const char *name, char *out, int n);

#ifdef __cplusplus
}
#endif
