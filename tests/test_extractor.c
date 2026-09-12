#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/stride.h"

/* ============================================================
 * Stride 提取测试：单段执行 + 多段完整提取
 * ============================================================ */

#define MAX_PARAMS 16

static stride_blob_t B(const char *s) {
    stride_blob_t b;
    b.data = s;
    b.bit_len = STRIDE_BITS(strlen(s));
    return b;
}

/* 下面所有用例的步长都是 8，因此 1 步 = 1 字节 */
static int param_eq(stride_param_t p, const char *s) {
    size_t n = strlen(s);
    return p.steps == n && n > 0 && memcmp(p.ptr, s, n) == 0;
}

static void test_extract_steps(void) {
    printf("Test: Extract steps (date segment)\n");

    /* ${4}$'-'${2}$'-'${2} */
    stride_seq_t *e = stride_seq_new();
    stride_seq_capture_steps(e, 4);
    stride_seq_skip_bits(e, STRIDE_BITS(1));
    stride_seq_capture_steps(e, 2);
    stride_seq_skip_bits(e, STRIDE_BITS(1));
    stride_seq_capture_steps(e, 2);
    ASSERT(stride_seq_param_count(e) == 3, "three params declared");

    const char *date = "2024-03-15";
    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extract_run(e, 8, date, STRIDE_BITS(10), p, MAX_PARAMS,
                              &n) == 0,
           "run ok");
    ASSERT(n == 3, "three params");
    ASSERT(param_eq(p[0], "2024") && param_eq(p[1], "03") &&
               param_eq(p[2], "15"),
           "values");
    ASSERT(p[0].ptr == date, "zero-copy into input");

    size_t n2 = 0;
    ASSERT(stride_extract_run(e, 8, "2024", STRIDE_BITS(4), p, MAX_PARAMS,
                              &n2) != 0,
           "short input fails");
    stride_seq_free(e);
}

static void test_extract_until(void) {
    printf("Test: Extract until delimiter\n");

    /* ${'='}$'='${} on "name=alice" */
    stride_seq_t *e = stride_seq_new();
    stride_blob_t eq = B("=");
    stride_seq_capture_until(e, &eq);
    stride_seq_skip_bits(e, STRIDE_BITS(1));
    stride_seq_capture_end(e);

    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extract_run(e, 8, "name=alice", STRIDE_BITS(10), p,
                              MAX_PARAMS, &n) == 0,
           "run ok");
    ASSERT(n == 2, "two params");
    ASSERT(param_eq(p[0], "name"), "captured name");
    ASSERT(param_eq(p[1], "alice"), "captured alice");

    /* 定界串缺失时后续跳过无字符可用 → 失败 */
    size_t n2 = 0;
    ASSERT(stride_extract_run(e, 8, "namealice", STRIDE_BITS(9), p,
                              MAX_PARAMS, &n2) != 0,
           "missing delimiter leaves nothing to skip");
    stride_seq_free(e);

    /* 单独使用时，定界串缺失则捕获到段尾 */
    stride_seq_t *e2 = stride_seq_new();
    stride_seq_capture_until(e2, &eq);
    size_t n3 = 0;
    ASSERT(stride_extract_run(e2, 8, "namealice", STRIDE_BITS(9), p,
                              MAX_PARAMS, &n3) == 0,
           "standalone capture-until runs");
    ASSERT(n3 == 1 && param_eq(p[0], "namealice"), "tail captured");
    stride_seq_free(e2);
}

static void test_extract_backtrack(void) {
    printf("Test: Extract backtrack (${}$[0]${'.'}$'.'${})\n");

    stride_seq_t *e = stride_seq_new();
    stride_blob_t dot = B(".");
    stride_seq_capture_end(e);
    stride_seq_abs_head(e, 0);
    stride_seq_capture_until(e, &dot);
    stride_seq_skip_bits(e, STRIDE_BITS(1));
    stride_seq_capture_end(e);

    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extract_run(e, 8, "document.pdf", STRIDE_BITS(12), p,
                              MAX_PARAMS, &n) == 0,
           "run ok");
    ASSERT(n == 3, "three params");
    ASSERT(param_eq(p[0], "document.pdf"), "whole first");
    ASSERT(param_eq(p[1], "document"), "stem");
    ASSERT(param_eq(p[2], "pdf"), "ext");

    stride_seq_free(e);
}

static void test_extract_errors(void) {
    printf("Test: Extract errors\n");

    stride_seq_t *e = stride_seq_new();
    stride_seq_capture_steps(e, 2);
    stride_seq_capture_steps(e, 2);
    stride_param_t p[1];
    size_t n = 0;
    ASSERT(stride_extract_run(e, 8, "abcd", STRIDE_BITS(4), p, 1, &n) != 0,
           "capacity exceeded");
    stride_seq_free(e);

    /* 非字节对齐的捕获（步长 1，先走 1 比特再捕获）*/
    e = stride_seq_new();
    stride_seq_step_fwd(e, 1);
    stride_seq_capture_steps(e, 1);
    n = 0;
    ASSERT(stride_extract_run(e, 1, "\xA0", 2, p, 1, &n) != 0,
           "unaligned capture fails");
    stride_seq_free(e);
}

static void test_extract_full(void) {
    printf("Test: Full extractor (multi-segment)\n");

    stride_seq_t *s0 = stride_seq_new();
    stride_seq_capture_steps(s0, 2);

    stride_seq_t *s1 = stride_seq_new();
    stride_seq_capture_end(s1);

    stride_extractor_t *arr[2] = {s0, s1};
    stride_full_extractor_t *full = stride_full_extractor_create(arr, 2);
    ASSERT(full != NULL, "full created");
    ASSERT(full->total_params == 2, "two total params");

    const void *segs[2] = {"ab", "cde"};
    size_t lens[2] = {STRIDE_BITS(2), STRIDE_BITS(3)};
    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_full_extractor_run(full, 8, segs, lens, 2, p, MAX_PARAMS,
                                     &n) == 0,
           "run ok");
    ASSERT(n == 2, "two params");
    ASSERT(param_eq(p[0], "ab") && param_eq(p[1], "cde"), "values");

    size_t n2 = 0;
    ASSERT(stride_full_extractor_run(full, 8, segs, lens, 1, p, MAX_PARAMS,
                                     &n2) != 0,
           "segment count mismatch fails");

    stride_full_extractor_destroy(full);
}

int main(void) {
    printf("=== Stride Extractor Tests ===\n\n");

    test_extract_steps();
    test_extract_until();
    test_extract_backtrack();
    test_extract_errors();
    test_extract_full();

    return test_summary("Extractor");
}
