#pragma once
// HAL: display. The platform owns and configures the u8g2 instance (bus, pins,
// controller); the application only ever receives a ready-to-draw u8g2_t*.
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

// Size of the display in pixels (a multiple of 8). The application reads the real size from the u8g2 instance
// (u8g2_GetDisplayWidth / Height) and lays itself out from it (gui_style_init), so changing the screen means changing these two
// numbers (or passing -DDISPLAY_WIDTH=128 -DDISPLAY_HEIGHT=128 to the build) and the platform's display driver; the UI code stays as it is.
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH  128
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 128
#endif

// Creates/initialises the display and returns it. Never returns NULL.
u8g2_t *display_init(void);

// Shows the frame buffer the application has drawn (in place of u8g2_SendBuffer). The platform may copy it and send it later from its own
// task (the prototype: only the changed tiles, so a slow bus never holds up notes and knobs); the caller may draw the next frame at once.
void display_send(u8g2_t *g);

#ifdef __cplusplus
}
#endif
