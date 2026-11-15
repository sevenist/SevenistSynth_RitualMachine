#include "file_block.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include "studio.h"

namespace studio {

void FileBlock::title(const char *kind, const char *name, int edit_what) {
    const wg_theme_t &t = c.theme;
    wg_fill(&c, gui_rect(x - 2, y - 2, w + 4, 1), t.line);
    wg_text_small(&c, x + 2, y + 8, kind, t.dim);
    const int nx = x + 2 + wg_text_w(&c, kind, true) + 8;
    if (st.editing && st.edit_what == edit_what) {
        const std::string s = st.edit_text + "_";
        wg_text_small(&c, nx, y + 8, s.c_str(), t.accent);
    } else {
        wg_text_small(&c, nx, y + 8, name, t.text);
    }
    y += 12;
}

bool FileBlock::rename(int id, char *name, size_t cap, int edit_what) {
    const bool typing = st.editing && st.edit_what == edit_what;
    if (!button(id, typing ? "Done" : "Rename", 0)) return false;
    if (!typing) { st.editing = true; st.edit_what = edit_what; st.edit_text = name; return false; }
    std::snprintf(name, cap, "%s", st.edit_text.c_str());
    st.editing = false;
    return true;
}

bool FileBlock::button(int id, const char *label, int col) {
    const int half = (w - 2) / 2;
    const gui_rect_t r = col < 0 ? gui_rect(x, y, w, 14) : gui_rect(x + col * (half + 2), y, col == 1 ? w - half - 2 : half, 14);
    return wg_button(&c, id, r, label, false);
}

bool FileBlock::files(int id, const std::vector<std::string> &names, int &sel) {
    if (!names.empty()) sel = std::max(0, std::min(static_cast<int>(names.size()) - 1, sel));
    const char *text = names.empty() ? "(no file yet)" : names[static_cast<size_t>(sel)].c_str();
    sel += wg_stepper(&c, id, gui_rect(x, y, w - 42, 14), text);
    return wg_button(&c, id + 1, gui_rect(x + w - 40, y, 40, 14), "Load", false) && !names.empty();
}

void FileBlock::heading(const char *text) {
    y += 4;
    wg_text_small(&c, x + 2, y + 6, text, c.theme.dim);
    y += 10;
}

void FileBlock::note(const char *text) {
    wg_text_small(&c, x + 2, y + 9, text, c.theme.dim);
}

std::vector<std::string> files_in(const std::string &dir, const char *ext) {
    std::vector<std::string> f;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ext) f.push_back(e.path().stem().string());
    std::sort(f.begin(), f.end());
    return f;
}

}  // namespace studio
