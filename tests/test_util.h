#ifndef STRIDE_TEST_UTIL_H
#define STRIDE_TEST_UTIL_H

#include <stdio.h>

/* ============================================================
 * Stride 单元测试公共工具
 *
 * 每个测试文件都是独立可执行程序，链接 libstride.a。
 * ============================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg)                          \
    do {                                           \
        if (cond) {                                \
            printf("  \xE2\x9C\x93 %s\n", msg);    \
            tests_passed++;                        \
        } else {                                   \
            printf("  \xE2\x9C\x97 FAILED: %s\n", msg); \
            tests_failed++;                        \
        }                                          \
    } while (0)

static int test_summary(const char *suite) {
    printf("\n=== %s Results ===\n", suite);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

#endif /* STRIDE_TEST_UTIL_H */
