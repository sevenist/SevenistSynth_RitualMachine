#pragma once
// The tests' one check: counts, and prints what failed with a message (printf style).
#include <cstdio>

extern int failures, checks;
extern const char *filter;          // the text given on the command line (nullptr: every test)

#define CHECK(cond, ...)                                                                    \
    do {                                                                                    \
        checks++;                                                                           \
        if (!(cond)) {                                                                      \
            failures++;                                                                     \
            std::printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                   \
            std::printf(__VA_ARGS__);                                                       \
            std::printf("\n");                                                              \
        }                                                                                   \
    } while (0)
