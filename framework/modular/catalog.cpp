// The modular catalog's descriptions (modular.h): the modules of modules.h with their inputs, outputs and values. The values' order is
// modules.h's P:: indexes.
#include <cctype>
#include <cmath>
#include <cstring>
#include "modular/modules.h"

namespace mod {

namespace {
const char *const kOnOff[] = {"Off", "On"};
const char *const kOscWaves[] = {"Sine", "Tri", "Saw", "Square"};
const char *const kMetalTypes[] = {"Squares", "FM"};
const char *const kLfoWaves[] = {"Sine", "Tri", "SawUp", "SawDown", "Square", "S&H"};
const char *const kFilterTypes[] = {"LP12", "LP24", "BP", "HP12", "HP24", "Notch"};
const char *const kStreams[] = {"A", "B"};
const char *const kLaws[] = {"Blend", "Power"};
const char *const kSteps[] = {"Sample", "Block"};
const char *const kRumbleFilters[] = {"LP6", "LP12", "LP24", "BP", "HP12", "HP24", "Notch", "Peak"};

const ModuleDef kNote = {
    "Note", "In / Out", "The voice's note: its gate, a pulse at its start, its velocity, its pitch (octaves over middle C); in the FX area: the last note", Scope::Both,
    0, 4, 0, {}, {}, {"Gate", "Trig", "Vel", "Pitch"}, {},
    "NoteIn", "the engine's NoteIn", MOD_OPS(NoteM)};

const ModuleDef kVoiceOut = {
    "VoiceOut", "In / Out", "The voice's sound: what comes in here is summed over the voices into the FX area", Scope::Voice,
    1, 1, 1, {"In"}, {kZero}, {"Out"}, {{"Level", 0, 100, 100, "%"}},
    "VoiceOut", "the engine's VoiceOut", MOD_OPS(OutM)};

const ModuleDef kVoices = {
    "Voices", "In / Out", "The sum of the voices (what their VoiceOut sent), in the FX area", Scope::Global,
    0, 1, 0, {}, {}, {"Out"}, {},
    "BusIn", "the engine's BusIn", MOD_OPS(VoicesM)};

const ModuleDef kOutput = {
    "Output", "In / Out", "The machine's output, after the FX", Scope::Global,
    1, 1, 1, {"In"}, {kZero}, {"Out"}, {{"Level", 0, 100, 100, "%"}},
    "MasterOut", "the engine's MasterOut", MOD_OPS(OutM)};

const ModuleDef kOsc = {
    "Osc", "Sources", "Sine, triangle, saw, square (PolyBLEP); Pitch input in octaves x PMod / 12, FM input linear", Scope::Both,
    2, 1, 8, {"Pitch", "FM"}, {kZero, kZero}, {"Out"},
    {{"Wave", 0, 3, 2, nullptr, Int, kOscWaves}, {"Tune", -48, 48, 0, "st", Int}, {"Fine", -100, 100, 0, "ct"},
     {"PW", 5, 95, 50, "%"}, {"Track", 0, 1, 1, nullptr, Int, kOnOff}, {"Level", 0, 100, 70, "%"}, {"PMod", 0, 48, 12, "st"},
     {"FM", 0, 400, 0, "%"}},
    "Osc", "mdsp.h Osc", MOD_OPS(OscM)};

const ModuleDef kOp = {
    "Op", "Sources", "An FM operator: a sine at Ratio x the pitch, its phase moved by PM (x Depth radians) and its own Feedback", Scope::Both,
    1, 1, 5, {"PM"}, {kZero}, {"Out"},
    {{"Ratio", 0.25f, 16, 1, "x", Exp}, {"Depth", 0, 12, 4, "rad"}, {"Feedback", 0, 100, 0, "%"}, {"Level", 0, 100, 100, "%"},
     {"Track", 0, 1, 1, nullptr, Int, kOnOff}},
    nullptr, "fm2.cpp operators", MOD_OPS(OpM)};

const ModuleDef kBody = {
    "Body", "Sources", "The Kick's oscillator: a sine to a triangle (Harm) and to a distorted sine (Shape); Tune on C3", Scope::Both,
    2, 1, 7, {"Pitch", "Harm"}, {kZero, kZero}, {"Out"},
    {{"Tune", 20, 1000, 50, "Hz", Exp}, {"Harm", 0, 100, 0, "%"}, {"Shape", 0, 100, 15, "%"}, {"Level", 0, 100, 100, "%"},
     {"PMod", 0, 60, 30, "st"}, {"Track", 0, 1, 1, nullptr, Int, kOnOff}, {"Steps", 0, 1, 0, nullptr, Int, kSteps}},
    nullptr, "kick.cpp / drumlab.cpp BODY", MOD_OPS(BodyM)};

const ModuleDef kNoise = {
    "Noise", "Sources", "White noise through a low pass (Tone); Seed > 0: the same noise at every note", Scope::Both,
    0, 1, 4, {}, {}, {"Out"},
    {{"Level", 0, 100, 50, "%"}, {"Tone", 100, 24000, 24000, "Hz", Exp}, {"Seed", 0, 999, 0, nullptr, Int},
     {"Stream", 0, 1, 0, nullptr, Int, kStreams}},
    nullptr, "mdsp.h Noise, drumlab.cpp NOISE", MOD_OPS(NoiseM)};

const ModuleDef kMetal = {
    "Metal", "Sources", "The hat's metal: six squares at the analog hat's ratios (or a 2-operator FM), through a band pass", Scope::Both,
    0, 1, 7, {}, {}, {"Out"},
    {{"Type", 0, 1, 0, nullptr, Int, kMetalTypes}, {"Tune", 50, 2000, 205, "Hz", Exp}, {"Index", 0, 100, 30, "%"},
     {"Ratio", 0.5f, 8, 1.41f, "x", Exp}, {"Tone", 1000, 16000, 8000, "Hz", Exp}, {"Level", 0, 100, 70, "%"},
     {"Track", 0, 1, 0, nullptr, Int, kOnOff}},
    nullptr, "drumlab.cpp METAL", MOD_OPS(MetalM)};

const ModuleDef kAdsr = {
    "ADSR", "Envelopes", "Attack, decay, sustain, release, analog style, on its Gate (by default the voice's)", Scope::Both,
    1, 1, 4, {"Gate"}, {kGate}, {"Env"},
    {{"Attack", 0.1f, 10000, 5, "ms", Exp}, {"Decay", 1, 10000, 300, "ms", Exp}, {"Sustain", 0, 100, 70, "%"},
     {"Release", 1, 10000, 300, "ms", Exp}},
    "Eg", "mdsp.h Adsr", MOD_OPS(AdsrM)};

const ModuleDef kPerc = {
    "Perc", "Envelopes", "A hit's envelope on a Trig: Attack, Hold, Decay; Law Blend: a line (Curve 0) to an exponential, Power: the Kick's", Scope::Both,
    1, 1, 7, {"Trig"}, {kTrig}, {"Env"},
    {{"Attack", 0, 100, 0.5f, "ms"}, {"Hold", 0, 500, 0, "ms"}, {"Decay", 2, 5000, 300, "ms", Exp}, {"Curve", 0, 100, 60, "%"},
     {"Amount", 0, 100, 100, "%"}, {"Law", 0, 1, 0, nullptr, Int, kLaws}, {"Steps", 0, 1, 0, nullptr, Int, kSteps}},
    "Eg", "kick.cpp amp / pitch envelopes", MOD_OPS(PercM)};

const ModuleDef kRatchet = {
    "Ratchet", "Envelopes", "The Snare's wires: Count bursts on a Trig, Spacing apart (each gap Accel x the last), then a Decay", Scope::Both,
    1, 1, 5, {"Trig"}, {kTrig}, {"Env"},
    {{"Count", 1, 8, 3, nullptr, Int}, {"Spacing", 1, 100, 8, "ms", Exp}, {"Accel", -100, 100, 0, "%"}, {"Burst", 1, 200, 15, "ms", Exp},
     {"Decay", 2, 2000, 180, "ms", Exp}},
    nullptr, "snare.cpp / drumlab.cpp NOISE", MOD_OPS(RatchetM)};

const ModuleDef kLfo = {
    "LFO", "Envelopes", "A slow oscillator: sine, triangle, saws, square, sample and hold; restarted by Trig when Retrig is on", Scope::Both,
    1, 1, 5, {"Trig"}, {kTrig}, {"Out"},
    {{"Wave", 0, 5, 0, nullptr, Int, kLfoWaves}, {"Rate", 0.02f, 50, 2, "Hz", Exp}, {"Retrig", 0, 1, 0, nullptr, Int, kOnOff},
     {"Unipolar", 0, 1, 0, nullptr, Int, kOnOff}, {"Level", 0, 100, 100, "%"}},
    "Lfo", "new", MOD_OPS(LfoM)};

const ModuleDef kFilter = {
    "Filter", "Filters", "State-variable filter: LP / HP 12 and 24 dB, BP, notch; Cutoff input in octaves x CMod; key Track", Scope::Both,
    2, 1, 5, {"In", "Cutoff"}, {kZero, kZero}, {"Out"},
    {{"Type", 0, 5, 0, nullptr, Int, kFilterTypes}, {"Cutoff", 20, 20000, 2000, "Hz", Exp}, {"Reso", 0, 100, 10, "%"},
     {"Track", 0, 100, 0, "%"}, {"CMod", 0, 8, 3, "oct"}},
    "Filter", "mdsp.h Svf, kick.cpp rumble filters", MOD_OPS(FilterM)};

const ModuleDef kEq3 = {
    "EQ3", "Filters", "A low shelf, a bell and a high shelf (each band skipped at 0 dB)", Scope::Both,
    1, 1, 7, {"In"}, {kZero}, {"Out"},
    {{"LowDb", -18, 18, 0, "dB"}, {"LowHz", 30, 1000, 120, "Hz", Exp}, {"MidDb", -18, 18, 0, "dB"}, {"MidHz", 100, 10000, 1000, "Hz", Exp},
     {"MidQ", 0.3f, 5, 0.9f, nullptr, Exp}, {"HighDb", -18, 18, 0, "dB"}, {"HighHz", 1000, 16000, 6000, "Hz", Exp}},
    nullptr, "mdsp.h Eq3, snare.cpp / drumlab.cpp EQ", MOD_OPS(Eq3M)};

const ModuleDef kDrive = {
    "Drive", "Shapers", "The Kick's drive: Tube (even harmonics), Fold, Sat, Clip, each mixed in; DC out; the Out level", Scope::Both,
    1, 1, 6, {"In"}, {kZero}, {"Out"},
    {{"Drive", 0, 24, 6, "dB"}, {"Tube", 0, 100, 25, "%"}, {"Fold", 0, 100, 0, "%"}, {"Sat", 0, 100, 35, "%"}, {"Clip", 0, 100, 0, "%"},
     {"Out", -24, 6, 0, "dB"}},
    "Shaper", "kick.cpp / drumlab.cpp DRIVE", MOD_OPS(DriveM)};

const ModuleDef kVca = {
    "VCA", "Utilities", "In x CV x Level (CV reads 1 when nothing comes in): an envelope's level, a tremolo", Scope::Both,
    2, 1, 1, {"In", "CV"}, {kZero, kOne}, {"Out"}, {{"Level", 0, 100, 100, "%"}},
    "Vca", "new", MOD_OPS(VcaM)};

const ModuleDef kMixer = {
    "Mixer", "Utilities", "Four inputs at their levels (a negative level inverts)", Scope::Both,
    4, 1, 4, {"In1", "In2", "In3", "In4"}, {kZero, kZero, kZero, kZero}, {"Out"},
    {{"Lvl1", -100, 100, 100, "%"}, {"Lvl2", -100, 100, 100, "%"}, {"Lvl3", -100, 100, 100, "%"}, {"Lvl4", -100, 100, 100, "%"}},
    "Mix4", "new", MOD_OPS(MixerM)};

const ModuleDef kRing = {
    "Ring", "Utilities", "A x B: ring modulation, or a signal scaled by another", Scope::Both,
    2, 1, 0, {"A", "B"}, {kZero, kZero}, {"Out"}, {},
    "Mult", "snare.cpp ring", MOD_OPS(RingM)};

const ModuleDef kConst = {
    "Const", "Utilities", "A steady value: an offset, a level to send into an input", Scope::Both,
    0, 1, 1, {}, {}, {"Out"}, {{"Value", -1, 1, 0}},
    "Const", "new", MOD_OPS(ConstM)};

const ModuleDef kDelay = {
    "Delay", "Effects", "An echo: a 16-bit line up to 1.3 s, a low pass in the loop (Damp), Mix", Scope::Both,
    1, 1, 4, {"In"}, {kZero}, {"Out"},
    {{"Time", 1, 1300, 250, "ms", Exp}, {"Feedback", 0, 95, 35, "%"}, {"Damp", 500, 20000, 8000, "Hz", Exp}, {"Mix", 0, 100, 30, "%"}},
    "Delay", "kick.cpp rumble echo", MOD_OPS(DelayM)};

const ModuleDef kRoom = {
    "Room", "Effects", "The Kick's reverb: four lines through a Hadamard matrix, at half the rate. Lean, not lush", Scope::Both,
    1, 1, 4, {"In"}, {kZero}, {"Out"},
    {{"Mix", 0, 100, 25, "%"}, {"Decay", 100, 10000, 1200, "ms", Exp}, {"Size", 5, 100, 70, "%"}, {"Damp", 0, 100, 40, "%"}},
    "Reverb", "kick.cpp Room", MOD_OPS(RoomM)};

const ModuleDef kGlue = {
    "Glue", "Effects", "The Kick's master: a glue compressor, saturation, a tilt around 800 Hz, the output level", Scope::Both,
    1, 1, 6, {"In"}, {kZero}, {"Out"},
    {{"Glue", 0, 100, 30, "%"}, {"Attack", 0.1f, 100, 10, "ms", Exp}, {"Release", 10, 2000, 150, "ms", Exp}, {"Sat", 0, 100, 0, "%"},
     {"Tilt", -100, 100, 0, "%"}, {"Out", -24, 12, 0, "dB"}},
    nullptr, "kick.cpp Fx (master shaper)", MOD_OPS(GlueM)};

const ModuleDef kGate = {
    "Gate", "Effects", "The Snare's gate: open above Threshold on its Key (by default its In), held, then down to 1 - Depth", Scope::Both,
    2, 1, 5, {"In", "Key"}, {kZero, kIn0}, {"Out"},
    {{"Depth", 0, 100, 100, "%"}, {"Thresh", -60, 0, -30, "dB"}, {"Attack", 0.1f, 50, 1, "ms", Exp}, {"Hold", 0, 500, 40, "ms"},
     {"Release", 1, 1000, 60, "ms", Exp}},
    nullptr, "snare.cpp Fx (gate)", MOD_OPS(GateM)};

const ModuleDef kRumble = {
    "Rumble", "Sources", "The Kick's rumble: noise / the kick (In Mix), Pre, a resonant filter with its envelope, Post, decay, echo, duck; at 1/8 rate", Scope::Both,
    3, 1, 16, {"Kick", "Key", "Vel"}, {kZero, kZero, kOne}, {"Out"},
    {{"InMix", 0, 100, 0, "%"}, {"Decay", 5, 4000, 300, "ms", Exp}, {"Pre", 0, 100, 30, "%"}, {"Filter", 0, 7, 1, nullptr, Int, kRumbleFilters},
     {"Cutoff", 20, 500, 90, "Hz", Exp}, {"Reso", 0, 100, 40, "%"}, {"EnvAmt", -5, 5, 0, "oct"}, {"EnvAtk", 0, 500, 0, "ms"},
     {"EnvDec", 5, 4000, 150, "ms", Exp}, {"Post", 0, 100, 40, "%"}, {"Delay", 10, 250, 80, "ms", Exp}, {"Feedback", 0, 90, 35, "%"},
     {"DlyMix", 0, 100, 30, "%"}, {"Duck", 0, 100, 50, "%"}, {"DuckRel", 10, 1000, 80, "ms", Exp}, {"Seed", 1, 99, 1, nullptr, Int}},
    nullptr, "kick.cpp rumble_tick", MOD_OPS(RumbleM)};

const ModuleDef kComp3 = {
    "Comp3", "Shapers", "The Kick's 3-band compressor: split at 120 Hz and 2.5 kHz, each band's amount its threshold and ratio", Scope::Both,
    1, 1, 5, {"In"}, {kZero}, {"Out"},
    {{"Low", 0, 100, 40, "%"}, {"Mid", 0, 100, 25, "%"}, {"High", 0, 100, 30, "%"}, {"Attack", 0.1f, 50, 6, "ms", Exp},
     {"Release", 10, 1000, 120, "ms", Exp}},
    nullptr, "kick.cpp 3-band compressor", MOD_OPS(Comp3M)};

const ModuleDef kLimit = {
    "Limit", "Shapers", "A soft limit: soft_clip(In x Gain) x Out (the Kick's last step)", Scope::Both,
    1, 1, 2, {"In"}, {kZero}, {"Out"},
    {{"Gain", -30, 12, 0, "dB"}, {"Out", 0, 100, 100, "%"}},
    nullptr, "kick.cpp output", MOD_OPS(LimitM)};

const ModuleDef *const kCatalog[] = {
    &kNote, &kVoiceOut, &kVoices, &kOutput,
    &kOsc, &kOp, &kBody, &kNoise, &kMetal, &kRumble,
    &kAdsr, &kPerc, &kRatchet, &kLfo,
    &kFilter, &kEq3,
    &kDrive, &kComp3, &kLimit, &kVca, &kMixer, &kRing, &kConst,
    &kDelay, &kRoom, &kGlue, &kGate,
};

bool same(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

// The catalog: its own modules, then the ones added at run time (catalog_add).
constexpr int kMaxModules = 128;
const ModuleDef *g_all[kMaxModules];
int g_n = 0;
void fill() {
    if (g_n) return;
    for (const ModuleDef *d : kCatalog) g_all[g_n++] = d;
}
}  // namespace

const ModuleDef *const *catalog(int *count) {
    fill();
    if (count) *count = g_n;
    return g_all;
}

const ModuleDef *find_module(const char *name) {
    fill();
    for (int i = 0; i < g_n; i++) if (same(g_all[i]->name, name)) return g_all[i];
    return nullptr;
}

bool catalog_add(const ModuleDef *d) {
    fill();
    if (!d || g_n >= kMaxModules || find_module(d->name)) return false;
    int at = g_n;                                       // after the last module of its kind: the kinds stay together
    for (int i = 0; i < g_n; i++) if (same(g_all[i]->category, d->category)) at = i + 1;
    for (int i = g_n; i > at; i--) g_all[i] = g_all[i - 1];
    g_all[at] = d;
    g_n++;
    return true;
}

int param_index(const ModuleDef &d, const char *name) {
    for (int i = 0; i < d.n_param; i++) if (same(d.param[i].name, name)) return i;
    return -1;
}
int in_index(const ModuleDef &d, const char *name) {
    for (int i = 0; i < d.n_in; i++) if (same(d.in_name[i], name)) return i;
    return -1;
}
int out_index(const ModuleDef &d, const char *name) {
    for (int i = 0; i < d.n_out; i++) if (same(d.out_name[i], name)) return i;
    return -1;
}

}  // namespace mod
