#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"
#include "stride/extractor.h"

/* ============================================================
 * 提取序列运行时测试（端到端：编译 → 执行）
 * ============================================================ */

#define MAX_PARAMS 16

/* 从模式编译出单段提取器 */
static stride_extractor_t *extractor_from_pattern(const char *pattern) {
    stride_compile_result_t r = stride_compile(pattern);
    if (r.status != STRIDE_OK) {
        printf("  (compile failed: %s)\n", r.error_msg ? r.error_msg : "?");
        stride_compile_free(&r);
        return NULL;
    }

    stride_extractor_t *ex =
        stride_extractor_create(r.extractors, r.extractor_count);
    stride_compile_free(&r);
    return ex;
}

static int param_eq(stride_param_t p, const char *s) {
    size_t n = strlen(s);
    return p.len == n && n > 0 && strncmp(p.ptr, s, n) == 0;
}

/* 定长捕获：${2} */
static void test_exec_len(void) {
    printf("Test: Execute length capture (${2} on \"ab12\")\n");

    stride_extractor_t *ex = extractor_from_pattern("${2}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    int ret = stride_extractor_execute(ex, "ab12", 4, params, MAX_PARAMS,
                                       &count);
    ASSERT(ret == 0, "Execute succeeded");
    ASSERT(count == 1, "One parameter");
    ASSERT(param_eq(params[0], "ab"), "Param is \"ab\"");
    ASSERT(params[0].ptr == "ab12", "Zero-copy: points into input");

    stride_extractor_destroy(ex);
}

/* 捕获到结尾：${} */
static void test_exec_capture_end(void) {
    printf("Test: Execute capture-to-end (${} on \"alice\")\n");

    stride_extractor_t *ex = extractor_from_pattern("${}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "alice", 5, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 1, "One parameter");
    ASSERT(param_eq(params[0], "alice"), "Param is \"alice\"");
    ASSERT(params[0].ptr == "alice", "Zero-copy: points into input");

    stride_extractor_destroy(ex);
}

/* 日期：${4}$'-'${2}$'-'${2} */
static void test_exec_date(void) {
    printf("Test: Execute date segment (${4}$'-'${2}$'-'${2})\n");

    stride_extractor_t *ex =
        extractor_from_pattern("${4}$'-'${2}$'-'${2}");
    ASSERT(ex != NULL, "Extractor built");
    ASSERT(ex->param_count == 3, "Three params declared");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "2024-03-15", 10, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 3, "Three parameters");
    ASSERT(param_eq(params[0], "2024"), "Year is 2024");
    ASSERT(param_eq(params[1], "03"), "Month is 03");
    ASSERT(param_eq(params[2], "15"), "Day is 15");

    /* 输入过短应失败 */
    size_t count2 = 0;
    ASSERT(stride_extractor_execute(ex, "2024", 4, params, MAX_PARAMS,
                                    &count2) == -1,
           "Fails on short input");

    stride_extractor_destroy(ex);
}

/* 版本号：$'v'${'.'}$'.'${} */
static void test_exec_version(void) {
    printf("Test: Execute version segment ($'v'${'.'}$'.'${})\n");

    stride_extractor_t *ex = extractor_from_pattern("$'v'${'.'}$'.'${}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "v2.0", 4, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 2, "Two parameters");
    ASSERT(param_eq(params[0], "2"), "Major is 2");
    ASSERT(param_eq(params[1], "0"), "Minor is 0");

    stride_extractor_destroy(ex);
}

/* 从段尾反向查找：$[<'=']${} */
static void test_exec_find_rev(void) {
    printf("Test: Execute reverse find ($[<'=']${} on \"name=alice\")\n");

    stride_extractor_t *ex = extractor_from_pattern("$[<'=']${}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "name=alice", 10, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 1, "One parameter");
    ASSERT(param_eq(params[0], "=alice"), "Param is \"=alice\"");

    stride_extractor_destroy(ex);
}

/* 回溯捕获：${}$[0]${'.'}$'.'${} */
static void test_exec_backtrack(void) {
    printf("Test: Execute backtrack (${}$[0]${'.'}$'.'${})\n");

    stride_extractor_t *ex =
        extractor_from_pattern("${}$[0]${'.'}$'.'${}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "document.pdf", 12, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 3, "Three parameters");
    ASSERT(param_eq(params[0], "document.pdf"), "Whole name first");
    ASSERT(param_eq(params[1], "document"), "Stem second");
    ASSERT(param_eq(params[2], "pdf"), "Extension third");

    stride_extractor_destroy(ex);
}

/* END 回退：${}$[<3]$'pdf'
 *
 * 提取阶段不重复验证关键字：$[<3]$'pdf' 的净位移为 0，因此在提取序列
 * 中被完全消除（关键字正确性由匹配阶段负责）。这里验证提取结果只剩
 * “捕获到段尾”这一个参数。
 */
static void test_exec_end_back(void) {
    printf("Test: Execute END back-off (${}$[<3]$'pdf')\n");

    stride_extractor_t *ex = extractor_from_pattern("${}$[<3]$'pdf'");
    ASSERT(ex != NULL, "Extractor built");
    ASSERT(ex->op_count == 1, "Trailing net-zero movement eliminated");
    ASSERT(ex->ops[0].type == STRIDE_EX_CAPTURE_END,
           "Only capture-to-end remains");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "document.pdf", 12, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 1, "One parameter");
    ASSERT(param_eq(params[0], "document.pdf"), "Captured whole segment");

    stride_extractor_destroy(ex);
}

/* 多段完整提取器 */
static void test_exec_full(void) {
    printf("Test: Execute multi-segment full extractor\n");

    stride_extractor_t *ex0 = extractor_from_pattern("${2}");
    stride_extractor_t *ex1 = extractor_from_pattern("${}");
    ASSERT(ex0 != NULL && ex1 != NULL, "Both segment extractors built");

    stride_extractor_t *arr[2] = {ex0, ex1};
    stride_full_extractor_t *full = stride_full_extractor_create(arr, 2);
    ASSERT(full != NULL, "Full extractor created");
    ASSERT(full->total_params == 2, "Two total params");

    const char *segments[2] = {"ab", "cde"};
    size_t seg_lens[2] = {2, 3};
    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_full_extractor_execute(full, segments, seg_lens, 2, params,
                                         MAX_PARAMS, &count) == 0,
           "Full execute succeeded");
    ASSERT(count == 2, "Two parameters across segments");
    ASSERT(param_eq(params[0], "ab"), "Param 0 from segment 0");
    ASSERT(param_eq(params[1], "cde"), "Param 1 from segment 1");

    /* 段数不匹配应失败 */
    size_t count2 = 0;
    ASSERT(stride_full_extractor_execute(full, segments, seg_lens, 1, params,
                                         MAX_PARAMS, &count2) == -1,
           "Fails when segment count mismatches");

    /* destroy 负责释放 ex0/ex1 */
    stride_full_extractor_destroy(full);
}

/* 参数容量不足 */
static void test_exec_capacity(void) {
    printf("Test: Execute with insufficient param capacity\n");

    stride_extractor_t *ex = extractor_from_pattern("${2}${2}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[1];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "abcd", 4, params, 1, &count) == -1,
           "Fails when capacity is too small");

    stride_extractor_destroy(ex);
}

int main(void) {
    printf("=== Stride Extractor Runtime Tests ===\n\n");

    test_exec_len();
    test_exec_capture_end();
    test_exec_date();
    test_exec_version();
    test_exec_find_rev();
    test_exec_backtrack();
    test_exec_end_back();
    test_exec_full();
    test_exec_capacity();

    return test_summary("Extractor Runtime");
}
