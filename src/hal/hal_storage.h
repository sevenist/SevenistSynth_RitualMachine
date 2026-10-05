#pragma once
// HAL: small settings files at the root of the TF card (the user's key layout, ...). One implementation per platform: the board reads and
// writes the card on its card task (the calls wait for it, at most about a second); the simulator uses the folder that holds samples/.
// Not for samples (hal_audio.h) and not for anything large: a file is read or written whole, in one call.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_FILE_MAX 4096                       // the largest settings file

// Reads the file `name` ("keys.cfg") into buf (at most cap - 1 bytes, then a 0). Returns the length, or -1 when there is no card or no file.
int  storage_read(const char *name, char *buf, int cap);

// Replaces the file `name` with `len` bytes of data. Returns false when there is no card or the write failed.
bool storage_write(const char *name, const char *data, int len);

#ifdef __cplusplus
}
#endif
