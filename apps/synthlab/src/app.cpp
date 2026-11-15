// SynthLab's controls and screen. The screen is one list: the synth, then its values. Portable: the same file runs on the desktop and on
// the board.
//
//   encoder A              the row (push: back to the top, the synth); it steps over the sections' titles
//   encoder B              the row's value: the synth, or the value (push: its default; on the synth: its sound read again from the card)
//   the note rows          notes: bottom left = C3 (48), +1 per key to the right, +1 row = + the row's length
//   function keys 1 / 2    octave down / up            (the function keys: the top row of the key matrix)
//   function keys 3 / 4    the previous / next synth
//   PLAY                   holds the last note played (a drone, to turn values while it sounds); again: lets it go. A synth with a
//                          tempo (lab::Options::play_bpm, the Kick: 135) loops it instead, a hit every beat; a key changes its note
//                          (a synth with grooves, DrumLab: its Groove at its Tempo)
//   VOLUME                 the master volume
//
// The sounds: SynthLab Studio's (framework/control/preset.h), one per synth, on the card in synthlab/sounds/ under the name Studio saves
// them (presets/Kick.sound -> synthlab/sounds/Kick.sound). A synth's sound is read the first time the synth is chosen while a card is
// in (the one playing: when the card comes in), then the synth keeps what is played on it; push B on the synth reads it again. Its
// values by name: a value the synth no longer has is skipped, one it gained keeps its own. The bar says what was read until the next
// control moves.
//
// The board's controls are found in its table (hal/hal_input.h), not by id: encoder A and B are the first two encoders (the first two
// endless pots on a board without encoders), their pushes the buttons at the same place, PLAY and VOLUME by name. So the app runs on
// any board of the library. A board without them (HWV1: seven endless pots, a joystick, no PLAY, no VOLUME) uses what it has: encoder
// B's push is its first button (HWV1: the joystick's push); the joystick's Y pushed up is PLAY, pushed down encoder A's push (back to
// the top); no VOLUME: the level the firmware starts with.
#include "app.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "control/preset.h"
#include "hal/hal_storage.h"
#include "lab.h"
#include "synth.h"
#include "ui/gui.h"

#if defined(SYNTHLAB_BENCH) && defined(ARDUINO_ARCH_ESP32)
void bench_start();                         // src/bench_esp32.cpp: the modules' cycles, printed on the console (a dev build)
#endif

