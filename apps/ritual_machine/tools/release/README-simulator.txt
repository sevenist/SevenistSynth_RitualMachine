RITUAL MACHINE - simulator, {VERSION} (Windows, 64 bit)

The machine's firmware running on a PC: the same application and the same sound engine as on the hardware, with a drawn front panel
in place of the real one. Sound goes to the default audio output.

START
  Unzip the folder anywhere and run one of the two programs from inside it (the folder "sdcard" next to them is the machine's TF
  card, shared by both):

    ritual-machine-sim-hwv1.exe     the first prototype, the board the firmware image of this release is for:
                                    4 function keys, 4 rows of 8 keys, 7 knobs without end stops, a joystick
    ritual-machine-sim-panel.exe    the next front panel: 4 columns of keys, 2 encoders, 8 knobs, a joystick, 4 buttons

  Two windows open: the OLED screen and the front panel. Esc closes the simulator.

THE PANEL
  Every part works with the mouse: click = press, wheel over a knob or an encoder = turn, drag a knob up / down, drag inside the
  joystick, right click on the joystick = its push. Each part shows the keyboard keys that work it (physical keys, QWERTY names:
  on another keyboard, the key at the same place).

  The first prototype (ritual-machine-sim-hwv1.exe):

    Function keys          F1 F2 F3 F4                    Shift, Menu, Back, Play
    Keys of the machine    1 2 3 4 5 6 7 8                four rows of eight:
                           Q W E R T Y U I                notes in the synth,
                           A S D F G H J K                pads and banks in the Pads section
                           Z X C V B N M ,
    Joystick               arrow keys, push = right Ctrl
    Top knobs              - =  volume      [ ]  rows      ; '  values
    Knobs by the joystick  9 0     O P     L .     keypad - +       (or the mouse: wheel, drag)
    Card out / in          F12

  The next panel (ritual-machine-sim-panel.exe):

    Function keys          F1 F2 F3 F4                    Shift, Menu, Back, Play
    Keys of the machine    1 2 3 4                        four rows of four
                           Q W E R
                           A S D F
                           Z X C V
    Joystick               arrow keys, push = right Ctrl
    Encoder A (rows)       [ ]     push \
    Encoder B (values)     ; '     push /
    Menu / Back / Shift    Enter / Backspace / left Shift
    Play                   Space
    Knobs                  the mouse: wheel, drag
    Card out / in          F12

  Shift + Menu switches between the synth and the Pads section (the sampler pads).
  Shift + joystick up / down changes the octave.
  Menu > PRESETS saves and loads sounds; the sound you leave comes back at the next start.

THE SEQUENCERS
  Synth: Play starts the step sequencer of the SEQUENCER page. Shift + Play arms recording: stopped, the keys enter steps (chords
  too); running, they are recorded live. The PATTERNS page holds 8 patterns and a chain.
  Pads section: Play starts the clock and the pads' sequencer. Shift + Play opens step mode: the pad keys are the steps of the
  selected pad. Back leaves it.

THE CARD
  sdcard\system\samples   the sample library (*.smp, and .wav / .mp3 files, which are converted when first used). Five synthetic demo
                          samples are in it: pad_c4, pluck_c4, bell_c5, kick, break8.
  sdcard\presets          your presets (*.preset, plain text). Rename and sort them here.
  sdcard\system\config    the machine's settings, the sound as it was left and the sequencers' patterns. Delete the folder's files
                          to start from scratch.

LICENCES
  The simulator contains code by other people: see THIRD_PARTY_NOTICES.md and the folder "licenses". The Braids and Plaits
  oscillator models are the work of Emilie Gillet (Mutable Instruments), MIT licence.
