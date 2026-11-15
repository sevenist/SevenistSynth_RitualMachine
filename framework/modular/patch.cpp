// A patch, its text form and its runner (patch.h).
#include "modular/patch.h"
#include "modular/mdsp.h"
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mod {

/* ---------------- the patch ---------------- */

int Patch::index_of(int id) const {
    for (int i = 0; i < n_nodes; i++) if (node[i].id == id) return i;
    return -1;
}
Node *Patch::find(int id) { const int i = index_of(id); return i < 0 ? nullptr : &node[i]; }
const Node *Patch::find(int id) const { const int i = index_of(id); return i < 0 ? nullptr : &node[i]; }

bool fits_area(const ModuleDef *d, int area) {
    return d->scope == Scope::Both || (d->scope == Scope::Voice) == (area == kVoiceArea);
}

Node *Patch::add(const ModuleDef *d, int area, int x, int y) {
    if (!d || n_nodes >= kMaxNodes || !fits_area(d, area)) return nullptr;
    int id = 1;
    while (id < 256 && index_of(id) >= 0) id++;
    if (id >= 256) return nullptr;
    Node &n = node[n_nodes++];
    n = Node{};
    n.id = static_cast<uint8_t>(id);
    n.area = static_cast<uint8_t>(area);
    n.def = d;
    for (int i = 0; i < d->n_param; i++) n.v[i] = d->param[i].def;
    n.x = static_cast<int16_t>(x);
    n.y = static_cast<int16_t>(y);
    return &n;
}

void Patch::disconnect(int k) {
    if (k < 0 || k >= n_cables) return;
    for (int i = k; i + 1 < n_cables; i++) cable[i] = cable[i + 1];
    n_cables--;
}

void Patch::remove(int id) {
    for (int k = n_cables - 1; k >= 0; k--) if (cable[k].src == id || cable[k].dst == id) disconnect(k);
    const int i = index_of(id);
    if (i < 0) return;
    const int g = node[i].group;
    for (int k = i; k + 1 < n_nodes; k++) node[k] = node[k + 1];
    n_nodes--;
    if (g) {                                            // (its group, left empty: gone)
        bool any = false;
        for (int k = 0; k < n_nodes; k++) if (node[k].group == g) any = true;
        if (!any) ungroup(g);
    }
}

/* ---------------- switch groups ---------------- */

Group *Patch::find_group(int id) { for (int i = 0; i < n_groups; i++) if (group[i].id == id) return &group[i]; return nullptr; }
const Group *Patch::find_group(int id) const { for (int i = 0; i < n_groups; i++) if (group[i].id == id) return &group[i]; return nullptr; }
Selector *Patch::find_selector(int id) { for (int i = 0; i < n_selectors; i++) if (selector[i].id == id) return &selector[i]; return nullptr; }
const Selector *Patch::find_selector(int id) const { for (int i = 0; i < n_selectors; i++) if (selector[i].id == id) return &selector[i]; return nullptr; }

int Patch::add_selector(const char *nm) {
    if (n_selectors >= kMaxSelectors) return 0;
    int id = 1;
    while (find_selector(id)) id++;
    Selector &sl = selector[n_selectors++];
    sl = Selector{};
    sl.id = static_cast<uint8_t>(id);
    std::snprintf(sl.name, sizeof sl.name, "%s", nm && *nm ? nm : "Select");
    return id;
}

int Patch::make_group(const int *ids, int n, const char *nm, int sel) {
    if (n_groups >= kMaxGroups || n <= 0) return 0;
    if (sel && !find_selector(sel)) return 0;
    int area = -1;
    for (int k = 0; k < n; k++) {
        const Node *nd = find(ids[k]);
        if (!nd) continue;
        if (area >= 0 && nd->area != area) return 0;
        area = nd->area;
    }
    if (area < 0) return 0;
    int id = 1;
    while (find_group(id)) id++;
    Group &g = group[n_groups++];
    g = Group{};
    g.id = static_cast<uint8_t>(id);
    g.selector = static_cast<uint8_t>(sel);
    std::snprintf(g.name, sizeof g.name, "%s", nm && *nm ? nm : "Group");
    for (int k = 0; k < n; k++) {
        Node *nd = find(ids[k]);
        if (!nd) continue;
        const int was = nd->group;
        nd->group = static_cast<uint8_t>(id);
        if (was) {                                      // (the group it left, empty now: gone)
            bool any = false;
            for (int q = 0; q < n_nodes; q++) if (node[q].group == was) any = true;
            if (!any) ungroup(was);
        }
    }
    return find_group(id) ? id : 0;
}

void Patch::ungroup(int id) {
    for (int k = 0; k < n_nodes; k++) if (node[k].group == id) node[k].group = 0;
    for (int i = 0; i < n_groups; i++) {
        if (group[i].id != id) continue;
        const int sel = group[i].selector;
        for (int k = i; k + 1 < n_groups; k++) group[k] = group[k + 1];
        n_groups--;
        bool used = false;                              // (its selector, without groups now: gone)
        for (int k = 0; k < n_groups; k++) if (sel && group[k].selector == sel) used = true;
        if (sel && !used)
            for (int k = 0; k < n_selectors; k++)
                if (selector[k].id == sel) { for (int q = k; q + 1 < n_selectors; q++) selector[q] = selector[q + 1]; n_selectors--; break; }
        return;
    }
}

namespace {
// Which groups play, from the switches' values (the list's, or the groups' and selectors' own when values is nullptr)
void groups_on_raw(const Group *g, int ng, const Selector *sl, int ns, int first, const float *values, bool *on) {
    int rank = 0;
    for (int i = 0; i < ng; i++) {
        if (g[i].selector) continue;
        on[i] = values ? values[first + rank] >= 0.5f : g[i].on;
        rank++;
    }
    for (int s = 0; s < ns; s++) {
        const int v = values ? static_cast<int>(values[first + rank] + 0.5f) : sl[s].value;
        int r = 0;
        for (int i = 0; i < ng; i++) if (g[i].selector == sl[s].id) { on[i] = r == v; r++; }
        rank++;
    }
}
int bypass_count(const Patch &p) { int n = 0; for (int i = 0; i < p.n_groups; i++) if (!p.group[i].selector) n++; return n; }
}  // namespace

void patch_switches(const Patch &p, int *first, int *count) {
    int macros = 0;
    for (int i = 0; i < p.n_nodes; i++) if (is_macro(p.node[i])) macros++;
    const int n = bypass_count(p) + p.n_selectors;
    *count = n;
    *first = (macros ? 1 + macros : 0) + 1;            // (after the MACROS section and the SWITCHES title)
}

void switch_row(const Patch &p, int rank, const char **name, const char **names, int *n) {
    static const char *const kOnOff[] = {"Bypass", "On"};
    *n = 0;
    *name = "";
    int r = 0;
    for (int i = 0; i < p.n_groups; i++) {
        if (p.group[i].selector) continue;
        if (r++ == rank) { *name = p.group[i].name; names[0] = kOnOff[0]; names[1] = kOnOff[1]; *n = 2; return; }
    }
    for (int s = 0; s < p.n_selectors; s++) {
        if (r++ != rank) continue;
        *name = p.selector[s].name;
        for (int i = 0; i < p.n_groups; i++) if (p.group[i].selector == p.selector[s].id) names[(*n)++] = p.group[i].name;
        return;
    }
}

void groups_on(const Patch &p, const float *values, bool *on) {
    int first, count;
    patch_switches(p, &first, &count);
    groups_on_raw(p.group, p.n_groups, p.selector, p.n_selectors, first, values, on);
}

bool Patch::connect(int src, int src_port, int dst, int dst_port, bool to_param, float depth) {
    const Node *s = find(src), *d = find(dst);
    if (!s || !d || s->area != d->area || src_port < 0 || src_port >= s->def->n_out || dst_port < 0) return false;
    if (dst_port >= (to_param ? d->def->n_param : d->def->n_in)) return false;
    for (int k = 0; k < n_cables; k++) {
        Cable &c = cable[k];
        if (c.src == src && c.src_port == src_port && c.dst == dst && c.dst_port == dst_port && c.to_param == to_param) { c.depth = depth; return true; }
    }
    int into = 0;
    for (int k = 0; k < n_cables; k++) if (cable[k].dst == dst) into++;
    if (n_cables >= kMaxCables || into >= 16) return false;     // (Runner::kRefs)
    Cable &c = cable[n_cables++];
    c.src = static_cast<uint8_t>(src); c.src_port = static_cast<uint8_t>(src_port);
    c.dst = static_cast<uint8_t>(dst); c.dst_port = static_cast<uint8_t>(dst_port);
    c.to_param = to_param;
    c.depth = depth;
    return true;
}

void Patch::clear() {
    n_nodes = 0;
    n_cables = 0;
    n_groups = 0;
    n_selectors = 0;
    voices = kMaxPatchVoices;
}

void Patch::init_default() {
    clear();
    std::snprintf(name, sizeof name, "Init");
    const int note = add(find_module("Note"), kVoiceArea, 16, 40)->id;
    const int osc = add(find_module("Osc"), kVoiceArea, 150, 20)->id;
    const int flt = add(find_module("Filter"), kVoiceArea, 290, 20)->id;
    const int env = add(find_module("ADSR"), kVoiceArea, 150, 200)->id;
    const int vca = add(find_module("VCA"), kVoiceArea, 430, 40)->id;
    const int out = add(find_module("VoiceOut"), kVoiceArea, 560, 40)->id;
    connect(osc, 0, flt, 0, false, 1.f);
    connect(flt, 0, vca, 0, false, 1.f);
    connect(env, 0, vca, 1, false, 1.f);
    connect(env, 0, flt, 1, false, 1.f);
    connect(vca, 0, out, 0, false, 1.f);
    (void)note;
    const int voices = add(find_module("Voices"), kFxArea, 16, 40)->id;
    const int room = add(find_module("Room"), kFxArea, 170, 40)->id;
    const int master = add(find_module("Output"), kFxArea, 330, 40)->id;
    connect(voices, 0, room, 0, false, 1.f);
    connect(room, 0, master, 0, false, 1.f);
}

void node_title(const Patch &p, int index, char *out, size_t size) {
    if (index < 0 || index >= p.n_nodes) { std::snprintf(out, size, "?"); return; }
    const ModuleDef *d = p.node[index].def;
    int rank = 0, same = 0;
    for (int i = 0; i < p.n_nodes; i++) {
        if (p.node[i].def != d) continue;
        same++;
        if (i <= index) rank++;
    }
    if (same > 1) std::snprintf(out, size, "%s %d", d->name, rank);
    else std::snprintf(out, size, "%s", d->name);
}

bool is_macro(const Node &n) { return n.def && !std::strcmp(n.def->name, "Macro"); }

void macro_name(const Node &n, char *out, size_t size) {
    if (n.name[0]) std::snprintf(out, size, "%s", n.name);
    else std::snprintf(out, size, "Macro %d", n.id);
}

int macro_count(const Patch &p) {
    int k = 0;
    for (int i = 0; i < p.n_nodes; i++) if (is_macro(p.node[i])) k++;
    return k;
}

int macro_twin(const Patch &p) {
    char a[24], b[24];
    for (int i = 0; i < p.n_nodes; i++) {
        if (!is_macro(p.node[i])) continue;
        macro_name(p.node[i], a, sizeof a);
        for (int j = 0; j < i; j++) {
            if (!is_macro(p.node[j])) continue;
            macro_name(p.node[j], b, sizeof b);
            const char *x = a, *y = b;
            while (*x && *y && std::tolower(static_cast<unsigned char>(*x)) == std::tolower(static_cast<unsigned char>(*y))) { x++; y++; }
            if (!*x && !*y) return i;
        }
    }
    return -1;
}

int patch_slots(const Patch &p, int *base) {
    int n = 0;
    for (int i = 0; i < p.n_nodes; i++) {               // the Macros: a section, a row each
        if (!is_macro(p.node[i])) continue;
        if (n == 0) n = 1;
        base[i] = n - 1;
        n++;
    }
    const int sw = bypass_count(p) + p.n_selectors;     // the switches: a section, a row each (patch_switches)
    if (sw) n += 1 + sw;
    for (int i = 0; i < p.n_nodes; i++) {
        if (is_macro(p.node[i])) continue;
        const int need = 1 + p.node[i].def->n_param;
        if (n + need > kMaxSlots) { base[i] = -1; continue; }
        base[i] = n;
        n += need;
    }
    return n;
}

/* ---------------- the text form ---------------- */

namespace {
struct Out {
    char *buf;
    int cap, n;
    void add(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};
void Out::add(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[256];
    const int k = std::vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    for (int i = 0; i < k && i < static_cast<int>(sizeof tmp) - 1; i++) { if (n + 1 < cap) buf[n] = tmp[i]; n++; }
    if (cap > 0) buf[n < cap ? n : cap - 1] = 0;
}

bool word(const char *&p, char *out, size_t size) {             // the next word (quotes removed); false at the end of the line
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '\r' || *p == '\n') { out[0] = 0; return false; }
    size_t n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && *p != '\n' && *p != '\r') { if (n + 1 < size) out[n++] = *p; p++; }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') { if (n + 1 < size) out[n++] = *p; p++; }
    }
    out[n] = 0;
    return true;
}

