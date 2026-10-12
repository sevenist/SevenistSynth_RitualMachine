// The sound's working state and the presets (core/patch.h, app/app_preset.c, scr_preset.c): the text of a patch, the sound that comes
// back after a restart, Save as / Save / Load / Init in the PRESETS tab, the browser that tries a preset before it is kept.
#include "ui_test.h"
#include "core/patch.h"

#define MENU CTL_BTN_1
#define BACK CTL_BTN_2

extern int ui_stub_builds;                  // tools/ui_stubs.c: audio_build calls
static int ui_stub_builds_count(void) { return ui_stub_builds; }

static uint32_t sound(void) { return patch_hash(&ui_app.rack, &ui_app.params, &ui_app.ui); }

// The menu on the PRESETS tab, then a push on its row `row` (1 Load, 2 Save, 3 Save as, 4 Init).
static void preset_button(int row) {
    if (!ui_app.ui.in_rack) ev_tap(MENU);
    ui_app.ui.menu_tab = tab_index_of(&ui_app.rack, TAB_PRESET);
    ui_app.ui.row = row;
    ev_tap(CTL_JOY_SW);
}

static int first_of(module_type_t t) {
    for (int i = 0; i < ui_app.rack.count; i++) if (ui_app.rack.slot[i].type == t) return i;
    return -1;
}

TEST(patch_text_round_trip_keeps_the_sound_and_the_machine_settings) {
    ui_fresh();
    const int fl = first_of(MOD_FILTER);
    CHECK(fl >= 0);
    if (fl < 0) return;
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 1234.0f;
    ui_app.rack.br_pan[1] = -40.0f;
    ui_app.rack.cfg.voices = 3;
    ui_app.params.amp_env.release_ms = 750.0f;
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    synth_ui_macro_add(&ui_app.ui, 2, &(macro_t){MACRO_MODULE, ui_app.rack.slot[fl].id, MP_FL_RES});
    const uint32_t h = sound();
    const int count = ui_app.rack.count, id = ui_app.rack.slot[fl].id;
    static char txt[STORAGE_FILE_MAX];
    const int n = patch_to_text(&ui_app.rack, &ui_app.params, &ui_app.ui, false, NULL, false, txt, sizeof txt);
    CHECK(n > 0 && n < 2000);                                            // the start sound: well inside a settings file
    CHECK(strstr(txt, "patch 1\n") != NULL);
    CHECK(strstr(txt, "seq ") == NULL && strstr(txt, "from ") == NULL);  // a preset: no sequencer, no "from"
    rack_init(&ui_app.rack);                                             // another sound, another volume: the text brings the first back
    synth_params_default(&ui_app.params);
    memset(ui_app.ui.macro, 0, sizeof ui_app.ui.macro);
    ui_app.rack.cfg.volume = 1.5f;
    ui_app.rack.cfg.knob_mode = 1;
    CHECK(sound() != h);
    CHECK(patch_from_text(&ui_app.rack, &ui_app.params, &ui_app.ui, NULL, NULL, txt));
    CHECK_EQ(sound(), h);
    CHECK_EQ(ui_app.rack.count, count);
    CHECK_EQ(ui_app.rack.slot[fl].id, id);
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 1234);
    CHECK_EQ(ui_app.ui.macro[2].n, 1);
    CHECK_EQ((int)(ui_app.rack.cfg.volume * 10), 15);                    // the machine's settings are not the patch's
    CHECK_EQ(ui_app.rack.cfg.knob_mode, 1);
    CHECK(!patch_from_text(&ui_app.rack, &ui_app.params, &ui_app.ui, NULL, NULL, "knob catch\nvol 1\n"));   // not a patch: nothing changes
    CHECK_EQ(sound(), h);
    CHECK_EQ(patch_to_text(&ui_app.rack, &ui_app.params, &ui_app.ui, false, NULL, false, txt, 200), -1);            // does not fit: said so
}

