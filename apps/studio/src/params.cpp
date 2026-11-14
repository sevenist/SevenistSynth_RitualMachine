#include "params.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace studio {

namespace {
float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
bool is_log(const lab::Param &p) { return p.curve == lab::Exp && p.min > 0.f && p.max > p.min; }
}  // namespace

float to_norm(const lab::Param &p, float v) {
    if (p.max <= p.min) return 0.f;
    if (is_log(p)) return clamp01(std::log(v / p.min) / std::log(p.max / p.min));
    return clamp01((v - p.min) / (p.max - p.min));
}

float from_norm(const lab::Param &p, float n) {
    n = clamp01(n);
    if (is_log(p)) return p.min * std::pow(p.max / p.min, n);
    const float v = p.min + n * (p.max - p.min);
    return p.curve == lab::Int ? std::floor(v + 0.5f) : v;
}

int knob_steps(const lab::Param &p) {
    return p.curve == lab::Int ? static_cast<int>(p.max - p.min) + 1 : 0;
}

float step_value(const lab::Param &p, float v, int detents) {
    float r;
    switch (p.curve) {
    case lab::Int: r = std::floor(v + 0.5f) + static_cast<float>(detents); break;
    case lab::Exp:
        if (p.min > 0.f) { r = v * std::pow(p.max / p.min, static_cast<float>(detents) / 100.f); break; }
        [[fallthrough]];
    default: {
        const float step = (p.max - p.min) / 100.f;
        r = p.min + step * std::floor((v - p.min) / step + 0.5f + static_cast<float>(detents));
    }
    }
    return r < p.min ? p.min : r > p.max ? p.max : r;
}

// (SynthLab's screen writes values the same way: src/app.cpp)
void format_value(const lab::Param &p, float v, char *out, size_t size) {
    if (p.curve == lab::Int) {
        const int i = static_cast<int>(std::floor(v + 0.5f));
        if (p.labels) std::snprintf(out, size, "%s", p.labels[i - static_cast<int>(p.min)]);
        else std::snprintf(out, size, "%d%s%s", i, p.unit ? " " : "", p.unit ? p.unit : "");
        return;
    }
    const char *unit = p.unit;
    if (unit && std::strcmp(unit, "ms") == 0 && std::fabs(v) >= 1000.f) { v /= 1000.f; unit = "s"; }
    const float a = std::fabs(v);
    char num[16];
    if (a < 1000.f && std::fabs(v - std::floor(v + 0.5f)) < 0.005f) std::snprintf(num, sizeof num, "%d", static_cast<int>(std::floor(v + 0.5f)));
    else if (a >= 10000.f) std::snprintf(num, sizeof num, "%.1fk", v / 1000.f);
    else if (a >= 1000.f) std::snprintf(num, sizeof num, "%.2fk", v / 1000.f);
    else if (a >= 100.f) std::snprintf(num, sizeof num, "%.0f", v);
    else if (a >= 10.f) std::snprintf(num, sizeof num, "%.1f", v);
    else std::snprintf(num, sizeof num, "%.2f", v);
    std::snprintf(out, size, "%s%s%s", num, unit ? " " : "", unit ? unit : "");
}

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
