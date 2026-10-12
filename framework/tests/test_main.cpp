#include <cstring>
#include "test.h"

// usage: engine_tests [name-substring]   (runs only the matching tests)
int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const auto &c : tst::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = tst::failures();
        std::printf("[ run ] %s\n", c.name);
        std::fflush(stdout);
        c.fn();
        ran++;
        std::printf("[%s] %s\n", tst::failures() == before ? " ok " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", ran, tst::failures());
    return tst::failures() ? 1 : 0;
}
