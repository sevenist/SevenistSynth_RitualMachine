// What the controls do, the screen and the LEDs. Portable: the same file runs on the desktop and on HWV2. The screen is the design's main
// view at 1:1 (it was drawn at 5x: 640 x 800 for 128 x 160): a 12-pixel header, seven rows of 16 for the tracks, a 36-pixel footer.
//
//   PLAY                play / stop
//   encoder A           the track (SHFT: its lane); push: the tracks <-> the selected track's engine
//   encoder B           the dice's depth TIGHT / RAW / WEIRD (SHFT: the tempo); push: the next pattern, at the end of the bar
//   function keys       STEP, TRK, PERF: what the 16 keys are; SHFT (held, or tapped to latch)
//   16 keys             STEP: the selected track's steps (off, hit, accent; SHFT + key: its lane) / TRK: select 1-7, LOAD, mute 1-7,
//                       ALL / PERF: PRINT TAKE FILL EVOLV ROLL x4 (not built yet), patterns A1..A8
//   B1 MOD              held or latched: MOD + a knob locks / unlocks that value against the dice
//   B2 DICE             rolls the selected track (SHFT: GEN, all seven) at the depth
//   B3 BACK             undoes a roll (16 kept)
//   K1..K4, R1..R3      the tracks' levels (main view), the selected track's seven values (engine view); a knob takes over once it
//                       reaches the value it is given
//   VOLUME              the master volume
//   joystick            (X MORPH, Y TENSE: not built yet)
#include "app.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "boards/hwv2/hwv2_controls.h"
#include "hal/hal_leds.h"
#include "synth.h"
#include "tg_app.h"
#include "ui/canvas.h"

using namespace tg;

namespace {
constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF;
const uint16_t kGrey = CANVAS_HEX(0x4A4D43);         // the header's and footer's rules
const int kKnobCtl[kKnobs] = {HWV2_COL_KNOB_0, HWV2_COL_KNOB_1, HWV2_COL_KNOB_2, HWV2_COL_KNOB_3, HWV2_KNOB_R1, HWV2_KNOB_R2, HWV2_KNOB_R3};
constexpr int kHeaderH = 12, kRowH = 16, kFooterY = kHeaderH + kTracks * kRowH;   // 12, 16, 124

AppState st;
uint16_t frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];
canvas_t cv;
bool dirty = true;
uint8_t mod_touched = 0;                              // the knobs that already toggled their lock while MOD is on (one toggle a knob)

uint16_t color_of(int t) { return CANVAS_HEX(track_def(t).color); }

/* ---------------- what changes the sound ---------------- */

void send_track(int t) { synth_set_track(t, st.track(t)); }
void send_all() {
    for (int t = 0; t < kTracks; t++) send_track(t);
    synth_set_steps(st.pattern[st.cur]);
}
void release_knobs() { for (bool &c : st.knob_caught) c = false; }
void say(const char *text) { std::snprintf(st.message, sizeof st.message, "%s", text); st.last_value = -1; }
uint16_t new_seed() { return static_cast<uint16_t>(1 + st.rng.below(65535)); }

void select_track(int t) {
    st.selected = (t % kTracks + kTracks) % kTracks;
    release_knobs();
    say("");
}

void dice() {
    st.undo.push(st.pattern[st.cur]);
    char text[24];
    if (st.shift.on()) {                              // GEN: all seven
        for (int t = 0; t < kTracks; t++) tg_roll(st.track(t), t, new_seed(), st.depth);
        std::snprintf(text, sizeof text, "ALL ROLLED %s", depth_name(st.depth));
    } else {
        tg_roll(st.sel(), st.selected, new_seed(), st.depth);
        std::snprintf(text, sizeof text, "ROLLED %s", depth_name(st.depth));
    }
    send_all();
    release_knobs();
    say(text);
}

void back() {
    char text[24];
    if (st.undo.pop(st.pattern[st.cur])) { std::snprintf(text, sizeof text, "BACK, %d LEFT", st.undo.n); send_all(); release_knobs(); }
    else std::snprintf(text, sizeof text, "NO ROLL TO UNDO");
    say(text);
}

