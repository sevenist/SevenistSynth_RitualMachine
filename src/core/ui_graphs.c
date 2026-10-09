// The pictures that fill the graph box: each one only needs the display, a rectangle and the values to show.
#include "core/ui_internal.h"
#include "core/dx7_algos.h"
#include "hal/hal_audio.h"
#include <math.h>
#include <stdio.h>

// `cycles` periods of the selected waveform (the style's wave_cycles).
void draw_wave(u8g2_t *g, gui_rect_t box, int wave, float pulse_width, float cycles) {
    const int cy = box.y + box.h / 2, amp = box.h / 2 - 3;
    uint32_t noise = 12345;
    int px = 0, py = 0;
    for (int i = 0; i < box.w - 2; i++) {
        float ph = fmodf((float)i * cycles / (float)(box.w - 2), 1.0f), v;
        switch (wave) {
            case WAVE_SINE:     v = sinf(2 * PI_F * ph); break;
            case WAVE_PULSE:    v = ph < pulse_width ? 1.0f : -1.0f; break;
            case WAVE_SAW_DOWN: v = 1.0f - 2.0f * ph; break;
            case WAVE_SAW_UP:   v = 2.0f * ph - 1.0f; break;
            case WAVE_TRIANGLE: v = 4.0f * fabsf(ph - 0.5f) - 1.0f; break;
            default:            noise = noise * 1664525u + 1013904223u; v = ((noise >> 16) & 0xFF) / 127.5f - 1.0f; break;
        }
        int x = box.x + 1 + i, y = cy - (int)(v * amp);
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// The Mutable Instruments models (rack Wav >= OC_FIRST_MI): the model itself, rendered by the engine (audio_osc_preview) and kept until one of
// its values changes. A periodic model shows `cycles` periods; a struck one its decay (the peak of each pixel column's slice of 4096 samples rendered,
// ~90 ms at 44.1 kHz).
// Without an engine (ui_dump, the UI tests) the model's name.
void draw_mi_preview(u8g2_t *g, gui_rect_t box, int model, float timbre, float morph, float harm, float cycles, const char *name) {
    enum { P = 64, N = 1024 };                                                    // period of the rendered pitch (689 Hz at 44.1 kHz), samples (2 KB)
    static int16_t buf[N];
    static int kind = -1, c_model = -1;
    static float c_t, c_m, c_h;
    if (model != c_model || timbre != c_t || morph != c_m || harm != c_h) {
        kind = audio_osc_preview(model, timbre, morph, harm, P, buf, N);
        c_model = model; c_t = timbre; c_m = morph; c_h = harm;
    }
    if (kind <= 0) { gui_draw_text_centered(g, box, name); return; }
    const int cy = box.y + box.h / 2, amp = box.h / 2 - 3, w = box.w - 2;
    if (kind == 2) {
        for (int x = 0; x < w; x++) {
            int pk = 0;
            for (int i = x * N / w; i < (x + 1) * N / w; i++) { const int a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > pk) pk = a; }
            const int h = pk * amp / 32768;
            u8g2_DrawVLine(g, box.x + 1 + x, cy - h, 2 * h + 1);
        }
        return;
    }
    int span = (int)(cycles * P);
    if (span < 2) span = 2;
    if (span > N) span = N;
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        const int x = box.x + 1 + i, y = cy - buf[N - span + i * span / w] * amp / 32768;
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// A sketch of what an oscillator engine does with its two controls (timbre and morph in 0..1): not the real signal, but it moves the same way.
void draw_engine_preview(u8g2_t *g, gui_rect_t box, int engine, float timbre, float morph, float cycles) {
    const int cy = box.y + box.h / 2, amp = box.h / 2 - 3, w = box.w - 2;
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        const float t = (float)i / (float)w, ph = 2.0f * PI_F * cycles * t;         // `cycles` periods
        float v = 0;
        switch (engine) {
            case 0: {                                                                // string: a saw-like pluck decaying slower with morph
                for (int k = 1; k <= 6; k++) v += sinf((float)k * ph) / (float)k * (0.35f + timbre);
                v *= 0.55f * expf(-t * (4.5f - 4.0f * morph));
            } break;
            case 1: {                                                                // modes: harmonic -> bell ratios with timbre
                for (int k = 1; k <= 4; k++) {
                    float r = (float)k + ((k == 1 ? 1.0f : k == 2 ? 2.76f : k == 3 ? 5.4f : 8.93f) - (float)k) * timbre;
                    v += sinf(r * ph * 0.6f) / (float)k * expf(-t * (5.0f - 3.0f * morph) * (1.0f + 0.5f * (float)k));
                }
                v *= 0.9f;
            } break;
            case 2: v = sinf(ph + 6.0f * timbre * sinf(ph * (0.5f + 3.5f * morph) * 2.0f)); break;
            case 3: v = sinf((1.0f + 7.0f * timbre) * ((1.0f - morph) * sinf(ph) + morph * (fmodf(ph / (2 * PI_F), 1.0f) * 2.0f - 1.0f))); break;
            case 4: {
                float s = 0;
                for (int k = -1; k <= 1; k++) { float p = ph * (1.0f + (float)k * 0.08f * timbre) / (2 * PI_F); s += (fmodf(p, 1.0f) * 2.0f - 1.0f); }
                v = s / 3.0f * (0.6f + 0.4f * morph);
            } break;
            case 5: {                                                                // a damped formant ring after every glottal pulse
                const float u = fmodf(t * cycles, 1.0f);
                v = sinf(u * (6.0f + 18.0f * (1.0f - timbre)) * 2 * PI_F) * expf(-u * (6.0f - 4.5f * morph));
            } break;
            case 6: for (int k = 1; k <= 8; k++) v += sinf((float)k * ph) * powf((float)k, -(2.0f - 1.8f * timbre)) * ((k & 1) ? 1.0f : 1.0f - morph); v *= 0.8f; break;
            case 8: {                                                                // strings: a detuned pair of saw / pulse / triangle (morph thirds)
                for (int k = -1; k <= 1; k += 2) {
                    const float p = fmodf(ph * (1.0f + (float)k * 0.03f * timbre) / (2 * PI_F), 1.0f);
                    v += morph < 0.34f ? 2.0f * p - 1.0f : (morph < 0.67f ? (p < 0.5f ? 1.0f : -1.0f) : 4.0f * fabsf(p - 0.5f) - 1.0f);
                }
                v *= 0.5f;
            } break;
            default: {                                                               // dust: a few pings at pseudo-random places
                const int cell = (int)(t * (4.0f + 28.0f * timbre));
                const float u = t * (4.0f + 28.0f * timbre) - (float)cell;
                const uint32_t h = (uint32_t)(cell * 2654435761u) >> 28;
                if (h < 6) v = sinf(u * 20.0f) * expf(-u * (8.0f - 6.0f * morph)) * ((h & 1) ? 1.0f : -1.0f);
            } break;
        }
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int x = box.x + 1 + i, y = cy - (int)(v * (float)amp);
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// Progress 0..1 of a segment at phase p for a curve in percent (see engine/dsp/curve.h): + fast start / slow end, - the opposite.
float curve_shape(float pct, float p) {
    float k = 8.0f * pct / 100.0f;
    if (fabsf(k) < 1e-3f) return p;
    return (1.0f - expf(-k * p)) / (1.0f - expf(-k));
}

// One curved segment drawn from (x0, y0) to (x1, y1); returns nothing, the caller continues from (x1, y1).
void draw_curve_seg(u8g2_t *g, int x0, int y0, int x1, int y1, float pct) {
    int px = x0, py = y0;
    const int n = x1 - x0 < 1 ? 1 : x1 - x0;
    for (int i = 1; i <= n; i++) {
        float p = (float)i / (float)n;
        int x = x0 + (int)((float)(x1 - x0) * p), y = y0 + (int)((float)(y1 - y0) * curve_shape(pct, p));
        u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// ADSR shape (attack, hold, decay, sustain, release, each with its curve) with a dotted marker where the note is released.
void draw_env(u8g2_t *g, gui_rect_t box, const env_params_t *e) {
    float body = e->attack_ms + e->hold_ms + e->decay_ms + e->release_ms;
    float hold = body * 0.25f + 1.0f;
    float total = body + hold;
    const int bottom = box.y + box.h - 3, span = box.h - 6, w = box.w - 3;
    float t[6] = {0, e->attack_ms, e->attack_ms + e->hold_ms, e->attack_ms + e->hold_ms + e->decay_ms,
                  e->attack_ms + e->hold_ms + e->decay_ms + hold, total};
    float v[6] = {0, 1, 1, e->sustain, e->sustain, 0};
    float crv[5] = {e->a_curve, 0, e->d_curve, 0, e->r_curve};
    int px = box.x + 1, py = bottom;
    for (int i = 1; i < 6; i++) {
        int x = box.x + 1 + (int)(t[i] / total * (float)w);
        int y = bottom - (int)(v[i] * (float)span);
        draw_curve_seg(g, px, py, x, y, crv[i - 1]);
        px = x; py = y;
    }
    int rx = box.x + 1 + (int)(t[4] / total * (float)w);
    for (int y = box.y + 2; y < bottom; y += 3) u8g2_DrawPixel(g, rx, y);
}

// Magnitude of a 2-pole filter at frequency f.
float filter_gain(int type, float f, float fc, float q) {
    float r = f / fc;
    float lp = 1.0f / sqrtf((1 - r * r) * (1 - r * r) + (r / q) * (r / q));
    switch (type) {
        case FILT_LP:    return lp;
        case FILT_LP24:  return lp * lp;
        case FILT_BP:    return (r / q) * lp;
        case FILT_HP:    return r * r * lp;
        case FILT_NOTCH: return fabsf(1 - r * r) * lp;
        case FILT_CHAM:  return lp;
        case FILT_AP:    return 1.0f;                                // all-pass: flat (draw_filter shows its phase)
        case FILT_LP6:   return 1.0f / sqrtf(1 + r * r);
        case FILT_LADDER: {                                          // four one-poles in a loop with gain k (resonance from q, like the engine)
            const float k = 3.9f * fminf(fmaxf((q - 0.5f) / 9.5f, 0.0f), 1.0f);
            const float g2 = 1.0f / (1 + r * r);                      // |one pole|^2; the pole's phase is -atan(r)
            const float a = g2 * g2, ph = -4.0f * atanf(r);           // four poles: magnitude^2 and phase
            const float mag = sqrtf(a), re = 1 + k * mag * cosf(ph), im = k * mag * sinf(ph);
            return mag / sqrtf(re * re + im * im) * (1.0f + 0.5f * k);
        }
        default:         return 1.0f;
    }
}

// Frequency response, 20 Hz..20 kHz (log) by -36..+18 dB, with 0 dB and cutoff markers. The all-pass, whose level is flat, shows its phase
// instead: 0 degrees at the top, -360 at the bottom (the dotted line is -180, reached at the cutoff).
void draw_filter(u8g2_t *g, gui_rect_t box, int type, float cutoff_hz, float resonance) {
    const float db_min = -36.0f, db_max = 18.0f;
    const int bottom = box.y + box.h - 2, span = box.h - 4, w = box.w - 2;
    if (type == FILT_AP) {
        const int top = box.y + 2, mid = top + (bottom - top) / 2;
        for (int x = box.x + 1; x < box.x + box.w - 1; x += 3) u8g2_DrawPixel(g, x, mid);
        int px = 0, py = 0;
        for (int i = 0; i < w; i++) {
            const float r = 20.0f * powf(1000.0f, i / (float)(w - 1)) / cutoff_hz;
            const float ph = 2.0f * atan2f(r / resonance, 1.0f - r * r);      // 0..2 pi
            const int x = box.x + 1 + i, y = top + (int)(ph / (2.0f * PI_F) * (float)(bottom - top));
            if (i > 0) u8g2_DrawLine(g, px, py, x, y);
            px = x; py = y;
        }
        const int cx = box.x + 1 + (int)(logf(cutoff_hz / 20.0f) / logf(1000.0f) * (w - 1));
        for (int y = box.y + 2; y < bottom; y += 3) u8g2_DrawPixel(g, cx, y);
        return;
    }
    int y0 = bottom - (int)((0 - db_min) / (db_max - db_min) * span);
    for (int x = box.x + 1; x < box.x + box.w - 1; x += 3) u8g2_DrawPixel(g, x, y0);
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        float f = 20.0f * powf(1000.0f, i / (float)(w - 1));
        float db = 20.0f * log10f(filter_gain(type, f, cutoff_hz, resonance) + 1e-6f);
        int y = clampi(bottom - (int)((db - db_min) / (db_max - db_min) * span), box.y + 1, bottom);
        int x = box.x + 1 + i;
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
    if (type != FILT_OFF) {
        float pos = logf(cutoff_hz / 20.0f) / logf(1000.0f);
        int cx = box.x + 1 + (int)(pos * (w - 1));
        for (int y = box.y + 2; y < bottom; y += 3) u8g2_DrawPixel(g, cx, y);
    }
}

// Transfer curve of the saturator (input -1..1 on x, output on y), blended with the dry line by mix.
void draw_sat(u8g2_t *g, gui_rect_t box, int mode, float drive, float mix) {
    const int w = box.w - 2, cy = box.y + box.h / 2, amp = box.h / 2 - 3;
    for (int x = box.x + 1; x < box.x + box.w - 1; x += 3) u8g2_DrawPixel(g, x, cy);   // zero line
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        float x = (i / (float)(w - 1)) * 2.0f - 1.0f, y;
        float d = x * drive;
        switch (mode) {                                                   // the engine's order (rack.c sat_names)
            case 0:  y = tanhf(d); break;                                 // Tanh
            case 1:  y = d; break;                                        // Clip (clamped below)
            case 2:  y = sinf(d * PI_F / 2.0f); break;                    // Fold
            case 3:  y = floorf(d * 4.0f + 0.5f) / 4.0f; break;           // Crush (coarse steps)
            case 4:  y = tanhf(d + 0.3f) - tanhf(0.3f); break;            // Tube
            case 5:  y = tanhf(d) * 0.9f; break;                          // Tape
            case 6:  y = d >= 0 ? tanhf(d) : 0.6f * tanhf(0.45f * d); break;   // Diode
            case 7: {                                                     // Cheb
                float c = d > 1 ? 1 : (d < -1 ? -1 : d);
                y = ((4 * c * c * c - 3 * c) + (16 * powf(c, 5) - 20 * c * c * c + 5 * c)) * 0.5f;
            } break;
            case 8:  y = 2.0f * fabsf(d > 1 ? 1 : (d < -1 ? -1 : d)) - 0.5f; break;   // Rect (DC removed on the real signal)
            default: y = floorf(d * 8.0f + 0.5f) / 8.0f; break;           // Decim
        }
        if (y > 1) y = 1;
        if (y < -1) y = -1;
        y = x + (y - x) * mix;
        int px_ = box.x + 1 + i, py_ = cy - (int)(y * amp);
        if (i > 0) u8g2_DrawLine(g, px, py, px_, py_);
        px = px_; py = py_;
    }
}

// The lane value (0..100, or -1 = nothing) at a position in steps, by the same rule as the engine module
// (engine/modules/motion_seq.cpp): between ACTIVE steps, STEP holds the previous one, LINEAR interpolates to the next.
float ms_value_at(const ms_lane_t *l, int steps, float pos) {
    unsigned mask = l->active & ((1u << steps) - 1u);
    if (!mask) return -1.0f;
    int k = (int)pos;
    if (k >= steps) k = steps - 1;
    float frac = pos - (float)k;
    int prev = k;
    for (int d = 0; d < steps; d++) { int i = (k - d + steps) % steps; if (mask >> i & 1u) { prev = i; break; } }
    if (!l->linear || (mask & (mask - 1)) == 0) return (float)l->val[prev];
    int next = prev;
    for (int d = 1; d <= steps; d++) { int i = (k + d) % steps; if (mask >> i & 1u) { next = i; break; } }
    int dist = (next - prev + steps) % steps, done = (k - prev + steps) % steps;
    float t = ((float)done + frac) / (float)dist;
    return (float)l->val[prev] + ((float)l->val[next] - (float)l->val[prev]) * t;
}

// The lane as a curve over the pattern (what the engine will output), bipolar lanes around a centre line.
void draw_ms_curve(u8g2_t *g, gui_rect_t box, const ms_lane_t *l, int steps) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int top = box.y + 2, bottom = box.y + box.h - 3, w = box.w - 4;
    if (l->bipolar) for (int x = box.x + 2; x < box.x + box.w - 2; x += 3) u8g2_DrawPixel(g, x, (top + bottom) / 2);
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        float v = ms_value_at(l, steps, (float)i * (float)steps / (float)w);
        if (v < 0) { px = 0; continue; }
        int x = box.x + 2 + i, y = bottom - (int)(v * (float)(bottom - top) / 100.0f);
        if (px) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// Comb response over 0 .. 8 harmonics of the tuned pitch: peaks at every harmonic (every other one for a negative feedback), sharper with Fb.
void draw_comb(u8g2_t *g, gui_rect_t box, float fb_pct, float mix) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const float fb = fb_pct / 100.0f;
    const int w = box.w - 4, bottom = box.y + box.h - 3, span = box.h - 7;
    float peak = 1.0f / (1.0f - (fb < 0 ? -fb : fb) + 1e-3f);
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        const float x = 8.0f * (float)i / (float)w;                                 // harmonics of the pitch
        const float h = 1.0f / sqrtf(1.0f + fb * fb - 2.0f * fb * cosf(2.0f * PI_F * x));
        const float y = 1.0f + mix * (h / peak * 4.0f - 1.0f);                       // dry + mix * (comb - dry), comb scaled so its peaks reach the top
        int px_ = box.x + 2 + i, py_ = bottom - (int)(fminf(y, 1.0f) * (float)span);
        if (i > 0) u8g2_DrawLine(g, px, py, px_, py_);
        px = px_; py = py_;
    }
}

// The whole shape: points with their curves (square-root time scale so a 1 ms attack and a 300 ms decay both show), a flat sustain after the
// sustain point, then the release. The selected point (sel >= 0) is drawn as a box.
void draw_eg_graph(u8g2_t *g, gui_rect_t box, const rack_slot_t *s, int sel) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int left = box.x + 3, right = box.x + box.w - 4, top = box.y + 4, bottom = box.y + box.h - 5;
    const int sus = (int)s->v[MP_EG_SUS];
    float wd[4], total = 0;
    for (int i = 0; i < 4; i++) { wd[i] = (i < sus && s->v[3 * i] > 0) ? sqrtf(s->v[3 * i]) : 0; total += wd[i]; }
    const float sus_w = total * 0.25f + 1.0f, rel_w = sqrtf(s->v[MP_EG_REL]);
    total += sus_w + rel_w;
    int px = left, py = bottom;
    float acc = 0;
    int bx[4], by[4];
    for (int i = 0; i < 4; i++) {
        bx[i] = -1; by[i] = 0;
        if (wd[i] <= 0) continue;
        acc += wd[i];
        const int x = left + (int)(acc / total * (float)(right - left)), y = bottom - (int)(s->v[3 * i + 1] * (float)(bottom - top) / 100.0f);
        draw_curve_seg(g, px, py, x, y, s->v[3 * i + 2]);
        px = x; py = y; bx[i] = x; by[i] = y;
    }
    acc += sus_w;
    const int xs = left + (int)(acc / total * (float)(right - left));
    u8g2_DrawLine(g, px, py, xs, py);
    for (int y = top; y <= bottom; y += 3) u8g2_DrawPixel(g, xs, y);
    draw_curve_seg(g, xs, py, right, bottom, s->v[MP_EG_RCV]);
    for (int i = 0; i < 4; i++) {
        if (bx[i] < 0) continue;
        if (i == sel) u8g2_DrawBox(g, bx[i] - 2, by[i] - 2, 5, 5);
        else          u8g2_DrawFrame(g, bx[i] - 1, by[i] - 1, 3, 3);
    }
}

// Amplitude overview (mirrored around the centre line) with optional slice ticks, a loop bracket and a start marker.
void draw_sample_graph(u8g2_t *g, gui_rect_t box, const audio_sample_info_t *in, int sel_slice, float start_ms, bool show_loop) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    if (in && in->pending) {                                     // a .wav / .mp3 that has not been decoded yet
        u8g2_DrawStr(g, box.x + 4, box.y + box.h / 2 - 3, in->kind == 2 ? "mp3 file" : "wav file");
        u8g2_DrawStr(g, box.x + 4, box.y + box.h / 2 + 7, "converts on");
        u8g2_DrawStr(g, box.x + 4, box.y + box.h / 2 + 15, "assign");
        return;
    }
    if (!in || in->frames == 0) { for (int x = box.x + 2; x < box.x + box.w - 2; x += 3) u8g2_DrawPixel(g, x, box.y + box.h / 2); return; }
    const int w = box.w - 2, mid = box.y + box.h / 2, amp = box.h / 2 - 2;
    for (int x = 0; x < w; x++) {
        int p = in->peaks[x * AUDIO_PEAKS / w] * amp / 255;
        u8g2_DrawVLine(g, box.x + 1 + x, mid - p, 2 * p + 1);
    }
    u8g2_SetDrawColor(g, 2);                                     // XOR over the waveform so markers stay visible
    for (int i = 0; i < in->slices; i++) {
        int x = box.x + 1 + (int)((uint64_t)in->slice[i] * (uint64_t)(w - 1) / in->frames);
        if (i == sel_slice) u8g2_DrawVLine(g, x, box.y + 1, box.h - 2);
        else                for (int y = box.y + 1; y < box.y + 6; y++) u8g2_DrawPixel(g, x, y);
    }
    if (show_loop && in->loop_end > in->loop_start + 1) {
        int x0 = box.x + 1 + (int)((uint64_t)in->loop_start * (uint64_t)(w - 1) / in->frames);
        int x1 = box.x + 1 + (int)((uint64_t)in->loop_end * (uint64_t)(w - 1) / in->frames);
        u8g2_DrawHLine(g, x0, box.y + box.h - 3, x1 - x0 + 1);
        u8g2_DrawVLine(g, x0, box.y + box.h - 6, 4);
        u8g2_DrawVLine(g, x1, box.y + box.h - 6, 4);
    }
    if (start_ms > 0.0f) {
        uint64_t sf = (uint64_t)(start_ms * (float)in->rate / 1000.0f);
        if (sf < in->frames) u8g2_DrawVLine(g, box.x + 1 + (int)(sf * (uint64_t)(w - 1) / in->frames), box.y + 1, box.h - 2);
    }
    u8g2_SetDrawColor(g, 1);
}

