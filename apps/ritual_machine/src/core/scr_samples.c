// The rack's samplers and the sample files' names, for the SOUND tab's sample rows (scr_sound.c, which holds what was the SAMPLES
// tab: ADR-044) and the lists that name a file.
#include "core/ui_screen.h"
#include <stdio.h>

int sampler_count(const rack_t *r) { int n = 0; for (int i = 0; i < r->count; i++) n += r->slot[i].type == MOD_SAMPLER; return n; }
int sampler_slot(const rack_t *r, int k) {           // slot of the k-th sampler (k from 1)
    for (int i = 0; i < r->count; i++) if (r->slot[i].type == MOD_SAMPLER && --k == 0) return i;
    return RACK_NONE;
}

void file_label(int index, char *buf, int n) {
    audio_sample_info_t in;
    if (index >= 0 && audio_sample_info(index, &in) && in.name[0]) snprintf(buf, (size_t)n, "%.8s%s", pad_file_base(in.name), in.pending ? "*" : "");
    else snprintf(buf, (size_t)n, "--");
}