void go_to_pattern(int n) {
    n = (n % kPatterns + kPatterns) % kPatterns;
    char text[24];
    if (!st.playing) {                                // stopped: at once
        st.cur = n;
        st.queued = -1;
        send_all();
        release_knobs();
        std::snprintf(text, sizeof text, "PATTERN A%d", n + 1);
    } else {                                          // playing: at the end of the bar
        st.queued = n;
        synth_queue_steps(st.pattern[n], n);
        std::snprintf(text, sizeof text, "NEXT A%d", n + 1);
    }
    say(text);
}

void set_playing(bool on) {
    st.playing = on;
    synth_set_transport(st.bpm, on);
    if (!on) {
        st.step = -1;
        if (st.queued >= 0) { st.cur = st.queued; st.queued = -1; send_all(); release_knobs(); }
    }
}

/* ---------------- the controls ---------------- */

void step_key(int i) {                                // one of the 16 keys, 0..15 from the top left
    Track &tr = st.sel();
    const TrackDef &d = track_def(st.selected);
    switch (st.keys) {
    case K_STEP:
        if (st.shift.on()) {                          // SHFT + a key: the lane
            if (i < d.lanes) {
                st.lane[st.selected] = static_cast<uint8_t>(i);
                char text[24];
                std::snprintf(text, sizeof text, "LANE %s", d.lane_name[i]);
                say(text);
            }
            return;
        } else {
            uint8_t &s = tr.step[st.lane[st.selected]][i];
            s = s == S_OFF ? S_HIT : (s == S_HIT ? S_ACCENT : S_OFF);   // off -> hit -> accent -> off
            synth_set_steps(st.pattern[st.cur]);
        }
        break;
    case K_TRACK:
        if (i < kTracks) select_track(i);
        else if (i == 7) say("LOAD: NOT YET");
        else if (i < 15) {
            Track &m = st.track(i - 8);
            m.muted = !m.muted;
            send_track(i - 8);
            synth_set_steps(st.pattern[st.cur]);
        } else {
            for (int t = 0; t < kTracks; t++) { st.track(t).muted = false; send_track(t); }
            synth_set_steps(st.pattern[st.cur]);
            say("ALL BACK ON");
        }
        break;
    case K_PERF: {
        static const char *const kPerf[8] = {"PRINT", "TAKE", "FILL", "EVOLVE", "ROLL 1/4", "ROLL 1/8", "ROLL 1/16", "ROLL 1/32"};
        if (i < 8) { char text[24]; std::snprintf(text, sizeof text, "%s: NOT YET", kPerf[i]); say(text); }
        else go_to_pattern(i - 8);
        break;
    }
    }
}

uint8_t *knob_target(int k) { return st.view == V_MAIN ? &st.track(k).level : &st.sel().value[k]; }

void knob(int k, int value) {
    const int pos = (value * 100 + INPUT_VALUE_MAX / 2) / INPUT_VALUE_MAX;
    const int prev = st.knob_pos[k];
    st.knob_pos[k] = pos;
    if (st.mod.on()) {                                // MOD + a knob: its lock (the engine view's values), once a knob while MOD is on
        if (st.view == V_ENGINE && !(mod_touched & (1 << k))) {
            mod_touched = static_cast<uint8_t>(mod_touched | (1 << k));
            st.sel().lock[k] = !st.sel().lock[k];
            char text[24];
            std::snprintf(text, sizeof text, "%s %s", track_def(st.selected).param[k].label, st.sel().lock[k] ? "LOCKED" : "UNLOCKED");
            say(text);
        }
        return;
    }
    uint8_t *target = knob_target(k);
    if (!st.knob_caught[k] && (std::abs(pos - *target) <= 2 || (prev >= 0 && (prev - *target) * (pos - *target) <= 0)))
        st.knob_caught[k] = true;                    // it reached the value, or went past it
    if (st.knob_caught[k]) {
        *target = static_cast<uint8_t>(pos);
        if (st.view == V_ENGINE) { tg_rules(st.sel(), st.selected); send_track(st.selected); }
        else send_track(k);
    }
    st.last_value = k;
    st.message[0] = 0;
}

void modifier_down(Modifier &m) { m.held = true; m.used = false; }
void modifier_up(Modifier &m) {
    m.held = false;
    if (!m.used) m.latched = !m.latched;             // tapped alone: latched on, or off again
}

