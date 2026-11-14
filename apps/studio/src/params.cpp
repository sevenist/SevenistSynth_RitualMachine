#include "params.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace studio {

int section_of(const lab::Synth &s, int i) {
    for (int k = i - 1; k >= 0; k--)
        if (s.params[k].curve == lab::Head) return k;
    return -1;
}

void param_path(const lab::Synth &s, int i, char *out, size_t size) {
    if (i < 0 || i >= s.n_params) { std::snprintf(out, size, "?"); return; }
    const int h = section_of(s, i);
    if (h < 0 || s.params[i].curve == lab::Head) std::snprintf(out, size, "%s", s.params[i].name);
    else std::snprintf(out, size, "%s/%s", s.params[h].name, s.params[i].name);
}

void quote_path(const lab::Synth &s, int i, char *out, size_t size) {
    char p[64];
    param_path(s, i, p, sizeof p);
    if (std::strchr(p, ' ')) std::snprintf(out, size, "\"%s\"", p);
    else std::snprintf(out, size, "%s", p);
}

bool read_word(const char *&p, char *out, size_t size) {
    auto space = [](char ch) { return ch == ' ' || ch == '\t'; };
    auto end = [](char ch) { return ch == 0 || ch == '\r' || ch == '\n'; };
    while (space(*p)) p++;
    if (end(*p)) { if (size) out[0] = 0; return false; }
    size_t n = 0;
    if (*p == '"') {
        p++;
        while (!end(*p) && *p != '"') { if (n + 1 < size) out[n++] = *p; p++; }
        if (*p == '"') p++;
    } else {
        while (!end(*p) && !space(*p)) { if (n + 1 < size) out[n++] = *p; p++; }
    }
    if (size) out[n] = 0;
    return true;
}

int find_param(const lab::Synth &s, const char *path) {
    char p[64];
    for (int i = 0; i < s.n_params; i++) {
        param_path(s, i, p, sizeof p);
        if (std::strcmp(p, path) == 0) return i;
    }
    for (int i = 0; i < s.n_params; i++)
        if (s.params[i].curve != lab::Head && std::strcmp(s.params[i].name, path) == 0) return i;
    return -1;
}

}  // namespace studio
