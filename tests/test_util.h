#pragma once

// Minimal, dependency-free assertion helpers for the test executables. No
// external test framework is used to keep the build self-contained.

#include <cstdio>

namespace testutil {
inline int& failureCount() {
    static int n = 0;
    return n;
}
} // namespace testutil

#define TEST_CHECK(cond)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            ++testutil::failureCount();                                                      \
        }                                                                                     \
    } while (0)

#define TEST_CHECK_EQ(actual, expected)                                                       \
    do {                                                                                       \
        auto actual_ = (actual);                                                               \
        auto expected_ = (expected);                                                           \
        if (!(actual_ == expected_)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s (got 0x%llx, expected 0x%llx)\n",       \
                          __FILE__, __LINE__, #actual, #expected,                               \
                          static_cast<unsigned long long>(actual_),                             \
                          static_cast<unsigned long long>(expected_));                          \
            ++testutil::failureCount();                                                        \
        }                                                                                       \
    } while (0)

#define TEST_MAIN_RETURN() (testutil::failureCount() == 0 ? 0 : 1)
