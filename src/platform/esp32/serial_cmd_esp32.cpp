#if defined(ARDUINO_ARCH_ESP32) && defined(DEV_SERIAL_CMD)
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "hal/hal_audio.h"
#include "platform/engine/engine_synth.h"
#include "platform/esp32/serial_cmd_esp32.h"
#ifdef HWV1
#include "platform/esp32/leds_esp32.h"
#endif

extern "C" void audio_dev_dump(int n);

namespace {
constexpr int kMaxTest = 8;
const int kChord[kMaxTest] = {48, 52, 55, 59, 62, 65, 69, 72};     // C E G B D F A C: eight distinct notes, so no voice retriggers another
constexpr int kMaxChord = 48;                                      // chord 9..48 adds chromatic notes above C5 (73, 74, ...): voice-count tests (Strings, 32 voices)
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
        else if (!strcmp(name, "para")) {                 // patch para N V: N Strng oscillators (spread in detune / octave) into one filter, Mod Para, V voices
            const int n = a < 1 ? 4 : (a > 8 ? 8 : a);
            rack_clear(&g_app->rack);
            for (int i = 0; i < n; i++) {
                rack_insert(&g_app->rack, i, MOD_OSC);
                rack_slot_t &s = g_app->rack.slot[i];
                s.v[MP_OC_WAVE] = (float)(OC_FIRST_ENGINE + 8);
                s.v[MP_OC_PW] = 0.3f + 0.1f * (float)(i % 4);                // detune 15 .. 30 cents
                s.v[MP_OC_MORPH] = 0;                                        // saw
                s.v[MP_OC_COARSE] = (float)((i % 3 == 2) ? 12 : 0);
                s.v[MP_OC_QUAL] = 1;                                         // Mip
            }
            rack_insert(&g_app->rack, n, MOD_FILTER);
            g_app->rack.cfg.type = SYNTH_MOD_PARA;
            g_app->rack.cfg.voices = (uint8_t)(b < 1 ? 4 : (b > SYNTH_MAX_VOICES ? SYNTH_MAX_VOICES : b));
        }
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
        else if (!strcmp(name, "para")) { g_app->rack.cfg.type = SYNTH_MOD_PARA; g_app->rack.cfg.para_env = (uint8_t)(glide < 0 || glide > 2 ? 0 : glide); }   // mode para [env 0 legato / 1 retrig / 2 voice]
        else { Serial.printf("[CMD] mode mono|poly [glide 0-6] [legato 0|1] | para [env 0-2]\n"); return; }
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
    if (!strcmp(line, "str") && g_app) {                  // str <field> <value>: Strings settings for tests (wave osc det mix lvl lp ftype), "str fx 0" = every FX slot None
        char f[8] = {};
        float x = 0;
        sscanf(arg ? arg : "", "%7s %f", f, &x);
        str_params_t &s = g_app->params.str;
        if (!strcmp(f, "wave")) s.wave = (uint8_t)x;
        else if (!strcmp(f, "osc")) s.osc = (uint8_t)x;
        else if (!strcmp(f, "det")) s.detune = x;
        else if (!strcmp(f, "mix")) s.mix = x;
        else if (!strcmp(f, "lvl")) s.level = x;
        else if (!strcmp(f, "lp")) s.lp_on = (uint8_t)x;
        else if (!strcmp(f, "ftype")) s.ftype = (uint8_t)x;
        else if (!strcmp(f, "fx")) { for (int k = 0; k < FXR_SLOTS; k++) fxr_set_type(&g_app->rack.cfg.fxr.slot[k], FX_NONE); }
        else { Serial.println("[CMD] str wave|osc|det|mix|lvl|lp|ftype|fx <value>"); return; }
        audio_build(&g_app->rack, &g_app->params);
        Serial.printf("[CMD] str %s %g\n", f, (double)x);
        return;
    }
#ifdef HWV1
    if (!strcmp(line, "leds")) {                          // leds N: only chain LED N lit (red) for 5 s, leds all: every LED white; to check the chain order
        if (arg && (!strcmp(arg, "off") || !strcmp(arg, "on"))) { leds_esp32_enable(!strcmp(arg, "on")); Serial.printf("[CMD] leds %s\n", arg); return; }
        const bool all = arg && !strcmp(arg, "all");
        leds_esp32_test(all ? -1 : v, 5000);
        Serial.printf("[CMD] leds %s\n", all ? "all" : arg ? arg : "0");
        return;
    }
#endif
    if (!strcmp(line, "dump")) { audio_dev_dump(v); return; }       // dump N: the next N output samples as "[DUMP]" lines (audio_esp32.cpp)
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
