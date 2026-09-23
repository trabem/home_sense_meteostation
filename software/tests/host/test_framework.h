#pragma once

/* Tiny host assert framework: counts failures, returns non-zero on failure. */

#include <stdio.h>
#include <string.h>

static int g_test_failures = 0;
static int g_test_checks = 0;

#define CHECK_TRUE(cond)                                                     \
    do {                                                                     \
        g_test_checks++;                                                     \
        if (!(cond)) {                                                       \
            g_test_failures++;                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(expected, actual)                                        \
    do {                                                                     \
        long long e_ = (long long)(expected);                                \
        long long a_ = (long long)(actual);                                  \
        g_test_checks++;                                                     \
        if (e_ != a_) {                                                      \
            g_test_failures++;                                               \
            printf("FAIL %s:%d: %s == %lld, expected %lld\n",                \
                   __FILE__, __LINE__, #actual, a_, e_);                     \
        }                                                                    \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                      \
    do {                                                                     \
        g_test_checks++;                                                     \
        if (strstr((haystack), (needle)) == NULL) {                          \
            g_test_failures++;                                               \
            printf("FAIL %s:%d: expected substring \"%s\" in \"%s\"\n",      \
                   __FILE__, __LINE__, (needle), (haystack));                \
        }                                                                    \
    } while (0)

#define CHECK_NOT_CONTAINS(haystack, needle)                                  \
    do {                                                                     \
        g_test_checks++;                                                     \
        if (strstr((haystack), (needle)) != NULL) {                          \
            g_test_failures++;                                               \
            printf("FAIL %s:%d: unexpected substring \"%s\" in \"%s\"\n",    \
                   __FILE__, __LINE__, (needle), (haystack));                \
        }                                                                    \
    } while (0)

#define CHECK_BYTES_EQ(expected, actual, len)                                 \
    do {                                                                     \
        g_test_checks++;                                                     \
        if (memcmp((expected), (actual), (len)) != 0) {                      \
            g_test_failures++;                                               \
            printf("FAIL %s:%d: byte mismatch at %s\n",                      \
                   __FILE__, __LINE__, #actual);                             \
            for (size_t i_ = 0; i_ < (len); i_++) {                          \
                unsigned char e_ = ((const unsigned char *)(expected))[i_];  \
                unsigned char a_ = ((const unsigned char *)(actual))[i_];    \
                if (e_ != a_) {                                              \
                    printf("  [%u] expected 0x%02X got 0x%02X\n",            \
                           (unsigned)i_, e_, a_);                            \
                }                                                            \
            }                                                                \
        }                                                                    \
    } while (0)

#define TEST_SUMMARY_AND_RETURN()                                             \
    do {                                                                     \
        printf("%s: %d checks, %d failures\n", __FILE__, g_test_checks,      \
               g_test_failures);                                             \
        return g_test_failures == 0 ? 0 : 1;                                 \
    } while (0)
