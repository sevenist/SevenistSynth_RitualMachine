// The node GUI's mock-ups (apps/ritual_machine/docs/GUI_NODES.md) as u8g2 drawing on the second prototype's 128 x 160 colour screen. Three frames,
// the same as docs/images/gui_nodes_mockup.svg: the MAP of a patch, one NODE with its ports, the PICK list of a cable being made.
// They are drawn as plain data (tables below), with no input: the frames change by themselves every 4 s, or at once when the board's BOOT
// button (GPIO 0) is pressed; after a press the frames stop changing by themselves.
// Nothing else is in the firmware: no engine, no audio, no LEDs, no card (platformio.ini).
#include <Arduino.h>
#include <math.h>
#include "hal/hal_display.h"
#include "ui/canvas.h"
#include "boards/esp32/board_pins.h"
#include "boards/esp32/display_panel.h"

// canvas_send's target (hal/hal_display.h): the library's display task is not in this firmware, so a frame goes straight to the panel (the whole
// of it: the panel is not asked which tiles changed).
extern "C" void display_send_rgb565(const uint16_t *frame) { display_panel_show_rgb565(frame, nullptr); }
extern "C" void display_send_indexed(const uint8_t *, const uint16_t *) {}     // (ui/canvas.c's indexed frames: this firmware draws RGB565 only)

namespace {

// The palette (GUI_NODES.md, section 5).
const uint16_t BG = CANVAS_HEX(0x000000), PANEL = CANVAS_HEX(0x141A24), TILE = CANVAS_HEX(0x161C28), TX = CANVAS_HEX(0xE8ECF2), DIM = CANVAS_HEX(0x5A6272);
const uint16_t SRC = CANVAS_HEX(0x3DDC84), SHP = CANVAS_HEX(0xA87BFF), MOD = CANVAS_HEX(0xFFB020), FX = CANVAS_HEX(0x35C2E8), UTL = CANVAS_HEX(0x8A93A3), OUT = CANVAS_HEX(0xFF5A5F);
const uint16_t GATE = CANVAS_HEX(0xFF4FD8), WHITE = 0xFFFF;

uint16_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
canvas_t cv;
u8g2_t *g;

// ---- drawing helpers (the canvas's own fill and u8g2's text)
void box(int x, int y, int w, int h, uint16_t c) { canvas_fill(&cv, x, y, w, h, c); }
void outline(int x, int y, int w, int h, uint16_t c, int t = 1) {
    box(x, y, w, t, c); box(x, y + h - t, w, t, c); box(x, y, t, h, c); box(x + w - t, y, t, h, c);
}
// A horizontal / vertical line from a to b (inclusive); on / off: a dash pattern in pixels (off = 0: solid).
void hline(int x0, int x1, int y, uint16_t c, int on = 1, int off = 0) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0, k = 0; x <= x1; x++, k++) if (!off || k % (on + off) < on) box(x, y, 1, 1, c);
}
void vline(int x, int y0, int y1, uint16_t c, int on = 1, int off = 0) {
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0, k = 0; y <= y1; y++, k++) if (!off || k % (on + off) < on) box(x, y, 1, 1, c);
}
enum { LEFT, RIGHT, CENTER };
const uint8_t *const FONT = u8g2_font_5x7_tr, *const SMALL = u8g2_font_4x6_tr;
// y is the baseline.
void text(int x, int y, const char *s, uint16_t c, int align = LEFT, const uint8_t *font = FONT) {
    u8g2_SetFont(g, font);
    u8g2_SetFontMode(g, 1);                          // transparent: the colour behind stays
    canvas_color(&cv, c, BG);
    const int w = u8g2_GetStrWidth(g, s);
    u8g2_DrawStr(g, align == RIGHT ? x - w : align == CENTER ? x - w / 2 : x, y, s);
}
void header(const char *name, uint16_t family, const char *right) {
    box(0, 0, 128, 12, family);
    text(3, 9, name, BG);
    text(125, 9, right, BG, RIGHT);
}
void legend(const char *left, const char *right) {
    box(0, 150, 128, 10, PANEL);
    text(3, 157, left, DIM, LEFT, SMALL);
    text(125, 157, right, DIM, RIGHT, SMALL);
}
void chip(int x, int y, int w, const char *s, uint16_t c) { outline(x, y, w, 8, c); text(x + 2, y + 7, s, c, LEFT, SMALL); }

