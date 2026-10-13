#pragma once
// The version history of the sample library (ADR-041 decision 37). Every processing (the pads' EDIT page) writes a new file, a child version
// of the one it came from; the file it all started from is the root (any library file without a parent here). Version files are named
// <root>~<n> (n counts up per root, the root cut so that the name fits a library name). Undo = the parent, Redo = the newest child;
// processing an older version makes a branch. Trimming (automatic, after every new version): kept are the root, every version a pad uses,
// and the last HISTORY_KEEP versions of that root; the others are deleted, and a version whose parent goes re-links to the nearest kept
// ancestor. A version also remembers the pad's trim points (Start, End, loop start) around its making: what they were on its parent
// and what they became on it (a crop, a stretch or a reverse moves them), so that Undo / Redo / Ver put them back.
// Saved as text next to the samples:   v <seq> |<name>|<parent or ->| <3 trim points on the parent> <3 on the version> <what was done>
#include <stdbool.h>
#include <stdint.h>
#include "core/card_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HISTORY_MAX   32                        // versions on the card (the library holds 32 files)
#define HISTORY_KEEP  8
#define HISTORY_NAME  48                        // = audio_sample_info_t.name (AUDIO_NAME_MAX)
#define HISTORY_FILE  STORAGE_DIR_SAMPLES "/history.cfg"

typedef struct {
    char     name[HISTORY_NAME];
    char     parent[HISTORY_NAME];
    char     what[16];                          // "Norm -0.3dB"
    uint32_t seq;                               // creation order
    uint32_t ptrim[3], ctrim[3];                // the pad's Start, End, loop start: on the parent when this was made / on this version
} hist_ver_t;

typedef struct {
    hist_ver_t v[HISTORY_MAX];
    int        n;
    uint32_t   seq;
} history_t;

void history_init(history_t *h);
const hist_ver_t *history_find(const history_t *h, const char *name);         // NULL: a root (or not a version)
void history_root(const history_t *h, const char *name, char *out);           // the file the chain started from
// The name for a new version of `root` (<root>~<n>, n one past the highest used for that root).
void history_new_name(const history_t *h, const char *root, char *out);
bool history_add(history_t *h, const char *name, const char *parent, const char *what);
void history_set_trim(history_t *h, const char *name, const uint32_t ptrim[3], const uint32_t ctrim[3]);
// The trim points for a pad that goes from version `from` to version `to`: what `to` had when the step away from it was made (`to` an
// ancestor of `from`), else what `to` started with. False (and zeros = the whole file) when nothing is known: a root that is no ancestor.
bool history_trim_for(const history_t *h, const char *from, const char *to, uint32_t out[3]);
// Undo / Redo: the parent ("" for a root) / the newest child ("" when none).
void history_parent(const history_t *h, const char *name, char *out);
void history_newest_child(const history_t *h, const char *name, char *out);
// The versions of a root in creation order, the root first: how many were written to out (at most max).
int  history_versions(const history_t *h, const char *root, char out[][HISTORY_NAME], int max);
// The trimming of a root: `used` = the names the pads use (n_used of them). Writes the versions to delete to out (at most max) and takes them
// out of the history (re-linking their children). Returns how many.
int  history_trim(history_t *h, const char *root, const char *const *used, int n_used, char out[][HISTORY_NAME], int max);

int  history_to_text(const history_t *h, char *buf, int cap);
bool history_from_text(history_t *h, const char *txt);
bool history_load(history_t *h);                                             // false: no card / no file (the history is then empty)
bool history_save(const history_t *h);

#ifdef __cplusplus
}
#endif
