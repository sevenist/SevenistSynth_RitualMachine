#pragma once
// First prototype: the card task. One task on core 0 does everything that touches the TF card (the card driver has no locks): it
// mounts and watches the card, reads and writes the small files of hal/hal_storage.h, makes the folders an app lists, and runs what
// an app adds through its hooks (a sample library's reads, its streams). Without hooks it serves hal_storage.h alone.
#if defined(ARDUINO_ARCH_ESP32)
#include <stdint.h>
#include "boards/hwv1/storage_sd.h"

// An app's own card work. Every function is called on the card task and may be null. The order in one turn of the task's loop:
// the card's poll (card_seen / card_ready / card_gone), calm work, a small file, the folders, always, idle, the streams (pump).
struct card_hooks_t {
    void (*card_seen)(uint32_t id);   // a card that reads was mounted (id: its serial number), before its checks: what belonged to another card goes
    void (*card_ready)(uint32_t id);  // ... and it is fast enough to stream from: just before the state becomes CARD_OK
    void (*card_gone)(void);          // the card was pulled out (it is unmounted already)
    bool (*calm_pending)(void);       // work without a deadline waits (a file's header, a folder's list) ...
    void (*calm_run)(void);           // ... do it: the task runs at the UI's priority meanwhile, so a long read does not hold the UI's core
    void (*always)(void);             // every turn while the card is usable: work that must not wait (a recording's next chunk)
    void (*idle)(void);               // every turn while the last pump left no read in flight: work that may wait (a processing job's chunk)
    bool (*pump)(void);               // the streams: read what they wait for; true = more reads wait (the task goes on at once, 8 times at most)
    void (*report)(void);             // HWV1_DEBUG_AUDIO builds: every turn, for a line printed now and then
};

// Starts the task (once). `hooks` must stay valid; null = the small files only.
void card_task_start(const card_hooks_t *hooks);
bool card_task_running();

enum card_state_t { CARD_NONE = 0, CARD_OK, CARD_SLOW };   // CARD_SLOW: a card is in, but too slow to stream from (its small files still work)
card_state_t card_task_state();
uint32_t card_task_read_us();        // CARD_SLOW: the measured time of a sector read
uint32_t card_task_generation();     // +1 on every insertion, removal and when the folders were made

// The engine's streaming device on this card (engine/sampler/storage.h). The card task services its pinned reads and closes its
// files when the card goes; the app attaches it to its sample bank and says which folder its files are in (set_dir).
SdStorage &card_task_storage();
#endif
