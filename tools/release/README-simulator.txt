RITUAL MACHINE - simulator, {VERSION} (Windows, 64 bit)

The machine's firmware running on a PC: the same application and the same sound engine as on the hardware, with a drawn front panel
in place of the real one. Sound goes to the default audio output.

START
  Unzip the folder anywhere and run ritual-machine-sim.exe from inside it (the folder "sdcard" next to it is the machine's TF card).
  Two windows open: the OLED screen and the front panel. Esc closes the simulator.

THE PANEL
  Every control works with the mouse: click = press, wheel over a knob or an encoder = turn, drag a knob up / down, drag inside the
  joystick pad. From the keyboard (physical keys, QWERTY names):

    Keys of the machine    F1 F2 F3 F4 . F5 F6 F7 F8      the function row
                           1 2 3 4 . 5 6 7 8              then four rows of eight:
                           Q W E R . T Y U I              notes in the synth,
                           A S D F . G H J K              pads and banks in the Pads section
                           Z X C V . B N M ,
    Joystick               arrow keys, push = right Ctrl
    Encoder A (rows)       [ ]     push \
    Encoder B (values)     ; '     push /
    Menu / Back / Shift    Enter / Backspace / left Shift
    Play                   Space
    Card out / in          F12

  Shift + Menu switches between the synth and the Pads section (the sampler pads).
  Menu > PRESETS saves and loads sounds; the sound you leave comes back at the next start.

THE CARD
  sdcard\system\samples   the sample library (*.smp, and .wav / .mp3 files, which are converted when first used). Five synthetic demo
                          samples are in it: pad_c4, pluck_c4, bell_c5, kick, break8.
  sdcard\presets          your presets (*.preset, plain text). Rename and sort them here.
  sdcard\system\config    the machine's settings and the sound as it was left. Delete the folder's files to start from scratch.

LICENCES
  The simulator contains code by other people: see THIRD_PARTY_NOTICES.md and the folder "licenses". The Braids and Plaits
  oscillator models are the work of Emilie Gillet (Mutable Instruments), MIT licence.
