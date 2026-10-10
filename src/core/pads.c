#include "core/pads.h"
#include "core/fine_step.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pad_default(pad_t *p) {
    memset(p, 0, sizeof *p);
    p->end = 1000;
    p->mode = PAD_ONESHOT;
    p->tail_ms = 20;
    p->level = 80;
    p->cut = 100;
}

void pads_init(pads_t *ps) { for (int i = 0; i < PAD_COUNT; i++) pad_default(&ps->pad[i]); }

void pad_label(int pad, char *out, int n) {
    if (pad < 0 || pad >= PAD_COUNT) { snprintf(out, (size_t)n, "--"); return; }
    snprintf(out, (size_t)n, "%c%d", 'A' + pad / PADS_PER_BANK, pad % PADS_PER_BANK + 1);
}

static const char *const labels[PADP_COUNT] = {"Smp", "Mode", "Strt", "End", "Loop", "Tail", "Tune", "Fine", "Lvl", "Pan", "Cut", "Res", "Chok", "Mute", "Grid"};
static const char *const mode_names[PAD_MODE_COUNT] = {"One", "Gate", "Togl"};
static const char *const mode_toks[PAD_MODE_COUNT]  = {"one", "gate", "toggle"};
static const char *const loop_names[PAD_LOOP_COUNT] = {"Off", "Trim", "File"};
static const char *const grid_names[PAD_GRID_COUNT] = {"Off", "Beat", "Bar"};
static const char *const grid_toks[PAD_GRID_COUNT]  = {"off", "beat", "bar"};

const char *pad_param_label(pad_param_t k) { return k >= 0 && k < PADP_COUNT ? labels[k] : "?"; }

float pad_cut_hz(const pad_t *p) { return p->cut >= 100 ? 0.0f : 20.0f * powf(1000.0f, p->cut / 100.0f); }

// A trim point (1/1000 of the file) as a time when the sample is known.
static void fmt_trim(int permille, uint32_t frames, uint32_t rate, char *out, int n) {
    if (!frames || !rate) { snprintf(out, (size_t)n, "%d.%d%%", permille / 10, permille % 10); return; }
    const double s = (double)permille * frames / 1000.0 / rate;
    if (s < 1.0) snprintf(out, (size_t)n, "%dms", (int)(s * 1000.0 + 0.5));
    else         snprintf(out, (size_t)n, "%.2fs", s);
}

