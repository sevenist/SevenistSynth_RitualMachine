#pragma once
// Arrangements: where a synth's values sit on the board's screen (128 x 160), as data. A layout is pages of elements; an element is a
// kind of drawing at a rectangle: one showing a value (a row, a knob, a bar, a big value, a fader, a toggle, a choice), a section title,
// or a decoration showing none (a label, a line, a box). An element's text replaces the name it shows (a label's and a box's: what they
// say). The same layout_draw draws it in the studio's editor and could draw it on the board: it uses u8g2 and the device's font only.
// Saved as text, one file per synth (presets/<synth>.layout):
//   page
//   title -        0 0 128 12          ("-": the synth's name; a section title by its name)
//   row BODY/Tune  0 14 128 10         (kind, the value, x y w h)
//   knob BODY/Tune 0 24 32 34 "Pitch"  (then, when there is one, the text in quotes)
//   label - 0 60 128 10 "Hello" center (a label's alignment: left, center, right)
#include <string>
#include <vector>
#include "lab.h"
#include "u8g2.h"

namespace studio {

constexpr int kScreenW = 128, kScreenH = 160;       // the board SynthLab is made for (HWV2)

enum Kind { kRow, kKnob, kBar, kTitle, kBig, kFader, kToggle, kChoice, kLabel, kLine, kBox, kKindCount };
enum Align { kLeft, kCenter, kRight };
const char *kind_name(int kind);                    // "row", "knob", "bar", "title", "big", "fader", "toggle", "choice", "label", "line", "box"
int         kind_by_name(const char *name);         // -1: unknown
void        kind_size(int kind, int &w, int &h);    // the size a new element of that kind gets
inline bool kind_is_deco(int kind) { return kind == kLabel || kind == kLine || kind == kBox; }    // shows no value, no title
inline bool kind_shows_value(int kind) { return kind != kTitle && !kind_is_deco(kind); }
constexpr int kMaxText = 31;                        // an element's text, in characters

struct El {
    int kind = kRow;
    int param = -1;                                 // the value (kTitle: a section's title, -1 the synth's name; a decoration: -1)
    int x = 0, y = 0, w = 128, h = 10;
    std::string text;                               // "": the value's (the section's, the synth's) name; a label's, a box's words
    int align = kLeft;                              // a label's
    El() = default;
    El(int kind_, int param_, int x_, int y_, int w_, int h_) : kind(kind_), param(param_), x(x_), y(y_), w(w_), h(h_) {}
};
struct Page { std::vector<El> els; };
struct Layout { std::vector<Page> pages; };

bool el_has_value(const lab::Synth &s, const El &e);    // shows a value (not a title, not a decoration): the encoders can reach it
// The name an element shows: its text, else its value's (its section's, the synth's) name; a decoration: its text.
std::string el_name(const lab::Synth &s, const El &e);
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
