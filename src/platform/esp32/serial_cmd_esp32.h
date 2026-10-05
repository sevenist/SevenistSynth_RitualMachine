#pragma once
// Dev-only serial command channel (build flag DEV_SERIAL_CMD): lets a PC script drive the engine over the USB serial port, e.g. to hold a chord and
// read the cycle counters back. tools/serial_test.py is the matching host side. Commands are one text line each:
//   ping            -> "[CMD] pong"
//   on N / off N    -> note on / off (MIDI note N)
//   chord K         -> release the previous test chord, then hold K notes (0..8; 0 = just release)
//   release         -> release everything the commands started
//   eng a b c d     -> set the engine (0..7) of the first oscillators of the patch, live
//   status          -> "[CMD] status ..."
// Every command answers with a "[CMD]" line. Notes go straight to the audio engine: the UI does not see them.
#ifdef __cplusplus
extern "C" {
#endif
void serial_cmd_poll(void);   // call from loop(): reads whatever has arrived and runs complete lines
#ifdef __cplusplus
}
#endif