namespace {
u8g2_t *g = nullptr;
gui_style_t style;
const input_control_t *controls = nullptr;  // the board's table
int n_controls = 0;
int key_rows = 0, key_cols = 0;             // the board's key matrix: row 0 is the function row, the last the lowest notes
int enc_a = INPUT_NO_CONTROL, enc_b = INPUT_NO_CONTROL, push_a = INPUT_NO_CONTROL, push_b = INPUT_NO_CONTROL;
int play_btn = INPUT_NO_CONTROL, volume_knob = INPUT_NO_CONTROL;
int joy_y = INPUT_NO_CONTROL;               // a board without PLAY or encoder A's push: the joystick's Y (up: PLAY, down: back to the top)
int joy_dir = 0;                            // where that stick is: +1 up, -1 down, 0 back near the centre

constexpr int kRowsMax = 8, kColsMax = 8;   // the largest key matrix this app plays (a key outside it does nothing)
uint8_t held[kRowsMax][kColsMax];           // note + 1 a key started, so its release stops the same note after an octave change
int octave = 0;
int last_note = 48;                         // what PLAY holds
int hold_note = -1;                         // the note PLAY holds, -1 = none
int row = 0;                                // the selected row: 0 = the synth, 1.. = its values
int top = 0;                                // the first row on screen
bool dirty = true;                          // the screen shows something else than the state

const char *const kFolders[] = {"synthlab", "synthlab/sounds"};   // on the card (made when missing)
constexpr const char *kSoundDir = "synthlab/sounds";
bool card_in = false;
bool sound_read[lab::kMaxSynths];           // the synth's sound was read from this card
char status[40];                            // the bar's message ("": the app's name)

const lab::Synth &cur() { return *lab::kSynths[synth_selected()]; }
int row_count() { return 1 + cur().n_params; }
bool is_title(int r) { return r > 0 && r < row_count() && cur().params[r - 1].curve == lab::Head; }

// Encoder A: `detents` rows that can be selected (titles do not count), stopping at both ends.
void move_row(int detents) {
    const int dir = detents > 0 ? 1 : -1;
    for (int k = 0; k < (detents > 0 ? detents : -detents); k++) {
        int r = row + dir;
        while (is_title(r)) r += dir;
        if (r < 0 || r >= row_count()) break;
        row = r;
    }
}

int find_control(int kind, int nth) {
    for (int i = 0; i < n_controls; i++)
        if (controls[i].kind == kind && nth-- == 0) return i;
    return INPUT_NO_CONTROL;
}
int find_push(int ctl) {                    // the button at the same place as `ctl`: its push
    if (ctl == INPUT_NO_CONTROL) return INPUT_NO_CONTROL;
    for (int i = 0; i < n_controls; i++)
        if (controls[i].kind == CK_BUTTON && controls[i].px == controls[ctl].px && controls[i].py == controls[ctl].py) return i;
    return INPUT_NO_CONTROL;
}
int find_named(const char *name) {
    for (int i = 0; i < n_controls; i++)
        if (std::strcmp(controls[i].name, name) == 0) return i;
    return INPUT_NO_CONTROL;
}

// Reads the sound of the selected synth from the card (status: what happened).
void read_sound() {
    const lab::Synth &s = cur();
    char name[48], path[80];
    pset_file_name(s.name, ".sound", name, sizeof name);
    std::snprintf(path, sizeof path, "%s/%s", kSoundDir, name);
    sound_read[synth_selected()] = true;
    static char text[STORAGE_FILE_MAX];
    const int n = storage_read(path, text, sizeof text);
    if (n < 0) { std::snprintf(status, sizeof status, card_in ? "No %s" : "No card", name); return; }
    if (n >= static_cast<int>(sizeof text) - 1) {   // (a file larger than the board reads: its last line may be cut, it goes)
        char *e = std::strrchr(text, '\n');
        if (e) e[1] = 0;
    }
    const pset_sound_t ps = {s.params, s.n_params, nullptr, [](void *, int i, float v) { synth_set_value(i, v); }, nullptr, nullptr, nullptr, 0};
    int skipped = 0;
    if (!(pset_from_text(&ps, text, &skipped) & PSET_HAD_VALUES)) std::snprintf(status, sizeof status, "%s: no value", name);
    else if (skipped) std::snprintf(status, sizeof status, "Sound read, %d skipped", skipped);
    else std::snprintf(status, sizeof status, "Sound read");
}

// The card's events: a card that comes in gets its folders and gives the playing synth its sound.
void poll_card() {
    storage_event_t e;
    while (storage_poll_event(&e)) {
        if (e.kind == STORAGE_EV_INSERTED) {
            card_in = true;
            std::memset(sound_read, 0, sizeof sound_read);
            if (e.missing) storage_make_folders();
            read_sound();
            dirty = true;
        } else if (e.kind == STORAGE_EV_REMOVED) {
            card_in = false;
            std::snprintf(status, sizeof status, "Card out");
            dirty = true;
        }
    }
}

void release_hold() {
    if (hold_note >= 0) synth_note_off(hold_note);
    hold_note = -1;
}

void select_synth(int index) {                      // (beyond the ends of the list: the first or the last synth)
    index = index < 0 ? 0 : (index >= lab::kSynthCount ? lab::kSynthCount - 1 : index);
    if (index == synth_selected()) return;
    hold_note = -1;                                 // (synth_select lets every note go)
    std::memset(held, 0, sizeof held);
    synth_select(index);
    if (card_in && !sound_read[index]) read_sound();
    if (row >= row_count()) row = row_count() - 1;
    while (is_title(row)) row--;                    // (row 0, the synth, is never a title)
}

// PLAY: holds the last note played, or lets it go; a synth with a tempo loops it instead (and stops the loop).
void play() {
    if (cur().opt.play_bpm > 0) {
        if (synth_looping()) synth_loop_stop();
        else synth_loop_start(last_note, cur().opt.play_bpm);
    } else if (hold_note >= 0) {
        release_hold();
    } else {
        hold_note = last_note;
        synth_note_on(hold_note);
    }
}

void edit(int detents) {
    if (row == 0) { select_synth(synth_selected() + detents); return; }
    if (is_title(row)) return;
    const lab::Param &p = cur().params[row - 1];
    synth_set_value(row - 1, pdesc_detents(&p, synth_value(row - 1), detents));
}

void key_event(int r, int c, bool pressed) {
    if (r == 0) {                                   // the function row
        if (!pressed) return;
        if (c == 0 && octave > -3) octave--;
        else if (c == 1 && octave < 3) octave++;
        else if (c == 2) select_synth(synth_selected() - 1);
        else if (c == 3) select_synth(synth_selected() + 1);
        return;
    }
    if (r >= kRowsMax || c >= kColsMax) return;
    uint8_t &h = held[r][c];
    if (pressed) {
        const int note = 48 + 12 * octave + key_cols * (key_rows - 1 - r) + c;
        if (note < 0 || note > 127 || h) return;
        synth_note_on(note);
        h = static_cast<uint8_t>(note + 1);
        last_note = note;
        synth_loop_note(note);                      // (a loop that runs plays it from its next hit)
    } else if (h) {
        if (h - 1 != hold_note) synth_note_off(h - 1);     // (the note PLAY holds keeps sounding)
        h = 0;
    }
}
}  // namespace