// Chorus: the LFO (its rate follows the mode).
void draw_chorus_graph(u8g2_t *g, gui_rect_t box, int mode, int mix) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int cy = box.y + box.h / 2, half = box.h / 2 - 4;
    if (mode == 0) {
        for (int x = box.x + 2; x < box.x + box.w - 2; x += 3) u8g2_DrawPixel(g, x, cy);
        return;
    }
    static const float cycles[4] = {0, 1.0f, 1.7f, 7.0f};
    static const float depth[4] = {0, 1.0f, 1.0f, 0.25f};
    int px = 0, py = 0;
    for (int i = 0; i < box.w - 4; i++) {
        float ph = cycles[mode & 3] * (float)i / (float)(box.w - 4);
        float tri = 4.0f * fabsf(ph - floorf(ph + 0.5f)) - 1.0f;
        int x = box.x + 2 + i, y = cy - (int)(tri * depth[mode & 3] * (float)half * (float)mix / 100.0f);
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// Delay taps: spacing = time, heights = feedback.
void draw_delay_graph(u8g2_t *g, gui_rect_t box, int mix, int ms, int fb) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int base = box.y + box.h - 3, span = box.h - 8, w = box.w - 8;
    u8g2_DrawHLine(g, box.x + 2, base, box.w - 4);
    u8g2_DrawVLine(g, box.x + 4, base - span, span);                                   // the dry hit
    float level = (float)mix / 100.0f;
    for (int k = 1; k < 6 && level > 0.04f; k++) {
        int x = box.x + 4 + (int)((float)w * (float)(k * ms) / 3000.0f);
        if (x >= box.x + box.w - 2) break;
        int h = (int)((float)span * level);
        if (h > 0) u8g2_DrawVLine(g, x, base - h, h);
        level *= (float)fb / 100.0f;
    }
}