// "3.Gate": a node id and a port name.
bool port_ref(const char *w, int &id, char *port, size_t size) {
    const char *dot = std::strchr(w, '.');
    if (!dot) return false;
    id = std::atoi(w);
    std::snprintf(port, size, "%s", dot + 1);
    return id > 0;
}
}  // namespace

int patch_to_text(const Patch &p, char *buf, int cap) {
    Out o{buf, cap, 0};
    if (cap > 0) buf[0] = 0;
    o.add("# SynthLab Studio patch (framework/modular/patch.h)\n");
    o.add("patch \"%s\"\n", p.name);
    o.add("voices %d\n", p.voices);
    for (int s = 0; s < p.n_selectors; s++) o.add("selector %d \"%s\" %d\n", p.selector[s].id, p.selector[s].name, p.selector[s].value);
    for (int i = 0; i < p.n_groups; i++) {
        const Group &g = p.group[i];
        if (g.selector) o.add("group %d \"%s\" sel %d\n", g.id, g.name, g.selector);
        else o.add("group %d \"%s\" %s\n", g.id, g.name, g.on ? "on" : "bypass");
    }
    for (int i = 0; i < p.n_nodes; i++) {
        const Node &n = p.node[i];
        o.add("node %d %s %s %d %d", n.id, n.area == kFxArea ? "fx" : "voice", n.def->name, n.x, n.y);
        if (n.name[0]) o.add(" \"%s\"", n.name);
        if (n.group) o.add(" @%d", n.group);
        for (int k = 0; k < n.def->n_param; k++) o.add(" %s=%g", n.def->param[k].name, static_cast<double>(n.v[k]));
        o.add("\n");
    }
    for (int k = 0; k < p.n_cables; k++) {
        const Cable &c = p.cable[k];
        const Node *s = p.find(c.src), *d = p.find(c.dst);
        if (!s || !d) continue;
        o.add("%s %d.%s %d.%s %g\n", c.to_param ? "mod" : "cable", c.src, s->def->out_name[c.src_port], c.dst,
              c.to_param ? d->def->param[c.dst_port].name : d->def->in_name[c.dst_port], static_cast<double>(c.depth));
    }
    return o.n;
}

