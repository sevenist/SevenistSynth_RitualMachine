// The PATCH tab's exports (export.h).
#include "export.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>
#include "modular/engine_adapter.h"
#include "modular/modules.h"
#include "studio.h"

namespace studio {

namespace {

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string today() {
    const std::time_t t = std::time(nullptr);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&t));
    return b;
}

bool write_text(const std::string &path, const std::string &text, std::string &err) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) { err = "cannot write " + path; return false; }
    return true;
}

std::string lit(float v) {                          // a float literal
    std::string s = fmt("%.9g", static_cast<double>(v));
    if (s.find_first_of(".en") == std::string::npos) s += ".";
    return s + "f";
}

std::string cstr(const char *s) {                   // a C string literal
    std::string o = "\"";
    for (; *s; s++) { if (*s == '"' || *s == '\\') o += '\\'; o += *s; }
    return o + "\"";
}

// The order the runner plays an area in (patch.cpp, Runner::build): a module after those it listens to; in a loop, the first one left.
std::vector<int> area_order(const mod::Patch &p, int area) {
    std::vector<int> order;
    std::vector<bool> done(static_cast<size_t>(p.n_nodes), false);
    int count = 0;
    for (int i = 0; i < p.n_nodes; i++) if (p.node[i].area == area) count++;
    while (static_cast<int>(order.size()) < count) {
        int pick = -1;
        for (int i = 0; i < p.n_nodes && pick < 0; i++) {
            if (done[static_cast<size_t>(i)] || p.node[i].area != area) continue;
            bool ready = true;
            for (int k = 0; k < p.n_cables && ready; k++) {
                const mod::Cable &c = p.cable[k];
                if (c.dst != p.node[i].id || c.src == c.dst) continue;
                const int s = p.index_of(c.src);
                if (s >= 0 && !done[static_cast<size_t>(s)] && p.node[s].area == area) ready = false;
            }
            if (ready) pick = i;
        }
        if (pick < 0) for (int i = 0; i < p.n_nodes && pick < 0; i++) if (!done[static_cast<size_t>(i)] && p.node[i].area == area) pick = i;
        done[static_cast<size_t>(pick)] = true;
        order.push_back(pick);
    }
    return order;
}

/* ---------------- the SynthLab synth ---------------- */

// One area as a struct's members and its block's code.
struct AreaCode { std::string members, start, body; bool any = false; };

AreaCode area_code(const mod::Patch &p, int area, const int *base) {
    AreaCode a;
    const std::vector<int> order = area_order(p, area);
    a.any = !order.empty();
    for (int i : order) {
        const mod::Node &n = p.node[i];
        const mod::ModuleDef &d = *n.def;
        a.members += fmt("    mod::%s m%d;%*s// %s %d\n", d.cpp, n.id, static_cast<int>(10 - std::strlen(d.cpp)), "", d.name, n.id);
        for (int k = 0; k < d.n_out; k++) a.members += fmt("    float o%d_%d[mod::kMaxFrames] = {};\n", n.id, k);
        a.start += fmt("        m%d.start(c);\n", n.id);
    }
    for (int i : order) {
        const mod::Node &n = p.node[i];
        const mod::ModuleDef &d = *n.def;
        std::string code = fmt("            {   // %s %d\n", d.name, n.id);
        // its values: the synth's list (p[]), or the patch's when the list has no room for them
        code += "                float v[mod::kMaxParams] = {";
        for (int j = 0; j < d.n_param; j++)
            code += (j ? ", " : "") + (base[i] >= 0 ? fmt("p[%d]", base[i] + 1 + j) : lit(n.v[j]));
        code += "};\n";
        // the modulations
        for (int k = 0; k < p.n_cables; k++) {
            const mod::Cable &cb = p.cable[k];
            if (cb.dst != n.id || !cb.to_param) continue;
            const mod::Node *s = p.find(cb.src);
            const mod::Param &q = d.param[cb.dst_port];
            code += fmt("                v[%d] = modulate(%s, %s, %d, v[%d], mean(o%d_%d, n) * %s);   // %s %d.%s\n", cb.dst_port, lit(q.min).c_str(), lit(q.max).c_str(),
                        static_cast<int>(q.curve), cb.dst_port, cb.src, cb.src_port, lit(cb.depth).c_str(), s->def->name, s->id, s->def->out_name[cb.src_port]);
        }
        // the inputs
        std::string ins[mod::kMaxIn];
        for (int k = 0; k < d.n_in; k++) {
            std::vector<const mod::Cable *> cs;
            for (int q = 0; q < p.n_cables; q++) if (p.cable[q].dst == n.id && !p.cable[q].to_param && p.cable[q].dst_port == k) cs.push_back(&p.cable[q]);
            if (cs.empty()) {
                switch (d.in_def[k]) {
                case mod::kOne: ins[k] = "one"; break;
                case mod::kGate: ins[k] = "gate"; break;
                case mod::kTrig: ins[k] = "trg"; break;
                case mod::kIn0: ins[k] = ins[0]; break;
                default: ins[k] = "zero"; break;
                }
            } else if (cs.size() == 1 && cs[0]->depth == 1.f) {
                ins[k] = fmt("o%d_%d", cs[0]->src, cs[0]->src_port);
            } else {
                a.members += fmt("    float s%d_%d[mod::kMaxFrames];\n", n.id, k);
                code += fmt("                for (int i = 0; i < n; i++) s%d_%d[i] =", n.id, k);
                for (size_t q = 0; q < cs.size(); q++) code += fmt("%s o%d_%d[i] * %s", q ? " +" : "", cs[q]->src, cs[q]->src_port, lit(cs[q]->depth).c_str());
                code += ";\n";
                ins[k] = fmt("s%d_%d", n.id, k);
            }
        }
        if (!std::strcmp(d.name, "Voices")) code += fmt("                for (int i = 0; i < n; i++) o%d_0[i] = io[off + i];   // the voices' sum\n", n.id);
        code += "                const mod::IO io_ = {{";
        for (int k = 0; k < mod::kMaxIn; k++) code += (k ? ", " : "") + (k < d.n_in ? ins[k] : std::string("zero"));
        code += "}, {";
        for (int k = 0; k < mod::kMaxOut; k++) code += (k ? ", " : "") + (k < d.n_out ? fmt("o%d_%d", n.id, k) : std::string("nullptr"));
        code += fmt("}, v, n};\n                m%d.process(c, io_);\n            }\n", n.id);
        a.body += code;
    }
    return a;
}

}  // namespace

