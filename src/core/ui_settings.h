#pragma once
// The UI settings that survive a reboot, in UI_SETTINGS_FILE on the card (next to keys.cfg): the column knobs' mode (Catch / Direct), the
// jump slots and what each column knob drives with Shift. A text file, editable on a PC; lines that are not understood are skipped:
//   # SynthCore UI settings (ui.cfg)
//   knob catch                    catch | direct
//   jump 1 mod 5 type 0 def 1 row 2   a slot on a module's page: module id, module type, which of its pages, row
//   jump 2 global 9 row 1             a slot on a global page (GP_*)
//   jump 3 tab 1 row 4                a slot in the menu: the tab's kind (tab_t), row
//   (the first format, "jump N page P" / "jump N menu T", stored positions: such a line takes what is at that position now)
//   shift 1 cfg spk               Shift + column knob 1..4: a GENERAL setting (its label), or "none"
// The module remembers what was last loaded or saved, so the app can tell when something changed (ui_settings_changed) and save it then.
#include <stdbool.h>
#include "core/rack.h"
#include "core/synth_ui.h"
#include "hal/hal_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SETTINGS_FILE STORAGE_DIR_CONFIG "/ui.cfg"

int  ui_settings_to_text(const synth_ui_t *ui, const rack_t *rack, char *buf, int cap);
bool ui_settings_from_text(synth_ui_t *ui, rack_t *rack, const char *txt);   // false: not a settings file (nothing changed)

// load: from the card (false: no card or no file; then the current state counts as the saved one). save: writes the current state.
bool ui_settings_load(synth_ui_t *ui, rack_t *rack);
bool ui_settings_save(const synth_ui_t *ui, const rack_t *rack);
bool ui_settings_changed(const synth_ui_t *ui, const rack_t *rack);          // differs from what was last loaded / saved

#ifdef __cplusplus
}
#endif