void pad_param_format(const pad_t *p, pad_param_t k, uint32_t frames, uint32_t rate, char *out, int n) {
    switch (k) {
        case PADP_FILE:  snprintf(out, (size_t)n, "%s", p->file[0] ? p->file : "--"); break;
        case PADP_MODE:  snprintf(out, (size_t)n, "%s", mode_names[p->mode % PAD_MODE_COUNT]); break;
        case PADP_START: fmt_trim(p->start, frames, rate, out, n); break;
        case PADP_END:   fmt_trim(p->end, frames, rate, out, n); break;
        case PADP_LOOP:  snprintf(out, (size_t)n, "%s", loop_names[p->loop % PAD_LOOP_COUNT]); break;
        case PADP_TAIL:  snprintf(out, (size_t)n, "%dms", p->tail_ms); break;
        case PADP_TUNE:  snprintf(out, (size_t)n, "%+dst", p->tune); break;
        case PADP_FINE:  snprintf(out, (size_t)n, "%+dc", p->fine); break;
        case PADP_LEVEL: snprintf(out, (size_t)n, "%d%%", p->level); break;
        case PADP_PAN:   if (p->pan) snprintf(out, (size_t)n, "%c%d", p->pan < 0 ? 'L' : 'R', abs(p->pan)); else snprintf(out, (size_t)n, "C"); break;
        case PADP_CUT: {
            const float hz = pad_cut_hz(p);
            if (hz <= 0.0f) snprintf(out, (size_t)n, "Off");
            else if (hz < 1000.0f) snprintf(out, (size_t)n, "%dHz", (int)(hz + 0.5f));
            else snprintf(out, (size_t)n, "%.1fk", hz / 1000.0f);
        } break;
        case PADP_RES:   snprintf(out, (size_t)n, "%d%%", p->res); break;
        case PADP_CHOKE: if (p->choke) snprintf(out, (size_t)n, "%d", p->choke); else snprintf(out, (size_t)n, "--"); break;
        case PADP_MUTE:  snprintf(out, (size_t)n, "%s", p->mute ? "On" : "Off"); break;
        case PADP_GRID:  snprintf(out, (size_t)n, "%s", grid_names[p->grid % PAD_GRID_COUNT]); break;
        default:         snprintf(out, (size_t)n, "?"); break;
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// The tail steps by 5 ms up to 50, 10 up to 200, then 50 (up to 2 s).
static int tail_step(int ms, int dir) {
    const int st = dir > 0 ? (ms < 50 ? 5 : ms < 200 ? 10 : 50) : (ms <= 50 ? 5 : ms <= 200 ? 10 : 50);
    return clampi(ms + dir * fine_int_step(st), 0, 2000);
}

bool pad_param_adjust(pad_t *p, pad_param_t k, int dir) {
    const pad_t old = *p;
    const int d = dir > 0 ? 1 : -1;
    switch (k) {
        case PADP_MODE:  p->mode = (uint8_t)((p->mode + PAD_MODE_COUNT + d) % PAD_MODE_COUNT); break;
        case PADP_START: p->start = (uint16_t)clampi(p->start + d * fine_int_step(5), 0, p->end - 1); break;
        case PADP_END:   p->end = (uint16_t)clampi(p->end + d * fine_int_step(5), p->start + 1, 1000); break;
        case PADP_LOOP:  p->loop = (uint8_t)((p->loop + PAD_LOOP_COUNT + d) % PAD_LOOP_COUNT); break;
        case PADP_TAIL:  p->tail_ms = (uint16_t)tail_step(p->tail_ms, d); break;
        case PADP_TUNE:  p->tune = (int8_t)clampi(p->tune + d, -24, 24); break;
        case PADP_FINE:  p->fine = (int8_t)clampi(p->fine + d, -50, 50); break;
        case PADP_LEVEL: p->level = (uint8_t)clampi(p->level + d * fine_int_step(5), 0, 100); break;
        case PADP_PAN:   p->pan = (int8_t)clampi(p->pan + d * fine_int_step(5), -50, 50); break;
        case PADP_CUT:   p->cut = (uint8_t)clampi(p->cut + d * fine_int_step(2), 0, 100); break;
        case PADP_RES:   p->res = (uint8_t)clampi(p->res + d * fine_int_step(5), 0, 100); break;
        case PADP_CHOKE: p->choke = (uint8_t)((p->choke + PAD_CHOKES + 1 + d) % (PAD_CHOKES + 1)); break;
        case PADP_MUTE:  p->mute = !p->mute; break;
        case PADP_GRID:  p->grid = (uint8_t)((p->grid + PAD_GRID_COUNT + d) % PAD_GRID_COUNT); break;
        default: break;
    }
    return memcmp(&old, p, sizeof old) != 0;
}

bool pad_param_set_norm(pad_t *p, pad_param_t k, float x) {
    const pad_t old = *p;
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    const int v1000 = (int)(x * 1000.0f + 0.5f), v100 = (int)(x * 100.0f + 0.5f);
    switch (k) {
        case PADP_MODE:  p->mode = (uint8_t)clampi((int)(x * PAD_MODE_COUNT), 0, PAD_MODE_COUNT - 1); break;
        case PADP_START: p->start = (uint16_t)clampi(v1000, 0, p->end - 1); break;
        case PADP_END:   p->end = (uint16_t)clampi(v1000, p->start + 1, 1000); break;
        case PADP_LOOP:  p->loop = (uint8_t)clampi((int)(x * PAD_LOOP_COUNT), 0, PAD_LOOP_COUNT - 1); break;
        case PADP_TAIL:  p->tail_ms = (uint16_t)(x * x * 2000.0f + 0.5f); break;
        case PADP_TUNE:  p->tune = (int8_t)clampi((int)lroundf(x * 48.0f) - 24, -24, 24); break;
        case PADP_FINE:  p->fine = (int8_t)clampi((int)lroundf(x * 100.0f) - 50, -50, 50); break;
        case PADP_LEVEL: p->level = (uint8_t)v100; break;
        case PADP_PAN:   p->pan = (int8_t)(v100 - 50); break;
        case PADP_CUT:   p->cut = (uint8_t)v100; break;
        case PADP_RES:   p->res = (uint8_t)v100; break;
        case PADP_CHOKE: p->choke = (uint8_t)clampi((int)(x * (PAD_CHOKES + 1)), 0, PAD_CHOKES); break;
        case PADP_MUTE:  p->mute = x >= 0.5f; break;
        case PADP_GRID:  p->grid = (uint8_t)clampi((int)(x * PAD_GRID_COUNT), 0, PAD_GRID_COUNT - 1); break;
        default: break;
    }
    return memcmp(&old, p, sizeof old) != 0;
}

/* ---------------- pads.cfg ---------------- */

int pads_to_text(const pads_t *ps, char *buf, int cap) {
    pad_t d;
    pad_default(&d);
    int n = snprintf(buf, (size_t)cap, "# SynthCore pads (pads.cfg): one line per pad that differs from the defaults, the file name last\npads\n");
    for (int i = 0; i < PAD_COUNT && n < cap; i++) {
        const pad_t *p = &ps->pad[i];
        if (!memcmp(p, &d, sizeof d)) continue;
        char l[8], line[160];
        pad_label(i, l, sizeof l);
        int k = snprintf(line, sizeof line, "pad %s", l);
#define FIELD(cond, ...) if ((cond) && k < (int)sizeof line) k += snprintf(line + k, sizeof line - (size_t)k, __VA_ARGS__)
        FIELD(p->mode != d.mode, " mode %s", mode_toks[p->mode % PAD_MODE_COUNT]);
        FIELD(p->loop != d.loop, " loop %d", p->loop);
        FIELD(p->start != d.start, " start %d", p->start);
        FIELD(p->end != d.end, " end %d", p->end);
        FIELD(p->tail_ms != d.tail_ms, " tail %d", p->tail_ms);
        FIELD(p->tune != d.tune, " tune %d", p->tune);
        FIELD(p->fine != d.fine, " fine %d", p->fine);
        FIELD(p->level != d.level, " level %d", p->level);
        FIELD(p->pan != d.pan, " pan %d", p->pan);
        FIELD(p->cut != d.cut, " cut %d", p->cut);
        FIELD(p->res != d.res, " res %d", p->res);
        FIELD(p->choke != d.choke, " choke %d", p->choke);
        FIELD(p->mute != d.mute, " mute %d", p->mute);
        FIELD(p->grid != d.grid, " grid %s", grid_toks[p->grid % PAD_GRID_COUNT]);
        FIELD(p->file[0], " file %s", p->file);
#undef FIELD
        n += snprintf(buf + n, (size_t)(cap - n), "%s\n", line);
    }
    return n < cap ? n : cap - 1;
}

// "A1" .. "D16" -> 0..63, -1 when it is not a pad.
static int parse_label(const char *w) {
    const int bank = w[0] - 'A';
    const int num = atoi(w + 1);
    if (bank < 0 || bank >= PAD_BANKS || num < 1 || num > PADS_PER_BANK) return -1;
    return bank * PADS_PER_BANK + num - 1;
}

static void parse_pad(pad_t *p, const char *s) {
    pad_default(p);
    for (;;) {
        char key[16];
        int used = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (sscanf(s, "%15s%n", key, &used) != 1) return;
        s += used;
        while (*s == ' ' || *s == '\t') s++;
        if (!strcmp(key, "file")) {                                  // the rest of the line
            int len = 0;
            while (s[len] && s[len] != '\n' && s[len] != '\r') len++;
            while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
            if (len >= PAD_NAME_MAX) len = PAD_NAME_MAX - 1;
            memcpy(p->file, s, (size_t)len);
            p->file[len] = 0;
            return;
        }
        char val[16];
        if (sscanf(s, "%15s%n", val, &used) != 1) return;
        s += used;
        const int v = atoi(val);
        if (!strcmp(key, "mode")) { for (int m = 0; m < PAD_MODE_COUNT; m++) if (!strcmp(val, mode_toks[m])) p->mode = (uint8_t)m; }
        else if (!strcmp(key, "loop"))  p->loop = (uint8_t)clampi(v, 0, PAD_LOOP_COUNT - 1);
        else if (!strcmp(key, "start")) p->start = (uint16_t)clampi(v, 0, 999);
        else if (!strcmp(key, "end"))   p->end = (uint16_t)clampi(v, 1, 1000);
        else if (!strcmp(key, "tail"))  p->tail_ms = (uint16_t)clampi(v, 0, 2000);
        else if (!strcmp(key, "tune"))  p->tune = (int8_t)clampi(v, -24, 24);
        else if (!strcmp(key, "fine"))  p->fine = (int8_t)clampi(v, -50, 50);
        else if (!strcmp(key, "level")) p->level = (uint8_t)clampi(v, 0, 100);
        else if (!strcmp(key, "pan"))   p->pan = (int8_t)clampi(v, -50, 50);
        else if (!strcmp(key, "cut"))   p->cut = (uint8_t)clampi(v, 0, 100);
        else if (!strcmp(key, "res"))   p->res = (uint8_t)clampi(v, 0, 100);
        else if (!strcmp(key, "choke")) p->choke = (uint8_t)clampi(v, 0, PAD_CHOKES);
        else if (!strcmp(key, "mute"))  p->mute = v != 0;
        else if (!strcmp(key, "grid")) { for (int g = 0; g < PAD_GRID_COUNT; g++) if (!strcmp(val, grid_toks[g])) p->grid = (uint8_t)g; }
        if (p->start >= p->end) p->start = (uint16_t)(p->end - 1);
    }
}

bool pads_from_text(pads_t *ps, const char *txt) {
    bool is_pads = false;
    pads_t n;
    pads_init(&n);
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        char w1[16], w2[16];
        int used = 0;
        const int got = sscanf(line, "%15s %15s %n", w1, w2, &used);
        if (got >= 1 && !strcmp(w1, "pads")) is_pads = true;
        else if (got == 2 && !strcmp(w1, "pad") && used > 0) {
            const int i = parse_label(w2);
            if (i >= 0) parse_pad(&n.pad[i], line + used);
        }
    }
    if (!is_pads) return false;
    *ps = n;
    return true;
}

static char g_buf[STORAGE_FILE_MAX];             // the text of pads.cfg on its way to / from the card

bool pads_load(pads_t *ps) {
    return storage_read(PADS_FILE, g_buf, sizeof g_buf) >= 0 && pads_from_text(ps, g_buf);
}

bool pads_save(const pads_t *ps) {
    const int n = pads_to_text(ps, g_buf, sizeof g_buf);
    return storage_write(PADS_FILE, g_buf, n);
}