std::string synth_source(const mod::Patch &p, const std::string &id) {
    int base[mod::kMaxNodes];
    mod::patch_slots(p, base);
    std::string o;
    o += fmt("// %s: generated by SynthLab Studio from its patch on %s (apps/studio, the PATCH tab's Export > Synth). The patch's modules\n", p.name, today().c_str());
    o += "// (framework/modular/modules.h) called directly in the order the studio plays them, a block of up to mod::kMaxFrames at a time; a\n";
    o += "// modulation cable moves its value by its source's mean over the block (the runner's rule: framework/modular/patch.cpp). Its values\n";
    o += "// are a section per module, as the studio's Modular synth lists them, their defaults the patch's values.\n";
    o += "// Exporting again overwrites this file: copy it under another name before changing it by hand (then optimise it as the Kick).\n";
    o += "#include \"lab.h\"\n#include \"modular/modules.h\"\n\nnamespace {\n\n";
    // the labels, the list
    for (int i = 0; i < p.n_nodes; i++) {
        const mod::ModuleDef &d = *p.node[i].def;
        for (int j = 0; j < d.n_param; j++) {
            const mod::Param &q = d.param[j];
            if (!q.labels) continue;
            o += fmt("const char *const kL%d_%d[] = {", p.node[i].id, j);
            for (int k = 0; k <= static_cast<int>(q.max - q.min); k++) o += (k ? ", " : "") + cstr(q.labels[k]);
            o += "};\n";
        }
    }
    o += "\nconst lab::Param kParams[] = {\n";
    static const char *const curves[] = {"lab::Lin", "lab::Exp", "lab::Int"};
    for (int i = 0; i < p.n_nodes; i++) {
        if (base[i] < 0) continue;
        const mod::Node &n = p.node[i];
        o += fmt("    lab::section(\"%s %d\"),\n", n.def->name, n.id);
        for (int j = 0; j < n.def->n_param; j++) {
            const mod::Param &q = n.def->param[j];
            o += fmt("    {%s, %s, %s, %s, %s, %s, %s},\n", cstr(q.name).c_str(), lit(q.min).c_str(), lit(q.max).c_str(), lit(n.v[j]).c_str(),
                     q.unit ? cstr(q.unit).c_str() : "nullptr", curves[q.curve], q.labels ? fmt("kL%d_%d", n.id, j).c_str() : "nullptr");
        }
    }
    o += "};\n\n";
    o += "// A value moved along its range (lo..hi, its curve) by a modulation: the runner's rule\n";
    o += "[[maybe_unused]] float modulate(float lo, float hi, int curve, float v, float amount) {\n";
    o += "    const mod::Param q{\"\", lo, hi, lo, nullptr, static_cast<mod::Curve>(curve)};\n";
    o += "    return mod::from_norm(q, mod::to_norm(q, v) + amount);\n}\n";
    o += "[[maybe_unused]] float mean(const float *b, int n) {\n    float s = 0.f;\n    for (int i = 0; i < n; i++) s += b[i];\n    return s / static_cast<float>(n);\n}\n\n";

    const AreaCode voice = area_code(p, mod::kVoiceArea, base);
    const AreaCode fx = area_code(p, mod::kFxArea, base);
    if (fx.any) o += "mod::LastNote g_last;   // the FX's note: the last note played (the voices write it, the FX reads it)\n\n";
    // the voice
    o += "struct Voice {\n    mod::Ctx c;\n    bool trig = false;\n    int quiet = 0;\n";
    o += "    float zero[mod::kMaxFrames] = {}, one[mod::kMaxFrames] = {}, gate[mod::kMaxFrames] = {}, trg[mod::kMaxFrames] = {};\n";
    o += voice.members;
    o += "\n    Voice() { for (float &x : one) x = 1.f; }\n";
    if (fx.any) o += "    ~Voice() { g_last.voice_gone(this); }\n";
    o += "    void ctx(const lab::Note &note) { c.sr = note.sr; c.inv_sr = 1.f / note.sr; c.hz = note.hz; c.velocity = note.velocity; c.gate = note.gate; }\n";
    o += "    void start(const lab::Note &note, const float *) {\n        ctx(note);\n" + voice.start + "        trig = true;\n        quiet = 0;\n" +
         (fx.any ? "        g_last.note_start(this, note.hz, note.velocity);\n" : "") + "    }\n";
    o += "    void release(const lab::Note &, const float *) {}\n";
    o += "    bool render(float *out, int frames, const lab::Note &note, const float *p) {\n        ctx(note);\n";
    if (fx.any) o += "        g_last.voice_block(this, c.gate, c.hz);\n";
    o += "        float peak = 0.f;\n";
    o += "        for (int off = 0; off < frames; off += mod::kMaxFrames) {\n";
    o += "            const int n = frames - off < mod::kMaxFrames ? frames - off : mod::kMaxFrames;\n";
    o += "            for (int i = 0; i < n; i++) { gate[i] = c.gate ? 1.f : 0.f; trg[i] = 0.f; }\n";
    o += "            if (trig) { trg[0] = 1.f; trig = false; }\n";
    o += voice.body;
    o += "            for (int i = 0; i < n; i++) {   // the VoiceOut modules: the voice's sound\n                float y = 0.f;\n";
    for (int i = 0; i < p.n_nodes; i++)
        if (p.node[i].area == mod::kVoiceArea && !std::strcmp(p.node[i].def->name, "VoiceOut")) o += fmt("                y += o%d_0[i];\n", p.node[i].id);
    o += "                if (y != y) y = 0.f;\n                out[off + i] += y;\n                peak = y > peak ? y : (-y > peak ? -y : peak);\n            }\n        }\n";
    o += "        if (note.gate || peak > 1e-4f) { quiet = 0; return true; }\n        return ++quiet < 16;\n    }\n};\n\n";
    // the FX
    if (fx.any) {
        o += "struct Fx {\n    mod::Ctx c;\n    uint32_t seen = 0;\n    bool trig = false;\n";
        o += "    float zero[mod::kMaxFrames] = {}, one[mod::kMaxFrames] = {}, gate[mod::kMaxFrames] = {}, trg[mod::kMaxFrames] = {};\n";
        o += fx.members;
        o += "\n    Fx() { for (float &x : one) x = 1.f; }\n";
        o += "    void render(float *io, int frames, float sr, const float *p) {\n        c.sr = sr;\n        c.inv_sr = 1.f / sr;\n";
        o += "        if (g_last.fx_block(c, seen)) {   // a new note: the FX's modules start on it\n" + fx.start + "            trig = true;\n        }\n";
        o += "        for (int off = 0; off < frames; off += mod::kMaxFrames) {\n";
        o += "            const int n = frames - off < mod::kMaxFrames ? frames - off : mod::kMaxFrames;\n";
        o += "            for (int i = 0; i < n; i++) { gate[i] = c.gate ? 1.f : 0.f; trg[i] = 0.f; }\n";
        o += "            if (trig) { trg[0] = 1.f; trig = false; }\n";
        o += fx.body;
        o += "            for (int i = 0; i < n; i++) {   // the Output modules\n                float y = 0.f;\n";
        for (int i = 0; i < p.n_nodes; i++)
            if (p.node[i].area == mod::kFxArea && !std::strcmp(p.node[i].def->name, "Output")) o += fmt("                y += o%d_0[i];\n", p.node[i].id);
        o += "                io[off + i] = y == y ? y : 0.f;\n            }\n        }\n    }\n};\n\n";
    }
    o += "}  // namespace\n\n";
    o += fmt("extern const lab::Synth synth_gen_%s = lab::make_synth<Voice%s>(%s, kParams);\n", id.c_str(), fx.any ? ", Fx" : "", cstr(p.name).c_str());
    return o;
}

