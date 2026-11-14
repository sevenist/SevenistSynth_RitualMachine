#pragma once
// Desktop only: a window for a desktop program that draws a large screen of its own (a tool, not a simulated board). It shows a colour
// frame (RGB565, ui/canvas.h) at a whole zoom, and reports the mouse and the keyboard as the widgets' input (ui/widgets.h: wg_input_t),
// in the frame's pixels. The frame follows the window: when the window is resized or goes full screen (F11, or Alt+Enter: the window
// handles them), the zoom becomes the largest that leaves the frame at least its smallest size, and the frame as large as the window
// divided by it, so the picture fills the window with no border. The app asks the frame's size each time it draws (desk_window_frame).
// The board's own windows (display_sim.c, panel_sim.c) are not used with it.
#ifdef PLATFORM_SIM
#include <stdbool.h>
#include <stdint.h>
#include "ui/widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opens the window, at the largest whole zoom of a w x h frame that fits the monitor. The frame is never smaller than w x h (the window's
// smallest size) and never larger than max_w x max_h (the app's canvas; past it the window shows a dark margin). False: no window.
bool desk_window_open(const char *title, int w, int h, int max_w, int max_h);
void desk_window_close(void);
void desk_window_title(const char *title);
void desk_window_frame(int *w, int *h);     // the frame's size now: draw this much (it changes when the window does)
void desk_window_fullscreen(bool on);
bool desk_window_is_fullscreen(void);
// Shows a frame of desk_window_frame's size, its rows `stride` pixels apart (a canvas wider than the frame: its width).
void desk_window_present(const uint16_t *rgb565, int stride);
// The input since the last call. Waits up to wait_ms for something to happen (0: returns at once). in->quit when the window was closed.
void desk_window_poll(wg_input_t *in, int wait_ms);

#ifdef __cplusplus
}
#endif
#endif  // PLATFORM_SIM
