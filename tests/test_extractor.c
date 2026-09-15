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
    b.len = strlen(s);
    return b;
}

static int param_eq(stride_param_t p, const char *s) {
    size_t n = strlen(s);
    return p.len == n && n > 0 && memcmp(p.ptr, s, n) == 0;
}

static void test_extract_bytes(void) {
    printf("Test: Extract bytes (date segment)\n");

    /* ${4}$'-'${2}$'-'${2} */
    stride_seq_t *e = stride_seq_new();
    stride_blob_t dash = B("-");
    stride_seq_capture_until(e, &dash);  /* 捕获 "2024"，跳过 '-' */
    stride_seq_capture_until(e, &dash);  /* 捕获 "03"，跳过 '-' */
    stride_seq_capture_end(e);           /* 捕获 "15" */
    ASSERT(stride_seq_param_count(e) == 3, "three params declared");

    const char *date = "2024-03-15";
    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extract_run(e, date, 10, p, MAX_PARAMS, &n) == 0,
           "run ok");
    ASSERT(n == 3, "three params");
    ASSERT(param_eq(p[0], "2024") && param_eq(p[1], "03") &&
               param_eq(p[2], "15"),
           "values");
    ASSERT(p[0].ptr == date, "zero-copy into input");

    /* 短输入时，CAPTURE_UNTIL 会捕获到段尾，不会失败 */
    size_t n2 = 0;
    ASSERT(stride_extract_run(e, "2024", 4, p, MAX_PARAMS, &n2) == 0,
           "short input captures what it can");
    ASSERT(n2 == 3, "still three params (some may be empty)");
    ASSERT(param_eq(p[0], "2024"), "first param captured");
    stride_seq_free(e);
}

static void test_extract_until(void) {
    printf("Test: Extract until delimiter\n");

    /* ${'='}${} on "name=alice" */
    stride_seq_t *e = stride_seq_new();
    stride_blob_t eq = B("=");
    stride_seq_capture_until(e, &eq);  /* 捕获 "name"，跳过 '=' */
    stride_seq_capture_end(e);         /* 捕获 "alice" */

    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extract_run(e, "name=alice", 10, p,
                              MAX_PARAMS, &n) == 0,
           "run ok");
    ASSERT(n == 2, "two params");
    ASSERT(param_eq(p[0], "name"), "captured name");
    ASSERT(param_eq(p[1], "alice"), "captured alice");

    /* 定界串缺失时捕获到段尾 */
    stride_seq_t *e2 = stride_seq_new();
    stride_seq_capture_until(e2, &eq);
    size_t n2 = 0;
    ASSERT(stride_extract_run(e2, "namealice", 9, p,
                              MAX_PARAMS, &n2) == 0,
           "standalone capture-until runs");
    ASSERT(n2 == 1 && param_eq(p[0], "namealice"), "tail captured");
    stride_seq_free(e2);
}

static void test_extract_backtrack(void) {
    printf("Test: Extract backtrack (${}$[0]${'.'}$'.'${})\n");

    stride_seq_t *e = stride_seq_new();
    stride_blob_t dot = B(".");
    stride_seq_capture_end(e);         /* 捕获 "document.pdf" */
    stride_seq_abs_head(e, 0);         /* 回到段首 */
    stride_seq_capture_until(e, &dot); /* 捕获 "document"，跳过 '.' */
    stride_seq_capture_end(e);         /* 捕获 "pdf" */

    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extract_run(e, "document.pdf", 12, p,
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
    stride_seq_capture_bytes(e, 2);
    stride_seq_capture_bytes(e, 2);
    stride_param_t p[1];
    size_t n = 0;
    ASSERT(stride_extract_run(e, "abcd", 4, p, 1, &n) != 0,
           "capacity exceeded");
    stride_seq_free(e);
}

static void test_extract_full(void) {
    printf("Test: Full extractor (multi-segment)\n");

    stride_seq_t *s0 = stride_seq_new();
    stride_seq_capture_bytes(s0, 2);

    stride_seq_t *s1 = stride_seq_new();
    stride_seq_capture_end(s1);

    stride_extractor_t *arr[2] = {s0, s1};
    stride_full_extractor_t *full = stride_full_extractor_create(arr, 2);
    ASSERT(full != NULL, "full created");
    ASSERT(full->total_params == 2, "two total params");

    const void *segs[2] = {"ab", "cde"};
    size_t lens[2] = {2, 3};
    stride_param_t p[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_full_extractor_run(full, segs, lens, 2, p, MAX_PARAMS,
                                     &n) == 0,
           "run ok");
    ASSERT(n == 2, "two params");
    ASSERT(param_eq(p[0], "ab") && param_eq(p[1], "cde"), "values");

    size_t n2 = 0;
    ASSERT(stride_full_extractor_run(full, segs, lens, 1, p, MAX_PARAMS,
                                     &n2) != 0,
           "segment count mismatch fails");

    stride_full_extractor_destroy(full);
}

int main(void) {
    printf("=== Stride Extractor Tests ===\n\n");

    test_extract_bytes();
    test_extract_until();
    test_extract_backtrack();
    test_extract_errors();
    test_extract_full();

    return test_summary("Extractor");
}
