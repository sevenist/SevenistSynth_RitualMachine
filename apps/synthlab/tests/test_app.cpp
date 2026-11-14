// The screen and the encoders (src/app.cpp), on HWV2's control table and a 128 x 160 frame, as on the board. The board is stubbed here:
// the display (the app draws into a colour canvas, kept to be written as a PNG) and the control table (HWV2's own, linked from
// framework/boards/hwv2/hwv2_controls.c).
#include <cmath>
#include <vector>
#include "app.h"
#include "boards/hwv2/hwv2_controls.h"
#include "lab.h"
#include "synth.h"
#include "tests/check.h"
#include "ui/canvas.h"

bool write_png(const char *path, const uint16_t *frame, int w, int h, int scale);      // png.cpp

namespace {
int frames_sent = 0;
}

extern "C" void display_send(u8g2_t *) { frames_sent++; }
extern "C" void display_send_rgb565(const uint16_t *) {}   // (the canvas links it; the app sends one-colour frames)
extern "C" const input_control_t *input_controls(int *count) { if (count) *count = HWV2_CONTROL_COUNT; return kHwv2Controls; }

namespace {
canvas_t screen;
uint16_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];

void send(int ctl, input_kind_t kind, int value = 0) { app_event(input_event_t{ctl, kind, value, false}); }
void turn(int ctl, int detents) { send(ctl, IN_DELTA, detents); }
void push(int ctl) { send(ctl, IN_PRESS); send(ctl, IN_RELEASE); }
void draw() { canvas_clear(&screen, 0); app_draw(); }   // (u8g2_ClearBuffer does not clear a canvas: the test does)
void shot(const char *path) {
    draw();
    CHECK(write_png(path, pixels, DISPLAY_WIDTH, DISPLAY_HEIGHT, 3), "could not write %s", path);
}
}  // namespace