// ---- 1: the MAP
struct Node { const char *name; uint8_t col, row; uint16_t family; };
const Node nodes[] = {
    {"OSC", 0, 0, SRC}, {"SMP", 1, 0, SRC}, {"LFO", 2, 0, MOD}, {"ENV", 3, 0, MOD},
    {"FLT", 0, 1, SHP}, {"FL2", 1, 1, SHP}, {"SAT", 0, 2, SHP}, {"VCA", 1, 2, UTL},
    {"CHR", 0, 3, FX},  {"MIX", 0, 4, UTL}, {"OUT", 0, 5, OUT},
};
const int colx[4] = {4, 36, 68, 100};
int rowy(int r) { return 16 + r * 17; }

void draw_map() {
    canvas_clear(&cv, BG);
    header("MAP  patch 1", OUT, "11 nodes");
    auto cx = [](int c) { return colx[c] + 14; };
    // audio: straight down a column, MIX takes the second branch from the side
    vline(cx(0), rowy(0) + 14, rowy(5), TX);
    vline(cx(1), rowy(0) + 14, rowy(2) + 14 + 0, TX);
    vline(cx(1), rowy(2) + 14, rowy(4) + 7, TX); hline(cx(0) + 14, cx(1), rowy(4) + 7, TX);
    // the gate: ENV to VCA, dotted
    vline(cx(3), rowy(0) + 14, rowy(2) + 7, GATE, 1, 2); hline(colx[1] + 28, cx(3), rowy(2) + 7, GATE, 1, 2);
    // modulation: LFO to the filter's cutoff, dashed amber, through the gaps between the tiles
    hline(colx[1] + 31, colx[2], rowy(0) + 7, MOD, 3, 2);
    vline(colx[1] + 31, rowy(0) + 7, rowy(1) - 2, MOD, 3, 2);
    hline(cx(0) + 5, colx[1] + 31, rowy(1) - 2, MOD, 3, 2);
    vline(cx(0) + 5, rowy(1) - 2, rowy(1), MOD, 3, 2);
    int i = 0;
    for (const Node &n : nodes) {
        const int x = colx[n.col], y = rowy(n.row);
        box(x, y, 28, 14, TILE);
        box(x, y, 2, 14, n.family);
        text(x + 5, y + 9, n.name, TX);
        box(x + 5, y + 11, 6 + (i * 7) % 13, 2, n.family);      // the main value's bar
        i++;
    }
    outline(colx[0] - 2, rowy(1) - 2, 32, 18, WHITE, 2);        // the cursor, on the filter
    box(0, 118, 128, 32, PANEL);
    box(4, 122, 5, 5, SHP);
    text(12, 128, "FILTER 1   LP24", TX);
    text(4, 137, "Cut 2.4k  Res .35", TX);
    chip(4, 140, 33, "in<-OSC", TX);
    chip(40, 140, 37, "out->SAT", TX);
    chip(80, 140, 41, "cut<-LFO", MOD);
    legend("A:value  B:zoom", "Btn1:patch");
}

