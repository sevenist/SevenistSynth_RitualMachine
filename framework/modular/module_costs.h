#pragma once
// What each module of the modular catalog costs on the board, in CPU cycles per 32-frame block: measured by apps/synthlab's
// bench (src/bench_esp32.cpp, environment hwv1_bench) and written by apps/synthlab/tools/module_bench.py --write. Do not edit by
// hand: run the bench again. SynthLab Studio's cycles wallet (the PATCH tab) adds them up against the board's budget.
//   cycles   the module alone with its inputs fed (a sine where an input reads silence by default), its default values
//   worst    the slowest of its values one at a time at their minimum / maximum (and each choice); worst_at: which
//   cold     the same block with the instruction cache emptied first: what code left in flash costs at worst
//   start    a note starting (once a note, not a block)        bytes: its state (a voice holds one per instance)
//   engine   the module as an engine type (modular/engine_modules.h, what an engine synth plays), q15 ports; 0: not measured
// The code is in internal RAM (SC_HOT, modules.h), so `cycles` is what a voice pays; what is left in flash (the C library's
// expf, powf, tanf ...) is why `cold` stays above it for some.
#include <cstdint>
#include <cstring>

namespace mod {

struct ModuleCost { const char *name; uint32_t cycles, worst; const char *worst_at; uint32_t cold, start, bytes, engine; };
struct BoardCost {
    const char *board, *measured;     // where and when
    int cpu_mhz, rate, block;
    uint32_t budget;                  // the cycles of one block of audio at this rate
    uint32_t engine_fixed;            // the engine with no note (its master, the bus): every block
    uint32_t engine_voice;            // what the engine adds for each voice playing (its note, the voice's mix)
    uint32_t voice_block;             // a generated synth's own work around its modules, a voice, a block
    uint32_t cable_lin, cable_exp, cable_int;   // a modulation cable onto a value of that curve (once a block)
    uint32_t summed_input;            // an input fed by a cable at a depth other than 1, or by two cables
};

inline constexpr BoardCost kBoardCost = {"hwv1", "2026-11-15", 240, 44100, 32, 174149, 6912, 2678, 1024, 270, 1539, 386, 398};

inline constexpr ModuleCost kModuleCosts[] = {
    {"Note", 651, 651, "", 8370, 119, 1, 0},
    {"VoiceOut", 311, 311, "", 2701, 119, 1, 0},
    {"Voices", 13, 13, "", 715, 119, 1, 0},
    {"Output", 285, 285, "", 2701, 119, 1, 0},
    {"Macro", 103, 103, "", 2475, 122, 1, 1389},
    {"Osc", 2267, 3449, "Wave=3", 3265, 298, 4, 3994},
    {"Op", 2363, 2363, "", 12396, 346, 12, 4133},
    {"Body", 10345, 10345, "", 11115, 119, 4, 12266},
    {"Noise", 1014, 1014, "", 12718, 571, 12, 2311},
    {"Metal", 8669, 8669, "", 18224, 1159, 112, 10040},
    {"Rumble", 3768, 4350, "EnvAmt=-5", 8798, 374, 3264, 6216},
    {"RMOsc", 5250, 6967, "pitch=127", 44525, 729, 5088, 0},
    {"RMOscEngines", 6509, 11298, "engine=6", 27311, 378, 10832, 0},
    {"RMStrings", 7871, 8994, "wave=1", 47510, 414, 5152, 0},
    {"RMMiOsc", 8571, 46142, "model=36", 65480, 415, 6368, 0},
    {"ADSR", 1574, 1574, "", 2908, 420, 48, 3046},
    {"Perc", 2408, 2408, "", 26380, 322, 36, 3544},
    {"Ratchet", 1587, 1941, "Count=8", 18701, 118, 48, 3063},
    {"LFO", 1520, 1520, "", 14138, 118, 24, 3029},
    {"RMEnv", 4309, 4309, "", 43006, 544, 6096, 0},
    {"RMLfo", 3443, 3443, "", 39739, 414, 5088, 0},
    {"RMEg", 6125, 6125, "", 26975, 250, 6752, 0},
    {"RMEnvG", 5138, 5138, "", 25822, 544, 6096, 0},
    {"Filter", 1709, 2759, "Type=4", 2927, 322, 80, 3872},
    {"EQ3", 462, 1228, "LowDb=18", 1631, 119, 120, 2250},
    {"RMFilter", 7736, 10363, "sections=4", 50120, 707, 5184, 0},
    {"RMEq3", 9851, 9851, "", 27518, 987, 5696, 0},
    {"RMTunedComb", 9626, 12664, "interval=24", 69106, 712, 16224, 0},
    {"RMEq3V", 6370, 6370, "", 41960, 228, 5696, 0},
    {"Drive", 4524, 6704, "Fold=100", 29257, 118, 8, 6143},
    {"Comp3", 5894, 5894, "", 6312, 419, 108, 7647},
    {"Limit", 1139, 1139, "", 9840, 119, 1, 2863},
    {"RMShaper", 5374, 6944, "mode=4", 44957, 712, 5088, 0},
    {"RMCompressor", 11065, 11065, "", 41709, 2761, 6416, 0},
    {"RMCompressorV", 9113, 9113, "", 55330, 544, 6416, 0},
    {"VCA", 446, 446, "", 2906, 120, 1, 2272},
    {"Mixer", 692, 692, "", 3696, 301, 1, 3465},
    {"Ring", 313, 313, "", 2069, 250, 1, 2351},
    {"Const", 131, 131, "", 1427, 119, 1, 1402},
    {"RMVca", 3040, 3040, "", 41944, 708, 5056, 0},
    {"RMMix4", 9414, 9414, "", 49024, 416, 5056, 0},
    {"RMMult", 3948, 3948, "", 39878, 410, 5056, 0},
    {"RMConst", 1614, 1614, "", 38143, 374, 5056, 0},
    {"Math", 467, 640, "Op=5", 4599, 119, 1, 2585},
    {"Func", 384, 3375, "Fn=5", 5197, 620, 1, 2091},
    {"Range", 1310, 23291, "Curve=100", 10721, 278, 1, 3040},
    {"Slew", 1054, 1428, "Mode=0", 17618, 274, 8, 2783},
    {"Delay", 2268, 3178, "Time=1300", 18856, 916, 131080, 4469},
    {"Room", 5945, 5945, "", 7152, 274, 12768, 7721},
    {"Glue", 4414, 5007, "Sat=100", 6748, 119, 12, 6188},
    {"Gate", 2520, 2520, "", 3246, 274, 16, 4467},
    {"RMDelay", 18444, 18444, "", 46075, 544, 225664, 0},
    {"RMSpectralFx", 7224, 9283, "stereo=1", 34890, 140, 25184, 0},
    {"RMVocoder", 5856, 5856, "", 29751, 138, 14752, 0},
    {"RMChorus", 15008, 15008, "", 40389, 140, 7008, 0},
    {"RMReverb", 64256, 64256, "", 90971, 544, 126176, 0},
    {"RMPhaser", 18056, 20624, "stages=4", 62621, 277, 5168, 0},
    {"RMFlanger", 14408, 14408, "", 55922, 140, 10048, 0},
    {"RMTremolo", 6223, 6903, "rate=-18432", 37503, 273, 5072, 0},
    {"RMShifter", 9100, 24497, "mode=1", 39176, 413, 5488, 0},
    {"RMEnsemble", 9635, 9635, "", 30741, 139, 6368, 0},
    {"RMPhaserV", 10767, 10767, "", 62918, 414, 5168, 0},
    {"RMFlangerV", 9120, 9120, "", 62146, 414, 7584, 0},
    {"RMTremoloV", 4655, 4655, "", 52022, 410, 5072, 0},
    {"RMShifterV", 4941, 13605, "mode=1", 53099, 708, 5488, 0},
};

inline const ModuleCost *module_cost(const char *name) {
    for (const ModuleCost &c : kModuleCosts) if (!std::strcmp(c.name, name)) return &c;
    return nullptr;                   // not measured (a module added since, an engine module)
}

}  // namespace mod