bool patch_from_text(Patch &out, const char *text, char *err, int err_cap, int *skipped) {
    static Patch r;                                             // (large: not on the stack; the studio's thread only)
    r.clear();
    std::snprintf(r.name, sizeof r.name, "Untitled");
    int bad = 0, line_no = 0;
    bool any = false;
    if (err_cap > 0) err[0] = 0;
    for (const char *p = text; *p;) {
        line_no++;
        const char *e = std::strchr(p, '\n');
        const size_t len = e ? static_cast<size_t>(e - p) : std::strlen(p);
        char line[1024];
        std::snprintf(line, sizeof line, "%.*s", static_cast<int>(len < sizeof line ? len : sizeof line - 1), p);
        p += len + (e ? 1 : 0);
        const char *q = line;
        char w[64];
        if (!word(q, w, sizeof w) || w[0] == '#') continue;
        if (!std::strcmp(w, "patch")) { word(q, r.name, sizeof r.name); any = true; continue; }
        if (!std::strcmp(w, "selector")) {                      // selector <id> "name" <value>
            char id[8], nm[24], v[8] = "0";
            if (!word(q, id, sizeof id) || !word(q, nm, sizeof nm) || r.n_selectors >= kMaxSelectors || std::atoi(id) <= 0) { bad++; continue; }
            word(q, v, sizeof v);
            Selector &sl = r.selector[r.n_selectors++];
            sl = Selector{};
            sl.id = static_cast<uint8_t>(std::atoi(id));
            std::snprintf(sl.name, sizeof sl.name, "%s", nm);
            sl.value = static_cast<uint8_t>(std::atoi(v));
            continue;
        }
        if (!std::strcmp(w, "group")) {                         // group <id> "name" on | bypass | sel <selector>
            char id[8], nm[24], how[8], sel[8] = "0";
            if (!word(q, id, sizeof id) || !word(q, nm, sizeof nm) || !word(q, how, sizeof how) || r.n_groups >= kMaxGroups || std::atoi(id) <= 0) { bad++; continue; }
            Group &g = r.group[r.n_groups++];
            g = Group{};
            g.id = static_cast<uint8_t>(std::atoi(id));
            std::snprintf(g.name, sizeof g.name, "%s", nm);
            if (!std::strcmp(how, "sel")) { word(q, sel, sizeof sel); g.selector = static_cast<uint8_t>(std::atoi(sel)); }
            else g.on = std::strcmp(how, "bypass") != 0;
            continue;
        }
        if (!std::strcmp(w, "voices")) {                        // the synth's voice count (an older file: none, 6)
            char v[8];
            const int n = word(q, v, sizeof v) ? std::atoi(v) : 0;
            if (n < 1 || n > kMaxPatchVoices) { bad++; continue; }
            r.voices = n;
            continue;
        }
        if (!std::strcmp(w, "node")) {
            char id[8], area[8], type[32], x[8], y[8];
            if (!word(q, id, sizeof id) || !word(q, area, sizeof area) || !word(q, type, sizeof type) || !word(q, x, sizeof x) || !word(q, y, sizeof y)) { bad++; continue; }
            const ModuleDef *d = find_module(type);
            const int nid = std::atoi(id);
            const int ar = !std::strcmp(area, "fx") ? kFxArea : kVoiceArea;
            if (!d || nid <= 0 || nid > 255 || r.find(nid) || r.n_nodes >= kMaxNodes || !fits_area(d, ar)) { bad++; continue; }
            Node &n = r.node[r.n_nodes++];
            n = Node{};
            n.id = static_cast<uint8_t>(nid);
            n.area = !std::strcmp(area, "fx") ? kFxArea : kVoiceArea;
            n.def = d;
            n.x = static_cast<int16_t>(std::atoi(x));
            n.y = static_cast<int16_t>(std::atoi(y));
            for (int k = 0; k < d->n_param; k++) n.v[k] = d->param[k].def;
            for (;;) {
                while (*q == ' ' || *q == '\t') q++;
                const bool quoted = *q == '"';
                if (!word(q, w, sizeof w)) break;
                if (quoted) { std::snprintf(n.name, sizeof n.name, "%s", w); continue; }     // its name
                if (w[0] == '@') { n.group = static_cast<uint8_t>(std::atoi(w + 1)); continue; }   // its group
                char *eq = std::strchr(w, '=');
                if (!eq) continue;
                *eq = 0;
                const int k = param_index(*d, w);
                if (k < 0) { bad++; continue; }
                const Param &pr = d->param[k];
                float v = static_cast<float>(std::atof(eq + 1));
                n.v[k] = v < pr.min ? pr.min : (v > pr.max ? pr.max : v);
            }
            any = true;
            continue;
        }
        if (!std::strcmp(w, "cable") || !std::strcmp(w, "mod")) {
            const bool to_param = w[0] == 'm';
            char a[48], b[48], dep[16] = "1", pa[32], pb[32];
            int sa, sb;
            if (!word(q, a, sizeof a) || !word(q, b, sizeof b) || !port_ref(a, sa, pa, sizeof pa) || !port_ref(b, sb, pb, sizeof pb)) { bad++; continue; }
            word(q, dep, sizeof dep);
            const Node *s = r.find(sa), *d = r.find(sb);
            if (!s || !d) { bad++; continue; }
            const int so = out_index(*s->def, pa), di = to_param ? param_index(*d->def, pb) : in_index(*d->def, pb);
            if (so < 0 || di < 0 || !r.connect(sa, so, sb, di, to_param, static_cast<float>(std::atof(dep)))) { bad++; continue; }
            continue;
        }
        bad++;
    }
    for (int i = 0; i < r.n_groups; i++)
        if (r.group[i].selector && !r.find_selector(r.group[i].selector)) { r.group[i].selector = 0; bad++; }
    for (int i = 0; i < r.n_nodes; i++) if (r.node[i].group && !r.find_group(r.node[i].group)) { r.node[i].group = 0; bad++; }
    if (skipped) *skipped = bad;
    if (!any) { std::snprintf(err, static_cast<size_t>(err_cap > 0 ? err_cap : 0), "no patch in the text"); return false; }
    out = r;
    return true;
}