// ---- 2: one NODE
void draw_node() {
    canvas_clear(&cv, BG);
    header("FLT  3/11", SHP, "LP24");
    box(0, 12, 128, 46, CANVAS_HEX(0x10151F));
    vline(4 + 54, 16, 55, DIM, 1, 2);
    int prev = -1;
    for (int i = 0; i <= 120; i++) {                              // a 4-pole low pass with a resonance peak
        const float x = i / 120.0f;
        const float u = x / 0.45f, u2 = u * u, u4 = u2 * u2;
        float y = 1.0f / sqrtf(1.0f + u4 * u4) + 0.55f * expf(-powf((x - 0.43f) / 0.04f, 2));
        if (y > 1.35f) y = 1.35f;
        const int py = 54 - (int)(y * 30);
        const int from = prev < 0 ? py : prev;
        for (int yy = min(from, py); yy <= max(from, py); yy++) box(4 + i, yy, 1, 1, SHP);
        prev = py;
    }
    struct Row { const char *name, *value; float v; bool mod; };
    const Row rows[] = {{"Cutoff", "2.4k", 0.62f, true}, {"Reso", "0.35", 0.35f, false}, {"Drive", "15%", 0.15f, false}, {"KeyTrk", "50%", 0.5f, false}};
    int y = 60;
    for (const Row &r : rows) {
        box(2, y, 124, 11, TILE);
        box(2, y, (int)(124 * r.v), 11, CANVAS_HEX(0x3A2A66));
        text(5, y + 8, r.name, TX);
        text(123, y + 8, r.value, TX, RIGHT);
        if (r.mod) { box(2, y + 9, 97, 2, MOD); text(66, y + 8, "v", MOD); }   // modulated: the live position under the value
        y += 12;
    }
    text(3, 117, "PORTS", DIM, LEFT, SMALL);
    box(0, 120, 128, 28, PANEL);
    box(0, 130, 128, 9, CANVAS_HEX(0x2A2410));                    // the focused port row
    text(4, 128, "in  pitch", TX, LEFT, SMALL);   text(124, 128, "<- NoteIn", TX, RIGHT, SMALL);
    text(4, 137, "in  cutoff", TX, LEFT, SMALL);  text(124, 137, "<- LFO 1 +40%", MOD, RIGHT, SMALL);
    text(4, 146, "out", TX, LEFT, SMALL);         text(124, 146, "-> SAT", TX, RIGHT, SMALL);
    legend("A:edit push:follow", "Back:map");
}

// ---- 3: PICK a source
void draw_pick() {
    canvas_clear(&cv, BG);
    header("PATCH cutoff", MOD, "Btn1 held");
    text(4, 22, "compatible sources", DIM, LEFT, SMALL);
    struct Item { const char *name, *info; };
    const Item items[] = {{"LFO 1", "sine 0.5Hz"}, {"LFO 2", "tri 3Hz"}, {"ENV 1", "adsr"}, {"Multi Env", "4 pts"},
                          {"Velocity", ""}, {"Macro 3", "global"}, {"Motion Seq", "16 steps"}};
    int y = 26;
    for (int i = 0; i < 7; i++) {
        const bool sel = i == 1;
        box(2, y, 124, 12, sel ? CANVAS_HEX(0x2A2410) : TILE);
        if (sel) outline(2, y, 124, 12, MOD);
        box(2, y, 3, 12, MOD);
        text(9, y + 9, items[i].name, TX);
        text(123, y + 9, items[i].info, DIM, RIGHT, SMALL);
        y += 13;
    }
    box(2, 120, 124, 26, PANEL);
    text(5, 129, "cutoff already has LFO 1", TX, LEFT, SMALL);
    const char *const buttons[3] = {"Replace", "Add (sum)", "Cancel"};
    for (int j = 0; j < 3; j++) {
        const int x = 4 + j * 40;
        if (j == 0) box(x, 133, 38, 11, MOD); else { box(x, 133, 38, 11, TILE); outline(x, 133, 38, 11, DIM); }
        text(x + 19, 141, buttons[j], j == 0 ? BG : TX, CENTER, SMALL);
    }
    legend("joy:pick", "release: connect");
}

void (*const frames[])() = {draw_map, draw_node, draw_pick};
int shown = -1;
bool paused = false;
uint32_t changed_at = 0;
bool last_button = true;

void show(int n) {
    shown = n;
    frames[n]();
    canvas_send(&cv);
    changed_at = millis();
}

}  // namespace

void setup() {
    pinMode(PIN_PWR_ON_EN, OUTPUT);                  // the supply stays on after the power button's first push (on the battery)
    digitalWrite(PIN_PWR_ON_EN, HIGH);
    pinMode(0, INPUT_PULLUP);                        // BOOT
    canvas_init(&cv, pixels, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    g = canvas_u8g2(&cv);
    display_panel_init();
    show(0);
}

void loop() {
    const bool button = digitalRead(0);
    if (!button && last_button) { paused = true; show((shown + 1) % 3); }
    last_button = button;
    if (!paused && millis() - changed_at > 4000) show((shown + 1) % 3);
    delay(20);
}
