// A sound as text (control/preset.h): words and lines, values by path, the macros and their links, older files.
#include <cstring>
#include <string>
#include "control/preset.h"
#include "test.h"

namespace {
const param_desc_t kParams[] = {
    {"Level", 0, 1, 0.8f},
    {"BODY", 0, 0, 0, nullptr, PARAM_HEAD},
    {"Tune", -24, 24, 0, "st", PARAM_INT},
    {"Dly Mix", 0, 1, 0.2f},
    {"RUMBLE", 0, 0, 0, nullptr, PARAM_HEAD},
    {"Tune", 20, 200, 50, "Hz", PARAM_LOG},
};
constexpr int kN = 6;
float g_vals[kN];
float get(void *, int i) { return g_vals[i]; }
void set(void *, int i, float v) { g_vals[i] = v; }
}  // namespace

TEST(preset_words_lines_and_paths) {
    const char *t = "value \"BODY/Dly Mix\" 0.5\r\n\nlast";
    char line[16], w[32];
    CHECK(pset_line(&t, line, sizeof line));
    CHECK(std::strcmp(line, "value \"BODY/Dly") == 0);                 // cut to the buffer, the rest of the line skipped
    CHECK(pset_line(&t, line, sizeof line) && line[0] == 0);           // an empty line
    CHECK(pset_line(&t, line, sizeof line) && std::strcmp(line, "last") == 0);
    CHECK(!pset_line(&t, line, sizeof line));
    const char *q = "  dest \"BODY/Dly Mix\"\t0.2 \r\n";
    CHECK(pset_word(&q, w, sizeof w) && std::strcmp(w, "dest") == 0);
    CHECK(pset_word(&q, w, sizeof w) && std::strcmp(w, "BODY/Dly Mix") == 0);
    CHECK(pset_word(&q, w, sizeof w) && std::strcmp(w, "0.2") == 0);
    CHECK(!pset_word(&q, w, sizeof w));
    char p[64];
    pset_path(kParams, kN, 0, p, sizeof p);
    CHECK(std::strcmp(p, "Level") == 0);                               // before any section
    pset_quote_path(kParams, kN, 3, p, sizeof p);
    CHECK(std::strcmp(p, "\"BODY/Dly Mix\"") == 0);
    CHECK_EQ(pset_find(kParams, kN, "RUMBLE/Tune"), 5);
    CHECK_EQ(pset_find(kParams, kN, "Tune"), 2);                       // a bare name: the first of that name
    CHECK_EQ(pset_find(kParams, kN, "BODY/Nope"), -1);
    CHECK_EQ(pset_section(kParams, kN, 5), 4);
}

TEST(preset_sound_round_trip_with_macros_and_links) {
    static mac_t m[MAC_MAX], back[MAC_MAX];
    std::memset(static_cast<void *>(m), 0, sizeof m);
    g_vals[0] = 0.25f; g_vals[2] = -7; g_vals[3] = 0.5f; g_vals[5] = 120;
    std::snprintf(m[0].name, sizeof m[0].name, "Punch");
    m[0].pos = 0.75f;
    ctl_map_t d = ctl_map_plain(ctl_target(PSET_KIND_VALUE, 0, 3));
    d.min = 0.2f; d.max = 0.9f; d.curve = CRV_EXP;
    mac_add(m, 0, d);
    mac_add(m, 0, ctl_map_plain(mac_target(1)));
    const pset_sound_t s = {kParams, kN, get, set, nullptr, m};
    const int n = pset_to_text(&s, "a test", nullptr, 0);
    std::string text(static_cast<size_t>(n) + 1, '\0');
    CHECK_EQ(pset_to_text(&s, "a test", &text[0], n + 1), n);
    text.resize(static_cast<size_t>(n));
    CHECK(text.find("# a test\nvalue Level 0.25\nvalue BODY/Tune -7\nvalue \"BODY/Dly Mix\" 0.5\nvalue RUMBLE/Tune 120\n") == 0);
    CHECK(text.find("macro 1 \"Punch\" 0.750\ndest \"BODY/Dly Mix\" 0.200 0.900 Exp\nlink 2 0.000 1.000 Lin\nmacro 2 \"\" 0.000\n") != std::string::npos);
    char small[20];
    CHECK_EQ(pset_to_text(&s, "a test", small, sizeof small), n);    // cut, the length still the whole text's
    CHECK_EQ(static_cast<int>(std::strlen(small)), 19);

    for (float &v : g_vals) v = 0;
    std::memset(static_cast<void *>(back), 0, sizeof back);
    const pset_sound_t r = {kParams, kN, get, set, nullptr, back};
    int dropped = -1;
    CHECK_EQ(pset_from_text(&r, text.c_str(), &dropped), PSET_HAD_VALUES | PSET_HAD_MACROS);
    CHECK_EQ(dropped, 0);
    CHECK(g_vals[0] == 0.25f && g_vals[2] == -7 && g_vals[3] == 0.5f && g_vals[5] == 120);
    CHECK(std::memcmp(back, m, sizeof m) == 0);
}

TEST(preset_reader_keeps_what_it_cannot_take) {
    static mac_t m[MAC_MAX];
    std::memset(static_cast<void *>(m), 0, sizeof m);
    for (float &v : g_vals) v = 0;
    const pset_sound_t s = {kParams, kN, get, set, nullptr, m};
    int dropped = 0;
    // an older file: the path unquoted; a value the synth no longer has; a comment; an unknown line
    CHECK_EQ(pset_from_text(&s, "# old\nvalue BODY/Dly Mix 0.75\nvalue BODY/Gone 3\nwhatever 1 2\n", &dropped), PSET_HAD_VALUES);
    CHECK(g_vals[3] == 0.75f && dropped == 1);
    m[2].n = 1;                                                        // (no macro in that text: the macros are left alone)
    CHECK_EQ(m[2].n, 1);
    // macros: a dest before any macro, a value it does not have, a loop, a link to no macro, a full macro
    std::string t = "dest Level 0 1 Lin\nmacro 1 \"a\" 2\nlink 2 0 1\ndest Nope 0 1 Lin\nmacro 2 \"b\" 0.5\nlink 1 0 1 Lin\nlink 0 0 1 Lin\nmacro 3 \"c\" 0.5\n";
    for (int i = 0; i < MAC_DESTS + 1; i++) t += "dest Level 0 1 S\n";
    CHECK_EQ(pset_from_text(&s, t.c_str(), &dropped), PSET_HAD_MACROS);
    CHECK_EQ(dropped, 4);
    CHECK(m[0].n == 1 && mac_of(&m[0].d[0].t) == 1 && m[0].pos == 1.0f);   // (the position kept inside 0..1)
    CHECK(m[1].n == 0 && m[2].n == MAC_DESTS && m[2].d[0].curve == CRV_S);
    CHECK_EQ(pset_from_text(&s, "nothing here\n", nullptr), 0);
}