/* ---------------- the engine's graph ---------------- */

namespace {

struct EngSet { const char *param; long value; std::string note; };
struct EngNode {
    const char *type = nullptr;                     // the engine module (nullptr: none yet)
    std::vector<EngSet> sets;
    const char *in[mod::kMaxIn] = {}, *in2[mod::kMaxIn] = {};       // our input -> the engine's input(s) (in2: a stereo module's R)
    const char *in_mod[mod::kMaxIn] = {};           // our input -> a modulation of this engine value (an engine module has fewer inputs)
    const char *out[mod::kMaxOut] = {};
    const char *prm[mod::kMaxParams] = {};          // our value j's modulations -> this engine value
    bool note_pitch = false, note_gate = false;     // it needs NoteIn's pitch / gate (when its port takes nothing)
    int pitch_port = -1, gate_port = 0;             // our input that takes it (-1: none, the module follows the key by itself)
    std::string note;
    std::string reason;                             // why it is left out though the engine has the type (its scope)
};

long q15(float pct) { const float v = pct * 327.67f; return std::lround(v > 32767.f ? 32767.f : (v < -32768.f ? -32768.f : v)); }
long pitch_of_hz(float hz) { return std::lround((69.f + 12.f * std::log2(hz / 440.f)) * 256.f); }

EngNode engine_node(const mod::Node &n) {
    EngNode e;
    if (mod::is_engine_module(n.def)) {             // a Ritual Machine module: itself, its values back in the engine's units
        const mod::ModuleDef &d = *n.def;
        e.type = d.engine;
        for (int j = 0; j < d.n_param; j++)
            e.sets.push_back({mod::engine_param_name(&d, j), static_cast<long>(mod::engine_raw_value(&d, j, n.v[j])),
                              fmt("%s %.4g%s%s", d.param[j].name, static_cast<double>(n.v[j]), d.param[j].unit ? " " : "", d.param[j].unit ? d.param[j].unit : "")});
        for (int k = 0; k < d.n_in; k++) {
            e.in[k] = d.in_name[k];
            if (!std::strcmp(d.in_name[k], "pitch")) { e.note_pitch = n.area == mod::kVoiceArea; e.pitch_port = k; }
            if (!std::strcmp(d.in_name[k], "gate")) { e.note_gate = n.area == mod::kVoiceArea; e.gate_port = k; }
        }
        for (int o = 0; o < d.n_out; o++) e.out[o] = d.out_name[o];
        for (int j = 0; j < d.n_param; j++) e.prm[j] = d.param[j].name;
        return e;
    }
    const float *v = n.v;
    const std::string name = n.def->name;
    using namespace mod::P;
    if (name == "Note") {
        e.type = "NoteIn";
        e.out[0] = "gate"; e.out[2] = "vel"; e.out[3] = "pitch";
        e.note = "Trig has no engine output: the engine's Eg starts on the gate";
    } else if (name == "VoiceOut") {
        e.type = "VoiceOut"; e.in[0] = "in";
        e.sets.push_back({"level", q15(v[0]), fmt("Level %g %%", static_cast<double>(v[0]))});
    } else if (name == "Voices") {
        e.type = "BusIn"; e.out[0] = "L";
        e.note = "mono: only L is used";
    } else if (name == "Output") {
        e.type = "MasterOut"; e.in[0] = "L"; e.in2[0] = "R";
        e.sets.push_back({"level", q15(v[0]), fmt("Level %g %%", static_cast<double>(v[0]))});
    } else if (name == "Osc") {
        static const long waves[4] = {0, 3, 1, 2};      // Sine, Tri, Saw, Square -> WAVE_SINE_, WAVE_TRI_, WAVE_SAW_, WAVE_PULSE_
        e.type = "Osc";
        e.sets.push_back({"wave", waves[static_cast<int>(v[OSC_WAVE] + 0.5f) & 3], fmt("Wave %s", n.def->param[OSC_WAVE].labels[static_cast<int>(v[OSC_WAVE] + 0.5f) & 3])});
        e.sets.push_back({"pitch", std::lround((60.f + v[OSC_TUNE] + v[OSC_FINE] * 0.01f) * 256.f), fmt("Tune %g st, Fine %g ct (middle C = 60 x 256)", static_cast<double>(v[OSC_TUNE]), static_cast<double>(v[OSC_FINE]))});
        e.sets.push_back({"pw", q15(v[OSC_PW]), fmt("PW %g %%", static_cast<double>(v[OSC_PW]))});
        e.sets.push_back({"level", q15(v[OSC_LEVEL]), fmt("Level %g %%", static_cast<double>(v[OSC_LEVEL]))});
        e.sets.push_back({"pitch_mod", std::lround(v[OSC_PMOD] * 256.f), fmt("PMod %g st: what full scale on the pitch does", static_cast<double>(v[OSC_PMOD]))});
        e.note_pitch = v[OSC_TRACK] >= 0.5f;
        e.in_mod[0] = "pitch";
        e.prm[OSC_TUNE] = "pitch"; e.prm[OSC_PW] = "pw"; e.prm[OSC_LEVEL] = "level";
        e.out[0] = "out";
        if (v[OSC_FM] > 0.f) e.note = "FM (linear) has no engine equivalent: its cables are left out";
    } else if (name == "Noise") {
        e.type = "Osc"; e.out[0] = "out";
        e.sets.push_back({"wave", 4, "WAVE_NOISE_"});
        e.sets.push_back({"level", q15(v[NOISE_LEVEL]), fmt("Level %g %%", static_cast<double>(v[NOISE_LEVEL]))});
        e.prm[NOISE_LEVEL] = "level";
        e.note = fmt("the engine's noise has no Tone (%g Hz) and no Seed: add a Filter, or a new module", static_cast<double>(v[NOISE_TONE]));
    } else if (name == "ADSR") {
        e.type = "Eg"; e.in[0] = "gate"; e.out[0] = "out";
        e.note_gate = true;
        e.sets.push_back({"t1", std::lround(v[ADSR_A]), fmt("Attack %g ms", static_cast<double>(v[ADSR_A]))});
        e.sets.push_back({"l1", 32767, "to full"});
        e.sets.push_back({"t2", std::lround(v[ADSR_D]), fmt("Decay %g ms", static_cast<double>(v[ADSR_D]))});
        e.sets.push_back({"l2", q15(v[ADSR_S]), fmt("Sustain %g %%", static_cast<double>(v[ADSR_S]))});
        e.sets.push_back({"sustain", 2, "holds at stage 2"});
        e.sets.push_back({"release", std::lround(v[ADSR_R]), fmt("Release %g ms", static_cast<double>(v[ADSR_R]))});
        e.note = "the engine's curves (c1, c2, r_curve) are left at their defaults: compare the shapes";
    } else if (name == "Perc") {
        e.type = "Eg"; e.in[0] = "gate"; e.out[0] = "out";
        e.note_gate = true;
        e.sets.push_back({"t1", std::lround(v[PERC_ATTACK]), fmt("Attack %g ms", static_cast<double>(v[PERC_ATTACK]))});
        e.sets.push_back({"l1", q15(v[PERC_AMOUNT]), fmt("Amount %g %%", static_cast<double>(v[PERC_AMOUNT]))});
        e.sets.push_back({"t2", std::lround(v[PERC_HOLD]), fmt("Hold %g ms", static_cast<double>(v[PERC_HOLD]))});
        e.sets.push_back({"l2", q15(v[PERC_AMOUNT]), "held"});
        e.sets.push_back({"t3", std::lround(v[PERC_DECAY]), fmt("Decay %g ms", static_cast<double>(v[PERC_DECAY]))});
        e.sets.push_back({"l3", 0, "to silence"});
        e.sets.push_back({"c3", q15(v[PERC_CURVE]), fmt("Curve %g %%: check the sign of the engine's curve", static_cast<double>(v[PERC_CURVE]))});
        e.sets.push_back({"oneshot", 1, "plays out in full"});
        e.note = "started by the gate, not by a Trig";
    } else if (name == "LFO") {
        static const long shapes[6] = {0, 1, 3, 2, 4, 5};   // Sine, Tri, SawUp, SawDown, Square, S&H -> LFOS_*
        e.type = "Lfo"; e.out[0] = "out";
        e.sets.push_back({"shape", shapes[static_cast<int>(v[LFO_WAVE] + 0.5f) % 6], fmt("Wave %s", n.def->param[LFO_WAVE].labels[static_cast<int>(v[LFO_WAVE] + 0.5f) % 6])});
        e.sets.push_back({"rate", std::lround(12.f * std::log2(v[LFO_RATE]) * 256.f), fmt("Rate %g Hz (12 x 256 a doubling, 0 = 1 Hz)", static_cast<double>(v[LFO_RATE]))});
        e.sets.push_back({"level", q15(v[LFO_LEVEL]), fmt("Level %g %%", static_cast<double>(v[LFO_LEVEL]))});
        e.sets.push_back({"unipolar", v[LFO_UNIPOLAR] >= 0.5f ? 1 : 0, ""});
        e.prm[LFO_RATE] = "rate"; e.prm[LFO_LEVEL] = "level";
        if (v[LFO_RETRIG] >= 0.5f) e.note = "Retrig has no engine equivalent";
    } else if (name == "Filter") {
        static const long modes[6] = {0, 0, 1, 2, 2, 3}, sections[6] = {1, 2, 1, 1, 2, 1};
        const int ty = static_cast<int>(v[FLT_TYPE] + 0.5f) % 6;
        e.type = "Filter"; e.in[0] = "in"; e.in_mod[1] = "cutoff"; e.out[0] = "out";
        e.sets.push_back({"mode", modes[ty], fmt("Type %s", n.def->param[FLT_TYPE].labels[ty])});
        e.sets.push_back({"sections", sections[ty], ""});
        e.sets.push_back({"cutoff", pitch_of_hz(v[FLT_CUTOFF]), fmt("Cutoff %g Hz (as a MIDI note x 256)", static_cast<double>(v[FLT_CUTOFF]))});
        e.sets.push_back({"res", q15(v[FLT_RESO]), fmt("Reso %g %%", static_cast<double>(v[FLT_RESO]))});
        e.sets.push_back({"cut_mod", std::lround(std::fmin(v[FLT_CMOD] * 12.f, 120.f) * 256.f), fmt("CMod %g oct: what full scale on the cutoff does", static_cast<double>(v[FLT_CMOD]))});
        e.prm[FLT_CUTOFF] = "cutoff"; e.prm[FLT_RESO] = "res";
        if (v[FLT_TRACK] > 0.f) e.note = fmt("Track %g %%: add a modulation from NoteIn's pitch onto cutoff", static_cast<double>(v[FLT_TRACK]));
    } else if (name == "Drive") {
        const float g = std::pow(10.f, v[DRV_DRIVE] / 20.f);
        e.type = "Shaper"; e.in[0] = "in"; e.out[0] = "out";
        e.sets.push_back({"mode", v[DRV_TUBE] > v[DRV_SAT] ? 4 : 0, v[DRV_TUBE] > v[DRV_SAT] ? "SHPM_TUBE (Tube leads)" : "SHPM_TANH (Sat leads)"});
        e.sets.push_back({"drive", std::lround(std::fmin(g, 4.f) * 256.f), fmt("Drive %g dB", static_cast<double>(v[DRV_DRIVE]))});
        e.prm[DRV_DRIVE] = "drive";
        e.note = "one shaper only: the engine's Shaper has one mode; Fold / Clip / Out are not carried over";
    } else if (name == "VCA") {
        e.type = "Vca"; e.in[0] = "in"; e.in_mod[1] = "level"; e.out[0] = "out";
        e.sets.push_back({"level", q15(v[0]), fmt("Level %g %% (0 when a CV comes in: the CV's modulation sets it)", static_cast<double>(v[0]))});
        e.prm[0] = "level";
    } else if (name == "Mixer") {
        e.type = "Mix4"; e.out[0] = "out";
        static const char *const ins[4] = {"1", "2", "3", "4"}, *const lv[4] = {"lvl1", "lvl2", "lvl3", "lvl4"};
        for (int k = 0; k < 4; k++) { e.in[k] = ins[k]; e.prm[k] = lv[k]; e.sets.push_back({lv[k], q15(v[k]), fmt("Lvl%d %g %%", k + 1, static_cast<double>(v[k]))}); }
    } else if (name == "Ring") {
        e.type = "Mult"; e.in[0] = "a"; e.in[1] = "b"; e.out[0] = "out";
    } else if (name == "Const") {
        e.type = "Const"; e.out[0] = "out";
        e.sets.push_back({"value", std::lround(v[0] * 32767.f), fmt("Value %g", static_cast<double>(v[0]))});
    } else if (name == "Delay") {
        e.type = "Delay"; e.in[0] = "L"; e.in2[0] = "R"; e.out[0] = "L";
        e.sets.push_back({"time", std::lround(v[DLY_TIME] * 16.f), fmt("Time %g ms (x 16)", static_cast<double>(v[DLY_TIME]))});
        e.sets.push_back({"feedback", std::lround(v[DLY_FEEDBACK] / 95.f * 31000.f), fmt("Feedback %g %%", static_cast<double>(v[DLY_FEEDBACK]))});
        e.sets.push_back({"damp", pitch_of_hz(v[DLY_DAMP]), fmt("Damp %g Hz", static_cast<double>(v[DLY_DAMP]))});
        e.sets.push_back({"mix", q15(v[DLY_MIX]), fmt("Mix %g %%", static_cast<double>(v[DLY_MIX]))});
    } else if (name == "Room") {
        e.type = "Reverb"; e.in[0] = "L"; e.in2[0] = "R"; e.out[0] = "L";
        e.sets.push_back({"mix", q15(v[ROOM_MIX]), fmt("Mix %g %%", static_cast<double>(v[ROOM_MIX]))});
        e.sets.push_back({"decay", std::lround(std::fmin(32400.f, 32400.f * std::log10(v[ROOM_DECAY] / 100.f) / 2.f)), fmt("Decay %g ms: an approximation, tune by ear", static_cast<double>(v[ROOM_DECAY]))});
        e.sets.push_back({"size", q15(v[ROOM_SIZE]), fmt("Size %g %%", static_cast<double>(v[ROOM_SIZE]))});
        e.sets.push_back({"damp", std::lround((135.f - 1.05f * v[ROOM_DAMP]) * 256.f), fmt("Damp %g %%: an approximation", static_cast<double>(v[ROOM_DAMP]))});
        e.note = "a different reverb (the engine's is lusher): the sound will change";
    }
    return e;
}

}  // namespace