/* ---------------- the screen ---------------- */

int baseline(u8g2_t *g, int top, int h) { return top + (h + u8g2_GetAscent(g)) / 2; }

void draw_header(u8g2_t *g) {
    char left[16], right[16];
    if (st.queued >= 0) std::snprintf(left, sizeof left, "A%d>A%d %d", st.cur + 1, st.queued + 1, st.bpm);
    else std::snprintf(left, sizeof left, "A%d %d", st.cur + 1, st.bpm);
    std::snprintf(right, sizeof right, "%s %s", st.playing ? ">" : ".", depth_name(st.depth));
    canvas_color(&cv, kWhite, kBlack);
    const int y = baseline(g, 0, kHeaderH - 1);
    u8g2_DrawStr(g, 1, y, left);
    u8g2_DrawStr(g, DISPLAY_WIDTH - 1 - u8g2_GetStrWidth(g, right), y, right);
    canvas_fill(&cv, 0, kHeaderH - 1, DISPLAY_WIDTH, 1, kGrey);
}

// A track's row: its short name, its 16 steps in 4 groups of 4 (4 pixels wide, 1 apart, 2 between groups), its level as a bar.
void draw_row(u8g2_t *g, int t) {
    const Track &tr = st.track(t);
    const bool sel = t == st.selected;
    const uint16_t ink = sel ? kBlack : color_of(t), paper = sel ? color_of(t) : kBlack;
    const int y0 = kHeaderH + t * kRowH;
    canvas_fill(&cv, 0, y0, DISPLAY_WIDTH, kRowH, paper);
    canvas_color(&cv, ink, paper);
    u8g2_DrawStr(g, 1, baseline(g, y0, kRowH), track_def(t).shortname);
    static const int kMark[S_KINDS] = {1, 6, 12, 2};  // off, hit, accent, hold: the mark's height in the 12-pixel cell
    for (int p = 0; p < kSteps; p++) {
        const int x = 23 + (p / 4) * 21 + (p % 4) * 5, cy = y0 + 2;
        const bool here = st.playing && p == st.step;
        const int h = tr.muted ? 1 : kMark[tg_step_max(tr, p)];
        canvas_fill(&cv, x, cy, 4, 12, here ? ink : paper);
        canvas_fill(&cv, x, cy + 12 - h, 4, h, here ? paper : ink);
    }
    const int bx = 107, by = y0 + 5;                  // the level: a box of 20 x 5, its fill 18 x 3
    canvas_fill(&cv, bx, by, 20, 5, ink);
    canvas_fill(&cv, bx + 1, by + 1, 18, 3, paper);
    canvas_fill(&cv, bx + 1, by + 1, tr.muted ? 0 : (18 * tr.level + 50) / 100, 3, ink);
}

