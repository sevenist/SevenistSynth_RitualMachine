#pragma once
// Dev-only serial command channel (build flag DEV_SERIAL_CMD): lets a PC script drive the engine over the USB serial port, e.g. to hold a chord and
// read the cycle counters back. tools/serial_test.py is the matching host side. Commands are one text line each:
//   ping            -> "[CMD] pong"
//   on N / off N    -> note on / off (MIDI note N)
//   chord K         -> release the previous test chord, then hold K notes (0..8; 0 = just release)
//   release         -> release everything the commands started
//   eng a b c d     -> set the engine (0..7) of the first oscillators of the patch, live
//   status          -> "[CMD] status ..."
//   samples         -> one "[CMD] sample N name ..." line per catalog entry
//   patch startup   -> the default patch (four oscillator engines, delay, reverb)
//   patch sampler F [L]   -> one sampler on catalog entry F (0-based, see `samples`) with loop mode L (0 file 1 off 2 fwd 3 ping-pong) into one filter, effects off
//   voices N        -> voice count 1..8 (a rebuild)
// patch / voices rebuild the synth the way leaving the menu does; they are run from loop(), like the UI.
// Every command answers with a "[CMD]" line. Notes go straight to the audio engine: the UI does not see them.
#include "core/app.h"
#ifdef __cplusplus
extern "C" {
#endif
void serial_cmd_attach(app_t *app);   // the application the patch commands act on
void serial_cmd_poll(void);   // call from loop(): reads whatever has arrived and runs complete lines
#ifdef __cplusplus
}
#endif