std::string engine_source(const mod::Patch &p, const std::string &id) {
    std::string o;
    o += fmt("// %s: the Ritual Machine engine's graph of a SynthLab Studio patch, generated on %s (the PATCH tab's Export > Engine).\n", p.name, today().c_str());
    o += "// The modules the engine has are added with their values converted to the engine's units (each one's original value in a\n";
    o += "// comment); the cables follow (a cable into an input the engine module does not have becomes a modulation of the value it\n";
    o += "// drives). The patch's modules the engine has no module for are listed at the end: the modules to write (framework/engine/modules),\n";
    o += "// with their values and cables. Read the notes: a few conversions are approximations.\n";
    o += "// The registry must hold the synth and FX modules (register_synth_modules, register_fx_modules: the Ritual Machine's\n";
    o += "// engine_synth.cpp does it); a module type it lacks makes build_*_graph return false.\n";
    o += "#include <cstring>\n#include \"engine/core/graph.h\"\n\nnamespace {\n\n";
    o += "// The type of that name, in that scope if it has one there (Osc, Filter, Lfo ... are registered in both)\n";
    o += "int type_of(const sc::Registry &reg, const char *name, sc::Scope scope) {\n    int any = -1;\n";
    o += "    for (int t = 0; t < sc::Registry::kMax; t++) {\n        if (!reg.get(t) || std::strcmp(reg.get(t)->info->name, name)) continue;\n";
    o += "        if (reg.get(t)->info->scope == scope) return t;\n        if (any < 0) any = t;\n    }\n    return any;\n}\n";
    o += "sc::NodeDesc *add(sc::GraphDesc &g, const sc::Registry &reg, int id, const char *type, sc::Scope scope) {\n    const int t = type_of(reg, type, scope);\n    return t < 0 ? nullptr : g.add_node(reg, id, t);\n}\n";
    o += "const sc::ModuleInfo &info(const sc::GraphDesc &g, const sc::Registry &reg, int id) {\n    for (int i = 0; i < g.n_nodes; i++) if (g.node[i].id == id) return *reg.get(g.node[i].type)->info;\n    return *reg.get(g.node[0].type)->info;\n}\n";
    o += "int find(const char *const *names, int n, const char *name) {\n    for (int i = 0; i < n; i++) if (names[i] && !std::strcmp(names[i], name)) return i;\n    return -1;\n}\n";
    o += "bool set(const sc::Registry &reg, sc::NodeDesc *n, const char *name, int32_t v) {\n    const sc::ModuleInfo &i = *reg.get(n->type)->info;\n";
    o += "    for (int k = 0; k < i.n_param; k++) if (!std::strcmp(i.param[k].name, name)) { n->param[k] = v < i.param[k].min ? i.param[k].min : (v > i.param[k].max ? i.param[k].max : v); return true; }\n    return false;\n}\n";
    o += "bool cable(sc::GraphDesc &g, const sc::Registry &reg, int src, const char *out, int dst, const char *in, float depth) {\n";
    o += "    const int so = find(info(g, reg, src).out_name, info(g, reg, src).n_out, out), di = find(info(g, reg, dst).in_name, info(g, reg, dst).n_in, in);\n";
    o += "    return so >= 0 && di >= 0 && g.connect(src, so, dst, sc::Dst::In, di, static_cast<sc::q15>(depth * 32767.f));\n}\n";
    o += "bool modulate(sc::GraphDesc &g, const sc::Registry &reg, int src, const char *out, int dst, const char *param, float depth) {\n";
    o += "    const sc::ModuleInfo &d = info(g, reg, dst);\n    int pi = -1;\n    for (int k = 0; k < d.n_param; k++) if (!std::strcmp(d.param[k].name, param)) pi = k;\n";
    o += "    const int so = find(info(g, reg, src).out_name, info(g, reg, src).n_out, out);\n";
    o += "    return so >= 0 && pi >= 0 && g.connect(src, so, dst, sc::Dst::Param, pi, static_cast<sc::q15>(depth * 32767.f));\n}\n\n}  // namespace\n\n";

    // the nodes
    std::vector<EngNode> en(static_cast<size_t>(p.n_nodes));
    for (int i = 0; i < p.n_nodes; i++) {
        const mod::Node &n = p.node[i];
        EngNode &e = en[static_cast<size_t>(i)];
        e = engine_node(n);
        if (e.type && !mod::engine_type_in(e.type, n.area)) {       // (the studio plays it in either area; the engine has it in one scope)
            e.reason = fmt("the engine's %s is %s: a %s %s to write", e.type, n.area == mod::kVoiceArea ? "global only" : "per voice only",
                           n.area == mod::kVoiceArea ? "per-voice" : "global", e.type);
            e.type = nullptr;
        }
        if (n.area == mod::kFxArea && (e.note_pitch || e.note_gate)) {   // the engine's global scope has no note: the studio's last note
            e.note_pitch = e.note_gate = false;
            e.note += std::string(e.note.empty() ? "" : "; ") + "it follows the last note in the studio: in the engine's global scope, give it a cable";
        }
    }
    int note_id = -1;
    for (int i = 0; i < p.n_nodes; i++) if (!std::strcmp(p.node[i].def->name, "Note") && p.node[i].area == mod::kVoiceArea) note_id = p.node[i].id;
    bool need_note = false;
    for (int i = 0; i < p.n_nodes; i++) if (en[static_cast<size_t>(i)].type && (en[static_cast<size_t>(i)].note_pitch || en[static_cast<size_t>(i)].note_gate)) need_note = true;

    o += fmt("// Builds the patch \"%s\" into g (the voice's modules and the global ones together: the engine places them by their scope).\n", p.name);
    o += "// False when the graph is full or the engine lacks one of the module types named here.\n";
    o += fmt("bool build_%s_graph(sc::GraphDesc &g, const sc::Registry &reg) {\n    sc::NodeDesc *n = nullptr;\n    bool ok = true;\n", id.c_str());
    if (need_note && note_id < 0) { note_id = 254; o += "    if (!add(g, reg, 254, \"NoteIn\", sc::Scope::Voice)) return false;   // (the patch has no Note: its oscillators and envelopes follow the key)\n"; }
    for (int i = 0; i < p.n_nodes; i++) {
        const mod::Node &n = p.node[i];
        const EngNode &e = en[static_cast<size_t>(i)];
        if (!e.type) continue;
        o += fmt("\n    // %s %d -> %s%s%s\n", n.def->name, n.id, e.type, e.note.empty() ? "" : ". ", e.note.c_str());
        o += fmt("    if (!(n = add(g, reg, %d, \"%s\", %s))) return false;\n", n.id, e.type, n.area == mod::kFxArea ? "sc::Scope::Global" : "sc::Scope::Voice");
        bool cv_in = false;                             // (a VCA driven by a CV: its level comes from the CV's modulation)
        for (int k = 0; k < p.n_cables; k++) if (p.cable[k].dst == n.id && !p.cable[k].to_param && p.cable[k].dst_port == 1) cv_in = true;
        for (const EngSet &s : e.sets) {
            const long val = (!std::strcmp(n.def->name, "VCA") && cv_in) ? 0 : s.value;
            o += fmt("    ok &= set(reg, n, \"%s\", %ld);%s%s\n", s.param, val, s.note.empty() ? "" : "   // ", s.note.c_str());
        }
    }
    o += "\n    // the cables\n";
    std::string missing;
    for (int k = 0; k < p.n_cables; k++) {
        const mod::Cable &cb = p.cable[k];
        const int si = p.index_of(cb.src), di = p.index_of(cb.dst);
        if (si < 0 || di < 0) continue;
        const mod::Node &s = p.node[si], &d = p.node[di];
        const EngNode &es = en[static_cast<size_t>(si)], &ed = en[static_cast<size_t>(di)];
        const std::string what = fmt("%s %d.%s -> %s %d.%s", s.def->name, s.id, s.def->out_name[cb.src_port], d.def->name, d.id,
                                     cb.to_param ? d.def->param[cb.dst_port].name : d.def->in_name[cb.dst_port]);
        const char *out = es.type ? es.out[cb.src_port] : nullptr;
        if (!es.type || !ed.type) { missing += "//   " + what + (cb.to_param ? " (modulation)" : "") + "\n"; continue; }
        if (!out) { o += fmt("    // %s: the engine's %s has no such output (left out)\n", what.c_str(), es.type); continue; }
        if (cb.to_param) {
            if (ed.prm[cb.dst_port]) o += fmt("    ok &= modulate(g, reg, %d, \"%s\", %d, \"%s\", %s);   // %s\n", s.id, out, d.id, ed.prm[cb.dst_port], lit(cb.depth).c_str(), what.c_str());
            else o += fmt("    // %s: the engine's %s cannot modulate it (left out)\n", what.c_str(), ed.type);
        } else if (ed.in[cb.dst_port]) {
            o += fmt("    ok &= cable(g, reg, %d, \"%s\", %d, \"%s\", %s);   // %s\n", s.id, out, d.id, ed.in[cb.dst_port], lit(cb.depth).c_str(), what.c_str());
            if (ed.in2[cb.dst_port]) o += fmt("    ok &= cable(g, reg, %d, \"%s\", %d, \"%s\", %s);   //   (and the right channel)\n", s.id, out, d.id, ed.in2[cb.dst_port], lit(cb.depth).c_str());
        } else if (ed.in_mod[cb.dst_port]) {
            o += fmt("    ok &= modulate(g, reg, %d, \"%s\", %d, \"%s\", %s);   // %s (an input the engine's %s does not have: a modulation)\n", s.id, out, d.id,
                     ed.in_mod[cb.dst_port], lit(cb.depth).c_str(), what.c_str(), ed.type);
        } else {
            o += fmt("    // %s: the engine's %s has no such input (left out)\n", what.c_str(), ed.type);
        }
    }
    for (int i = 0; i < p.n_nodes; i++) {                   // the key's pitch and gate, where our modules take them implicitly
        const EngNode &e = en[static_cast<size_t>(i)];
        if (!e.type) continue;
        auto taken = [&](int port) {
            for (int k = 0; k < p.n_cables; k++) if (p.cable[k].dst == p.node[i].id && !p.cable[k].to_param && p.cable[k].dst_port == port) return true;
            return false;
        };
        if (e.note_pitch && (e.pitch_port < 0 || !taken(e.pitch_port))) o += fmt("    ok &= cable(g, reg, %d, \"pitch\", %d, \"pitch\", 1.f);   // %s %d follows the key (Track)\n", note_id, p.node[i].id, p.node[i].def->name, p.node[i].id);
        if (e.note_gate && !taken(e.gate_port)) o += fmt("    ok &= cable(g, reg, %d, \"gate\", %d, \"gate\", 1.f);   // %s %d on the voice's gate\n", note_id, p.node[i].id, p.node[i].def->name, p.node[i].id);
    }
    o += "    return ok;\n}\n";

    // what the engine lacks
    std::string lack;
    for (int i = 0; i < p.n_nodes; i++) {
        const mod::Node &n = p.node[i];
        const EngNode &e = en[static_cast<size_t>(i)];
        if (e.type) continue;
        if (!e.reason.empty()) lack += fmt("//   %s %d (%s): %s\n//     values:", n.def->name, n.id, n.area == mod::kVoiceArea ? "per voice" : "global", e.reason.c_str());
        else lack += fmt("//   %s %d (%s, from %s; framework/modular/modules.h %s): %s\n//     values:", n.def->name, n.id, n.area == mod::kVoiceArea ? "per voice" : "global",
                         n.def->origin, n.def->cpp ? n.def->cpp : "-", n.def->doc);
        for (int j = 0; j < n.def->n_param; j++) lack += fmt(" %s %g%s%s", n.def->param[j].name, static_cast<double>(n.v[j]), n.def->param[j].unit ? " " : "", n.def->param[j].unit ? n.def->param[j].unit : "");
        lack += "\n";
    }
    if (!lack.empty()) {
        o += "\n// NEW MODULES NEEDED: the patch's modules the engine has nothing like yet, or not in that scope (left out of the graph above):\n" + lack;
        o += "// and their cables (left out too):\n" + missing;
    } else {
        o += "\n// Every module of the patch has an engine module.\n";
    }
    return o;
}