// The engine view's picture: a sketch of the sound from the seven values and the seed (the design's mock-up, point for point).
void draw_wave(u8g2_t *g, uint16_t color) {
    const Track &tr = st.sel();
    const int i = st.selected, W = DISPLAY_WIDTH, H = 64, top = kHeaderH;
    Rng r(tr.seed);
    float p[kParams];
    for (int k = 0; k < kParams; k++) p[k] = tr.value[k] / 100.f;
    auto rnd = [&r]() { return static_cast<float>(r.next()) / 4294967296.f; };
    const float two_pi = 6.2831853f;
    float phase = 0;
    int px = -1, py = 0;
    canvas_color(&cv, color, kBlack);
    for (int x = 0; x <= W; x++) {
        const float u = static_cast<float>(x) / static_cast<float>(W);
        float y = 0;
        if (i == T_KICK) {
            phase += 0.1f + 2.2f * p[2] * expf(-u * 22) + 0.24f * p[0];
            y = sinf(phase) * expf(-u * (9 - 7 * p[3])) + 0.28f * p[6] * sinf(phase * 0.5f) * expf(-u * 1.6f);
        } else if (i == T_BASS) {
            phase += 0.18f;
            const float f = fmodf(phase / two_pi, 1.f);
            const int h = 2 + static_cast<int>(p[0] * 10);
            for (int n = 1; n <= h; n++) y += sinf(static_cast<float>(n) * f * two_pi) / static_cast<float>(n);
            y = tanhf(y * (0.6f + p[3] * 2)) * 0.8f;
        } else if (i == T_DRUM) {
            const float b = fmodf(u * 4, 1.f);
            y = (rnd() * 2 - 1) * expf(-b * (11 - 9 * p[2])) * (0.5f + p[4] * 0.5f);
        } else if (i == T_PERC) {
            y = (sinf(u * 120 * (0.5f + p[1])) + 0.7f * sinf(u * 311 * (0.4f + p[3])) + 0.5f * sinf(u * 517)) * 0.45f * expf(-u * (10 - 8 * p[2]));
        } else if (i == T_PAD) {
            const float f1 = fmodf(u * 16, 1.f), f2 = fmodf(u * (16.4f + p[0] * 2), 1.f), f3 = fmodf(u * (8.1f + p[1]), 1.f);
            y = (f1 + f2 + f3 - 1.5f) * 0.42f * (0.5f + 0.5f * sinf(u * 7));
        } else if (i == T_TEXT) {
            const float gate = sinf(u * 40 * (0.2f + p[1])) > 0.2f - p[1] ? 1.f : 0.1f;
            y = (rnd() * 2 - 1) * gate * (0.3f + p[0] * 0.6f);
        } else {
            const float env = fabsf(sinf(u * 19)) * expf(-fmodf(u * 4, 1.f) * 2.5f);
            y = (rnd() * 2 - 1) * env * 0.9f;
        }
        y = y < -1 ? -1 : (y > 1 ? 1 : y);
        const int yy = top + H / 2 - static_cast<int>(lroundf(y * static_cast<float>(H / 2 - 2)));
        if (px >= 0) u8g2_DrawLine(g, static_cast<u8g2_uint_t>(px), static_cast<u8g2_uint_t>(py), static_cast<u8g2_uint_t>(x < W ? x : W - 1),
                                   static_cast<u8g2_uint_t>(yy));
        px = x < W ? x : W - 1;
        py = yy;
    }
}

// The seven values as bars on a line, 16 pixels wide, 2 apart: filled in the track's colour, hollow when locked.
void draw_values(uint16_t color) {
    const Track &tr = st.sel();
    const int bottom = kFooterY - 1;
    for (int k = 0; k < kParams; k++) {
        const int x = 2 + k * 18, h = tr.value[k] * 44 / 100 < 3 ? 3 : tr.value[k] * 44 / 100;
        canvas_fill(&cv, x, bottom, 16, 1, color);
        canvas_fill(&cv, x, bottom - h, 16, h, color);
        if (tr.lock[k]) canvas_fill(&cv, x + 1, bottom - h + 1, 14, h - 2, kBlack);
    }
}

void draw_footer(u8g2_t *g) {
    canvas_fill(&cv, 0, kFooterY, DISPLAY_WIDTH, 1, kGrey);
    const TrackDef &d = track_def(st.selected);
    const Track &tr = st.sel();
    char a[24], b[24];
    std::snprintf(a, sizeof a, "%s %04X", d.name, tr.seed);
    if (st.message[0]) std::snprintf(b, sizeof b, "%s", st.message);
    else if (st.last_value >= 0) {
        const int k = st.last_value;
        const bool main = st.view == V_MAIN;
        const int v = main ? st.track(k).level : tr.value[k];
        const char *label = main ? track_def(k).shortname : d.param[k].label;
        if (st.knob_caught[k] || st.knob_pos[k] < 0) std::snprintf(b, sizeof b, main ? "%s LEVEL %d" : "%s %d", label, v);
        else std::snprintf(b, sizeof b, main ? "%s LEVEL %d (%d)" : "%s %d (%d)", label, v, st.knob_pos[k]);   // (where the knob is)
    } else if (tr.muted) std::snprintf(b, sizeof b, "MUTED");
    else if (d.lanes > 1) std::snprintf(b, sizeof b, "T%d LANE %s", st.selected + 1, d.lane_name[st.lane[st.selected]]);
    else std::snprintf(b, sizeof b, "T%d OF %d", st.selected + 1, kTracks);
    const int asc = u8g2_GetAscent(g), top = kFooterY + 1, h = DISPLAY_HEIGHT - top, gap = 6;
    const int y1 = top + (h - (2 * asc + gap)) / 2 + asc;
    canvas_color(&cv, color_of(st.selected), kBlack);
    u8g2_DrawStr(g, 1, y1, a);
    canvas_color(&cv, kWhite, kBlack);
    u8g2_DrawStr(g, 1, y1 + asc + gap, b);
}

