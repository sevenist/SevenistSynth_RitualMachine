#pragma once
// Arrangements: where a synth's values sit on the board's screen (128 x 160), as data. A layout is pages of elements; an element is a
// kind of drawing (a row, a knob, a bar, a section title, a big value) at a rectangle, showing one value. The same layout_draw draws it
// in the studio's editor and could draw it on the board: it uses u8g2 and the device's font only.
// Saved as text, one file per synth (presets/<synth>.layout):
//   page
//   title -        0 0 128 12          ("-": the synth's name; a section title by its name)
//   row BODY/Tune  0 14 128 10         (kind, the value, x y w h)
#include <string>
#include <vector>
#include "lab.h"
#include "u8g2.h"

namespace studio {

constexpr int kScreenW = 128, kScreenH = 160;       // the board SynthLab is made for (HWV2)

enum Kind { kRow, kKnob, kBar, kTitle, kBig, kKindCount };
const char *kind_name(int kind);                    // "row", "knob", "bar", "title", "big"
int         kind_by_name(const char *name);         // -1: unknown
void        kind_size(int kind, int &w, int &h);    // the size a new element of that kind gets

struct El {
    int kind = kRow;
    int param = -1;                                 // the value (kTitle: a section's title, -1 the synth's name)
    int x = 0, y = 0, w = 128, h = 10;
};
struct Page { std::vector<El> els; };
struct Layout { std::vector<Page> pages; };

bool el_has_value(const lab::Synth &s, const El &e);    // shows a value (not a title): the encoders can reach it
// The value elements of a page in the order encoder A visits them: top to bottom, then left to right.
std::vector<int> nav_order(const lab::Synth &s, const Page &p);

// Fills the layout from the synth's list: kRow one value a row (SynthLab's own screen), kKnob a grid of knobs; each section starts
// with its title; a page per screenful.
void layout_auto(const lab::Synth &s, Layout &l, int style);

// Draws page `page` on a display of kScreenW x kScreenH (u8g2 colour 1 = ink). values: the synth's values; focus: the element the
// encoders are on (-1: none), drawn as the board would show it.
void layout_draw(u8g2_t *g, const lab::Synth &s, const Layout &l, int page, const float *values, int focus);

std::string layout_to_text(const lab::Synth &s, const Layout &l);
bool layout_from_text(const lab::Synth &s, Layout &l, const char *text, int *dropped = nullptr);

}  // namespace studio
