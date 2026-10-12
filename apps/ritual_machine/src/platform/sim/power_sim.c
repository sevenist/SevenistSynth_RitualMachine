#ifdef PLATFORM_SIM
#include "hal/hal_power.h"

// The simulator runs on the computer's supply: no battery.
int power_battery_percent(void) { return -1; }
int power_battery_mv(void) { return 0; }
#endif
