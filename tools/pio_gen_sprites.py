# PlatformIO pre-build script (platformio.ini: extra_scripts = pre:tools/pio_gen_sprites.py): regenerates src/core/ui_sprites_gen.c/.h from
# assets/UI_Sprites/ before every firmware build, so the firmware always has the current images (the same converter as build.ps1).
import os
import subprocess
import sys

Import("env")  # noqa: F821 (provided by PlatformIO)

root = env.subst("$PROJECT_DIR")  # noqa: F821
r = subprocess.run([sys.executable, os.path.join(root, "tools", "gen_ui_sprites.py")], cwd=root)
if r.returncode != 0:
    print("gen_ui_sprites: some UI sprites could not be converted (see above); the build goes on with the others")