// Reverb tail: pre-delay gap, then an exponential decay whose speed follows Decay; Damp thins the line.
void draw_reverb_graph(u8g2_t *g, gui_rect_t box, int mix, int decay, int size, int damp) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int base = box.y + box.h - 3, span = box.h - 7, w = box.w - 6;
    u8g2_DrawHLine(g, box.x + 2, base, box.w - 4);
    const int x0 = box.x + 3 + 20 * w / 800;
    u8g2_DrawVLine(g, box.x + 3, base - span, span);                                   // the dry hit
    if (mix == 0) return;
    float rate = 1.0f / (0.05f + 0.01f * (float)decay * (0.5f + 0.01f * (float)size) * 3.0f);   // decay per unit of box width
    for (int x = x0; x < box.x + box.w - 2; x += 1 + (damp > 60)) {
        float t = (float)(x - x0) / (float)w;
        float a = expf(-t * 4.0f * rate * 0.35f) * (float)mix / 100.0f;
        int h = (int)(a * (float)span);
        if (h < 1) break;
        if (((x * 7) & 3) != 0 || damp < 30) u8g2_DrawVLine(g, x, base - h, h);       // gaps give the tail its grain
    }
}

// Compressor: input level (x) against output level (y), -60 .. 0 dB, with the threshold marked.
void draw_comp_graph(u8g2_t *g, gui_rect_t box, int thr, int ratio, int gain) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int w = box.w - 4, h = box.h - 4;
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        float in_db = -60.0f + 60.0f * (float)i / (float)(w - 1), out_db = in_db > (float)thr ? (float)thr + (in_db - (float)thr) / (float)ratio : in_db;
        out_db += (float)gain;
        int x = box.x + 2 + i, y = box.y + 2 + h - 1 - (int)((out_db + 60.0f) / 60.0f * (float)(h - 1));
        if (y < box.y + 2) y = box.y + 2;
        if (y > box.y + 2 + h - 1) y = box.y + 2 + h - 1;
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
    const int tx = box.x + 2 + (int)((float)(thr + 60) / 60.0f * (float)(w - 1));
    for (int y = box.y + 2; y < box.y + box.h - 2; y += 3) u8g2_DrawPixel(g, tx, y);
}