const char *app_name() { return APP_NAME; }

void app_start(u8g2_t *display) {
    g = display;
    controls = input_controls(&n_controls);
    key_rows = key_cols = 0;
    joy_dir = 0;
    for (int i = 0; i < n_controls; i++) {
        if (controls[i].kind != CK_KEY) continue;
        if (controls[i].row >= key_rows) key_rows = controls[i].row + 1;
        if (controls[i].col >= key_cols) key_cols = controls[i].col + 1;
    }
    enc_a = find_control(CK_ENCODER, 0);
    enc_b = find_control(CK_ENCODER, 1);
    if (enc_a == INPUT_NO_CONTROL) { enc_a = find_control(CK_ENDLESS, 0); enc_b = find_control(CK_ENDLESS, 1); }
    push_a = find_push(enc_a);
    push_b = find_push(enc_b);
    play_btn = find_named("PLAY");
    volume_knob = find_named("VOLUME");
    if (push_b == INPUT_NO_CONTROL) push_b = find_control(CK_BUTTON, 0);
    if (play_btn == INPUT_NO_CONTROL || push_a == INPUT_NO_CONTROL) joy_y = find_control(CK_AXIS, 1);   // (the second axis: Y)
    gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    storage_set_folders(kFolders, 2);
    dirty = true;
#if defined(SYNTHLAB_BENCH) && defined(ARDUINO_ARCH_ESP32)
    bench_start();
#endif
}

void app_event(const input_event_t &e) {
    if (e.ctl < 0 || e.ctl >= n_controls) return;
    const input_control_t &c = controls[e.ctl];
    if (e.kind != IN_VALUE && e.kind != IN_RELEASE && status[0]) { status[0] = 0; dirty = true; }   // (a control moved: the message goes)
    if (c.kind == CK_KEY && (e.kind == IN_PRESS || e.kind == IN_RELEASE)) {
        key_event(c.row, c.col, e.kind == IN_PRESS);
    } else if (e.kind == IN_DELTA && e.ctl == enc_a) {
        move_row(e.value);
    } else if (e.kind == IN_DELTA && e.ctl == enc_b) {
        edit(e.value);
    } else if (e.kind == IN_PRESS && e.ctl == push_a) {
        row = 0;
    } else if (e.kind == IN_PRESS && e.ctl == push_b) {
        if (row == 0) read_sound();
        else if (!is_title(row)) synth_set_value(row - 1, cur().params[row - 1].def);
    } else if (e.kind == IN_PRESS && e.ctl == play_btn) {
        play();
    } else if (e.kind == IN_VALUE && e.ctl == joy_y) {
        // the stick: up past a third of its travel, or down; back within a sixth of the centre it is free again (one action a push)
        const int d = INPUT_AXIS_CENTER - e.value, third = INPUT_VALUE_MAX / 3;       // up = positive (a low Y is the stick pushed up)
        const int dir = d > third ? 1 : d < -third ? -1 : (std::abs(d) < INPUT_VALUE_MAX / 6 ? 0 : joy_dir);
        if (dir == joy_dir) return;
        joy_dir = dir;
        if (dir > 0 && play_btn == INPUT_NO_CONTROL) play();
        else if (dir < 0 && push_a == INPUT_NO_CONTROL) row = 0;
        else return;
        if (status[0]) status[0] = 0;
    } else if (e.kind == IN_VALUE && e.ctl == volume_knob) {
        synth_set_volume(static_cast<float>(e.value) / INPUT_VALUE_MAX);
        return;                                     // (nothing on screen changes)
    } else {
        return;
    }
    dirty = true;
}

