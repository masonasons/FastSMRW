#pragma once

// Minimal, dependency-free test harness. Each translation unit registers
// CHECK()s; main() reports the totals and returns non-zero on any failure.

#include <cstdio>

namespace fastsmtest {

inline int& checks() {
    static int n = 0;
    return n;
}
inline int& failures() {
    static int n = 0;
    return n;
}

} // namespace fastsmtest

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++fastsmtest::checks();                                                            \
        if (!(cond)) {                                                                      \
            ++fastsmtest::failures();                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                    \
        }                                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        ++fastsmtest::checks();                                                            \
        if (!((a) == (b))) {                                                               \
            ++fastsmtest::failures();                                                      \
            std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);             \
        }                                                                                  \
    } while (0)
