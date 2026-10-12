Put your .wav and .mp3 files here, then run  .\build.ps1  (or  .\build.ps1 -Sd <SDCard Path eg: E:>  to copy them to the TF card as well).

Every new or changed file is converted to ..\samples\<name>.smp, the format the engine plays (the simulator reads samples\,
the board reads the card's system\samples\ folder; build.ps1 copies the files to the simulator's card sdcard\system\samples\ and,
with -Sd, to the real card).

  - stereo is mixed to mono; files over 5 minutes are cut; names are cut to 23 characters
  - the loop points and the root note come from the WAV 'smpl' chunk when it has one
  - otherwise a note at the end of the name sets the root: pad_c4.wav = C4, bass_fs2.wav (or bass_f#2) = F#2, lead_bb3.wav = Bb3
    (C4 = MIDI 60); with neither, the root is C4
  - to convert once without a full build:  build\smp_convert.exe samples_src samples [--force]
