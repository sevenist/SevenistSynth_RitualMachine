// Dev serial commands (DEV_SERIAL_CMD). Every command, its options and examples: human_docs/SERIAL_COMMANDS.md (keep it in step when adding one).
#if defined(ARDUINO_ARCH_ESP32) && defined(DEV_SERIAL_CMD)
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "hal/hal_audio.h"
#include "hal/hal_storage.h"
#include "platform/engine/engine_synth.h"
#include "engine/modules/mi_osc.h"
#include "engine/dsp/fft.h"
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include "core/ui_settings.h"
#include "core/pads.h"
#include "platform/esp32/serial_cmd_esp32.h"
#include "platform/esp32/sd_card.h"
#include "engine/offline/process.h"
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
    ui_settings_load(&g_app->ui, &g_app->rack);         // the jump slots and Shift targets stay (the autosave would write the reset ones)
    audio_build(&g_app->rack, &g_app->params);
    g_app->dirty = true;
}

void clear_row_m() {                                                // every module of row M goes (the effects of every synth type, ADR-041)
    for (int i = g_app->rack.count - 1; i >= 0; i--) if (g_app->rack.slot[i].row == ROW_M) rack_delete(&g_app->rack, i);
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
    if (!strcmp(line, "pieconv")) {                       // pieconv: the Convolver's PIE dot products (pie_conv2_s16) against the C sums, and their cost
        int16_t *xl = static_cast<int16_t *>(heap_caps_aligned_alloc(16, 3 * 520 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!xl) { Serial.println("[CMD] pieconv: no memory"); return; }
        int16_t *xr = xl + 520, *t = xr + 520;
        uint32_t seed = 12345;
        auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return static_cast<int16_t>(seed >> 16); };
        int bad = 0, cases = 0;
        uint32_t cyc = UINT32_MAX;
        for (int rep = 0; rep < 6; rep++) {
            for (int i = 0; i < 520; i++) {                   // full scale both ways first (taps never -32768: the module clamps them), then random
                xl[i] = rep == 0 ? -32768 : rep == 1 ? 32767 : rnd();
                xr[i] = rep == 0 ? 32767 : rep == 1 ? -32768 : rnd();
                t[i] = rep == 0 ? 32767 : rep == 1 ? -32767 : rnd();
                if (t[i] == -32768) t[i] = -32767;
            }
            for (int nv = 1; nv <= 64; nv++) {                // up to the module's 512 taps (a full-scale sum of more would pass 40 bits)
                alignas(16) int32_t acc[4];
                const uint32_t t0 = esp_cpu_get_cycle_count();
                pie_conv2_s16(xl, xr, t, nv, acc);
                const uint32_t dt = esp_cpu_get_cycle_count() - t0;
                if (nv == 16 && dt < cyc) cyc = dt;
                int64_t cl = 0, cr = 0;
                for (int j = 0; j < 8 * nv; j++) { cl += static_cast<int32_t>(t[j]) * xl[j]; cr += static_cast<int32_t>(t[j]) * xr[j]; }
                const int64_t pl = static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(acc[1] & 0xff))) << 32 | static_cast<uint32_t>(acc[0]));
                const int64_t pr = static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(acc[3] & 0xff))) << 32 | static_cast<uint32_t>(acc[2]));
                bad += (pl != cl) + (pr != cr);
                cases += 2;
            }
        }
        heap_caps_free(xl);
        Serial.printf("[CMD] pieconv: %d of %d sums differ from C (full scale, 1..64 vectors); 128 taps x 2 channels: %u cycles\n", bad, cases, (unsigned)cyc);
        return;
    }
    if (!strcmp(line, "samples")) {                       // the samples of the library's root folder join the catalog (the test tools pick from it), then the catalog
        const int total = audio_dir_open("");
        for (int k = 0; k < total; k++) {
            audio_dir_entry_t e;
            if (audio_dir_read(k, &e, 1) == 1 && !e.dir && audio_sample_use(e.name) == -2) break;   // the catalog is full
        }
        audio_sample_info_t in;
        const int n = audio_sample_count();
        for (int i = 0; i < n; i++) if (audio_sample_info(i, &in)) Serial.printf("[CMD] sample %d %s %u frames %u Hz root %d loop %u..%u mode %d\n", i, in.name, (unsigned)in.frames, (unsigned)in.rate, in.root, (unsigned)in.loop_start, (unsigned)in.loop_end, in.loop_mode);
        Serial.printf("[CMD] %d sample(s)\n", n);
        return;
    }
    if (!strcmp(line, "dir")) {                           // dir [PATH] | dir #N: reads a folder of the library as the browser does ("" = the samples folder), timed; its first entries
        const char *path = arg ? arg : "";
        if (!strcmp(path, "-")) { audio_dir_close(); Serial.println("[CMD] dir closed"); return; }   // dir -: forgets the list (the next dir reads the card)
        if (path[0] == '#') {                             // dir #N: entry N of the folder listed last (nothing is read, nothing joins the catalog)
            audio_dir_entry_t e;
            if (audio_dir_read(atoi(path + 1), &e, 1) == 1) Serial.printf("[CMD] entry %d \"%s\"%s\n", atoi(path + 1), e.name, e.dir ? " (a folder)" : "");
            else Serial.printf("[CMD] entry %d: none\n", atoi(path + 1));
            return;
        }
        const uint32_t t0 = micros();
        const int n = audio_dir_open(path);
        const uint32_t us = micros() - t0;
        Serial.printf("[CMD] dir \"%s\": %d entries in %u us\n", path, n, (unsigned)us);
        for (int k = 0; k < n && k < 6; k++) {
            audio_dir_entry_t e;
            if (audio_dir_read(k, &e, 1) == 1) Serial.printf("[CMD]   %d %s%s\n", k, e.name, e.dir ? "/" : "");
        }
        return;
    }
    if (!strcmp(line, "use")) {                           // use NAME | use #N: a sample into the catalog by name (its path below the samples folder), timed; use alone: the catalog
        if (!arg || !*arg) {
            audio_sample_info_t in;
            int used = 0;
            const int n = audio_sample_count();
            for (int i = 0; i < n; i++) if (audio_sample_info(i, &in)) { Serial.printf("[CMD] cat %d \"%s\"\n", i, in.name); used += in.name[0] != 0; }
            Serial.printf("[CMD] catalog: %d in use of %d entries (max %d)\n", used, n, AUDIO_SAMPLES_MAX);
            return;
        }
        audio_dir_entry_t e;
        const char *name = arg;
        if (arg[0] == '#') {                              // use #N: entry N of the folder `dir` listed last (a root-folder sample)
            if (audio_dir_read(atoi(arg + 1), &e, 1) != 1 || e.dir) { Serial.printf("[CMD] use %s: no such sample in the list\n", arg); return; }
            name = e.name;
        }
        const uint32_t t0 = micros();
        const int index = audio_sample_use(name);
        const uint32_t us = micros() - t0;
        Serial.printf("[CMD] use \"%s\": index %d in %u us\n", name, index, (unsigned)us);   // -1 no such sample, -2 the catalog is full
        return;
    }
    if (!strcmp(line, "patch") && g_app) {
        char name[16] = {};
        int a = 0, b = 0;
        sscanf(arg ? arg : "", "%15s %d %d", name, &a, &b);
        if (!strcmp(name, "startup")) rack_init_startup(&g_app->rack);
        else if (!strcmp(name, "sampler")) rack_init_sampler(&g_app->rack, a, b);
        else if (!strcmp(name, "strings")) g_app->rack.cfg.type = SYNTH_STRINGS;          // the Strings type, current pages and effects (ADR-037)
        else if (!strcmp(name, "para")) {                 // patch para N V: N Strng oscillators (spread in detune / octave) into one filter with Para on, V voices
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
            rack_set_para(&g_app->rack, n, true);
            g_app->rack.cfg.voices = (uint8_t)(b < 1 ? 4 : (b > SYNTH_MAX_VOICES ? SYNTH_MAX_VOICES : b));
        }
        else { Serial.printf("[CMD] unknown patch '%s'\n", name); return; }
        use_rack();
        Serial.printf("[CMD] patch %s %d %d\n", name, a, b);
        return;
    }
    if (!strcmp(line, "mode") && g_app) {                 // mode mono | poly [glide_index] [legato 0/1] | para [env]: Voices 1 / several (ADR-041), or Para on branch 1's first filter (a rebuild)
        char name[8] = {};
        int glide = g_app->rack.cfg.glide, legato = g_app->rack.cfg.legato;
        sscanf(arg ? arg : "", "%7s %d %d", name, &glide, &legato);
        rack_t &rk = g_app->rack;
        if (!strcmp(name, "mono")) rk.cfg.voices = 1;
        else if (!strcmp(name, "poly")) { if (rk.cfg.voices < 2) rk.cfg.voices = SYNTH_MAX_VOICES; }
        else if (!strcmp(name, "para")) {                 // mode para [env 0 legato / 1 retrig / 2 voice]
            int f = RACK_NONE;
            for (int i = 0; i < rk.count && f == RACK_NONE; i++) if (rk.slot[i].type == MOD_FILTER && rk.slot[i].row == 0) f = i;
            if (f == RACK_NONE || !rack_set_para(&rk, f, true)) { Serial.println("[CMD] mode para: no filter in branch 1 that takes Para"); return; }
            rk.slot[f].penv = (uint8_t)(glide < 0 || glide > 2 ? 0 : glide);
            glide = rk.cfg.glide;
            if (rk.cfg.voices < 2) rk.cfg.voices = SYNTH_MAX_VOICES;
        }
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
    if (!strcmp(line, "popup") && g_app) {                // popup info | error | ask: raises a test popup (core/popup.h); the answer is printed
        const char *k = arg ? arg : "";
        auto answer = [](void *, bool yes) { Serial.printf("[CMD] popup answer %s\n", yes ? "yes" : "no"); };
        if (!strcmp(k, "error")) popup_error(&g_app->popup, nullptr, "The card could not be read. Check that it is inserted.", answer, nullptr);
        else if (!strcmp(k, "ask")) popup_ask(&g_app->popup, "SD CARD", "The card is not formatted. Format it?", false, answer, nullptr);
        else popup_info(&g_app->popup, "SD CARD", "Card inserted", 0, millis(), 2000);
        g_app->redraw_owed = true;                         // not `dirty`: app_step clears that before it looks
        Serial.printf("[CMD] popup %s\n", k[0] ? k : "info");
        return;
    }
    if (!strcmp(line, "mi") && g_app) {                   // mi <name|number|next|prev|list> [timbre morph harm]: every rack OSC plays that MI model (ADR-039)
        char what[16] = {};
        float t = -1, m = -1, h = -1;
        sscanf(arg ? arg : "", "%15s %f %f %f", what, &t, &m, &h);
        if (!strcmp(what, "list")) {
            for (int i = 0; i < sc::MI_MODELS; i++) Serial.printf("[CMD] mi %d %s\n", i, sc::mi_model_name(i));
            return;
        }
        int cur = -1;
        for (int i = 0; i < g_app->rack.count && cur < 0; i++)
            if (g_app->rack.slot[i].type == MOD_OSC && g_app->rack.slot[i].v[MP_OC_WAVE] >= OC_FIRST_MI) cur = (int)g_app->rack.slot[i].v[MP_OC_WAVE] - OC_FIRST_MI;
        int model = -1;
        if (!strcmp(what, "next")) model = (cur + 1) % sc::MI_MODELS;
        else if (!strcmp(what, "prev")) model = cur <= 0 ? sc::MI_MODELS - 1 : cur - 1;
        else if (what[0] >= '0' && what[0] <= '9') model = atoi(what);
        else for (int i = 0; i < sc::MI_MODELS; i++) if (!strcasecmp(what, sc::mi_model_name(i))) model = i;
        if (model < 0 || model >= sc::MI_MODELS) { Serial.printf("[CMD] mi: unknown model '%s' (mi list)\n", what); return; }
        int n = 0;
        for (int i = 0; i < g_app->rack.count; i++) {
            rack_slot_t &s = g_app->rack.slot[i];
            if (s.type != MOD_OSC) continue;
            s.v[MP_OC_WAVE] = (float)(OC_FIRST_MI + model);
            if (t >= 0) s.v[MP_OC_PW] = t < 0.05f ? 0.05f : (t > 0.95f ? 0.95f : t);        // Timb shares PW's range
            if (m >= 0) s.v[MP_OC_MORPH] = m > 1 ? 1 : m;
            if (h >= 0) s.v[MP_OC_HARM] = h > 1 ? 1 : h;
            n++;
        }
        use_rack();
        Serial.printf("[CMD] mi %d %s on %d oscillators\n", model, sc::mi_model_name(model), n);
        return;
    }
    if (!strcmp(line, "flt") && g_app) {                  // flt T: the type of every FL module (0 Off 1 LP 2 BP 3 HP 4 LP24 5 Notch 6 LP6 7 Ladr 8 ChLP 9 AP), a live change
        for (int i = 0; i < g_app->rack.count; i++)
            if (g_app->rack.slot[i].type == MOD_FILTER) g_app->rack.slot[i].v[MP_FL_TYPE] = (float)(v < 0 ? 0 : (v >= FILT_COUNT ? FILT_COUNT - 1 : v));
        audio_set_params(&g_app->rack, &g_app->params);
        Serial.printf("[CMD] flt %d\n", v);
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
        else if (!strcmp(f, "fx")) clear_row_m();
        else { Serial.println("[CMD] str wave|osc|det|mix|lvl|lp|ftype|fx <value>"); return; }
        audio_build(&g_app->rack, &g_app->params);
        Serial.printf("[CMD] str %s %g\n", f, (double)x);
        return;
    }
    if (!strcmp(line, "pad") && g_app) {                  // the Pads section for tests: pad on|off, fill F N, def <A1> <pads.cfg words>, push|rel K, stop, clock 0|1, mem
        char sub[8] = {};
        int used = 0;
        const char *s = arg ? arg : "";
        if (sscanf(s, "%7s%n", sub, &used) < 1) { Serial.println("[CMD] pad on|off|fill F N|def A1 ...|push K|rel K|stop|clock 0/1|mem|edit F OP AMOUNT"); return; }
        s += used;
        while (*s == ' ') s++;
        const int a = atoi(s);
        if (!strcmp(sub, "on") || !strcmp(sub, "off")) { app_set_section(g_app, !strcmp(sub, "on")); Serial.printf("[CMD] pad section %s\n", sub); return; }
        if (!strcmp(sub, "fill")) {                       // fill F N: catalog file F on pads 0..N-1 (one-shot, default settings)
            int f = 0, n = 0;
            sscanf(s, "%d %d", &f, &n);
            audio_sample_info_t in;
            if (!audio_sample_info(f, &in)) { Serial.printf("[CMD] pad fill: no sample %d\n", f); return; }
            for (int k = 0; k < n && k < PAD_COUNT; k++) {
                pad_default(&g_app->pads.pad[k]);
                snprintf(g_app->pads.pad[k].file, PAD_NAME_MAX, "%s", in.name);
                app_pad_changed(g_app, k);
            }
            Serial.printf("[CMD] pad fill %s x %d\n", in.name, n);
            return;
        }
        if (!strcmp(sub, "def")) {                        // def A1 <words>: that pad as a pads.cfg line ("loop 1 xfade 20 file name")
            int label = -1;
            if (s[0] >= 'A' && s[0] <= 'D') label = (s[0] - 'A') * PADS_PER_BANK + atoi(s + 1) - 1;
            const char *words = strchr(s, ' ');
            if (label < 0 || label >= PAD_COUNT || !words) { Serial.println("[CMD] pad def: A1..D16 then pads.cfg words"); return; }
            pad_default(&g_app->pads.pad[label]);
            pad_parse(&g_app->pads.pad[label], words + 1);
            app_pad_changed(g_app, label);
            Serial.printf("[CMD] pad def %s\n", s);
            return;
        }
        if (!strcmp(sub, "push")) { audio_pad_push(a); Serial.printf("[CMD] pad push %d\n", a); return; }
        if (!strcmp(sub, "rel"))  { audio_pad_release(a); Serial.printf("[CMD] pad rel %d\n", a); return; }
        if (!strcmp(sub, "stop")) { audio_pads_stop(); Serial.println("[CMD] pad stop"); return; }
        if (!strcmp(sub, "clock")) { g_app->pads_run = a != 0; Serial.printf("[CMD] pad clock %d\n", a); return; }
        if (!strcmp(sub, "mem")) {                        // the heaps: internal RAM and PSRAM free / largest block, the engine's two heaps
            size_t fu = 0, fc = 0, fh = 0, bu = 0, sp = 0;
            engine_synth_heap_stats(&fu, &fc, &fh, &bu, &sp);
            Serial.printf("[CMD] pad mem: internal free %u largest %u, PSRAM free %u largest %u, fast heap %u / %u (high %u), bulk used %u, spilled %u, playing %llx\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                          (unsigned)fu, (unsigned)fc, (unsigned)fh, (unsigned)bu, (unsigned)sp, (unsigned long long)audio_pads_playing());
            return;
        }
        if (!strcmp(sub, "rec")) {                        // rec S: records what plays for S seconds into a scratch file, and how the card kept up
            const int secs = a > 0 && a <= 60 ? a : 5;
            uint32_t u0 = 0, u1 = 0, rd = 0, frames = 0, lost = 0;
            engine_synth_sampler_stats(&u0, &rd);
            const uint32_t t0 = millis();
            if (!audio_capture_start("zzrec", 0, 0, (uint32_t)secs * (uint32_t)audio_rate())) { Serial.println("[CMD] pad rec: refused (busy, no card or no memory)"); return; }
            int st = CAPTURE_ST_ARMED;
            while (millis() - t0 < (uint32_t)secs * 1000u + 8000u) {
                st = audio_capture_poll(&frames, &lost);
                if (st == CAPTURE_ST_DONE || st == CAPTURE_ST_FAILED || st == CAPTURE_ST_EMPTY) break;
                delay(20);
            }
            const uint32_t ms = millis() - t0;
            audio_capture_done();
            engine_synth_sampler_stats(&u1, &rd);
            storage_remove(STORAGE_DIR_SAMPLES "/zzrec.smp");
            Serial.printf("[CMD] pad rec %d s: %s, %u frames, %u lost, finished after %u ms, stream underruns +%u\n", secs,
                          st == CAPTURE_ST_DONE ? "done" : st == CAPTURE_ST_FAILED ? "FAILED" : st == CAPTURE_ST_EMPTY ? "EMPTY" : "TIMEOUT",
                          (unsigned)frames, (unsigned)lost, (unsigned)ms, (unsigned)(u1 - u0));
            return;
        }
        if (!strcmp(sub, "verify")) {                     // verify 0|1: the card driver reads every write back and compares (slow), and its counts
            uint32_t n = 0, bad = 0;
            sd_card_verify(a != 0, &n, &bad);
            Serial.printf("[CMD] pad verify %d: %u sectors read back, %u differed\n", a != 0, (unsigned)n, (unsigned)bad);
            return;
        }
        if (!strcmp(sub, "fx")) {                         // fx clear | fx route N | fx <A|B|C> <code> [values]: the Pads section's FX chains
            rack_t *fx = &g_app->pads.fx;
            char w[8] = {}, code[8] = {};
            int n = 0;
            if (sscanf(s, "%7s%n", w, &n) < 1) { Serial.println("[CMD] pad fx clear | route N | <A|B|C> <code> [values]"); return; }
            s += n;
            if (!strcmp(w, "clear")) rack_init_fx(fx);
            else if (!strcmp(w, "route")) fx->fx_route = (uint8_t)(atoi(s) >= 0 && atoi(s) < RACK_ROUTE_COUNT ? atoi(s) : 0);
            else {
                const int row = w[0] - 'A';
                int t = MOD_TYPE_COUNT;
                if (sscanf(s, "%7s%n", code, &n) == 1) for (int k = 0; k < MOD_TYPE_COUNT; k++) if (!strcasecmp(code, rack_type_code((module_type_t)k))) t = k;
                if (row < 0 || row >= RACK_ROWS || t == MOD_TYPE_COUNT || !rack_insert_row(fx, fx->count, row, (module_type_t)t)) {
                    Serial.printf("[CMD] pad fx: '%s %s' refused (a chain A..C, an effect's code, 4 a chain)\n", w, code);
                    return;
                }
                s += n;
                float x = 0;
                for (int i = 0; i < rack_mparam_count((module_type_t)t) && sscanf(s, "%f%n", &x, &n) == 1; i++, s += n) fx->slot[fx->count - 1].v[i] = x;
            }
            audio_pads_fx(&g_app->pads, &g_app->rack);
            Serial.printf("[CMD] pad fx: %d module(s), route %s\n", fx->count, rack_route_name(fx->fx_route));
            return;
        }
        if (!strcmp(sub, "edit")) {                       // edit F OP AMOUNT [AMOUNT2]: catalog file F through ProcOp OP (pads.h PROC_*) into a scratch file, timed
            int f = 0, op = 0, amt = 0, amt2 = 0;
            sscanf(s, "%d %d %d %d", &f, &op, &amt, &amt2);
            audio_sample_info_t in;
            if (!audio_sample_info(f, &in)) { Serial.printf("[CMD] pad edit: no sample %d\n", f); return; }
            uint32_t u0 = 0, u1 = 0, rd = 0;
            engine_synth_sampler_stats(&u0, &rd);
            const size_t free0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), big0 = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
            const uint32_t t0 = millis();
            if (!audio_render_start(in.name, "zzedit", op, amt, amt2)) {
                Serial.printf("[CMD] pad edit %s op %d amount %d: refused, PSRAM free %u largest %u\n", in.name, op, amt, (unsigned)free0, (unsigned)big0);
                return;
            }
            const size_t free1 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            int pct = 0, state = RENDER_STATE_RUNNING;
            while ((state = audio_render_poll(&pct)) == RENDER_STATE_RUNNING && millis() - t0 < 600000) delay(20);
            const uint32_t ms = millis() - t0;
            audio_render_done();
            engine_synth_sampler_stats(&u1, &rd);
            storage_remove(STORAGE_DIR_SAMPLES "/zzedit.smp");
            Serial.printf("[CMD] pad edit %s frames %u rate %u op %d amount %d: %s in %u ms, memory %u (PSRAM free %u largest %u before), underruns +%u\n", in.name,
                          (unsigned)in.frames, (unsigned)in.rate, op, amt, state == RENDER_STATE_DONE ? "done" : state == RENDER_STATE_FAILED ? "FAILED" : "TIMEOUT", (unsigned)ms,
                          (unsigned)(free0 - free1), (unsigned)free0, (unsigned)big0, (unsigned)(u1 - u0));
            if (sc::g_proc_frames)                        // ENGINE_PROFILE: where a spectral frame's time went (ms of CPU per frame at 240 MHz)
                Serial.printf("[CMD] pad edit frames %u, ms each: load %.2f, fft %.2f, spectrum %.2f, ifft %.2f, overlap %.2f\n", (unsigned)sc::g_proc_frames,
                              sc::g_proc_prof[0] / 240000.0 / sc::g_proc_frames, sc::g_proc_prof[1] / 240000.0 / sc::g_proc_frames,
                              sc::g_proc_prof[2] / 240000.0 / sc::g_proc_frames, sc::g_proc_prof[3] / 240000.0 / sc::g_proc_frames,
                              sc::g_proc_prof[4] / 240000.0 / sc::g_proc_frames);
            return;
        }
        Serial.printf("[CMD] pad: unknown '%s'\n", sub);
        return;
    }
    if (!strcmp(line, "fx") && g_app) {                   // fx clear | fx <code> [v0 .. v7]: append an FX module to row M (code as on the RACK tab: DL RV CH ...; values in the module's units, missing = default)
        char code[8] = {};
        int used = 0;
        const char *s = arg ? arg : "";
        if (sscanf(s, "%7s%n", code, &used) < 1) { Serial.println("[CMD] fx clear | fx <code> [values...]"); return; }
        if (!strcmp(code, "clear")) { clear_row_m(); audio_build(&g_app->rack, &g_app->params); Serial.println("[CMD] fx clear"); return; }
        int t = MOD_TYPE_COUNT;
        for (int k = 0; k < MOD_TYPE_COUNT; k++) if (!strcasecmp(code, rack_type_code((module_type_t)k)) && rack_is_processor((module_type_t)k)) t = k;
        const int slot = t < MOD_TYPE_COUNT ? rack_add_m(&g_app->rack, (module_type_t)t) : RACK_NONE;
        if (slot == RACK_NONE) { Serial.printf("[CMD] fx: '%s' is not a row M module, or the rack is full\n", code); return; }
        s += used;
        float x = 0;
        int n = 0;
        for (int i = 0; i < rack_mparam_count((module_type_t)t) && sscanf(s, "%f%n", &x, &n) == 1; i++, s += n) g_app->rack.slot[slot].v[i] = x;
        audio_build(&g_app->rack, &g_app->params);
        char desc[32];
        rack_describe(&g_app->rack, slot, desc, sizeof desc);
        Serial.printf("[CMD] fx %s in row M\n", desc);
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
    static char buf[112];
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
