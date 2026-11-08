// What the controls do, and the screen. Portable: the same file runs on the desktop and on HWV2. A minimal first screen (the main view
// comes next): the seven tracks in their colours, PLAY, encoder A picks a track, B2 rolls it.
#include "app.h"
#include <cstdio>
#include "boards/hwv2/hwv2_controls.h"
#include "synth.h"
#include "tg_model.h"
#include "ui/canvas.h"
#include "ui/gui.h"

namespace {
uint16_t frame[DISPLAY_WIDTH * DISPLAY_HEIGHT];
canvas_t cv;
gui_style_t style;
tg::Pattern pat;
int selected = 0;
bool playing = false;
uint16_t seed_counter = 1;
bool dirty = true;

void send_all() {
    for (int t = 0; t < tg::kTracks; t++) synth_set_track(t, pat.track[t]);
    synth_set_steps(pat);
}
}  // namespace

void app_start(u8g2_t *display) {
    (void)display;                              // (the board's one-colour u8g2: this app draws on its own colour canvas)
    canvas_init(&cv, frame, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    gui_style_init(&style, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    tg::tg_init_pattern(pat);
    send_all();
    synth_set_transport(132, false);
}

void app_event(const input_event_t &e) {
    if (e.ctl == HWV2_PLAY && e.kind == IN_PRESS) { playing = !playing; synth_set_transport(132, playing); }
    else if (e.ctl == HWV2_ENC_A && e.kind == IN_DELTA) selected = (selected + (e.value > 0 ? 1 : tg::kTracks - 1)) % tg::kTracks;
    else if (e.ctl == HWV2_BTN_2 && e.kind == IN_PRESS) {
        tg::tg_roll(pat.track[selected], selected, static_cast<uint16_t>(seed_counter++ * 7919u), tg::D_RAW);
        send_all();
    } else return;
    dirty = true;
}

void app_draw() {
    if (!dirty) return;
    dirty = false;
    u8g2_t *g = canvas_u8g2(&cv);
    canvas_clear(&cv, 0);
    gui_begin(g, &style);
    for (int t = 0; t < tg::kTracks; t++) {
        const uint16_t c = CANVAS_HEX(tg::track_def(t).color);
        const int y = 12 + t * 16;
        if (t == selected) canvas_fill(&cv, 0, y, DISPLAY_WIDTH, 16, c);
        canvas_color(&cv, t == selected ? 0 : c, t == selected ? c : 0);
        char line[24];
        std::snprintf(line, sizeof line, "%s %04X", tg::track_def(t).shortname, pat.track[t].seed);
        u8g2_DrawStr(g, 2, y + 12, line);
    }
    canvas_color(&cv, 0xFFFF, 0);
    u8g2_DrawStr(g, 2, 10, playing ? "TECHNOGEN >" : "TECHNOGEN .");
    canvas_send(&cv);
}
