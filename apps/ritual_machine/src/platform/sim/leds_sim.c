#ifdef PLATFORM_SIM
#include "hal/hal_leds.h"
#include "platform/sim/leds_sim.h"

// The simulator's LEDs: one colour per control, drawn by the Panel window as a strip along the top of each key (panel_sim.c). The colours are
// shown as the application sets them (full intensity, readable on a monitor); the global brightness is ignored.
static led_color_t frame[CTL_COUNT];

void leds_init(void) { leds_clear(); }
void leds_set_brightness(uint8_t level) { (void)level; }
void leds_clear(void) { for (int i = 0; i < CTL_COUNT; i++) frame[i] = (led_color_t){0, 0, 0}; }
void leds_set(control_id_t ctl, led_color_t color) { if (ctl >= 0 && ctl < CTL_COUNT) frame[ctl] = color; }
void leds_set_all(led_color_t color) { for (int i = 0; i < CTL_COUNT; i++) frame[i] = color; }
void leds_show(void) {}                                   // the panel reads the frame when it renders
led_color_t leds_sim_get(control_id_t ctl) { return ctl >= 0 && ctl < CTL_COUNT ? frame[ctl] : (led_color_t){0, 0, 0}; }
#endif // PLATFORM_SIM
