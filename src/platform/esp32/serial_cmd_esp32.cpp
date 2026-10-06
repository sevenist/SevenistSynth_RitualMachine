#if defined(ARDUINO_ARCH_ESP32) && defined(DEV_SERIAL_CMD)
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "hal/hal_audio.h"
#include "platform/engine/engine_synth.h"
#include "platform/esp32/serial_cmd_esp32.h"
#ifdef ALT_ESP32SYNTH
#include "platform/esp32/alt_esp32synth.h"
#endif

namespace {
constexpr int kMaxTest = 8;
const int kChord[kMaxTest] = {48, 52, 55, 59, 62, 65, 69, 72};     // C E G B D F A C: eight distinct notes, so no voice retriggers another
constexpr int kMaxChord = 48;                                      // chord 9..48 adds chromatic notes above C5 (73, 74, ...): voice-count tests of the "alt" engine
int chord_note(int i) { return i < kMaxTest ? kChord[i] : kChord[kMaxTest - 1] + 1 + (i - kMaxTest); }
bool held[128];                                                    // notes started by these commands
int chord_held = 0;
app_t *g_app = nullptr;

void note_on(int n) { if (n < 0 || n > 127) return; if (held[n]) audio_note_off(n); audio_note_on(n); held[n] = true; }
void note_off(int n) { if (n < 0 || n > 127 || !held[n]) return; audio_note_off(n); held[n] = false; }
void release_all() { for (int n = 0; n < 128; n++) note_off(n); chord_held = 0; }

// A new rack in the running application: the UI forgets its cursor, the synth is rebuilt (what leaving the menu does).
void use_rack() {
    release_all();
    synth_ui_init(&g_app->ui, &g_app->rack);
    audio_build(&g_app->rack, &g_app->params);
    g_app->dirty = true;
}

void run(char *line) {
    char *arg = strchr(line, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    const int v = arg ? atoi(arg) : 0;
    if (!strcmp(line, "ping")) { Serial.println("[CMD] pong"); return; }
    if (!strcmp(line, "on")) { note_on(v); Serial.printf("[CMD] on %d\n", v); return; }
    if (!strcmp(line, "off")) { note_off(v); Serial.printf("[CMD] off %d\n", v); return; }
    if (!strcmp(line, "release")) { release_all(); Serial.println("[CMD] released"); return; }
    if (!strcmp(line, "chord")) {
        const int k = v < 0 ? 0 : (v > kMaxChord ? kMaxChord : v);
        release_all();
        for (int i = 0; i < k; i++) note_on(chord_note(i));
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
    if (!strcmp(line, "samples")) {
        audio_sample_info_t in;
        const int n = audio_sample_count();
        for (int i = 0; i < n; i++) if (audio_sample_info(i, &in)) Serial.printf("[CMD] sample %d %s %u frames %u Hz root %d loop %u..%u mode %d\n", i, in.name, (unsigned)in.frames, (unsigned)in.rate, in.root, (unsigned)in.loop_start, (unsigned)in.loop_end, in.loop_mode);
        Serial.printf("[CMD] %d sample(s)\n", n);
        return;
    }
    if (!strcmp(line, "patch") && g_app) {
        char name[16] = {};
        int a = 0, b = 0;
        sscanf(arg ? arg : "", "%15s %d %d", name, &a, &b);
        if (!strcmp(name, "startup")) rack_init_startup(&g_app->rack);
        else if (!strcmp(name, "sampler")) rack_init_sampler(&g_app->rack, a, b);
        else if (!strcmp(name, "strings")) g_app->rack.cfg.type = SYNTH_STRINGS;          // the Strings type, current pages and effects (ADR-037)
        else { Serial.printf("[CMD] unknown patch '%s'\n", name); return; }
        use_rack();
        Serial.printf("[CMD] patch %s %d %d\n", name, a, b);
        return;
    }
    if (!strcmp(line, "mode") && g_app) {                 // mode mono | poly [glide_index] [legato 0/1]: the Mono or Poly type of the current synth (a rebuild)
        char name[8] = {};
        int glide = g_app->rack.cfg.glide, legato = g_app->rack.cfg.legato;
        sscanf(arg ? arg : "", "%7s %d %d", name, &glide, &legato);
        const bool fm = synth_type_is_fm(g_app->rack.cfg.type);
        if (!strcmp(name, "mono")) g_app->rack.cfg.type = fm ? SYNTH_FM_MONO : SYNTH_MOD_MONO;
        else if (!strcmp(name, "poly")) g_app->rack.cfg.type = fm ? SYNTH_FM : SYNTH_MODULAR;
        else { Serial.printf("[CMD] mode mono|poly [glide 0-6] [legato 0|1]\n"); return; }
        g_app->rack.cfg.glide = (uint8_t)glide;
        g_app->rack.cfg.legato = (uint8_t)legato;
        use_rack();
        Serial.printf("[CMD] mode %s, %d voice(s)\n", name, synth_config_voices(&g_app->rack.cfg));
        return;
    }
    if (!strcmp(line, "voices") && g_app) {
        g_app->rack.cfg.voices = (uint8_t)(v < 1 ? 1 : (v > SYNTH_MAX_VOICES ? SYNTH_MAX_VOICES : v));
        use_rack();
        Serial.printf("[CMD] voices %d\n", g_app->rack.cfg.voices);
        return;
    }
#ifdef ALT_ESP32SYNTH
    if (!strcmp(line, "alt")) {                           // alt on | off: ESP32Synth instead of our engine (off rebuilds ours); other words: alt_command
        if (arg && !strcmp(arg, "on")) { release_all(); alt_set_active(true); }
        else if (arg && !strcmp(arg, "off")) { release_all(); alt_set_active(false); if (g_app) use_rack(); }
        else alt_command(arg);
        return;
    }
#endif
    if (!strcmp(line, "status")) { Serial.printf("[CMD] status chord %d, uptime %lu ms, free heap %u\n", chord_held, (unsigned long)millis(), (unsigned)ESP.getFreeHeap()); return; }
    Serial.printf("[CMD] unknown '%s'\n", line);
}
}  // namespace

extern "C" void serial_cmd_attach(app_t *app) { g_app = app; }

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
