#include "core/history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void history_init(history_t *h) { memset(h, 0, sizeof *h); }

const hist_ver_t *history_find(const history_t *h, const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < h->n; i++) if (!strcmp(h->v[i].name, name)) return &h->v[i];
    return NULL;
}

void history_root(const history_t *h, const char *name, char *out) {
    char cur[HISTORY_NAME];
    snprintf(cur, sizeof cur, "%s", name ? name : "");
    for (int guard = 0; guard < HISTORY_MAX; guard++) {                       // (a loop in a damaged file cannot hang it)
        const hist_ver_t *v = history_find(h, cur);
        if (!v || !v->parent[0]) break;
        snprintf(cur, sizeof cur, "%s", v->parent);
    }
    snprintf(out, HISTORY_NAME, "%s", cur);
}

// The number after "~" of a version name of `root`, 0 when it is not one.
static int version_number(const char *name, const char *stem) {
    const size_t sl = strlen(stem);
    if (strncmp(name, stem, sl) || name[sl] != '~') return 0;
    return atoi(name + sl + 1);
}

// The root cut so that "<stem>~<n>" fits a library name.
static void stem_of(const char *root, char *out) { snprintf(out, 19, "%s", root); }

void history_new_name(const history_t *h, const char *root, char *out) {
    char stem[19];
    stem_of(root, stem);
    int top = 0;
    for (int i = 0; i < h->n; i++) { const int k = version_number(h->v[i].name, stem); if (k > top) top = k; }
    snprintf(out, HISTORY_NAME, "%s~%d", stem, top + 1);
}

bool history_add(history_t *h, const char *name, const char *parent, const char *what) {
    if (h->n >= HISTORY_MAX || history_find(h, name)) return false;
    hist_ver_t *v = &h->v[h->n++];
    snprintf(v->name, sizeof v->name, "%s", name);
    snprintf(v->parent, sizeof v->parent, "%s", parent ? parent : "");
    snprintf(v->what, sizeof v->what, "%s", what ? what : "");
    v->seq = ++h->seq;
    return true;
}

void history_parent(const history_t *h, const char *name, char *out) {
    const hist_ver_t *v = history_find(h, name);
    snprintf(out, HISTORY_NAME, "%s", v ? v->parent : "");
}

void history_newest_child(const history_t *h, const char *name, char *out) {
    const hist_ver_t *best = NULL;
    for (int i = 0; i < h->n; i++)
        if (!strcmp(h->v[i].parent, name) && (!best || h->v[i].seq > best->seq)) best = &h->v[i];
    snprintf(out, HISTORY_NAME, "%s", best ? best->name : "");
}

int history_versions(const history_t *h, const char *root, char out[][HISTORY_NAME], int max) {
    int n = 0;
    if (n < max) snprintf(out[n++], HISTORY_NAME, "%s", root);
    int idx[HISTORY_MAX], k = 0;
    for (int i = 0; i < h->n; i++) {
        char r[HISTORY_NAME];
        history_root(h, h->v[i].name, r);
        if (!strcmp(r, root)) idx[k++] = i;
    }
    for (int a = 1; a < k; a++)                                               // by creation order
        for (int b = a; b > 0 && h->v[idx[b]].seq < h->v[idx[b - 1]].seq; b--) { const int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    for (int i = 0; i < k && n < max; i++) snprintf(out[n++], HISTORY_NAME, "%s", h->v[idx[i]].name);
    return n;
}

static bool in_list(const char *name, const char *const *list, int n) {
    for (int i = 0; i < n; i++) if (list[i] && !strcmp(list[i], name)) return true;
    return false;
}

int history_trim(history_t *h, const char *root, const char *const *used, int n_used, char out[][HISTORY_NAME], int max) {
    char vers[HISTORY_MAX + 1][HISTORY_NAME];
    const int nv = history_versions(h, root, vers, HISTORY_MAX + 1);          // vers[0] is the root
    const int first_kept = nv - 1 > HISTORY_KEEP ? nv - HISTORY_KEEP : 1;      // the last HISTORY_KEEP versions
    int n = 0;
    for (int i = 1; i < nv && n < max; i++) {
        if (i >= first_kept || in_list(vers[i], used, n_used)) continue;
        const hist_ver_t *v = history_find(h, vers[i]);
        char parent[HISTORY_NAME];
        snprintf(parent, sizeof parent, "%s", v ? v->parent : "");
        for (int k = 0; k < h->n; k++)                                         // its children now come from its parent
            if (!strcmp(h->v[k].parent, vers[i])) snprintf(h->v[k].parent, sizeof h->v[k].parent, "%s", parent);
        for (int k = 0; k < h->n; k++)
            if (!strcmp(h->v[k].name, vers[i])) { h->v[k] = h->v[h->n - 1]; h->n--; break; }
        snprintf(out[n++], HISTORY_NAME, "%s", vers[i]);
    }
    return n;
}

/* ---------------- history.cfg ---------------- */

int history_to_text(const history_t *h, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "# SynthCore sample history: v <seq> <name> <parent or -> <what>\nhistory %lu\n", (unsigned long)h->seq);
    for (int i = 0; i < h->n && n < cap; i++) {
        const hist_ver_t *v = &h->v[i];
        n += snprintf(buf + n, (size_t)(cap - n), "v %lu %s %s %s\n", (unsigned long)v->seq, v->name, v->parent[0] ? v->parent : "-", v->what);
    }
    return n < cap ? n : cap - 1;
}

bool history_from_text(history_t *h, const char *txt) {
    history_t r;
    history_init(&r);
    bool ok = false;
    for (const char *line = txt; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        unsigned long seq = 0;
        char name[HISTORY_NAME], parent[HISTORY_NAME];
        int used = 0;
        if (sscanf(line, "history %lu", &seq) == 1) { r.seq = (uint32_t)seq; ok = true; continue; }
        if (sscanf(line, "v %lu %23s %23s %n", &seq, name, parent, &used) < 3 || r.n >= HISTORY_MAX) continue;
        hist_ver_t *v = &r.v[r.n++];
        snprintf(v->name, sizeof v->name, "%s", name);
        snprintf(v->parent, sizeof v->parent, "%s", strcmp(parent, "-") ? parent : "");
        int len = 0;
        const char *w = used > 0 ? line + used : "";
        while (w[len] && w[len] != '\n' && w[len] != '\r' && len < (int)sizeof v->what - 1) len++;
        memcpy(v->what, w, (size_t)len);
        v->what[len] = 0;
        v->seq = (uint32_t)seq;
        if (v->seq > r.seq) r.seq = v->seq;
    }
    if (!ok) return false;
    *h = r;
    return true;
}

#define g_buf g_storage_buf                      // the shared settings buffer (hal_storage.h)

bool history_load(history_t *h) {
    history_init(h);
    return storage_read(HISTORY_FILE, g_buf, STORAGE_FILE_MAX) >= 0 && history_from_text(h, g_buf);
}

bool history_save(const history_t *h) {
    const int n = history_to_text(h, g_buf, STORAGE_FILE_MAX);
    return storage_write(HISTORY_FILE, g_buf, n);
}
