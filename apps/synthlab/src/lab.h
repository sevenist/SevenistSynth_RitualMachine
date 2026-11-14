#pragma once
// SynthLab's API: what a synth is. A synth is one file in src/synths/: a list of parameters and a Voice, written in plain float C++.
// Nothing else in the app needs to change but the list in src/synths/synth_list.cpp.
//
//   const lab::Param kParams[] = {                         // what the screen lists, in this order (at most lab::kMaxParams)
//       {"Cutoff", 40, 16000, 1200, "Hz", lab::Exp},       //   name, min, max, default, unit, how the encoder moves it
//       lab::section("ENVELOPE"),                          //   a title row: the list's sections (it takes an index of p[] too)
//       {"Decay",  5,  4000,  300,  "ms", lab::Exp},
//   };
//   struct Voice {                                         // one voice: the engine runs one per key held (poly) or one (mono)
//       void start(const lab::Note &n, const float *p);    //   a key went down (a fresh voice or a stolen one: reset what must be)
//       void release(const lab::Note &n, const float *p);  //   the key went up
//       bool render(float *out, int frames, const lab::Note &n, const float *p);   // ADD frames samples to out (it is zeroed);
//   };                                                     //   return false once silent: the voice is then freed
//   extern const lab::Synth synth_my_sound = lab::make_synth<Voice>("My sound", kParams);
//
// A synth may add an effect that runs ONCE on the sum of its voices (a reverb, a delay: one copy of its memory, and its tail goes on after
// the voices end), between the voices and the master:
//   struct Fx {
//       void render(float *io, int frames, float sr, const float *p);   // io: the voices' sum (mono, full scale +-1), changed in place
//   };
//   extern const lab::Synth synth_my_sound = lab::make_synth<Voice, Fx>("My sound", kParams);
//
// p[i] is the value of kParams[i] as the screen shows it (Hz in Hz, ms in ms ...), read at every block (kBlock frames, 32 by default):
// a value the encoder moves jumps from one block to the next, smooth it in the voice when that clicks. out is full scale at +-1.0;
// a voice should stay around +-0.5 (several sound at once, the sum is clipped).
//
// The rules of the audio thread hold in start / release / render: no new / malloc, no printf, no locks. The Voice is built (its
// default member values) when its synth is selected, not at every note; so is the Fx (its memory zeroed: a reverb starts empty).
#include <cstddef>
#include <cstdint>
#include <new>

