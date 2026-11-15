#pragma once
// The file block at the bottom of a tab's left panel, the same on the three tabs (PATCH, SOUND, LAYOUT): a rule and "KIND  name", rows of
// buttons (two side by side, or one across), the saved files with Load, and EXPORT. It lays itself out in a column (x, w) from y, a row
// of 17 px at a time.
#include <cstddef>
#include <string>
#include <vector>
#include "ui/widgets.h"

namespace studio {

constexpr int kFileRow = 17;

struct FileBlock {
    wg_ctx_t &c;
    int x, w, y;

    // The rule and "KIND  name"; while the name is typed (st.editing with st.edit_what == edit_what), what is typed.
    void title(const char *kind, const char *name, int edit_what);
    // Rename (Done while typing): starts typing the name (st.edit_what = edit_what), or keeps what was typed into `name`.
    // True when the name changed.
    bool rename(int id, char *name, size_t cap, int edit_what);
    // A button in column `col` (0 or 1) of two, or across (col -1).
    bool button(int id, const char *label, int col);
    // The saved files (names without their extension): a stepper over them and Load; ids id and id + 1. True when Load was pushed on
    // one (sel: its index, kept in range).
    bool files(int id, const std::vector<std::string> &names, int &sel);
    void heading(const char *text);             // "EXPORT"
    void note(const char *text);                // a dim line of text
    void next() { y += kFileRow; }
};

// The height a block takes: its title, `rows` rows, and an EXPORT heading when `heading`.
constexpr int file_block_h(int rows, bool heading) { return 12 + rows * kFileRow + (heading ? 14 : 0) + 4; }

// The names of the files with extension `ext` in `dir`, sorted, without the extension.
std::vector<std::string> files_in(const std::string &dir, const char *ext);

}  // namespace studio
