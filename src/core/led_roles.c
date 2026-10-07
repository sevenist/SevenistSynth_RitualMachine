#include "core/led_roles.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *name; led_color_t rgb; } palette[LED_PALETTE] = {
    {"Off",    {0, 0, 0}},       {"White",  {255, 255, 255}}, {"Red",    {255, 0, 0}},     {"Orange", {255, 90, 0}},
    {"Amber",  {255, 150, 0}},   {"Yellow", {255, 200, 0}},   {"Lime",   {140, 255, 0}},   {"Green",  {0, 255, 0}},
    {"Mint",   {0, 255, 190}},   {"Teal",   {0, 200, 255}},   {"Sky",    {60, 120, 255}},  {"Blue",   {0, 0, 255}},
    {"Indigo", {80, 0, 255}},    {"Violet", {170, 0, 255}},   {"Pink",   {255, 0, 140}},   {"Rose",   {255, 60, 90}},
};
enum { P_OFF, P_WHITE, P_RED, P_ORANGE, P_AMBER, P_YELLOW, P_LIME, P_GREEN, P_MINT, P_TEAL, P_SKY, P_BLUE, P_INDIGO, P_VIOLET, P_PINK, P_ROSE };

static const char *const role_names[LR_COUNT] = {"Note", "Sharp", "Root", "Shift", "Mod", "Menu", "Back", "Play", "Octave", "Jump", "Nav", "None"};

#define SH(c, p) ((led_shade_t){(c), (p)})
// The colours of the first version as they looked on the board (dim when idle, full when active), as palette colour + brightness. That
// version sent red and green swapped (LED colour order, fixed 2026-10-07): these are the nearest palette colours to what was seen, which the
// user liked; Play stays green and Back stays red (the user's choice; they looked red / green then).
static const led_role_def_t defaults[LR_COUNT] = {
    [LR_NOTE]   = {SH(P_WHITE, 18),  SH(P_LIME, 100)},
    [LR_SHARP]  = {SH(P_BLUE, 16),   SH(P_LIME, 100)},
    [LR_ROOT]   = {SH(P_VIOLET, 55), SH(P_LIME, 100)},
    [LR_SHIFT]  = {SH(P_LIME, 31),   SH(P_LIME, 100)},
    [LR_MOD]    = {SH(P_MINT, 27),   SH(P_MINT, 100)},
    [LR_MENU]   = {SH(P_BLUE, 35),   SH(P_INDIGO, 100)},
    [LR_BACK]   = {SH(P_RED, 35),    SH(P_RED, 35)},
    [LR_PLAY]   = {SH(P_GREEN, 20),  SH(P_GREEN, 100)},
    [LR_OCTAVE] = {SH(P_TEAL, 30),   SH(P_TEAL, 100)},
    [LR_JUMP]   = {SH(P_PINK, 16),   SH(P_PINK, 63)},
    [LR_NAV]    = {SH(P_WHITE, 12),  SH(P_WHITE, 12)},
    [LR_NONE]   = {SH(P_OFF, 0),     SH(P_OFF, 0)},
};

static led_role_def_t g_roles[LR_COUNT];
static unsigned       g_rev;

void led_roles_init(void) { memcpy(g_roles, defaults, sizeof g_roles); g_rev++; }

const led_role_def_t *led_role(int r) { return &g_roles[r >= 0 && r < LR_COUNT ? r : LR_NONE]; }

static led_shade_t clamp_shade(led_shade_t s) { return SH(s.color % LED_PALETTE, s.pct > 100 ? 100 : s.pct); }

void led_role_set(int r, led_role_def_t d) {
    if (r < 0 || r >= LR_COUNT) return;
    d.idle = clamp_shade(d.idle); d.active = clamp_shade(d.active);
    if (!memcmp(&g_roles[r], &d, sizeof d)) return;
    g_roles[r] = d;
    g_rev++;
}

