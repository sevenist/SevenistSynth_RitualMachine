#pragma once
// HAL: the TF card's folders, its events (inserted, removed) and small settings files (the user's key layout, ...). One implementation per
// platform: the board reads and writes the card on its card task (the calls wait for it, at most about a second); the simulator's card is
// the folder sdcard/ (next to samples/, F12 pulls it out and puts it back).
// Not for samples (hal_audio.h) and not for anything large: a file is read or written whole, in one call.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_FILE_MAX 2048                       // the largest settings file (keys.cfg: under 1 KB)

// The folders of a SynthCore card, relative to its root. Parents come before their children (they are created in this order).
#define STORAGE_DIR_SAMPLES "system/samples"         // the sample library (*.smp)
#define STORAGE_DIR_CONFIG  "system/config"          // settings files (keys.cfg ...)
#define STORAGE_DIR_IMPORT  "import"                 // .wav / .mp3 dropped here to be imported
#define STORAGE_DIR_PRESETS "presets"                // racks and patches
#define STORAGE_FOLDERS     {"system", STORAGE_DIR_SAMPLES, STORAGE_DIR_CONFIG, STORAGE_DIR_IMPORT, STORAGE_DIR_PRESETS}
#define STORAGE_FOLDER_COUNT 5

// Reads the file `name` ("system/config/keys.cfg") into buf (at most cap - 1 bytes, then a 0). Returns the length, or -1 when there is no card or no file.
int  storage_read(const char *name, char *buf, int cap);

// Replaces the file `name` with `len` bytes of data. Returns false when there is no card or the write failed.
bool storage_write(const char *name, const char *data, int len);

// Card events: the platform watches the card and queues what happened; the application polls them (once per step) and tells the user.
typedef enum {
    STORAGE_EV_NONE,
    STORAGE_EV_INSERTED,        // a card was mounted and checked: read time, slow, missing folders
    STORAGE_EV_REMOVED,
    STORAGE_EV_FOLDERS_DONE,    // storage_make_folders() finished: ok, moved
} storage_ev_kind_t;

typedef struct {
    uint8_t  kind;              // storage_ev_kind_t
    bool     slow;              // INSERTED: a sector read takes longer than read_limit_us: samples are off for this card (settings files still work)
    uint8_t  missing;           // INSERTED: bit i set = folder i of STORAGE_FOLDERS is missing
    bool     ok;                // FOLDERS_DONE: every folder exists now
    bool     moved;             // FOLDERS_DONE: files of the old layout (/samples, /keys.cfg) were moved into /system
    uint32_t read_us;           // INSERTED: the measured time of one sector read
    uint32_t read_limit_us;     // INSERTED: the limit for streaming samples
} storage_event_t;

// The next card event (false: none). Never blocks.
bool storage_poll_event(storage_event_t *e);

// Creates the missing folders of STORAGE_FOLDERS on the card, and moves the files of the old layout (/samples/*, /keys.cfg) into
// /system. Runs in the background; STORAGE_EV_FOLDERS_DONE follows, then the sample library is read again.
void storage_make_folders(void);

#ifdef __cplusplus
}
#endif