/* ---------------- the runner ---------------- */

namespace {
size_t align_up(size_t n, size_t a) { return (n + a - 1) / a * a; }
size_t inst_bytes(const ModuleDef *d) {
    return align_up(d->size, 16) + static_cast<size_t>(d->n_out + d->n_in) * kMaxFrames * sizeof(float);
}
}  // namespace

size_t Runner::bytes_for(const Patch &p, int area) {
    size_t n = 0;
    for (int i = 0; i < p.n_nodes; i++) if (p.node[i].area == area) n += inst_bytes(p.node[i].def);
    return n;
}

void Runner::clear() {
    for (int i = 0; i < n_; i++) inst_[i].def->destroy(inst_[i].state);
    n_ = 0;
}

bool Runner::build(const Patch &p, int area, void *mem, size_t bytes) {
    clear();
    if (bytes_for(p, area) > bytes || !mem) return false;
    int base[kMaxNodes];
    patch_slots(p, base);

    // the order: a module after those it listens to (Kahn's; when only loops are left, the first module left goes next)
    int order[kMaxNodes], n = 0;
    bool done[kMaxNodes] = {};
    int count = 0;
    for (int i = 0; i < p.n_nodes; i++) if (p.node[i].area == area) count++;
    while (n < count) {
        int pick = -1;
        for (int i = 0; i < p.n_nodes && pick < 0; i++) {
            if (done[i] || p.node[i].area != area) continue;
            bool ready = true;
            for (int k = 0; k < p.n_cables && ready; k++) {
                const Cable &c = p.cable[k];
                if (c.dst != p.node[i].id || c.src == c.dst) continue;
                const int s = p.index_of(c.src);
                if (s >= 0 && !done[s] && p.node[s].area == area) ready = false;
            }
            if (ready) pick = i;
        }
        if (pick < 0) for (int i = 0; i < p.n_nodes && pick < 0; i++) if (!done[i] && p.node[i].area == area) pick = i;
        done[pick] = true;
        order[n++] = pick;
    }

    // the modules and their buffers, in mem
    unsigned char *m = static_cast<unsigned char *>(mem);
    int at_of[kMaxNodes];
    for (int i = 0; i < p.n_nodes; i++) at_of[i] = -1;
    for (int k = 0; k < n; k++) {
        const Node &nd = p.node[order[k]];
        Inst &in = inst_[k];
        in = Inst{};
        in.def = nd.def;
        in.state = m;
        if (nd.def->construct_in) nd.def->construct_in(m, area);
        else nd.def->construct(m);
        m += align_up(nd.def->size, 16);
        for (int o = 0; o < nd.def->n_out; o++) { in.out[o] = reinterpret_cast<float *>(m); std::memset(m, 0, kMaxFrames * sizeof(float)); m += kMaxFrames * sizeof(float); }
        for (int i = 0; i < nd.def->n_in; i++) { in.sum[i] = reinterpret_cast<float *>(m); m += kMaxFrames * sizeof(float); }
        in.base = base[order[k]];
        std::memcpy(in.v, nd.v, sizeof in.v);
        in.is_out = !std::strcmp(nd.def->name, "VoiceOut") || !std::strcmp(nd.def->name, "Output");
        in.is_voices = !std::strcmp(nd.def->name, "Voices");
        at_of[order[k]] = k;
        n_ = k + 1;
    }
    // the switch groups: copies of the patch's, each module's group, its outputs that leave the group, the cables into the group
    n_gdef_ = p.n_groups;
    n_sdef_ = p.n_selectors;
    for (int g = 0; g < p.n_groups; g++) { gdef_[g] = p.group[g]; grp_[g] = GroupRt{}; }
    for (int s = 0; s < p.n_selectors; s++) sdef_[s] = p.selector[s];
    int sw_count = 0;
    patch_switches(p, &sw_first_, &sw_count);
    auto group_index = [&](int gid) { for (int g = 0; g < p.n_groups; g++) if (p.group[g].id == gid) return g; return -1; };
    for (int k = 0; k < n_; k++) inst_[k].grp = -1;
    for (int i = 0; i < p.n_nodes; i++) if (at_of[i] >= 0 && p.node[i].group) inst_[at_of[i]].grp = static_cast<int8_t>(group_index(p.node[i].group));
    for (int c = 0; c < p.n_cables; c++) {
        const Cable &cb = p.cable[c];
        const int s = p.index_of(cb.src), d = p.index_of(cb.dst);
        if (s < 0 || d < 0 || at_of[s] < 0 || at_of[d] < 0 || cb.to_param) continue;
        const int gs = inst_[at_of[s]].grp, gd = inst_[at_of[d]].grp;
        if (gs >= 0 && gs != gd) inst_[at_of[s]].exit |= static_cast<uint8_t>(1u << cb.src_port);     // leaves its group
        if (gd >= 0 && gd != gs && grp_[gd].n_entry < kRefs)                                              // comes into a group
            grp_[gd].entry[grp_[gd].n_entry++] = Ref{inst_[at_of[s]].out[cb.src_port], cb.depth, cb.dst_port, false};
    }
    // the cables: each one a reference to its source's output buffer
    for (int c = 0; c < p.n_cables; c++) {
        const Cable &cb = p.cable[c];
        const int s = p.index_of(cb.src), d = p.index_of(cb.dst);
        if (s < 0 || d < 0 || at_of[s] < 0 || at_of[d] < 0) continue;
        Inst &dst = inst_[at_of[d]];
        if (dst.n_ref >= kRefs) continue;
        dst.ref[dst.n_ref++] = Ref{inst_[at_of[s]].out[cb.src_port], cb.depth, cb.dst_port, cb.to_param};
    }
    for (int i = 0; i < kMaxFrames; i++) one_[i] = 1.f;
    return true;
}

