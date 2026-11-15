#pragma once
// A sound as text: a synth's values by name and its macros. The file SynthLab Studio saves (presets/<synth>.sound) and a board reads.
// Portable C (header only: static inline), usable from C++.
//
//   # SynthLab Studio sound: Kick           a comment (any line starting with #)
//   name "Hollow Kick"                      the sound's name
//   value BODY/Tune 50                     a value by its path, "SECTION/Name" (the section: the PARAM_HEAD before it), in quotes
//   value "RUMBLE/Dly Mix" 0.3              when it holds a space; the number in the value's own unit
//   macro 1 "Punch" 0.500                   a macro (control/macro.h): its number, its name, its position 0..1
//   dest BODY/Tune 0.200 0.800 Exp          ... a destination: a value, Min and Max (0..1 of its range), the curve
//   link 2 0.000 0.500 Lin                  ... or another macro (macro 2, driven by this one)
//
// Values by name, not by place: a synth that gains, loses or reorders values still reads its sounds (a value it no longer has is
// dropped, one it gained keeps what it had), so a sound made on the desktop loads on a board whose engine was exported and fixed.
// A line the reader does not know is skipped. (Files written before the paths were quoted read too: in a value line the number is
// the last word, the path everything before it.)
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control/macro.h"
#include "control/param.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSET_LINE       200             // the longest line read (longer ones are cut)
#define PSET_PATH       64              // the longest path
#define PSET_KIND_VALUE 1               // a macro destination on a value (ctl_target_t prm: its index)

enum { PSET_HAD_VALUES = 1, PSET_HAD_MACROS = 2, PSET_HAD_NAME = 4 };     // what pset_from_text found

/* ---------------- words and lines ---------------- */

// The next line of *p into out (no \r / \n; cut to cap - 1), *p moved past it. False at the end of the text.
static inline bool pset_line(const char **p, char *out, int cap) {
    const char *s = *p;
    if (!*s) { if (cap > 0) out[0] = 0; return false; }
    const char *e = strchr(s, '\n');
    int n = e ? (int)(e - s) : (int)strlen(s);
    *p = s + n + (e ? 1 : 0);
    if (n > 0 && s[n - 1] == '\r') n--;
    if (cap <= 0) return true;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, s, (size_t)n);
    out[n] = 0;
    return true;
}

// The next word at *p (a word in quotes may hold spaces; the quotes are removed), *p moved past it. False at the end of the line.
static inline bool pset_word(const char **p, char *out, int cap) {
    const char *s = *p;
    int n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\r' || *s == '\n') { if (cap > 0) out[0] = 0; *p = s; return false; }
    if (*s == '"') {
        s++;
        while (*s && *s != '\r' && *s != '\n' && *s != '"') { if (n + 1 < cap) out[n++] = *s; s++; }
        if (*s == '"') s++;
    } else {
        while (*s && *s != '\r' && *s != '\n' && *s != ' ' && *s != '\t') { if (n + 1 < cap) out[n++] = *s; s++; }
    }
    if (cap > 0) out[n] = 0;
    *p = s;
    return true;
}

/* ---------------- values by path ---------------- */

// The section title over value i (its index), -1: none.
static inline int pset_section(const param_desc_t *v, int n, int i) {
    for (int k = (i < n ? i : n) - 1; k >= 0; k--)
        if (v[k].curve == PARAM_HEAD) return k;
    return -1;
}

// "BODY/Tune" ("Tune" when no section comes before it; a section title: its name; "?" for no value).
static inline void pset_path(const param_desc_t *v, int n, int i, char *out, int cap) {
    if (i < 0 || i >= n) { snprintf(out, (size_t)cap, "?"); return; }
    const int h = pset_section(v, n, i);
    if (h < 0 || v[i].curve == PARAM_HEAD) snprintf(out, (size_t)cap, "%s", v[i].name);
    else snprintf(out, (size_t)cap, "%s/%s", v[h].name, v[i].name);
}

// The path as a file word: in quotes when it holds a space.
static inline void pset_quote_path(const param_desc_t *v, int n, int i, char *out, int cap) {
    char p[PSET_PATH];
    pset_path(v, n, i, p, sizeof p);
    if (strchr(p, ' ')) snprintf(out, (size_t)cap, "\"%s\"", p);
    else snprintf(out, (size_t)cap, "%s", p);
}

