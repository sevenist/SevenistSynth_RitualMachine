#pragma once
// Dev-only micro benchmark of the DSP inner loops on the chip (see bench_esp32.cpp). Only built with -DHWV1_BENCH.
#ifdef HWV1_BENCH
void bench_run(void);
#endif