void Runner::start(const Ctx &c) {
    for (int i = 0; i < n_; i++) inst_[i].def->start(inst_[i].state, c);
    trig_ = true;
}

float Runner::process(const Ctx &c, const float *values, const float *in, float *out, int frames) {
    float peak = 0.f;
    for (int off = 0; off < frames; off += kMaxFrames) {
        const int m = frames - off < kMaxFrames ? frames - off : kMaxFrames;
        run_chunk(c, values, in ? in + off : nullptr, out + off, m, peak);
    }
    return peak;
}

void Runner::run_chunk(const Ctx &c, const float *values, const float *in, float *out, int frames, float &peak) {
    const float g = c.gate ? 1.f : 0.f;
    const float pitch = fast_log2(c.hz * (1.f / 261.6256f));     // (the voice's pitch, octaves over middle C: kPitch)
    for (int i = 0; i < frames; i++) { gate_[i] = g; trigbuf_[i] = 0.f; pitchbuf_[i] = pitch; }
    if (trig_) { trigbuf_[0] = 1.f; trig_ = false; }
    bool on[kMaxGroups] = {};
    if (n_gdef_) groups_on_raw(gdef_, n_gdef_, sdef_, n_sdef_, sw_first_, values, on);
    int entry_of = -1;                                  // (entry_ holds that group's entry signal)
    for (int k = 0; k < n_; k++) {
        Inst &s = inst_[k];
        const ModuleDef &d = *s.def;
        if (s.grp >= 0 && !on[s.grp]) {                 // a group switched off: the module does not run
            const bool bypass = !gdef_[s.grp].selector;
            for (int o = 0; o < d.n_out; o++) {
                float *dst = s.out[o];
                if (bypass && (s.exit & (1u << o))) {   // where the group's signal leaves it: what came into the group
                    if (entry_of != s.grp) {
                        for (int f = 0; f < frames; f++) entry_[f] = 0.f;
                        const GroupRt &g = grp_[s.grp];
                        for (int r = 0; r < g.n_entry; r++) for (int f = 0; f < frames; f++) entry_[f] += g.entry[r].buf[f] * g.entry[r].depth;
                        entry_of = s.grp;
                    }
                    for (int f = 0; f < frames; f++) dst[f] = entry_[f];
                } else {
                    for (int f = 0; f < frames; f++) dst[f] = 0.f;
                }
            }
            continue;
        }
        // the inputs: nothing (the default), one cable at depth 1 (its buffer), or the cables summed
        for (int i = 0; i < d.n_in; i++) {
            int cnt = 0;
            const Ref *only = nullptr;
            for (int r = 0; r < s.n_ref; r++) if (!s.ref[r].to_param && s.ref[r].port == i) { cnt++; only = &s.ref[r]; }
            if (cnt == 0) {
                switch (d.in_def[i]) {
                case kOne: s.in[i] = one_; break;
                case kGate: s.in[i] = gate_; break;
                case kTrig: s.in[i] = trigbuf_; break;
                case kIn0: s.in[i] = s.in[0]; break;
                case kPitch: s.in[i] = pitchbuf_; break;
                default: s.in[i] = zero_; break;
                }
            } else if (cnt == 1 && only->depth == 1.f) {
                s.in[i] = only->buf;
            } else {
                float *sum = s.sum[i];
                for (int f = 0; f < frames; f++) sum[f] = 0.f;
                for (int r = 0; r < s.n_ref; r++) {
                    const Ref &rf = s.ref[r];
                    if (rf.to_param || rf.port != i) continue;
                    for (int f = 0; f < frames; f++) sum[f] += rf.buf[f] * rf.depth;
                }
                s.in[i] = sum;
            }
        }
        // the values: the list's (or the patch's), moved by the modulation cables (their sources' mean over the block)
        float pv[kMaxParams];
        for (int j = 0; j < d.n_param; j++) pv[j] = values && s.base >= 0 ? values[s.base + 1 + j] : s.v[j];
        for (int r = 0; r < s.n_ref; r++) {
            const Ref &rf = s.ref[r];
            if (!rf.to_param || rf.port >= d.n_param) continue;
            float mean = 0.f;
            for (int f = 0; f < frames; f++) mean += rf.buf[f];
            mean *= rf.depth / static_cast<float>(frames);
            const Param &pr = d.param[rf.port];
            pv[rf.port] = from_norm(pr, to_norm(pr, pv[rf.port]) + mean);
        }
        if (s.is_voices) {
            float *o = s.out[0];
            for (int f = 0; f < frames; f++) o[f] = in ? in[f] : 0.f;
        }
        IO io;
        for (int i = 0; i < kMaxIn; i++) io.in[i] = i < d.n_in ? s.in[i] : zero_;
        for (int o = 0; o < kMaxOut; o++) io.out[o] = o < d.n_out ? s.out[o] : nullptr;
        io.p = pv;
        io.frames = frames;
        d.process(s.state, c, io);
        if (s.is_out) {
            const float *o = s.out[0];
            for (int f = 0; f < frames; f++) {
                const float y = o[f] == o[f] ? o[f] : 0.f;          // (a NaN stays inside the patch)
                out[f] += y;
                const float a = y < 0.f ? -y : y;
                if (a > peak) peak = a;
            }
        }
    }
}

}  // namespace mod
