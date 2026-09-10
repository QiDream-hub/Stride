#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"
#include "stride/extractor.h"

/* ============================================================
 * 提取序列测试：编译（含优化）+ 运行时提取
 * ============================================================ */

#define MAX_PARAMS 16

/* ---------------- 编译 ---------------- */

static stride_extractor_op_t *compile_extractors(const char *pattern,
                                                 size_t *out_count,
                                                 size_t *out_params) {
    stride_extractor_op_t *ops = NULL;
    size_t count = 0, params = 0;

    if (stride_compile_extractors(pattern, &ops, &count, &params) != 0) {
        return NULL;
    }
    *out_count = count;
    *out_params = params;
    return ops;
}

static void test_basic_capture_with_optimization(void) {
    printf("Test: Basic capture with match optimization (${4}$'a')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("${4}$'a'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "First op is CAPTURE_LEN");
    ASSERT(ops[0].data.capture_len.length == 4, "Capture length is 4");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_LEN,
           "Second op is SKIP_LEN (optimized from MATCH)");
    ASSERT(ops[1].data.skip_len.length == 1, "Skip length is 1");

    free(ops);
}

static void test_capture_chr_no_optimization(void) {
    printf("Test: Capture char without optimization (${'='}$'='${})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${'='}$'='${}", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extractor ops output");
    ASSERT(params == 2, "Two parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_CHR, "First op is CAPTURE_CHR");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_LEN, "Second op is SKIP_LEN");
    ASSERT(ops[2].type == STRIDE_EX_CAPTURE_END, "Third op is CAPTURE_END");

    free(ops);
}

static void test_const_move_merge(void) {
    printf("Test: Constant move merge (${2}$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}$[>3]$'abc'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "First op is CAPTURE_LEN");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_LEN, "Second op is SKIP_LEN (merged)");
    ASSERT(ops[1].data.skip_len.length == 6, "Skip length is 6 (3+3)");

    free(ops);
}

static void test_dynamic_interrupt_merge(void) {
    printf("Test: Dynamic interrupt merge (${2}$[>'=']$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}$[>'=']$[>3]$'abc'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extractor ops output");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "First op is CAPTURE_LEN");
    ASSERT(ops[1].type == STRIDE_EX_FIND_FWD, "Second op is FIND_FWD");
    ASSERT(ops[2].type == STRIDE_EX_SKIP_LEN, "Third op is SKIP_LEN (merged)");
    ASSERT(ops[2].data.skip_len.length == 6, "Skip length is 6 (3+3)");

    free(ops);
}

static void test_forward_backward_cancel(void) {
    printf("Test: Forward/backward cancel ($[>5]$[<3]$'key')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>5]$[<3]$'key'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output (merged)");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_SKIP_LEN, "Op is SKIP_LEN");
    ASSERT(ops[0].data.skip_len.length == 5, "Skip length is 5 (5-3+3)");

    free(ops);
}

static void test_match_and_capture_only(void) {
    printf("Test: Match-only and capture-only\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("$'hello'", &count, &params);
    ASSERT(ops != NULL && count == 1, "One op for match-only");
    ASSERT(params == 0, "No parameters");
    ASSERT(ops[0].type == STRIDE_EX_SKIP_LEN, "Op is SKIP_LEN");
    ASSERT(ops[0].data.skip_len.length == 5, "Skip length is 5");
    free(ops);

    ops = compile_extractors("${10}", &count, &params);
    ASSERT(ops != NULL && count == 1, "One op for capture-only");
    ASSERT(params == 1, "One parameter");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "Op is CAPTURE_LEN");
    ASSERT(ops[0].data.capture_len.length == 10, "Capture length is 10");
    free(ops);
}

static void test_multiple_const_moves_and_cancel(void) {
    printf("Test: Multiple const moves and complete cancel\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>2]$[>3]$[>4]", &count, &params);
    ASSERT(ops != NULL && count == 1, "All moves merged into one");
    ASSERT(ops[0].type == STRIDE_EX_JUMP_FWD, "Op is JUMP_FWD");
    ASSERT(ops[0].data.jump_fwd.offset == 9, "Offset is 9 (2+3+4)");
    free(ops);

    ops = compile_extractors("$[>5]$[<5]", &count, &params);
    ASSERT(ops != NULL && count == 0, "Complete cancel emits nothing");
    ASSERT(params == 0, "No parameters");
    free(ops);
}

static void test_zero_net_merge_dropped(void) {
    printf("Test: Zero net merge dropped (${}$[<4]$'dddd')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${}$[<4]$'dddd'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "Only the capture op remains");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_END, "Op is CAPTURE_END");

    stride_extractor_t *ex = stride_extractor_create(ops, count);
    ASSERT(ex != NULL, "Extractor created");
    ASSERT(ex->param_count == 1, "Runtime param_count is 1");
    stride_extractor_destroy(ex);

    free(ops);
}

static void test_extractor_object(void) {
    printf("Test: Extractor object lifecycle (${2}${})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("${2}${}", &count, &params);
    ASSERT(ops != NULL && params == 2, "Two parameters produced");

    stride_extractor_t *ex = stride_extractor_create(ops, count);
    ASSERT(ex != NULL, "Extractor created");
    ASSERT(ex->op_count == count, "Op count copied");
    ASSERT(ex->param_count == 2, "Param count recomputed as 2");

    stride_extractor_destroy(ex);
    free(ops);
}

/* ---------------- 运行时提取 ---------------- */

static stride_extractor_t *extractor_from_pattern(const char *pattern) {
    stride_extractor_op_t *ops = NULL;
    size_t count = 0, params = 0;

    if (stride_compile_extractors(pattern, &ops, &count, &params) != 0) {
        return NULL;
    }
    stride_extractor_t *ex = stride_extractor_create(ops, count);
    free(ops);
    return ex;
}

static int param_eq(stride_param_t p, const char *s) {
    size_t n = strlen(s);
    return p.len == n && n > 0 && strncmp(p.ptr, s, n) == 0;
}

static void test_exec_len_and_end(void) {
    printf("Test: Execute length capture and capture-to-end\n");

    stride_extractor_t *ex = extractor_from_pattern("${2}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "ab12", 4, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 1 && param_eq(params[0], "ab"), "Param is \"ab\"");
    ASSERT(params[0].ptr == "ab12", "Zero-copy: points into input");
    stride_extractor_destroy(ex);

    ex = extractor_from_pattern("${}");
    ASSERT(ex != NULL, "Extractor built");
    count = 0;
    ASSERT(stride_extractor_execute(ex, "alice", 5, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 1 && param_eq(params[0], "alice"), "Param is \"alice\"");
    ASSERT(params[0].ptr == "alice", "Zero-copy: points into input");
    stride_extractor_destroy(ex);
}

static void test_exec_date(void) {
    printf("Test: Execute date segment (${4}$'-'${2}$'-'${2})\n");

    stride_extractor_t *ex = extractor_from_pattern("${4}$'-'${2}$'-'${2}");
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

    size_t count2 = 0;
    ASSERT(stride_extractor_execute(ex, "2024", 4, params, MAX_PARAMS,
                                    &count2) == -1,
           "Fails on short input");
    stride_extractor_destroy(ex);
}

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

static void test_exec_find_rev(void) {
    printf("Test: Execute reverse find ($[<'=']${})\n");

    stride_extractor_t *ex = extractor_from_pattern("$[<'=']${}");
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_execute(ex, "name=alice", 10, params, MAX_PARAMS,
                                    &count) == 0,
           "Execute succeeded");
    ASSERT(count == 1 && param_eq(params[0], "=alice"),
           "Param is \"=alice\"");
    stride_extractor_destroy(ex);
}

static void test_exec_backtrack(void) {
    printf("Test: Execute backtrack (${}$[0]${'.'}$'.'${})\n");

    stride_extractor_t *ex = extractor_from_pattern("${}$[0]${'.'}$'.'${}");
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
    ASSERT(count == 1 && param_eq(params[0], "document.pdf"),
           "Captured whole segment");
    stride_extractor_destroy(ex);
}

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

    size_t count2 = 0;
    ASSERT(stride_full_extractor_execute(full, segments, seg_lens, 1, params,
                                         MAX_PARAMS, &count2) == -1,
           "Fails when segment count mismatches");

    /* destroy 负责释放 ex0/ex1 */
    stride_full_extractor_destroy(full);
}

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
    printf("=== Stride Extractor Sequence Tests ===\n\n");

    test_basic_capture_with_optimization();
    test_capture_chr_no_optimization();
    test_const_move_merge();
    test_dynamic_interrupt_merge();
    test_forward_backward_cancel();
    test_match_and_capture_only();
    test_multiple_const_moves_and_cancel();
    test_zero_net_merge_dropped();
    test_extractor_object();

    test_exec_len_and_end();
    test_exec_date();
    test_exec_version();
    test_exec_find_rev();
    test_exec_backtrack();
    test_exec_end_back();
    test_exec_full();
    test_exec_capacity();

    return test_summary("Extractor");
}
