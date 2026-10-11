// The sample browser (synth_ui.h): one folder of the library as a full-screen list, the overview of the highlighted sample on top.
// The rows: ".." first (below the root), then the folder's sub-folders, then its samples, as audio_dir_open sorted them. The screen keeps
// only the rows it shows (br_page); the platform holds the folder's list. The app reads the highlighted sample once the highlight rests
// (br_state BR_FRESH -> BR_SHOWN / BR_NONE, br_info) and acts on br_done.
#include "core/ui_screen.h"
#include <stdio.h>
#include <string.h>

#define BR_WAVE_H 24
#define BR_ROW_H  GUI_GRID

static int br_up(const synth_ui_t *ui) { return ui->br_dir[0] ? 1 : 0; }                  // the ".." row
static int br_count(const synth_ui_t *ui) { return ui->br_total + br_up(ui); }
static int br_rows(void) { return clampi((DISPLAY_HEIGHT - GUI_BAR_H - BR_WAVE_H - 4) / BR_ROW_H, 2, BROWSE_PAGE); }   // 10 on 128 px

static const audio_dir_entry_t *br_entry(const synth_ui_t *ui, int row) {
    return row >= ui->br_first && row < ui->br_first + ui->br_n ? &ui->br_page[row - ui->br_first] : NULL;
}

static void br_load(synth_ui_t *ui) {                                                    // the rows from br_first on
    const int up = br_up(ui);
    int n = 0;
    if (up && ui->br_first == 0) {
        memset(&ui->br_page[0], 0, sizeof ui->br_page[0]);
        snprintf(ui->br_page[0].name, sizeof ui->br_page[0].name, "..");
        ui->br_page[0].dir = 1;
        n = 1;
    }
    const int got = audio_dir_read(ui->br_first + n - up, &ui->br_page[n], BROWSE_PAGE - n);
    ui->br_n = n + (got > 0 ? got : 0);
}

static void br_move(synth_ui_t *ui, int row, uint32_t now) {
    const int count = br_count(ui), rows = br_rows();
    if (row >= count) row = count - 1;
    if (row < 0) row = 0;
    int first = ui->br_first;
    if (row < first) first = row;
    if (row >= first + rows) first = row - rows + 1;
    if (first != ui->br_first || !ui->br_n) { ui->br_first = first; br_load(ui); }
    if (row != ui->br_cur) { ui->br_state = BR_FRESH; ui->br_ms = now; ui->br_tries = 0; }
    ui->br_cur = row;
}

// Shows the folder `dir` with the highlight on its entry `focus` (a folder when focus_dir), else on its first row.
static bool br_set_dir(synth_ui_t *ui, const char *dir, const char *focus, bool focus_dir, uint32_t now) {
    const int total = audio_dir_open(dir);
    if (total < 0) return false;
    snprintf(ui->br_dir, sizeof ui->br_dir, "%s", dir);
    ui->br_total = total;
    int row = 0;
    if (focus && focus[0]) {
        audio_dir_entry_t e[BROWSE_PAGE];
        bool found = false;
        for (int k = 0; k < total && !found;) {
            const int n = audio_dir_read(k, e, BROWSE_PAGE);
            if (n <= 0) break;
            for (int i = 0; i < n && !found; i++)
                if ((e[i].dir != 0) == focus_dir && !strcmp(e[i].name, focus)) { row = k + i + br_up(ui); found = true; }
            k += n;
        }
    }
    ui->br_first = 0; ui->br_n = 0; ui->br_cur = -1;
    br_move(ui, row, now);
    return true;
}

static void br_parent(synth_ui_t *ui, uint32_t now) {
    if (!ui->br_dir[0]) return;
    char dir[PAD_NAME_MAX], from[PAD_NAME_MAX];
    snprintf(dir, sizeof dir, "%s", ui->br_dir);
    char *s = strrchr(dir, '/');
    snprintf(from, sizeof from, "%s", s ? s + 1 : dir);
    if (s) *s = 0; else dir[0] = 0;
    if (!br_set_dir(ui, dir, from, true, now)) br_set_dir(ui, "", "", false, now);       // (its parent is gone: the root)
}

static void br_enter(synth_ui_t *ui, uint32_t now) {
    const audio_dir_entry_t *e = br_entry(ui, ui->br_cur);
    if (!e || !e->dir) return;
    if (br_up(ui) && ui->br_cur == 0) { br_parent(ui, now); return; }                    // the ".." row
    char dir[PAD_NAME_MAX + PAD_NAME_MAX];
    const int len = snprintf(dir, sizeof dir, ui->br_dir[0] ? "%s/%s" : "%s%s", ui->br_dir, e->name);
    if (len >= PAD_NAME_MAX - 2) return;                                                 // too deep: nothing in it could have a name
    br_set_dir(ui, dir, "", false, now);
}

bool browse_ui_open(synth_ui_t *ui, int what, const char *current, uint32_t now) {
    char dir[PAD_NAME_MAX] = "", base[PAD_NAME_MAX] = "";
    if (current && current[0]) {
        const char *b = pad_file_base(current);
        snprintf(base, sizeof base, "%s", b);
        char *tilde = strchr(base, '~');                                                 // a version: the list shows the file it comes from
        if (tilde) *tilde = 0;
        if (b > current) { const size_t len = (size_t)(b - current) - 1; memcpy(dir, current, len); dir[len] = 0; }
    }
    if (!br_set_dir(ui, dir, base, false, now) && !(dir[0] && br_set_dir(ui, "", "", false, now))) return false;
    ui->br_open = (uint8_t)what;
    ui->br_done = 0;
    return true;
}