namespace lab {

// The values of a synth, its section titles included (sent to the engine 63 at a time: lab_voice.h). A desktop program may raise it
// (-DLAB_MAX_PARAMS=256: apps/studio, whose modular synth lists a patch's values; at most 256, the blob's index is a byte).
#ifndef LAB_MAX_PARAMS
#define LAB_MAX_PARAMS 128
#endif
constexpr int kMaxParams = LAB_MAX_PARAMS;
constexpr int kMaxSynths = 32;

// How the second encoder moves a value: Lin in equal steps (100 detents from min to max), Exp by an equal ratio (100 detents; for Hz,
// ms, anything heard on a log scale; min must be > 0), Int by 1 (whole numbers; with `labels`, a name for each). Head: not a value but
// a section's title (lab::section); encoder A steps over it.
enum Curve : uint8_t { Lin, Exp, Int, Head };

struct Param {
    const char *name;                   // about 8 characters fit beside the value
    float min, max, def;
    const char *unit = nullptr;         // drawn after the value: "Hz", "ms", "%", "st" ... (nullptr: none)
    Curve curve = Lin;
    const char *const *labels = nullptr;    // Int: the names of min .. max, shown instead of the number
};

constexpr Param section(const char *title) { return Param{title, 0, 0, 0, nullptr, Head, nullptr}; }

struct Note {
    float hz;                           // the pitch, in Hz: it follows the glide (mono) at every block
    float velocity;                     // 0..1
    bool gate;                          // the key is held
    float sr;                           // the sample rate, in Hz
};

// A pattern PLAY can loop (Options::grooves): one hit per step, at the velocity of the step (0: a rest), on the note last played
// raised by the step's semitones. Played by the engine's sequencer, on time to the sample.
struct Groove {
    uint8_t steps;                      // 1..64
    uint8_t rate;                       // steps per beat: 2 (eighths), 3 (eighth triplets), 4 (sixteenths), 6, 8 (32nds)
    const uint8_t *vel;                 // `steps` velocities, 1..127; 0 = a rest
    const int8_t *semi = nullptr;       // `steps` semitones added to the note (nullptr: none)
};

struct Options {
    bool mono = false;                  // one voice, last note wins (a held key comes back when the top one is let go)
    bool legato = false;                // mono: a new key while one is held changes the pitch, no start()
    int glide_ms = 0;                   // mono: the pitch slides to the new note
    int play_bpm = 0;                   // > 0: PLAY starts / stops a loop of the last note played, one hit per beat at this tempo (the
                                        //   engine's sequencer: on time to the sample); 0: PLAY holds the last note
    // Grooves (with play_bpm > 0): PLAY loops grooves[the value of groove_param] instead of one hit a beat, at the value of tempo_param
    // (BPM) with the swing of swing_param (0..75 %), each read live: a new groove starts at the end of the playing one. -1: not used
    // (no tempo_param: play_bpm; no swing_param: none).
    const Groove *grooves = nullptr;
    int n_grooves = 0;
    int groove_param = -1, tempo_param = -1, swing_param = -1;
};

// A synth as the app sees it. make_synth fills it from the Voice type (and the Fx type).
struct Synth {
    const char *name;
    const Param *params;
    int n_params;
    Options opt;
    size_t size, align;                 // of the Voice
    void (*construct)(void *);
    void (*destroy)(void *);
    void (*start)(void *, const Note &, const float *);
    void (*release)(void *, const Note &, const float *);
    bool (*render)(void *, float *, int, const Note &, const float *);
    size_t fx_size, fx_align;           // of the Fx (0: none)
    void (*fx_construct)(void *);
    void (*fx_destroy)(void *);
    void (*fx_render)(void *, float *, int, float, const float *);
};

template <typename V>
struct VoiceOps {
    static void construct(void *m) { new (m) V(); }
    static void destroy(void *m) { static_cast<V *>(m)->~V(); }
    static void start(void *m, const Note &n, const float *p) { static_cast<V *>(m)->start(n, p); }
    static void release(void *m, const Note &n, const float *p) { static_cast<V *>(m)->release(n, p); }
    static bool render(void *m, float *out, int frames, const Note &n, const float *p) { return static_cast<V *>(m)->render(out, frames, n, p); }
};

template <typename F>
struct FxOps {
    static void construct(void *m) { new (m) F(); }
    static void destroy(void *m) { static_cast<F *>(m)->~F(); }
    static void render(void *m, float *io, int frames, float sr, const float *p) { static_cast<F *>(m)->render(io, frames, sr, p); }
};

template <typename V, int N>
constexpr Synth make_synth(const char *name, const Param (&params)[N], Options opt = {}) {
    static_assert(N <= kMaxParams, "a synth has at most lab::kMaxParams parameters");
    static_assert(alignof(V) <= 16, "the voice's memory is aligned to 16 bytes");
    return Synth{name, params, N, opt, sizeof(V), alignof(V), &VoiceOps<V>::construct, &VoiceOps<V>::destroy, &VoiceOps<V>::start,
                 &VoiceOps<V>::release, &VoiceOps<V>::render, 0, 0, nullptr, nullptr, nullptr};
}
template <typename V, typename F, int N>
constexpr Synth make_synth(const char *name, const Param (&params)[N], Options opt = {}) {
    static_assert(alignof(F) <= 16, "the effect's memory is aligned to 16 bytes");
    Synth s = make_synth<V, N>(name, params, opt);
    s.fx_size = sizeof(F);
    s.fx_align = alignof(F);
    s.fx_construct = &FxOps<F>::construct;
    s.fx_destroy = &FxOps<F>::destroy;
    s.fx_render = &FxOps<F>::render;
    return s;
}

// The synths, in the order of the screen's list (src/synths/synth_list.cpp).
extern const Synth *const kSynths[];
extern const int kSynthCount;

}  // namespace lab
