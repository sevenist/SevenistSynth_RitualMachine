#ifdef PLATFORM_SIM
// The desktop's TF card (hal/hal_storage.h): the folder sdcard/ next to samples/ (samples/ is found next to the working directory or one
// or two levels up; without one, the working directory). It is created at the first start, "inserted" at once, and F12 pulls it out /
// puts it back (sim_card_toggle). Its folders are the ones the app lists (storage_set_folders); an app's build may make them too.
#include "hal/hal_storage.h"
#include "boards/desktop/sim_card.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0777)
#endif

static const char *const *g_dirs;   // the app's folders (storage_set_folders)
static int g_dir_n;
static const storage_move_t *g_moves;
static int g_move_n;
static void (*g_on_folders)(void);  // sim_card_on_folders

void storage_set_folders(const char *const *folders, int count) { g_dirs = folders; g_dir_n = count > STORAGE_FOLDERS_MAX ? STORAGE_FOLDERS_MAX : count; }
void storage_set_moves(const storage_move_t *moves, int count) { g_moves = moves; g_move_n = count; }
void sim_card_on_folders(void (*fn)(void)) { g_on_folders = fn; }

static char g_root[64];             // "sdcard/", "../sdcard/" ...
static bool g_in;                   // the card is "inserted"
static unsigned g_gen;

static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && (st.st_mode & S_IFDIR); }
static bool is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && (st.st_mode & S_IFREG); }

static void find_root(void) {
    if (g_root[0]) return;
    static const char *const roots[] = {"", "../", "../../"};
    const char *base = "";
    for (int i = 0; i < 3; i++) {
        char dir[64];
        snprintf(dir, sizeof dir, "%ssamples", roots[i]);
        if (is_dir(dir)) { base = roots[i]; break; }
    }
    snprintf(g_root, sizeof g_root, "%ssdcard/", base);
}

void sim_card_path(const char *name, char *out, int n) {
    find_root();
    snprintf(out, (size_t)n, "%s%s", g_root, name);
}

/* ---------------- events ---------------- */

#define EVENTS 8
static storage_event_t g_ev[EVENTS];
static int g_ev_n;

static void push_event(storage_event_t e) {
    if (g_ev_n < EVENTS) g_ev[g_ev_n++] = e;
}

static uint8_t missing_folders(void) {
    uint8_t m = 0;
    char p[128];
    for (int i = 0; i < g_dir_n; i++) {
        sim_card_path(g_dirs[i], p, sizeof p);
        if (!is_dir(p)) m |= (uint8_t)(1u << i);
    }
    return m;
}

static void insert(void) {
    char root[64];
    sim_card_path("", root, sizeof root);
    if (!is_dir(root)) make_dir(root);                  // a fresh checkout: an empty card (the app then asks for the folders)
    g_in = true;
    g_gen++;
    storage_event_t e = {0};
    e.kind = STORAGE_EV_INSERTED;
    e.read_limit_us = 15000;
    e.missing = missing_folders();                      // read time 0: a folder on the PC is never slow
    push_event(e);
}

// The card is in from the start: the first call of anything here inserts it (app_init reads keys.cfg before it polls the events).
static void start(void) {
    static bool started;
    if (!started) { started = true; insert(); }
}

void sim_card_toggle(void) {
    start();
    if (!g_in) { insert(); printf("card: inserted\n"); return; }
    g_in = false;
    g_gen++;
    storage_event_t e = {0};
    e.kind = STORAGE_EV_REMOVED;
    push_event(e);
    printf("card: removed\n");
}

bool sim_card_present(void) { start(); return g_in; }
unsigned sim_card_generation(void) { start(); return g_gen; }

bool storage_poll_event(storage_event_t *e) {
    start();
    if (g_ev_n == 0) return false;
    *e = g_ev[0];
    memmove(&g_ev[0], &g_ev[1], (size_t)(g_ev_n - 1) * sizeof g_ev[0]);
    g_ev_n--;
    return true;
}

// Makes the folders that lead to `name` (its parents, not `name` itself).
static void make_parents(const char *name) {
    char part[128], p[128];
    for (int i = 0; name[i] && i < (int)sizeof part - 1; i++) {
        if (name[i] != '/') continue;
        memcpy(part, name, (size_t)i);
        part[i] = 0;
        sim_card_path(part, p, sizeof p);
        if (!is_dir(p)) make_dir(p);
    }
}

// As on the board: the app's moves from an older layout first (storage_set_moves), then the missing folders. At once (no card task).
void storage_make_folders(void) {
    storage_event_t e = {0};
    e.kind = STORAGE_EV_FOLDERS_DONE;
    if (!g_in) { push_event(e); return; }
    char a[128], b[128];
    for (int i = 0; i < g_move_n; i++) {
        make_parents(g_moves[i].to);
        sim_card_path(g_moves[i].from, a, sizeof a); sim_card_path(g_moves[i].to, b, sizeof b);
        if ((is_dir(a) || is_file(a)) && !is_dir(b) && !is_file(b)) e.moved = rename(a, b) == 0 || e.moved;
    }
    for (int i = 0; i < g_dir_n; i++) { sim_card_path(g_dirs[i], a, sizeof a); if (!is_dir(a)) make_dir(a); }
    e.ok = missing_folders() == 0;
    push_event(e);
    if (g_on_folders) g_on_folders();
    g_gen++;
}

/* ---------------- settings files ---------------- */

int storage_read(const char *name, char *buf, int cap) {
    start();
    if (!g_in) return -1;
    char p[256];
    sim_card_path(name, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    const int n = (int)fread(buf, 1, (size_t)(cap - 1), f);
    fclose(f);
    buf[n] = 0;
    return n;
}

bool storage_remove(const char *name) {
    start();
    if (!g_in) return false;
    char p[256];
    sim_card_path(name, p, sizeof p);
    return remove(p) == 0;
}

bool storage_write(const char *name, const char *data, int len) {
    start();
    if (!g_in) return false;
    char p[256];
    sim_card_path(name, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, (size_t)len, f) == (size_t)len;
    return fclose(f) == 0 && ok;
}
#endif // PLATFORM_SIM