// EQ: the combined response of the three bands, 20 Hz .. 20 kHz by +-15 dB.
void draw_eq_graph(u8g2_t *g, gui_rect_t box, int low, int mid, int midf, int high) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int w = box.w - 4, cy = box.y + box.h / 2, half = box.h / 2 - 3;
    for (int x = box.x + 2; x < box.x + box.w - 2; x += 3) u8g2_DrawPixel(g, x, cy);
    const float fm = 440.0f * powf(2.0f, ((float)midf - 69.0f) / 12.0f);
    int px = 0, py = 0;
    for (int i = 0; i < w; i++) {
        const float f = 20.0f * powf(1000.0f, (float)i / (float)(w - 1));
        float db = (float)low / (1.0f + powf(f / 150.0f, 2.0f)) + (float)high / (1.0f + powf(6000.0f / f, 2.0f));
        float l = logf(f / fm) / 0.7f;
        db += (float)mid * expf(-l * l);
        int x = box.x + 2 + i, y = cy - (int)(db / 15.0f * (float)half);
        if (y < box.y + 1) y = box.y + 1;
        if (y > box.y + box.h - 2) y = box.y + box.h - 2;
        if (i > 0) u8g2_DrawLine(g, px, py, x, y);
        px = x; py = y;
    }
}