TEST(patch_text_keeps_fm_and_the_motion_sequences) {
    ui_fresh();
    ui_set_type(SYNTH_FM);
    ui_app.rack.cfg.fm.algorithm = 7;
    ui_app.rack.cfg.fm.op[2].eg_t[1] = 4321;
    snprintf(ui_app.rack.cfg.fm.name, DX7_NAME_LEN, "MY BELL 2");
    const uint32_t h = sound();
    static char txt[STORAGE_FILE_MAX];
    CHECK(patch_to_text(&ui_app.rack, &ui_app.params, &ui_app.ui, false, NULL, false, txt, sizeof txt) > 0);
    ui_fresh();
    CHECK(patch_from_text(&ui_app.rack, &ui_app.params, &ui_app.ui, NULL, NULL, txt));
    CHECK_EQ(ui_app.rack.cfg.type, SYNTH_FM);
    CHECK_EQ(ui_app.rack.cfg.fm.algorithm, 7);
    CHECK_EQ(ui_app.rack.cfg.fm.op[2].eg_t[1], 4321);
    CHECK_STR(ui_app.rack.cfg.fm.name, "MY BELL 2");
    CHECK_EQ(sound(), h);

    ui_fresh();                                                          // a motion sequencer: its lanes go with its module
    CHECK(rack_insert_row(&ui_app.rack, ui_app.rack.count, 0, MOD_MSEQ));
    ms_pattern_t *m = rack_ms(&ui_app.rack, ui_app.rack.count - 1);
    CHECK(m != NULL);
    if (!m) return;
    m->lane[1].val[5] = 85; m->lane[1].active = 0x0421; m->lane[1].tgt_id = ui_app.rack.slot[0].id; m->lane[1].tgt_param = 2; m->lane[1].depth = 0.5f;
    const uint32_t h2 = sound();
    CHECK(patch_to_text(&ui_app.rack, &ui_app.params, &ui_app.ui, false, NULL, false, txt, sizeof txt) > 0);
    ui_fresh();
    CHECK(patch_from_text(&ui_app.rack, &ui_app.params, &ui_app.ui, NULL, NULL, txt));
    const ms_pattern_t *back = rack_ms_const(&ui_app.rack, ui_app.rack.count - 1);
    CHECK(back != NULL && back->lane[1].val[5] == 85 && back->lane[1].active == 0x0421 && back->lane[1].tgt_param == 2);
    CHECK_EQ(sound(), h2);
}

TEST(sound_and_sequencer_come_back_after_a_restart) {
    ui_stub_ramcard(true);
    ui_fresh();
    const int fl = first_of(MOD_FILTER);
    if (fl < 0) return;
    char buf[64];
    CHECK(storage_read(PATCH_STATE_FILE, buf, sizeof buf) < 0);          // nothing changed yet: nothing written
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 777.0f;
    ui_app.seq.bpm = 97;
    ui_app.seq.step[3].note[0] = 60;
    ui_app.rack.cfg.volume = 0.5f;
    ev_idle(20);
    CHECK(storage_read(PATCH_STATE_FILE, buf, sizeof buf) < 0);          // not at once: a moment after the last change
    ev_idle(600);
    CHECK(storage_read(PATCH_STATE_FILE, buf, sizeof buf) > 0);
    CHECK(storage_read("system/config/seq.cfg", buf, sizeof buf) > 0);   // the sequencer: its own files (app/app_seq.c)
    CHECK(storage_read("system/config/pattern1.cfg", buf, sizeof buf) > 0);
    ui_fresh();                                                          // power off, power on
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 777);
    CHECK_EQ(ui_app.seq.bpm, 97);
    CHECK_EQ(ui_app.seq.step[3].note[0], 60);
    CHECK_EQ((int)(ui_app.rack.cfg.volume * 10), 5);                     // (the volume: ui.cfg)
    CHECK(!ui_app.state_pending);
    ev_idle(600);
    CHECK(!ui_app.state_pending);                                        // what was read is what the card holds: no write follows
}