bool export_synth(const mod::Patch &p, const std::string &id, std::string &path, std::string &err) {
    for (int i = 0; i < p.n_nodes; i++)
        if (mod::is_engine_module(p.node[i].def)) {
            err = std::string(p.node[i].def->name) + " is the Ritual Machine's own module: a SynthLab synth cannot hold it (Export > Engine can)";
            return false;
        }
    const std::string dir = st.preset_dir + "../../synthlab/src/synths/";
    path = dir + "gen_" + id + ".cpp";
    if (!write_text(path, synth_source(p, id), err)) return false;
    // its line in SynthLab's list, once
    const std::string list = dir + "synth_list.cpp";
    FILE *f = std::fopen(list.c_str(), "rb");
    if (!f) { err = "cannot read " + list; return false; }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    const std::string sym = "synth_gen_" + id;
    if (text.find(sym + ";") == std::string::npos) {
        const bool crlf = text.find("\r\n") != std::string::npos;
        const std::string nl = crlf ? "\r\n" : "\n";
        const std::string m1 = "// (synths exported from a patch by SynthLab Studio are added above this line)";
        const std::string m2 = "    // (exported synths: above this line)";
        const size_t a = text.find(m1);
        if (a == std::string::npos || text.find(m2) == std::string::npos) { err = "synth_list.cpp has no place for exported synths"; return false; }
        text.insert(a, "extern const lab::Synth " + sym + ";" + nl);
        text.insert(text.find(m2), "    &" + sym + "," + nl);
        if (!write_text(list, text, err)) return false;
    }
    return true;
}

bool export_engine(const mod::Patch &p, const std::string &id, std::string &path, std::string &err) {
    path = st.preset_dir + "../exports/" + id + "_graph.cpp";
    return write_text(path, engine_source(p, id), err);
}

bool export_patch(const mod::Patch &p, const std::string &id, std::string &path, std::string &err) {
    path = st.preset_dir + "../exports/" + id + ".patch";
    static char buf[64 * 1024];
    const int n = mod::patch_to_text(p, buf, sizeof buf);
    return write_text(path, std::string(buf, static_cast<size_t>(n < static_cast<int>(sizeof buf) ? n : static_cast<int>(sizeof buf) - 1)), err);
}

}  // namespace studio