// The value at `path`, -1: none. A bare name matches the first value of that name.
static inline int pset_find(const param_desc_t *v, int n, const char *path) {
    char p[PSET_PATH];
    for (int i = 0; i < n; i++) {
        pset_path(v, n, i, p, sizeof p);
        if (!strcmp(p, path)) return i;
    }
    for (int i = 0; i < n; i++)
        if (v[i].curve != PARAM_HEAD && !strcmp(v[i].name, path)) return i;
    return -1;
}

// A synth's file name: its name, a space or a slash as "_", then `ext` ("Kick Lite" + ".sound" -> "Kick_Lite.sound"). SynthLab Studio
// saves a synth's sound under it, a board reads it there.
static inline void pset_file_name(const char *synth, const char *ext, char *out, int cap) {
    int n = 0;
    for (const char *c = synth; *c && n + 1 < cap; c++) out[n++] = (*c == ' ' || *c == '/' || *c == '\\') ? '_' : *c;
    if (cap > 0) out[n] = 0;
    if (ext) snprintf(out + n, (size_t)(cap - n), "%s", ext);
}

/* ---------------- a sound ---------------- */

// What a sound is made of: the synth's values (PARAM_HEAD entries are its sections) and how to read / set them, its macros.
typedef struct {
    const param_desc_t *params;
    int   n;
    float (*get)(void *ctx, int i);            // value i in its own unit; NULL: the values are not written
    void  (*set)(void *ctx, int i, float v);   // NULL: the values are not read
    void *ctx;
    mac_t *macros;                             // MAC_MAX macros; NULL: none written or read
    char  *name;                               // the sound's name (name_cap bytes); NULL or "": none written, none read
    int    name_cap;
} pset_sound_t;

static inline void pset__add(char *buf, int cap, int *n, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    const int room = *n < cap ? cap - *n : 0;
    const int k = vsnprintf(room ? buf + *n : NULL, (size_t)room, fmt, a);
    va_end(a);
    if (k > 0) *n += k;
}

// The sound's text: `title` as a comment (NULL: none), the values, the macros. Returns the length of the whole text, like snprintf
// (the text is cut when that is cap or more).
static inline int pset_to_text(const pset_sound_t *s, const char *title, char *buf, int cap) {
    int n = 0;
    char path[PSET_PATH + 2];
    if (cap > 0) buf[0] = 0;
    if (title) pset__add(buf, cap, &n, "# %s\n", title);
    if (s->name && s->name[0]) pset__add(buf, cap, &n, "name \"%s\"\n", s->name);
    if (s->get)
        for (int i = 0; i < s->n; i++) {
            if (s->params[i].curve == PARAM_HEAD) continue;
            pset_quote_path(s->params, s->n, i, path, sizeof path);
            pset__add(buf, cap, &n, "value %s %g\n", path, (double)s->get(s->ctx, i));
        }
    if (s->macros) {
        pset__add(buf, cap, &n, "# macro N \"name\" position     dest SECTION/Value min max curve     link M min max curve (macro M driven by it)"
                                "   (positions 0..1 along the range)\n");
        for (int k = 0; k < MAC_MAX; k++) {
            const mac_t *m = &s->macros[k];
            pset__add(buf, cap, &n, "macro %d \"%s\" %.3f\n", k + 1, m->name, (double)m->pos);
            for (int i = 0; i < m->n && i < MAC_DESTS; i++) {
                const ctl_map_t *d = &m->d[i];
                const int j = mac_of(&d->t);
                if (j >= 0) {
                    pset__add(buf, cap, &n, "link %d %.3f %.3f %s\n", j + 1, (double)d->min, (double)d->max, crv_name(d->curve));
                } else if (d->t.kind == PSET_KIND_VALUE) {
                    pset_quote_path(s->params, s->n, d->t.prm, path, sizeof path);
                    pset__add(buf, cap, &n, "dest %s %.3f %.3f %s\n", path, (double)d->min, (double)d->max, crv_name(d->curve));
                }
            }
        }
    }
    return n;
}

