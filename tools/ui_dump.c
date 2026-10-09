// Renders the application's screens to ASCII from the u8g2 buffer, without SDL and without audio: drives app_step()
// with input events and prints the 128x64 frame. Useful to check layouts and to catch drawing crashes.
//
//   gcc -O1 -w -Isrc -Ilib/u8g2/csrc tools/ui_dump.c src/core/*.c lib/u8g2/csrc/*.c -lm -o build/ui_dump.exe
//   build/ui_dump.exe "menu right right"        (events: see ev() below)
//   several arguments print one frame per argument (a script each, continuing the same session)
//
// UI_DUMP_SIZE=128x128 in the environment renders on a 128 x 128 screen (the width is fixed at 128 here).
//
// With no argument it shows the main view, then every menu tab of the modular synth and of the FM synth.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/app.h"
#include "hal/hal_audio.h"
#include "hal/hal_display.h"
#include "hal/hal_leds.h"
#include "hal/hal_storage.h"

#include "ui_stubs.c"            // the platform stand-ins (no audio, no card), shared with the UI tests

static u8g2_t disp;

static void dump(const char *title) {
    printf("---- %s\n", title);
    const uint8_t *buf = u8g2_GetBufferPtr(&disp);
    for (int y = 0; y < u8g2_GetDisplayHeight(&disp); y++) {
        for (int x = 0; x < 128; x++) {
            int page = y / 8, bit = y % 8;
            putchar((buf[page * 128 + x] >> bit) & 1 ? '#' : '.');
        }
        putchar('\n');
    }
}

// Event names: the hardware controls, by the role they have in the default binding table.
//   up down left right (joystick directions)  latch (joystick push)  menu (BTN 1)  back (BTN 2)  play  shift / unshift (BTN 3 down / up)
//   encA:+1  encB:-2 (encoder turns)  encAsw encBsw (encoder pushes)  knob:<ctl number>:<0..1023>  key:<row>.<col>  keyup:<row>.<col>
//   popup:info popup:error popup:ask (raise a test popup, core/popup.h)
static input_event_t ev(const char *name) {
    input_event_t e = {CTL_NONE, IN_NONE, 0, false};
    int a = 0, b = 0, c = 0;
    if (!strcmp(name, "menu")) { e.ctl = CTL_BTN_1; e.kind = IN_PRESS; }
    else if (!strcmp(name, "up")) { e.ctl = CTL_JOY_UP; e.kind = IN_PRESS; }
    else if (!strcmp(name, "down")) { e.ctl = CTL_JOY_DOWN; e.kind = IN_PRESS; }
    else if (!strcmp(name, "left")) { e.ctl = CTL_JOY_LEFT; e.kind = IN_PRESS; }
    else if (!strcmp(name, "right")) { e.ctl = CTL_JOY_RIGHT; e.kind = IN_PRESS; }
    else if (!strcmp(name, "latch")) { e.ctl = CTL_JOY_SW; e.kind = IN_PRESS; }
    else if (!strcmp(name, "back")) { e.ctl = CTL_BTN_2; e.kind = IN_PRESS; }
    else if (!strcmp(name, "play")) { e.ctl = CTL_PLAY; e.kind = IN_PRESS; }
    else if (!strcmp(name, "shift")) { e.ctl = CTL_BTN_3; e.kind = IN_PRESS; }
    else if (!strcmp(name, "unshift")) { e.ctl = CTL_BTN_3; e.kind = IN_RELEASE; }
    else if (!strcmp(name, "encAsw")) { e.ctl = CTL_ENC_A_SW; e.kind = IN_PRESS; }
    else if (!strcmp(name, "encBsw")) { e.ctl = CTL_ENC_B_SW; e.kind = IN_PRESS; }
    else if (!strncmp(name, "encA:", 5)) { e.ctl = CTL_ENC_A; e.kind = IN_DELTA; e.value = atoi(name + 5); }
    else if (!strncmp(name, "encB:", 5)) { e.ctl = CTL_ENC_B; e.kind = IN_DELTA; e.value = atoi(name + 5); }
    else if (sscanf(name, "knob:%d:%d", &a, &b) == 2) { e.ctl = (control_id_t)a; e.kind = IN_VALUE; e.value = b; }
    else if (sscanf(name, "key:%d.%d", &a, &b) == 2) { e.ctl = (control_id_t)CTL_KEY(a, b); e.kind = IN_PRESS; }
    else if (sscanf(name, "keyup:%d.%d", &a, &b) == 2) { e.ctl = (control_id_t)CTL_KEY(a, b); e.kind = IN_RELEASE; }
    (void)c;
    return e;
}

static void feed(app_t *app, const char *script) {
    char buf[512];
    strncpy(buf, script, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) {
        if (!strcmp(t, "popup:info"))  { popup_info(&app->popup, "SD CARD", "Card inserted", 0, audio_millis(), 2000); app->redraw_owed = true; }
        else if (!strcmp(t, "popup:error")) { popup_error(&app->popup, NULL, "The card could not be read. Check that it is inserted.", NULL, NULL); app->redraw_owed = true; }
        else if (!strcmp(t, "popup:ask"))   { popup_ask(&app->popup, "SD CARD", "The card is not formatted. Format it?", false, NULL, NULL); app->redraw_owed = true; }
        app_step(app, ev(t));
    }
}

int main(int argc, char **argv) {
    const char *size = getenv("UI_DUMP_SIZE");                         // "128x128" renders on a taller screen
    if (size && !strcmp(size, "128x128")) u8g2_Setup_sh1107_128x128_f(&disp, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
    else u8g2_Setup_ssd1306_i2c_128x64_noname_f(&disp, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetFont(&disp, u8g2_font_5x7_tr);
    static app_t app;
    app_init(&app, &disp);
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            feed(&app, argv[i]);
            printf("[page %d of %d: slot %d def %d, row %d] octave %d shift %d latched %d status \"%s\" slot0 v: %g %g %g %g\n", app.ui.page, app.ui.page_count, app.ui.pg_slot[app.ui.page], app.ui.pg_def[app.ui.page], app.ui.row,
                   app.in.octave, app.in.shift, app.ui.latched, app.status, app.rack.slot[0].v[0], app.rack.slot[0].v[1], app.rack.slot[0].v[2], app.rack.slot[0].v[3]);
            dump(argv[i]);
        }
        return 0;
    }

    dump("main view");
    feed(&app, "menu");
    dump("modular: RACK (branches 1 and 2, row M)");
    feed(&app, "up right");                  // row 0, next tab
    dump("modular: GENERAL");
    feed(&app, "right");
    dump("modular: SAMPLES");
    feed(&app, "right");
    dump("modular: KEYS");
    feed(&app, "right");
    dump("modular: LEDS");
    feed(&app, "left left left");           // back to GENERAL, switch to the FM synth
    feed(&app, "down right up");            // Type: Modular -> FM
    dump("after Type -> FM (menu tab list changes)");
    feed(&app, "right");
    dump("FM: ALGORITHM (the operator tree)");
    feed(&app, "left left");
    dump("FM: RACK (row M only)");
    feed(&app, "menu");
    dump("main view in FM mode (FM SYNTH)");
    feed(&app, "right");
    dump("FM: OP1 page");
    feed(&app, "right");
    dump("FM: OP1 ENV page");
    return 0;
}