// An FX rack module's picture: its effect's sketch when it has one (the values converted to the sketches' units: %, MIDI note), else its
// name in a frame.
void draw_fx_module(u8g2_t *g, gui_rect_t box, const rack_slot_t *ms) {
    const float *v = ms->v;
    const int pct = 100;
    switch (ms->type) {
        case MOD_CHORUS: draw_chorus_graph(g, box, (int)v[MP_CH_MODE], (int)lroundf(v[MP_CH_MIX] * pct)); return;
        case MOD_DELAY:  draw_delay_graph(g, box, (int)lroundf(v[MP_DL_MIX] * pct), (int)v[MP_DL_TIME], (int)lroundf(v[MP_DL_FB] * pct)); return;
        case MOD_REVERB: draw_reverb_graph(g, box, (int)lroundf(v[MP_RV_MIX] * pct), (int)lroundf(v[MP_RV_DEC] * pct), (int)lroundf(v[MP_RV_SIZE] * pct),
                                           (int)lroundf(v[MP_RV_DAMP] * pct)); return;
        case MOD_COMP:   draw_comp_graph(g, box, (int)v[MP_CP_THR], (int)v[MP_CP_RATIO], (int)v[MP_CP_GAIN]); return;
        case MOD_EQ:     draw_eq_graph(g, box, (int)v[MP_EQ_LOW], (int)v[MP_EQ_MID], (int)lroundf(69.0f + 12.0f * log2f(v[MP_EQ_MIDF] / 440.0f)),
                                       (int)v[MP_EQ_HIGH]); return;
        default: break;
    }
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    gui_draw_text_centered(g, gui_rect(box.x, box.y + box.h / 2 - 4, box.w, 9), rack_type_name((module_type_t)ms->type));
}

