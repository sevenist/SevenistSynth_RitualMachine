// TechnoGen's host tests: the app's portable code (the track model, the dice, the screen) and the library parts it brings in (the colour
// canvas, the engine's track modules). The harness is the library's (framework/tests/test.h). Built and run by tools/build_tests.ps1.
//   tests.exe [name-substring]     only the tests whose name contains the text
#include <cstring>
#include "tests/test.h"

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const auto &c : tst::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = tst::failures();
        c.fn();
        ran++;
        if (tst::failures() != before) std::printf("[FAIL] %s\n", c.name);
    }
    std::printf("%d tests, %d failed checks\n", ran, tst::failures());
    return tst::failures() ? 1 : 0;
}