/* ---------------- the LEDs ---------------- */

led_color_t led(uint32_t rgb, int pct) {
    return led_color_t{static_cast<uint8_t>(((rgb >> 16) & 0xFF) * pct / 100), static_cast<uint8_t>(((rgb >> 8) & 0xFF) * pct / 100),
                       static_cast<uint8_t>((rgb & 0xFF) * pct / 100)};
}

void update_leds() {
    const uint32_t white = 0xFFFFFF, dim = 0x303030;
    const uint32_t sel_rgb = track_def(st.selected).color;
    static const KeyPage kPages[3] = {K_STEP, K_TRACK, K_PERF};
    for (int c = 0; c < 3; c++) leds_set(HWV2_KEY(0, c), led(st.keys == kPages[c] ? white : dim, 100));
    leds_set(HWV2_KEY(0, 3), led(st.shift.on() ? white : dim, 100));
    const Track &tr = st.sel();
    for (int i = 0; i < 16; i++) {
        const int ctl = HWV2_KEY(1 + i / 4, i % 4);
        led_color_t c = led(0, 0);
        if (st.keys == K_STEP) {
            const int s = tr.step[st.lane[st.selected]][i];
            if (st.playing && i == st.step) c = led(white, 60);
            else if (s == S_ACCENT) c = led(sel_rgb, 100);
            else if (s == S_HIT) c = led(sel_rgb, 35);
            else if (s == S_HOLD) c = led(sel_rgb, 12);
        } else if (st.keys == K_TRACK) {
            if (i < kTracks) c = led(track_def(i).color, i == st.selected ? 100 : 25);
            else if (i == 7 || i == 15) c = led(dim, 100);
            else c = st.track(i - 8).muted ? led(0, 0) : led(track_def(i - 8).color, 25);
        } else {
            if (i < 8) c = led(dim, 100);
            else c = i - 8 == st.cur ? led(white, 80) : (i - 8 == st.queued ? led(0xFFC000, 80) : led(dim, 100));
        }
        leds_set(ctl, c);
    }
    for (int k = 0; k < kKnobs; k++) {
        led_color_t c;
        if (st.view == V_MAIN) c = st.track(k).muted ? led(0, 0) : led(track_def(k).color, st.knob_caught[k] ? 100 : 30);
        else c = st.sel().lock[k] ? led(white, 80) : led(sel_rgb, st.knob_caught[k] ? 100 : 30);
        leds_set(kKnobCtl[k], c);
    }
    leds_set(HWV2_VOLUME, led(dim, 100));
    leds_show();
}
}  // namespace

namespace tg { AppState &app_state() { return st; } }

void app_start(u8g2_t *display) {
    (void)display;                                    // (the board's one-colour u8g2: this app draws on its colour canvas)
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    st = AppState{};
    mod_touched = 0;
    for (Pattern &p : st.pattern) tg_init_pattern(p);   // eight copies of the first roll: variations are made from it
    leds_init();
    send_all();
    synth_set_volume(80);
    synth_set_transport(st.bpm, false);
    dirty = true;
}

