#ifdef PLATFORM_SIM
#include "hal/hal_leds.h"

// The simulator has no LEDs yet (the Panel window could draw them under the keys later): everything is a no-op.
void leds_init(void) {}
void leds_set_brightness(uint8_t level) { (void)level; }
void leds_clear(void) {}
void leds_set(control_id_t ctl, led_color_t color) { (void)ctl; (void)color; }
void leds_set_all(led_color_t color) { (void)color; }
void leds_show(void) {}
#endif // PLATFORM_SIM