TEST(preset_save_as_save_and_init) {
    ui_stub_ramcard(true);
    ui_fresh();
    const int fl = first_of(MOD_FILTER);
    if (fl < 0) return;
    char buf[64];
    CHECK_STR(ui_app.preset, "");
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 500.0f;
    preset_button(3);                                                    // Save as: the first free name
    CHECK_STR(ui_app.preset, "Preset 1");
    CHECK_STR(ui_app.ui.preset_name, "Preset 1");
    CHECK(storage_read("presets/Preset 1.preset", buf, sizeof buf) > 0);
    CHECK_STR(ui_app.popup.info.text, "Saved: Preset 1");
    ev_idle(60);
    CHECK(!ui_app.ui.preset_edited);
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 900.0f;                          // edited: the tab says so, the file does not follow
    ev_idle(60);
    CHECK(ui_app.ui.preset_edited);
    preset_button(3);                                                    // Save as again: another file
    CHECK_STR(ui_app.preset, "Preset 2");
    CHECK(storage_read("presets/Preset 2.preset", buf, sizeof buf) > 0);
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 950.0f;
    preset_button(2);                                                    // Save: asks first, Yes is the focus
    CHECK(popup_modal(&ui_app.popup));
    ev_tap(CTL_JOY_SW);
    CHECK(!popup_modal(&ui_app.popup));
    CHECK_STR(ui_app.preset, "Preset 2");
    ev_idle(60);
    CHECK(!ui_app.ui.preset_edited);
    ui_fresh();                                                          // a restart: the sound, and the preset it came from
    CHECK_STR(ui_app.preset, "Preset 2");
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 950);
    ev_idle(60);
    CHECK(!ui_app.ui.preset_edited);
    const int count = ui_app.rack.count;
    rack_delete(&ui_app.rack, ui_app.rack.count - 1);
    preset_button(4);                                                    // Init: asks, No is the focus; Yes starts the start sound again
    CHECK(popup_modal(&ui_app.popup));
    ev_tap(CTL_JOY_SW);
    CHECK(!popup_modal(&ui_app.popup));
    CHECK_EQ(ui_app.rack.count, count - 1);                              // (No: nothing happened)
    preset_button(4);
    ev_turn(CTL_ENC_B, -1);                                              // to Yes
    ev_tap(CTL_JOY_SW);
    CHECK_EQ(ui_app.rack.count, count);
    CHECK_STR(ui_app.preset, "");
}

TEST(preset_browser_tries_a_preset_and_back_brings_the_sound_back) {
    ui_stub_ramcard(true);
    ui_fresh();
    const int fl = first_of(MOD_FILTER);
    if (fl < 0) return;
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 500.0f;
    preset_button(3);                                                    // Preset 1: 500 Hz
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 900.0f;
    preset_button(3);                                                    // Preset 2: 900 Hz
    ui_app.rack.slot[fl].v[MP_FL_CUT] = 1100.0f;                         // ... edited since
    const int builds = ui_stub_builds_count();
    preset_button(1);                                                    // Load: the browser, on the preset the sound came from
    CHECK_EQ(ui_app.ui.br_open, BROWSE_PRESET);
    CHECK_EQ(ui_app.ui.br_total, 2);
    CHECK_EQ(ui_app.ui.br_cur, 1);
    ev_turn(CTL_ENC_A, -1);                                              // up: Preset 1, tried once the highlight rests
    CHECK_EQ(ui_app.ui.br_cur, 0);
    ev_idle(40);
    CHECK_EQ(ui_app.ui.br_state, BR_SHOWN);
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 500);
    CHECK(ui_stub_builds_count() > builds);                              // the engine plays it
    CHECK_STR(ui_app.preset, "Preset 2");                                // (not chosen yet)
    ev_tap(BACK);                                                        // Back: the sound as it was, edits and all
    CHECK(!ui_app.ui.br_open);
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 1100);
    CHECK_STR(ui_app.preset, "Preset 2");
    ev_idle(60);
    CHECK(ui_app.ui.preset_edited);
    preset_button(1);                                                    // again, and a push keeps it
    ev_turn(CTL_ENC_A, -1);
    ev_tap(CTL_JOY_SW);
    CHECK(!ui_app.ui.br_open);
    CHECK_STR(ui_app.preset, "Preset 1");
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 500);
    ev_idle(600);                                                        // ... and the working state follows
    ui_fresh();
    CHECK_STR(ui_app.preset, "Preset 1");
    CHECK_EQ((int)ui_app.rack.slot[fl].v[MP_FL_CUT], 500);
}

TEST(preset_without_a_card_says_so) {
    ui_fresh();                                                          // (no RAM card: nothing can be written)
    preset_button(3);
    CHECK_STR(ui_app.preset, "");
    CHECK(popup_info_showing(&ui_app.popup) || popup_modal(&ui_app.popup));
}