void app_event(const input_event_t &e) {
    if (e.kind == IN_NONE) return;
    st.rng.next();                                    // every control used stirs the dice's seeds
    const bool press = e.kind == IN_PRESS;
    const bool other = !(e.ctl == HWV2_KEY(0, 3) || e.ctl == HWV2_BTN_1);
    if (other && e.kind != IN_RELEASE) {              // a modifier held while something else is used: its release does not latch it
        if (st.shift.held) st.shift.used = true;
        if (st.mod.held) st.mod.used = true;
    }
    const bool mod_was = st.mod.on();
    for (int k = 0; k < kKnobs; k++)
        if (e.ctl == kKnobCtl[k] && e.kind == IN_VALUE) { knob(k, e.value); dirty = true; return; }
    switch (e.ctl) {
    case HWV2_VOLUME:
        if (e.kind == IN_VALUE) synth_set_volume((e.value * 100 + INPUT_VALUE_MAX / 2) / INPUT_VALUE_MAX);
        return;                                       // (nothing on the screen changes)
    case HWV2_PLAY: if (press) set_playing(!st.playing); break;
    case HWV2_ENC_A:
        if (e.kind != IN_DELTA) return;
        if (st.shift.on()) {                          // SHFT + A: the lanes of the track
            const TrackDef &d = track_def(st.selected);
            uint8_t &l = st.lane[st.selected];
            l = static_cast<uint8_t>(((l + e.value) % d.lanes + d.lanes) % d.lanes);
            char text[24];
            std::snprintf(text, sizeof text, "LANE %s", d.lane_name[l]);
            say(text);
        } else select_track(st.selected + (e.value > 0 ? 1 : -1));
        break;
    case HWV2_ENC_A_SW: if (press) { st.view = st.view == V_MAIN ? V_ENGINE : V_MAIN; release_knobs(); say(""); } break;
    case HWV2_ENC_B:
        if (e.kind != IN_DELTA) return;
        if (st.shift.on()) {                          // SHFT + B: the tempo
            st.bpm = st.bpm + e.value < 60 ? 60 : (st.bpm + e.value > 200 ? 200 : st.bpm + e.value);
            synth_set_transport(st.bpm, st.playing);
            say("");
        } else {
            st.depth = st.depth + e.value < 0 ? 0 : (st.depth + e.value >= D_COUNT ? D_COUNT - 1 : st.depth + e.value);
            say("");
        }
        break;
    case HWV2_ENC_B_SW: if (press) go_to_pattern((st.queued >= 0 ? st.queued : st.cur) + 1); break;
    case HWV2_BTN_1: if (press) modifier_down(st.mod); else if (e.kind == IN_RELEASE) modifier_up(st.mod); break;
    case HWV2_BTN_2: if (press) dice(); break;
    case HWV2_BTN_3: if (press) back(); break;
    default: {
        const input_control_t *c = nullptr;
        int n = 0;
        const input_control_t *table = input_controls(&n);
        if (e.ctl >= 0 && e.ctl < n) c = &table[e.ctl];
        if (!c || c->kind != CK_KEY) return;
        if (c->row == 0) {                            // the function keys
            if (c->col == 3) { if (press) modifier_down(st.shift); else if (e.kind == IN_RELEASE) modifier_up(st.shift); }
            else if (press) st.keys = c->col == 0 ? K_STEP : (c->col == 1 ? K_TRACK : K_PERF);
        } else if (press) step_key((c->row - 1) * 4 + c->col);
        break;
    }
    }
    if (mod_was && !st.mod.on()) mod_touched = 0;    // MOD off: the next MOD + knob toggles again
    if (!mod_was && st.mod.on()) mod_touched = 0;
    dirty = true;
}

void app_draw() {
    const uint32_t taken = synth_taken();             // a queued pattern became the playing one: its values now
    if (taken != st.taken) {
        st.taken = taken;
        if (st.queued >= 0 && static_cast<int>(taken & 0xFF) == st.queued) {
            st.cur = st.queued;
            st.queued = -1;
            for (int t = 0; t < kTracks; t++) send_track(t);
            release_knobs();
        }
        dirty = true;
    }
    const int step = st.playing ? synth_step() : -1;
    if (step != st.step) { st.step = step; dirty = true; }
    if (!dirty) return;
    dirty = false;
    u8g2_t *g = canvas_u8g2(&cv);
    canvas_clear(&cv, kBlack);
    u8g2_SetFont(g, u8g2_font_04b_03b_tr);
    u8g2_SetFontMode(g, 1);                           // transparent: the text over the row's own colour
    u8g2_SetDrawColor(g, 1);
    draw_header(g);
    if (st.view == V_MAIN) {
        for (int t = 0; t < kTracks; t++) draw_row(g, t);
    } else {
        const uint16_t c = color_of(st.selected);
        draw_wave(g, c);
        draw_values(c);
    }
    draw_footer(g);
    canvas_send(&cv);
    update_leds();
}