const char *led_role_name(int r) { return r >= 0 && r < LR_COUNT ? role_names[r] : "?"; }
bool led_role_has_active(int r) { return r != LR_BACK && r != LR_NAV && r != LR_NONE; }

int led_role_of(key_fn_t f) {
    switch (f.act) {
        case ACT_NOTE: {
            const int pc = ((KEYBOARD_BASE_NOTE + f.arg) % 12 + 12) % 12;       // the octave shift moves every key by 12: same role
            if (pc == 0) return LR_ROOT;
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10 ? LR_SHARP : LR_NOTE;
        }
        case ACT_SHIFT:  return LR_SHIFT;
        case ACT_MOD:    return LR_MOD;
        case ACT_MENU:   return LR_MENU;
        case ACT_BACK:   return LR_BACK;
        case ACT_PLAY:   return LR_PLAY;
        case ACT_OCTAVE: return LR_OCTAVE;
        case ACT_JUMP:   return LR_JUMP;
        case ACT_NONE:   return LR_NONE;
        default:         return LR_NAV;                                     // navigation and the other control actions
    }
}

const char *led_palette_name(int c) { return palette[c % LED_PALETTE].name; }

led_color_t led_shade_rgb(led_shade_t s) {
    const led_color_t c = palette[s.color % LED_PALETTE].rgb;
    const int p = s.pct > 100 ? 100 : s.pct;
    return (led_color_t){(uint8_t)((c.r * p + 50) / 100), (uint8_t)((c.g * p + 50) / 100), (uint8_t)((c.b * p + 50) / 100)};
}

led_color_t led_role_rgb(int r, bool active) {
    const led_role_def_t *d = led_role(r);
    return led_shade_rgb(active && led_role_has_active(r) ? d->active : d->idle);
}

unsigned led_roles_rev(void) { return g_rev; }

/* ---------------- ui.cfg lines: "led note white 18 orange 100" ---------------- */

static void lower(const char *in, char *out, int n) {
    int i = 0;
    for (; in[i] && i < n - 1; i++) out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = 0;
}

static int find_word(const char *w, const char *(*name)(int), int count) {
    char t[16];
    for (int i = 0; i < count; i++) { lower(name(i), t, sizeof t); if (!strcmp(t, w)) return i; }
    return -1;
}

int led_roles_to_text(char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "leds\n");
    for (int r = 0; r < LR_COUNT && n < cap; r++) {
        const led_role_def_t *d = &g_roles[r];
        if (!memcmp(d, &defaults[r], sizeof *d)) continue;
        char rn[16], ic[16], ac[16];
        lower(role_names[r], rn, sizeof rn);
        lower(palette[d->idle.color].name, ic, sizeof ic);
        lower(palette[d->active.color].name, ac, sizeof ac);
        n += snprintf(buf + n, (size_t)(cap - n), "led %s %s %d %s %d\n", rn, ic, d->idle.pct, ac, d->active.pct);
    }
    return n < cap ? n : cap - 1;
}

void led_roles_from_text_begin(void) { memcpy(g_roles, defaults, sizeof g_roles); g_rev++; }

bool led_roles_from_line(const char *line) {
    char w[8], rn[16], ic[16], ac[16];
    int ip = 0, ap = 0;
    if (sscanf(line, "%7s %15s %15s %d %15s %d", w, rn, ic, &ip, ac, &ap) != 6 || strcmp(w, "led")) return false;
    const int r = find_word(rn, led_role_name, LR_COUNT), i = find_word(ic, led_palette_name, LED_PALETTE), a = find_word(ac, led_palette_name, LED_PALETTE);
    if (r < 0 || i < 0 || a < 0 || ip < 0 || ap < 0) return false;
    led_role_set(r, (led_role_def_t){SH((uint8_t)i, (uint8_t)(ip > 100 ? 100 : ip)), SH((uint8_t)a, (uint8_t)(ap > 100 ? 100 : ap))});
    return true;
}
