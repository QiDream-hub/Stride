#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/extractor.h"
#include "stride/grammar.h"

/* ============================================================
 * 提取序列模块单元测试（编译部分）
 * ============================================================ */

/* 辅助函数：词法分析并编译提取序列 */
static stride_extractor_op_t *compile_extractors(const char *pattern,
                                                 size_t *out_count,
                                                 size_t *out_params) {
    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;
    stride_extractor_op_t *extractors = NULL;
    size_t extractor_count = 0, param_count = 0;

    if (stride_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        return NULL;
    }

    if (stride_extractor_compile(ops, op_count, &extractors,
                                 &extractor_count, &param_count) != 0) {
        stride_ops_free(ops);
        return NULL;
    }

    stride_ops_free(ops);
    *out_count = extractor_count;
    *out_params = param_count;
    return extractors;
}

/* 测试示例 1：基础捕获（含匹配优化）${4}$'a' */
static void test_basic_capture_with_optimization(void) {
    printf("Test: Basic capture with match optimization (${4}$'a')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("${4}$'a'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "First op is EX_CAPTURE_LEN");
    ASSERT(ops[0].data.capture_len.length == 4, "Capture length is 4");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_LEN,
           "Second op is EX_SKIP_LEN (optimized from OP_MATCH)");
    ASSERT(ops[1].data.skip_len.length == 1, "Skip length is 1");

    free(ops);
}

/* 测试示例 2：捕获到字符（无优化）${'='}$'='${} */
static void test_capture_chr_no_optimization(void) {
    printf("Test: Capture char without optimization (${'='}$'='${})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${'='}$'='${}", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extractor ops output");
    ASSERT(params == 2, "Two parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_CHR, "First op is EX_CAPTURE_CHR");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_LEN, "Second op is EX_SKIP_LEN");
    ASSERT(ops[2].type == STRIDE_EX_CAPTURE_END, "Third op is EX_CAPTURE_END");

    free(ops);
}

/* 测试示例 3：常量移动合并 ${2}$[>3]$'abc' */
static void test_const_move_merge(void) {
    printf("Test: Constant move merge (${2}$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}$[>3]$'abc'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "First op is EX_CAPTURE_LEN");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_LEN,
           "Second op is EX_SKIP_LEN (merged)");
    ASSERT(ops[1].data.skip_len.length == 6, "Skip length is 6 (3+3)");

    free(ops);
}

/* 测试示例 4：动态操作打断合并 ${2}$[>'=']$[>3]$'abc' */
static void test_dynamic_interrupt_merge(void) {
    printf("Test: Dynamic interrupt merge (${2}$[>'=']$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}$[>'=']$[>3]$'abc'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "First op is EX_CAPTURE_LEN");
    ASSERT(ops[1].type == STRIDE_EX_FIND_FWD, "Second op is EX_FIND_FWD");
    ASSERT(ops[2].type == STRIDE_EX_SKIP_LEN,
           "Third op is EX_SKIP_LEN (merged)");
    ASSERT(ops[2].data.skip_len.length == 6, "Skip length is 6 (3+3)");

    free(ops);
}

/* 测试示例 5：正向与负向抵消 $[>5]$[<3]$'key' */
static void test_forward_backward_cancel(void) {
    printf("Test: Forward/backward cancel ($[>5]$[<3]$'key')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>5]$[<3]$'key'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output (merged)");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_SKIP_LEN,
           "Op is EX_SKIP_LEN (merged with match)");
    ASSERT(ops[0].data.skip_len.length == 5, "Skip length is 5 (5-3+3)");

    free(ops);
}

/* 测试：仅匹配操作 $'hello' */
static void test_match_only(void) {
    printf("Test: Match only ($'hello')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("$'hello'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_SKIP_LEN, "Op is EX_SKIP_LEN");
    ASSERT(ops[0].data.skip_len.length == 5, "Skip length is 5");

    free(ops);
}

/* 测试：仅捕获操作 ${10} */
static void test_capture_only(void) {
    printf("Test: Capture only (${10})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("${10}", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_LEN, "Op is EX_CAPTURE_LEN");
    ASSERT(ops[0].data.capture_len.length == 10, "Capture length is 10");

    free(ops);
}

/* 测试：多个连续常量移动合并 $[>2]$[>3]$[>4] */
static void test_multiple_const_moves(void) {
    printf("Test: Multiple constant moves ($[>2]$[>3]$[>4])\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>2]$[>3]$[>4]", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output (all merged)");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_JUMP_FWD, "Op is EX_JUMP_FWD");
    ASSERT(ops[0].data.jump_fwd.offset == 9, "Jump offset is 9 (2+3+4)");

    free(ops);
}

/* 测试：正向负向完全抵消 $[>5]$[<5] */
static void test_complete_cancel(void) {
    printf("Test: Complete cancel ($[>5]$[<5])\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>5]$[<5]", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 0, "Zero extractor ops output (completely cancelled)");
    ASSERT(params == 0, "No parameters produced");

    free(ops);
}

/* 测试：捕获操作打断合并，净位移为 0 的合并段被丢弃
 *
 * ${}$[<4]$'dddd'：EX_CAPTURE_END 打断；其后 JUMP_BACK(4)+SKIP_LEN(4)
 * 净位移为 0，语义上是无操作，不应产生任何操作。
 * （这正是相对 URLRouter 原始实现修复的越界输出问题）
 */
static void test_zero_net_merge_dropped(void) {
    printf("Test: Zero net merge dropped (${}$[<4]$'dddd')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${}$[<4]$'dddd'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "Only the capture op remains");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_END, "Op is EX_CAPTURE_END");

    /* 参数数量必须与提取序列中实际产生的捕获操作数一致 */
    stride_extractor_t *ex = stride_extractor_create(ops, count);
    ASSERT(ex != NULL, "Extractor created");
    ASSERT(ex->param_count == 1, "Runtime param_count is 1");
    stride_extractor_destroy(ex);

    free(ops);
}

/* 测试：提取器对象创建 / 销毁与参数计数 */
static void test_extractor_object(void) {
    printf("Test: Extractor object lifecycle (${2}${})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops = compile_extractors("${2}${}", &count, &params);
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(params == 2, "Two parameters produced");

    stride_extractor_t *ex = stride_extractor_create(ops, count);
    ASSERT(ex != NULL, "Extractor created");
    ASSERT(ex->op_count == count, "Op count copied");
    ASSERT(ex->param_count == 2, "Param count recomputed as 2");

    stride_extractor_destroy(ex);
    free(ops);
}

int main(void) {
    printf("=== Stride Extractor Sequence Unit Tests ===\n\n");

    test_basic_capture_with_optimization();
    test_capture_chr_no_optimization();
    test_const_move_merge();
    test_dynamic_interrupt_merge();
    test_forward_backward_cancel();
    test_match_only();
    test_capture_only();
    test_multiple_const_moves();
    test_complete_cancel();
    test_zero_net_merge_dropped();
    test_extractor_object();

    return test_summary("Extractor");
}