void test_app() {
    static std::vector<unsigned char> fast(1u << 20), bulk(1u << 20);
    CHECK(synth_init(fast.data(), fast.size(), bulk.data(), bulk.size()), "the engine did not start");
    CHECK(canvas_init(&screen, pixels, DISPLAY_WIDTH, DISPLAY_HEIGHT), "canvas %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    app_start(canvas_u8g2(&screen));
    draw();
    CHECK(frames_sent == 1, "the first frame is sent (%d)", frames_sent);
    app_draw();
    CHECK(frames_sent == 1, "nothing changed: no frame (%d)", frames_sent);
    shot("build/screen_start.png");

    // row 0: encoder B picks the synth, and stops at both ends of the list
    turn(HWV2_ENC_B, 1);
    CHECK(synth_selected() == (lab::kSynthCount > 1 ? 1 : 0), "encoder B on the first row: the next synth (%d)", synth_selected());
    turn(HWV2_ENC_B, -50);
    CHECK(synth_selected() == 0, "the first synth at most (%d)", synth_selected());

    // Basic's values: row 1 = Wave (Int, labels), row 2 = Cutoff (Exp)
    const lab::Synth &s = *lab::kSynths[0];
    turn(HWV2_ENC_A, 1);
    turn(HWV2_ENC_B, 1);
    CHECK(synth_value(0) == s.params[0].def + 1, "Int: one detent = 1 (%g)", synth_value(0));
    turn(HWV2_ENC_B, 50);
    CHECK(synth_value(0) == s.params[0].max, "Int: stops at its max (%g)", synth_value(0));
    turn(HWV2_ENC_A, 1);
    const float before = synth_value(1);
    turn(HWV2_ENC_B, 10);
    const float expect = before * std::pow(s.params[1].max / s.params[1].min, 0.1f);
    CHECK(std::fabs(synth_value(1) - expect) < 0.01f * expect, "Exp: 10 detents = a tenth of the range's ratio (%g, not %g)", synth_value(1), expect);
    shot("build/screen_edit.png");
    push(HWV2_ENC_B_SW);
    CHECK(synth_value(1) == s.params[1].def, "encoder B's push: the default (%g)", synth_value(1));

    // encoder A stops at the last row, its push goes back to the top
    turn(HWV2_ENC_A, 100);
    shot("build/screen_last.png");
    push(HWV2_ENC_A_SW);
    turn(HWV2_ENC_B, 1);
    CHECK(synth_selected() == (lab::kSynthCount > 1 ? 1 : 0), "after encoder A's push the first row is the synth again (%d)", synth_selected());
    shot("build/screen_synth2.png");

    // a synth with sections: encoder A steps over their titles, both ways
    int with_titles = -1;
    for (int k = 0; k < lab::kSynthCount && with_titles < 0; k++)
        if (lab::kSynths[k]->n_params > 0 && lab::kSynths[k]->params[0].curve == lab::Head) with_titles = k;
    if (with_titles >= 0) {
        push(HWV2_ENC_A_SW);
        turn(HWV2_ENC_B, with_titles - synth_selected());
        CHECK(synth_selected() == with_titles, "select the synth with titles (%d)", synth_selected());
        const lab::Synth &t = *lab::kSynths[with_titles];
        turn(HWV2_ENC_A, 1);                                // row 1 is a title: the first value is row 2
        const float v0 = synth_value(1);
        turn(HWV2_ENC_B, 1);
        CHECK(synth_value(1) != v0, "encoder A skipped the title: the first value moved (%g)", synth_value(1));
        CHECK(synth_value(0) == 0.f, "the title has no value (%g)", synth_value(0));
        shot("build/screen_sections.png");
        turn(HWV2_ENC_A, 100);
        shot("build/screen_sections_end.png");
        turn(HWV2_ENC_A, -100);
        turn(HWV2_ENC_B, -1);                               // back on row 0: the synth before
        CHECK(synth_selected() == with_titles - 1 || with_titles == 0, "encoder A back up stops on the synth row (%d)", synth_selected());
        (void)t;
    }

    // the keys and PLAY (the voices: through the engine, rendered here)
    int16_t buf[2 * 64];
    push(HWV2_KEY(0, 3));                                   // F4: the next synth (or none)
    push(HWV2_KEY(0, 2));                                   // F3: back
    send(HWV2_KEY(4, 0), IN_PRESS);                         // the lowest key
    for (int i = 0; i < 4; i++) synth_render(buf, 64);
    CHECK(synth_active_voices() == 1, "a key plays a voice (%d)", synth_active_voices());
    push(HWV2_PLAY);                                        // holds it
    send(HWV2_KEY(4, 0), IN_RELEASE);
    for (int i = 0; i < 4; i++) synth_render(buf, 64);
    CHECK(synth_active_voices() == 1, "PLAY holds the note after the key is let go (%d)", synth_active_voices());
    push(HWV2_PLAY);
    for (int i = 0; i < 20000 && synth_active_voices() > 0; i++) { synth_render(buf, 64); synth_update(); }
    CHECK(synth_active_voices() == 0, "PLAY again lets it go (%d)", synth_active_voices());

    // a synth with a tempo (lab::Options::play_bpm): PLAY loops a hit every beat, on time (measured from the sound: its onsets)
    int looper = -1;
    for (int k = 0; k < lab::kSynthCount && looper < 0; k++)
        if (lab::kSynths[k]->opt.play_bpm > 0) looper = k;
    if (looper >= 0) {
        push(HWV2_ENC_A_SW);
        turn(HWV2_ENC_B, looper - synth_selected());
        CHECK(synth_selected() == looper, "select the synth with a tempo (%d)", synth_selected());
        const int bpm = lab::kSynths[looper]->opt.play_bpm;
        push(HWV2_PLAY);
        CHECK(synth_looping(), "PLAY starts its loop");
        shot("build/screen_loop.png");
        const int sr = synth_sample_rate(), total = 3 * sr, win = sr / 500;   // 3 s; the level in windows of 2 ms
        std::vector<float> level;
        float acc = 0.f;
        int in_win = 0;
        for (int done = 0; done < total; done += 64) {
            synth_render(buf, 64);
            synth_update();
            for (int i = 0; i < 64; i++) {
                const float v = buf[2 * i] / 32768.f;
                acc += v * v;
                if (++in_win == win) { level.push_back(std::sqrt(acc / win)); acc = 0.f; in_win = 0; }
            }
        }
        // a hit: a window above -26 dBFS and 4 x louder than the one 10 ms before it (silence before the start), at least 150 ms after
        // the last one (the rumble coming back after its duck is a rise too, 20 x quieter: not a hit)
        std::vector<int> onsets;
        for (size_t w = 0; w < level.size(); w++) {
            const float before = w >= 5 ? level[w - 5] : 0.f;
            if (level[w] > 0.05f && level[w] > 4.f * before && (onsets.empty() || static_cast<int>(w) - onsets.back() > 75)) onsets.push_back(static_cast<int>(w));
        }
        const double beat_ms = 60000.0 / bpm;
        const int expected = static_cast<int>(3000.0 / beat_ms) + 1;
        CHECK(static_cast<int>(onsets.size()) == expected, "%d hits in 3 s at %d BPM (%zu)", expected, bpm, onsets.size());
        CHECK(!onsets.empty() && onsets[0] * 2 <= 3, "the first hit at once (at %d ms)", onsets.empty() ? -1 : onsets[0] * 2);
        double worst = 0.0;
        for (size_t k = 1; k < onsets.size(); k++) {
            const double ms = (onsets[k] - onsets[k - 1]) * 2.0, err = std::fabs(ms - beat_ms);
            if (err > worst) worst = err;
        }
        CHECK(worst <= 3.0, "every hit a beat (%.1f ms) after the last: the worst is %.1f ms off (the windows: 2 ms)", beat_ms, worst);
        push(HWV2_PLAY);
        CHECK(!synth_looping(), "PLAY again stops it");
        for (int i = 0; i < 20000 && synth_active_voices() > 0; i++) { synth_render(buf, 64); synth_update(); }
        CHECK(synth_active_voices() == 0, "after the loop the voice ends (%d)", synth_active_voices());
    }

    // a synth with grooves (lab::Options::grooves, DrumLab): PLAY loops the Groove row's pattern at the Tempo row's BPM, both live
    int grooved = -1;
    for (int k = 0; k < lab::kSynthCount && grooved < 0; k++)
        if (lab::kSynths[k]->opt.grooves) grooved = k;
    if (grooved >= 0) {
        const lab::Options &o = lab::kSynths[grooved]->opt;
        push(HWV2_ENC_A_SW);
        turn(HWV2_ENC_B, grooved - synth_selected());
        CHECK(synth_selected() == grooved, "select the synth with grooves (%d)", synth_selected());
        const int sr = synth_sample_rate(), win = sr / 500;    // the level in windows of 2 ms
        // the onsets (ms from the call) over `ms` of sound: a window above -26 dBFS, 4 x louder than 10 ms before, 150 ms after the last
        const auto onsets_over = [&](int ms) {
            std::vector<float> level;
            float acc = 0.f;
            int in_win = 0;
            for (int done = 0; done < ms * sr / 1000; done += 64) {
                synth_render(buf, 64);
                synth_update();
                for (int i = 0; i < 64; i++) {
                    const float v = buf[2 * i] / 32768.f;
                    acc += v * v;
                    if (++in_win == win) { level.push_back(std::sqrt(acc / win)); acc = 0.f; in_win = 0; }
                }
            }
            std::vector<int> on;
            for (size_t w = 0; w < level.size(); w++) {
                const float before = w >= 5 ? level[w - 5] : 0.f;
                if (level[w] > 0.05f && level[w] > 4.f * before && (on.empty() || static_cast<int>(w) * 2 - on.back() > 150)) on.push_back(static_cast<int>(w) * 2);
            }
            return on;
        };
        synth_set_value(o.groove_param, 2);                     // Offbeat: the eighth after each beat
        synth_set_value(o.tempo_param, 120);
        push(HWV2_PLAY);
        CHECK(synth_looping() && synth_loop_bpm() == 120, "PLAY loops the groove at its Tempo (%d BPM)", synth_loop_bpm());
        std::vector<int> on = onsets_over(2000);
        bool offbeats = on.size() == 4;
        for (size_t k = 0; k < on.size(); k++) offbeats = offbeats && std::abs(on[k] - (250 + 500 * static_cast<int>(k))) <= 3;
        CHECK(offbeats, "Offbeat at 120 BPM: 4 hits at 250, 750, 1250, 1750 ms (%zu hits, the first at %d ms)", on.size(), on.empty() ? -1 : on[0]);
        synth_set_value(o.tempo_param, 100);                    // live: 600 ms a beat from now on
        on = onsets_over(3000);
        bool slower = on.size() >= 4;
        for (size_t k = 1; k < on.size(); k++) slower = slower && std::abs(on[k] - on[k - 1] - 600) <= 3;
        CHECK(slower, "Tempo 100 while it plays: every hit 600 ms after the last (%zu hits)", on.size());
        synth_set_value(o.groove_param, 0);                     // Four: from the end of the playing bar, a hit on every beat
        on = onsets_over(4000);
        // the offbeats go on to the bar's end, then one gap of half a beat (the last offbeat to the next downbeat), then a beat apart
        int halves = 0, beats = 0, after = 0;
        for (size_t k = 1; k < on.size(); k++) {
            const int gap = on[k] - on[k - 1];
            if (std::abs(gap - 300) <= 3) halves++;
            else if (std::abs(gap - 600) <= 3) { beats++; if (halves) after++; }
        }
        CHECK(halves == 1 && beats == static_cast<int>(on.size()) - 2 && after >= 3,
              "a new Groove from the next bar: one half-beat gap then a hit every beat (%zu hits, %d half-beat gaps, %d after)", on.size(), halves, after);
        push(HWV2_PLAY);
        CHECK(!synth_looping(), "PLAY again stops the groove");
        for (int i = 0; i < 20000 && synth_active_voices() > 0; i++) { synth_render(buf, 64); synth_update(); }
    }
    synth_shutdown();
}