void app_draw() {
    poll_card();                                    // (the loop's one call to the app between events)
    if (!dirty || !g) return;
    dirty = false;
    u8g2_ClearBuffer(g);
    gui_begin(g, &style);
    gui_rect_t area = gui_screen(g);

    // the bar: the app, the octave, the held note
    char text[40];
    const gui_rect_t bar = gui_take_top(&area, GUI_BAR_H);
    gui_draw_text_left(g, &style, bar, status[0] ? status : "SynthLab");
    if (synth_looping()) std::snprintf(text, sizeof text, "%d BPM  oct %+d", synth_loop_bpm(), octave);
    else std::snprintf(text, sizeof text, "%soct %+d", hold_note >= 0 ? "HOLD  " : "", octave);
    if (!status[0]) gui_draw_text_right(g, &style, bar, text);    // (a message takes the whole bar)
    u8g2_DrawHLine(g, 0, GUI_BAR_H - 1, static_cast<u8g2_uint_t>(bar.w));

    // the list: as many rows as fit, scrolled to keep the selected one in view
    area = gui_inset(area, style.margin);
    const int row_h = gui_row_h(g, &style) + style.gap;
    const int fit = area.h / row_h > 0 ? area.h / row_h : 1;
    const int rows = row_count();
    if (row < top) top = row;
    if (is_title(row - 1) && row - 1 < top) top = row - 1;      // a section's first value shows its title
    if (row >= top + fit) top = row - fit + 1;
    if (top > rows - fit) top = rows - fit > 0 ? rows - fit : 0;
    if (rows > fit) {                               // a scroll bar on the right
        const int bar_h = area.h * fit / rows, bar_y = area.y + (area.h - bar_h) * top / (rows - fit);
        u8g2_DrawVLine(g, static_cast<u8g2_uint_t>(gui_right(area) - 1), static_cast<u8g2_uint_t>(bar_y), static_cast<u8g2_uint_t>(bar_h));
        area.w -= 3;
    }
    for (int i = top; i < rows && i < top + fit; i++) {
        const gui_rect_t r = gui_take_top(&area, row_h);
        const gui_rect_t field = gui_rect(r.x, r.y, r.w, row_h - style.gap);
        if (i == 0) {
            gui_draw_field(g, &style, field, "Synth", cur().name, i == row);
            continue;
        }
        const lab::Param &p = cur().params[i - 1];
        if (p.curve == lab::Head) {                 // a section's title: centred between two rules
            const int tw = gui_text_w(g, p.name), mid = field.y + field.h / 2, gap = 3;
            const int tx = field.x + (field.w - tw) / 2;
            gui_draw_text_centered(g, field, p.name);
            if (tx - gap > field.x) u8g2_DrawHLine(g, static_cast<u8g2_uint_t>(field.x), static_cast<u8g2_uint_t>(mid), static_cast<u8g2_uint_t>(tx - gap - field.x));
            if (tx + tw + gap < gui_right(field)) u8g2_DrawHLine(g, static_cast<u8g2_uint_t>(tx + tw + gap), static_cast<u8g2_uint_t>(mid), static_cast<u8g2_uint_t>(gui_right(field) - tx - tw - gap));
            continue;
        }
        pdesc_format(&p, synth_value(i - 1), text, sizeof text);
        gui_draw_field(g, &style, field, p.name, text, i == row);
    }
    display_send(g);
}
