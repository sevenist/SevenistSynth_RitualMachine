#pragma once
// The simulator's TF card (storage_sim.c): the folder sdcard/, F12 pulls it out and puts it back.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void     sim_card_path(const char *name, char *out, int n);   // path on the PC of `name` ("system/samples") on the card
void     sim_card_toggle(void);                               // pull the card out / put it back (queues the card event)
bool     sim_card_present(void);
unsigned sim_card_generation(void);                           // +1 on every insertion, removal and folder change

#ifdef __cplusplus
}
#endif