// Algorithm diagram (generated sprite) with the selected operator inverted (sel_op < 0: none).
void draw_algo(u8g2_t *g, gui_rect_t box, const dx7_patch_t *p, int sel_op) {
    const dx7_algo_gfx_t *a = &dx7_algo_gfx[(p->algorithm - 1) & 31];
    const int x = box.x + (box.w > DX7_ALGO_W ? (box.w - DX7_ALGO_W) / 2 : 0);    // centred in the box
    const int y = box.y + (box.h > DX7_ALGO_H ? (box.h - DX7_ALGO_H) / 2 : 0);
    u8g2_SetBitmapMode(g, 1);                                 // transparent: the box frame stays
    u8g2_DrawBitmap(g, (u8g2_uint_t)x, (u8g2_uint_t)y, 8, DX7_ALGO_H, a->bits);
    u8g2_SetBitmapMode(g, 0);
    if (sel_op < 0 || sel_op >= DX7_OPS) return;
    u8g2_SetDrawColor(g, 2);                                  // XOR: inverts the box and its digit
    u8g2_DrawBox(g, x + a->opx[sel_op], y + a->opy[sel_op], DX7_ALGO_BOX, DX7_ALGO_BOX);
    u8g2_SetDrawColor(g, 1);
}

// Graphical envelope editor: the 4 stages as a polyline, the selected point drawn as a box.
// Horizontal size of a stage grows with the logarithm of its time, so 1 ms and 5 s both stay editable.
void draw_eg_editor(u8g2_t *g, gui_rect_t box, const dx7_op_t *o, int sel) {
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const int left = box.x + 4, right = box.x + box.w - 5, top = box.y + 4, bottom = box.y + box.h - 5;
    float w[4], total = 0;
    for (int i = 0; i < 4; i++) { w[i] = log2f(1.0f + (float)o->eg_t[i]) + 2.0f; total += w[i]; }
    int px[5], py[5];
    px[0] = left; py[0] = bottom - (int)((float)o->eg_l[3] * (float)(bottom - top) / 99.0f);   // starts at the release level
    float acc = 0;
    for (int i = 0; i < 4; i++) {
        acc += w[i];
        px[i + 1] = left + (int)(acc / total * (float)(right - left));
        py[i + 1] = bottom - (int)((float)o->eg_l[i] * (float)(bottom - top) / 99.0f);
    }
    for (int y = top; y <= bottom; y += 3) u8g2_DrawPixel(g, px[3], y);        // key-off: the release (stage 4) follows
    for (int i = 0; i < 4; i++) u8g2_DrawLine(g, px[i], py[i], px[i + 1], py[i + 1]);
    for (int i = 0; i < 4; i++) {
        if (i == sel) u8g2_DrawBox(g, px[i + 1] - 2, py[i + 1] - 2, 5, 5);
        else          u8g2_DrawFrame(g, px[i + 1] - 1, py[i + 1] - 1, 3, 3);
    }
}