bool browse_ui_file(const synth_ui_t *ui, char *out, int n) {
    const audio_dir_entry_t *e = br_entry(ui, ui->br_cur);
    if (!e || e->dir) return false;
    const int len = snprintf(out, (size_t)n, ui->br_dir[0] ? "%s/%s" : "%s%s", ui->br_dir, e->name);
    return len > 0 && len < n && len < PAD_NAME_MAX;
}

void browse_ui_event(synth_ui_t *ui, ui_event_t e, uint32_t now) {
    switch (e) {
        case UI_UP: case UI_NAV_UP: case UI_VALUE_DEC:     br_move(ui, ui->br_cur - 1, now); break;
        case UI_DOWN: case UI_NAV_DOWN: case UI_VALUE_INC: br_move(ui, ui->br_cur + 1, now); break;
        case UI_PAGE_PREV: br_move(ui, ui->br_cur - br_rows(), now); break;
        case UI_PAGE_NEXT: br_move(ui, ui->br_cur + br_rows(), now); break;
        case UI_NAV_LEFT: case UI_LEFT: br_parent(ui, now); break;
        case UI_NAV_RIGHT: case UI_RIGHT: br_enter(ui, now); break;                       // (a sample: only a push chooses it)
        case UI_LATCH: case UI_SELECT: {
            const audio_dir_entry_t *en = br_entry(ui, ui->br_cur);
            if (en && en->dir) br_enter(ui, now);
            else if (en) ui->br_done = 1;
        } break;
        case UI_BACK: ui->br_done = 2; break;
        default: break;
    }
}

void browse_ui_draw(const synth_ui_t *ui, u8g2_t *g) {
    gui_style_t style;
    gui_style_init(&style, u8g2_GetDisplayWidth(g), u8g2_GetDisplayHeight(g));
    const gui_style_t *st = &style;
    u8g2_ClearBuffer(g);
    gui_begin(g, st);
    gui_rect_t screen = gui_inset(gui_screen(g), st->margin);
    const gui_rect_t bar = gui_take_top(&screen, GUI_BAR_H);
    const gui_rect_t header = gui_rect(bar.x, bar.y, bar.w, bar.h - 1);
    u8g2_DrawHLine(g, bar.x, gui_bottom(bar) - 1, bar.w);
    // the header: the folder at the left, the highlighted row's place in it at the right
    const int count = br_count(ui), ty = gui_text_center(g, header, "B").y;
    char pos[16];
    snprintf(pos, sizeof pos, "%d/%d", count ? ui->br_cur + 1 : 0, count);
    const int pw = u8g2_GetStrWidth(g, pos);
    u8g2_DrawStr(g, header.x + header.w - 1 - pw, ty, pos);
    u8g2_SetClipWindow(g, header.x, header.y, header.x + header.w - pw - 4, header.y + header.h);
    u8g2_DrawStr(g, header.x + 1, ty, ui->br_dir[0] ? pad_file_base(ui->br_dir) : "SAMPLES");
    u8g2_SetMaxClipWindow(g);

    // the highlighted sample's overview and length
    const gui_rect_t box = gui_rect(screen.x, screen.y + 1, screen.w, BR_WAVE_H);
    const audio_dir_entry_t *cur = br_entry(ui, ui->br_cur);
    if (cur && !cur->dir && ui->br_state == BR_SHOWN && !ui->br_info.pending && ui->br_info.frames >= 4) {
        const audio_sample_info_t *in = &ui->br_info;
        const int mid = box.y + box.h / 2, amp = box.h / 2 - 1;
        for (int x = 0; x < box.w; x++) {
            const int h = in->peaks[x * AUDIO_PEAKS / box.w] * amp / 255;
            u8g2_DrawVLine(g, box.x + x, mid - h, 2 * h + 1);
        }
        char len[16];
        const uint32_t ds = (uint32_t)((uint64_t)in->frames * 10u / (in->rate ? in->rate : 1));
        snprintf(len, sizeof len, "%u.%us", (unsigned)(ds / 10), (unsigned)(ds % 10));
        const int lw = u8g2_GetStrWidth(g, len);
        u8g2_SetDrawColor(g, 0);
        u8g2_DrawBox(g, box.x + box.w - lw - 3, box.y, lw + 3, 9);
        u8g2_SetDrawColor(g, 1);
        u8g2_DrawStr(g, box.x + box.w - lw - 1, box.y + 7, len);
    } else {
        u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
        gui_draw_text_centered(g, box, !cur ? "empty folder" : cur->dir ? "folder" : ui->br_state == BR_FRESH ? "..." :
                                       ui->br_state == BR_SHOWN ? "not converted yet" : "cannot be read");
    }

    const int rows = br_rows(), y0 = box.y + box.h + 2;
    for (int i = 0; i < rows; i++) {
        const int row = ui->br_first + i;
        const audio_dir_entry_t *en = br_entry(ui, row);
        if (!en) break;
        const gui_rect_t r = gui_rect(screen.x, y0 + i * BR_ROW_H, screen.w, BR_ROW_H);
        const bool sel = row == ui->br_cur;
        if (sel) u8g2_DrawBox(g, r.x, r.y, r.w, r.h);
        u8g2_SetDrawColor(g, sel ? 0 : 1);
        char line[PAD_NAME_MAX + 2];
        snprintf(line, sizeof line, en->dir ? "%s/" : "%s", en->name);
        const int w = u8g2_GetStrWidth(g, line);
        u8g2_SetClipWindow(g, r.x + 1, r.y, r.x + r.w - 1, r.y + r.h);
        // a name too long for the row: the highlighted one shows its end (where such names differ), the others their start
        u8g2_DrawStr(g, sel && w > r.w - 4 ? r.x + r.w - 2 - w : r.x + 2, gui_text_center(g, r, "B").y, line);
        u8g2_SetMaxClipWindow(g);
        u8g2_SetDrawColor(g, 1);
    }
}
