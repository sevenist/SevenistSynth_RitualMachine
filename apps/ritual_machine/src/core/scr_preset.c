// The PRESETS tab of the menu: the sound that plays is a working copy (it is saved by itself and comes back at power-on); a preset is a
// file of the card's presets folder (core/patch.h). Load opens the browser on that folder (the highlighted preset plays under the keys,
// a push keeps it, Back brings the sound back); Save writes the sound to the preset it came from; Save as makes a new one next to it
// ("Preset N": rename and sort the files on a computer); Init starts a new sound. The box on the right names the preset and says
// whether the sound was edited since. The app does the work (app/app_preset.c): a button only raises ui->preset_req.
// A declarative screen (see ui_screen.h).
#include "core/ui_screen.h"
#include <stdio.h>
#include <string.h>

static void request(const ui_ctx_t *c) { c->ui->preset_req = (uint8_t)c->arg; }

// The screen is data only, with no layout() (ui_screen.h, widget geometry): the buttons are a column of rows in the left half (the first one in the
// area, each next one below the one before it), the box on the right is a static widget that fills what is left of the area.
#define COLUMN_FIRST {PL_IN, 0, {0, UN_PX, 0}, {2, UN_PX, 0}, {50, UN_PCT, -1}, {1, UN_ROW, 0}}
#define COLUMN_NEXT  {PL_BELOW, 0, {0, UN_PX, 0}, {1, UN_GAP, 0}, {0, UN_SAME, 0}, {1, UN_ROW, 0}}
#define INFO_BOX     {PL_IN, 0, {50, UN_PCT, 2}, {0, UN_PX, 0}, {1, UN_FILL, 0}, {1, UN_FILL, 0}}

static void info_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t box, bool focused, bool latched);

static const el_def_t elements[] = {
    {"Load",    EL_BUTTON, 0, 0, 1, NULL, NULL, request, NULL, NULL, NULL, PRESET_REQ_LOAD,    0, COLUMN_FIRST},
    {"Save",    EL_BUTTON, 1, 0, 1, NULL, NULL, request, NULL, NULL, NULL, PRESET_REQ_SAVE,    0, COLUMN_NEXT},
    {"Save as", EL_BUTTON, 2, 0, 1, NULL, NULL, request, NULL, NULL, NULL, PRESET_REQ_SAVE_AS, 0, COLUMN_NEXT},
    {"Init",    EL_BUTTON, 3, 0, 1, NULL, NULL, request, NULL, NULL, NULL, PRESET_REQ_INIT,    0, COLUMN_NEXT},
    {"",        EL_VALUE,  0, 0, 1, NULL, NULL, NULL,    info_draw, NULL, NULL, 0, EF_STATIC,  INFO_BOX},
};
#define N_ELEMENTS ((int)(sizeof elements / sizeof elements[0]))

// One line of the box, cut at its edges (a long name shows its start).
static void box_line(u8g2_t *g, gui_rect_t box, int y, const char *s) {
    const int w = u8g2_GetStrWidth(g, s);
    u8g2_SetClipWindow(g, box.x + 2, box.y + 1, box.x + box.w - 2, box.y + box.h - 1);
    u8g2_DrawStr(g, w <= box.w - 4 ? box.x + (box.w - w) / 2 : box.x + 2, y, s);
    u8g2_SetMaxClipWindow(g);
}

static void info_draw(u8g2_t *g, const gui_style_t *st, const ui_ctx_t *c, gui_rect_t box, bool focused, bool latched) {
    (void)focused; (void)latched;
    const int rh = gui_row_h(g, st);
    u8g2_DrawFrame(g, box.x, box.y, box.w, box.h);
    const char *name = c->ui->preset_name;
    if (!name[0]) {
        box_line(g, box, box.y + rh + 1, "new sound");
        box_line(g, box, box.y + 2 * rh + 3, "not saved");
        return;
    }
    char dir[PAD_NAME_MAX];
    const char *base = pad_file_base(name);
    snprintf(dir, sizeof dir, "%.*s", (int)(base > name ? base - name - 1 : 0), name);
    box_line(g, box, box.y + rh + 1, base);
    if (dir[0]) box_line(g, box, box.y + 2 * rh + 3, dir);
    box_line(g, box, box.y + 3 * rh + 5, c->ui->preset_edited ? "edited" : "saved");
}

const screen_def_t scr_preset_screen = {elements, N_ELEMENTS, true, false, NULL, NULL, NULL, NULL};
