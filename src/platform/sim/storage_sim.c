#ifdef PLATFORM_SIM
// Settings files of the simulator (hal/hal_storage.h). The folder that holds samples/ plays the root of the TF card (samples/ is found next to
// the working directory or one or two levels up, as in sample_sim.cpp); without one, the working directory.
#include "hal/hal_storage.h"
#include <stdio.h>
#include <sys/stat.h>

static void path_of(const char *name, char *out, int n) {
    static const char *const roots[] = {"", "../", "../../"};
    for (int i = 0; i < 3; i++) {
        char dir[64];
        struct stat st;
        snprintf(dir, sizeof dir, "%ssamples", roots[i]);
        if (stat(dir, &st) == 0 && (st.st_mode & S_IFDIR)) { snprintf(out, (size_t)n, "%s%s", roots[i], name); return; }
    }
    snprintf(out, (size_t)n, "%s", name);
}

int storage_read(const char *name, char *buf, int cap) {
    char p[256];
    path_of(name, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    const int n = (int)fread(buf, 1, (size_t)(cap - 1), f);
    fclose(f);
    buf[n] = 0;
    return n;
}

bool storage_write(const char *name, const char *data, int len) {
    char p[256];
    path_of(name, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, (size_t)len, f) == (size_t)len;
    return fclose(f) == 0 && ok;
}
#endif // PLATFORM_SIM
