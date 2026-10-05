#if defined(ARDUINO_ARCH_ESP32) && defined(DEV_SERIAL_CMD)
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "hal/hal_audio.h"
#include "platform/engine/engine_synth.h"
#include "platform/esp32/serial_cmd_esp32.h"

namespace {
constexpr int kMaxTest = 8;
const int kChord[kMaxTest] = {48, 52, 55, 59, 62, 65, 69, 72};     // C E G B D F A C: eight distinct notes, so no voice retriggers another
bool held[128];                                                    // notes started by these commands
int chord_held = 0;

void note_on(int n) { if (n < 0 || n > 127) return; if (held[n]) audio_note_off(n); audio_note_on(n); held[n] = true; }
void note_off(int n) { if (n < 0 || n > 127 || !held[n]) return; audio_note_off(n); held[n] = false; }
void release_all() { for (int n = 0; n < 128; n++) note_off(n); chord_held = 0; }

void run(char *line) {
    char *arg = strchr(line, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    const int v = arg ? atoi(arg) : 0;
    if (!strcmp(line, "ping")) { Serial.println("[CMD] pong"); return; }
    if (!strcmp(line, "on")) { note_on(v); Serial.printf("[CMD] on %d\n", v); return; }
    if (!strcmp(line, "off")) { note_off(v); Serial.printf("[CMD] off %d\n", v); return; }
    if (!strcmp(line, "release")) { release_all(); Serial.println("[CMD] released"); return; }
    if (!strcmp(line, "chord")) {
        const int k = v < 0 ? 0 : (v > kMaxTest ? kMaxTest : v);
        release_all();
        for (int i = 0; i < k; i++) note_on(kChord[i]);
        chord_held = k;
        Serial.printf("[CMD] chord %d\n", k);
        return;
    }
    if (!strcmp(line, "eng")) {                       // eng a b c d: the oscillator engines (0 karp 1 modal 2 fm2 3 fold 4 ssaw 5 vowel 6 add 7 dust) of the first oscillators
        int idx = 0;
        const char *q = arg;
        while (q && *q && idx < 4) {
            engine_synth_set_osc_engine(idx++, atoi(q));
            q = strchr(q, ' ');
            if (q) while (*q == ' ') q++;
        }
        Serial.printf("[CMD] eng set for %d oscillators\n", idx);
        return;
    }
    if (!strcmp(line, "status")) { Serial.printf("[CMD] status chord %d, uptime %lu ms, free heap %u\n", chord_held, (unsigned long)millis(), (unsigned)ESP.getFreeHeap()); return; }
    Serial.printf("[CMD] unknown '%s'\n", line);
}
}  // namespace

extern "C" void serial_cmd_poll(void) {
    static char buf[48];
    static int n = 0;
    while (Serial.available() > 0) {
        const int ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            buf[n] = 0;
            if (n) run(buf);
            n = 0;
        } else if (n < static_cast<int>(sizeof buf) - 1) {
            buf[n++] = static_cast<char>(ch);
        }
    }
}
#endif