static inline float pset__unit(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

// Reads `text`: every value it names is set (s->set), and when it has macros they replace s->macros (every macro's destinations are
// cleared first; a macro the text does not name keeps its name and position). Returns PSET_HAD_VALUES | MACROS | NAME for what it
// found (0: not a sound). *dropped (may be NULL): the lines it could not take (a value the synth does not have, a full macro, a link
// that would loop or to no macro).
static inline int pset_from_text(const pset_sound_t *s, const char *text, int *dropped) {
    int had = 0, lost = 0, cur = -1;
    char line[PSET_LINE], w[4][PSET_PATH];
    const char *p = text;
    while (pset_line(&p, line, sizeof line)) {
        const char *q = line;
        if (!pset_word(&q, w[0], sizeof w[0])) continue;
        if (!strcmp(w[0], "name")) {
            if (s->name && s->name_cap > 0) { pset_word(&q, s->name, s->name_cap); had |= PSET_HAD_NAME; }
        } else if (!strcmp(w[0], "value")) {
            if (!s->set) continue;
            // the number is the last word; the path all before it (quoted, or with spaces in an older file)
            while (*q == ' ' || *q == '\t') q++;
            int e = (int)strlen(q);
            while (e > 0 && (q[e - 1] == ' ' || q[e - 1] == '\t')) e--;
            int b = e;
            while (b > 0 && q[b - 1] != ' ' && q[b - 1] != '\t') b--;
            int pe = b;
            while (pe > 0 && (q[pe - 1] == ' ' || q[pe - 1] == '\t')) pe--;
            if (pe <= 0 || b >= e) continue;
            int ps = 0;
            if (q[0] == '"' && pe >= 2 && q[pe - 1] == '"') { ps = 1; pe--; }
            char path[PSET_PATH];
            snprintf(path, sizeof path, "%.*s", pe - ps, q + ps);
            const int i = pset_find(s->params, s->n, path);
            had |= PSET_HAD_VALUES;
            if (i < 0) { lost++; continue; }
            s->set(s->ctx, i, (float)atof(q + b));
        } else if (!s->macros) {
            continue;
        } else if (!strcmp(w[0], "macro")) {
            const int k = pset_word(&q, w[1], sizeof w[1]) ? atoi(w[1]) - 1 : -1;
            if (k < 0 || k >= MAC_MAX) continue;                                 // (not a macro line: skipped)
            if (!(had & PSET_HAD_MACROS))
                for (int j = 0; j < MAC_MAX; j++) { s->macros[j].n = 0; memset((void *)s->macros[j].d, 0, sizeof s->macros[j].d); }
            had |= PSET_HAD_MACROS;
            cur = k;
            mac_t *m = &s->macros[k];
            const char *q1 = strchr(q, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (q1 && q2) {
                snprintf(m->name, sizeof m->name, "%.*s", (int)(q2 - q1 - 1), q1 + 1);
                m->pos = (float)atof(q2 + 1);
            }
            m->pos = pset__unit(m->pos);
        } else if (cur >= 0 && (!strcmp(w[0], "dest") || !strcmp(w[0], "link"))) {
            const bool link = w[0][0] == 'l';
            if (!pset_word(&q, w[1], sizeof w[1]) || !pset_word(&q, w[2], sizeof w[2]) || !pset_word(&q, w[3], sizeof w[3])) continue;
            char cn[16];
            if (!pset_word(&q, cn, sizeof cn)) snprintf(cn, sizeof cn, "Lin");
            ctl_map_t d;
            if (link) {
                const int j = atoi(w[1]) - 1;
                d = ctl_map_plain(mac_target(j < 0 ? MAC_MAX : j));
            } else {
                const int i = pset_find(s->params, s->n, w[1]);
                if (i < 0 || s->params[i].curve == PARAM_HEAD) { lost++; continue; }
                d = ctl_map_plain(ctl_target(PSET_KIND_VALUE, 0, i));
            }
            d.min = pset__unit((float)atof(w[2]));
            d.max = pset__unit((float)atof(w[3]));
            const int c = crv_by_name(cn);
            d.curve = c < 0 ? CRV_LIN : c;
            mac_t *m = &s->macros[cur];
            if (link) { if (mac_add(s->macros, cur, d) < 0) lost++; }            // not a macro, full, or a loop
            else if (m->n >= MAC_DESTS) lost++;
            else m->d[m->n++] = d;
        }
    }
    if (dropped) *dropped = lost;
    return had;
}

#ifdef __cplusplus
}
#endif
