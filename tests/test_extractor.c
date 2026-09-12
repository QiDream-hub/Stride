#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"
#include "stride/extractor.h"

/* ============================================================
 * 提取序列测试：编译（含优化）+ 运行时提取
 *
 * 单位：步长以比特计；文本示例用步长 8（1 字节/步）。
 * ============================================================ */

#define MAX_PARAMS 16
#define STEP_BYTE 8u

/* ---------------- 编译 ---------------- */

static stride_extractor_op_t *compile_extractors(const char *pattern,
                                                 size_t stride,
                                                 size_t *out_count,
                                                 size_t *out_params) {
    stride_extractor_op_t *ops = NULL;
    size_t count = 0, params = 0;

    if (stride_compile_extract(pattern, 0, stride, &ops, &count, &params) != 0) {
        return NULL;
    }
    *out_count = count;
    *out_params = params;
    return ops;
}

static void test_basic_capture_with_optimization(void) {
    printf("Test: Basic capture with match optimization (${4}$'a')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${4}$'a'", STEP_BYTE, &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extract ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_STEPS, "First is CAPTURE_STEPS");
    ASSERT(ops[0].data.capture_steps.steps == 4, "Capture steps is 4");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_BITS, "Second is SKIP_BITS");
    ASSERT(ops[1].data.skip_bits.bit_len == 8,
           "Skip is 8 bits — no stride baked in");

    stride_extractor_free(ops, count);
}

static void test_capture_until_no_merge(void) {
    printf("Test: Capture-until interrupts merging (${'='}$'='${})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${'='}$'='${}", STEP_BYTE, &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extract ops output");
    ASSERT(params == 2, "Two parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_UNTIL, "First is CAPTURE_UNTIL");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_BITS, "Second is SKIP_BITS");
    ASSERT(ops[2].type == STRIDE_EX_CAPTURE_END, "Third is CAPTURE_END");

    stride_extractor_free(ops, count);
}

static void test_same_unit_merge(void) {
    printf("Test: Same-unit merge, no cross-unit baking (${2}$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}$[>3]$'abc'", STEP_BYTE, &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extract ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_STEPS, "First is CAPTURE_STEPS");
    ASSERT(ops[1].type == STRIDE_EX_JUMP_FWD, "Second is JUMP_FWD");
    ASSERT(ops[1].data.jump_fwd.steps == 3, "Jump is 3 steps");
    ASSERT(ops[2].type == STRIDE_EX_SKIP_BITS, "Third is SKIP_BITS");
    ASSERT(ops[2].data.skip_bits.bit_len == 24,
           "Skip stores 24 bits, not a stride-derived step count");

    stride_extractor_free(ops, count);
}

static void test_find_interrupt_merge(void) {
    printf("Test: Find interrupts merging (${2}$[>'=']$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}$[>'=']$[>3]$'abc'", STEP_BYTE, &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 4, "Four extract ops output");
    ASSERT(ops[0].type == STRIDE_EX_CAPTURE_STEPS, "0 is CAPTURE_STEPS");
    ASSERT(ops[1].type == STRIDE_EX_FIND_FWD, "1 is FIND_FWD");
    ASSERT(ops[2].type == STRIDE_EX_JUMP_FWD, "2 is JUMP_FWD");
    ASSERT(ops[2].data.jump_fwd.steps == 3, "Jump is 3 steps (same-unit merge)");
    ASSERT(ops[3].type == STRIDE_EX_SKIP_BITS, "3 is SKIP_BITS");
    ASSERT(ops[3].data.skip_bits.bit_len == 24, "Skip is 24 bits");

    stride_extractor_free(ops, count);
}

static void test_forward_backward_cancel(void) {
    printf("Test: Forward/backward cancel ($[>5]$[<3]$'key')\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>5]$[<3]$'key'", STEP_BYTE, &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extract ops output");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == STRIDE_EX_JUMP_FWD, "Op 0 is JUMP_FWD");
    ASSERT(ops[0].data.jump_fwd.steps == 2, "Steps is 2 (5-3)");
    ASSERT(ops[1].type == STRIDE_EX_SKIP_BITS, "Op 1 is SKIP_BITS");
    ASSERT(ops[1].data.skip_bits.bit_len == 24, "Literal skip is 24 bits");

    stride_extractor_free(ops, count);
}

static void test_zero_net_merge_dropped(void) {
    printf("Test: Zero net merge dropped ($[>5]$[<5])\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("$[>5]$[<5]", STEP_BYTE, &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 0, "Complete cancel emits nothing");
    ASSERT(params == 0, "No parameters");

    /* 零操作的提取器仍然可用 */
    stride_extractor_t *ex = stride_extractor_create(ops, count);
    ASSERT(ex != NULL, "Empty extractor created");
    ASSERT(ex->param_count == 0, "No params declared");

    stride_param_t out[MAX_PARAMS];
    size_t n = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "", 0, out, MAX_PARAMS, &n) == 0,
           "Empty extractor runs");
    ASSERT(n == 0, "No params produced");
    stride_extractor_destroy(ex);

    stride_extractor_free(ops, count);
}

static void test_unknown_stride_no_cross_merge(void) {
    printf("Test: Unknown stride (0) does not merge across units\n");

    size_t count = 0, params = 0;

    /* 已知步长：字面量跳过与常量移动合并为一步位移 */
    stride_extractor_op_t *ops =
        compile_extractors("$'ab'$[>1]", STEP_BYTE, &count, &params);
    ASSERT(ops != NULL && count == 1, "Known stride merges to one op");
    ASSERT(ops[0].type == STRIDE_EX_SKIP_STEPS, "Merged op is SKIP_STEPS");
    ASSERT(ops[0].data.skip_steps.steps == 3, "3 steps = 2 (literal) + 1");
    stride_extractor_free(ops, count);

    /* 未知步长：不同单位各自保留 */
    ops = compile_extractors("$'ab'$[>1]", 0, &count, &params);
    ASSERT(ops != NULL && count == 2, "Unknown stride keeps two ops");
    ASSERT(ops[0].type == STRIDE_EX_SKIP_STEPS, "First is SKIP_STEPS");
    ASSERT(ops[0].data.skip_steps.bit_len == 16, "Skip is 16 bits");
    ASSERT(ops[1].type == STRIDE_EX_JUMP_FWD, "Second is JUMP_FWD");
    ASSERT(ops[1].data.jump_fwd.steps == 1, "Jump is 1 step");
    stride_extractor_free(ops, count);

    /* 未知步长下同单位仍可合并 */
    ops = compile_extractors("$[>2]$[>3]$[>4]", 0, &count, &params);
    ASSERT(ops != NULL && count == 1, "Same-unit run still merges");
    ASSERT(ops[0].type == STRIDE_EX_JUMP_FWD, "Op is JUMP_FWD");
    ASSERT(ops[0].data.jump_fwd.steps == 9, "Steps is 9 (2+3+4)");
    stride_extractor_free(ops, count);
}

static void test_extractor_object(void) {
    printf("Test: Extractor object lifecycle (${2}${})\n");

    size_t count = 0, params = 0;
    stride_extractor_op_t *ops =
        compile_extractors("${2}${}", STEP_BYTE, &count, &params);
    ASSERT(ops != NULL && params == 2, "Two parameters produced");

    stride_extractor_t *ex = stride_extractor_create(ops, count);
    ASSERT(ex != NULL, "Extractor created");
    ASSERT(ex->op_count == count, "Op count copied");
    ASSERT(ex->param_count == 2, "Param count recomputed as 2");

    stride_extractor_destroy(ex);
    stride_extractor_free(ops, count);
}

/* ---------------- 运行时提取 ---------------- */

static stride_extractor_t *extractor_from_pattern(const char *pattern,
                                                  size_t stride) {
    stride_extractor_op_t *ops = NULL;
    size_t count = 0, params = 0;

    if (stride_compile_extract(pattern, 0, stride, &ops, &count, &params) != 0) {
        return NULL;
    }
    stride_extractor_t *ex = stride_extractor_create(ops, count);
    stride_extractor_free(ops, count);
    return ex;
}

static int param_eq(stride_param_t p, const char *s) {
    size_t n = strlen(s);
    return p.bit_len == STRIDE_BITS(n) && memcmp(p.ptr, s, n) == 0;
}

static void test_run_len_and_end(void) {
    printf("Test: Run length capture and capture-to-end\n");

    stride_extractor_t *ex = extractor_from_pattern("${2}", STEP_BYTE);
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "ab12", STRIDE_BITS(4), params,
                                MAX_PARAMS, &count) == 0,
           "Run succeeded");
    ASSERT(count == 1 && param_eq(params[0], "ab"), "Param is \"ab\"");
    ASSERT(params[0].ptr == (const void *)"ab12",
           "Zero-copy: points into input");
    ASSERT(params[0].bit_len == 16, "Param bit length is 16");
    stride_extractor_destroy(ex);

    ex = extractor_from_pattern("${}", STEP_BYTE);
    ASSERT(ex != NULL, "Extractor built");
    count = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "alice", STRIDE_BITS(5), params,
                                MAX_PARAMS, &count) == 0,
           "Run succeeded");
    ASSERT(count == 1 && param_eq(params[0], "alice"), "Param is \"alice\"");
    stride_extractor_destroy(ex);
}

static void test_run_date(void) {
    printf("Test: Run date segment (${4}$'-'${2}$'-'${2})\n");

    stride_extractor_t *ex =
        extractor_from_pattern("${4}$'-'${2}$'-'${2}", STEP_BYTE);
    ASSERT(ex != NULL, "Extractor built");
    ASSERT(ex->param_count == 3, "Three params declared");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "2024-03-15", STRIDE_BITS(10),
                                params, MAX_PARAMS, &count) == 0,
           "Run succeeded");
    ASSERT(count == 3, "Three parameters");
    ASSERT(param_eq(params[0], "2024"), "Year is 2024");
    ASSERT(param_eq(params[1], "03"), "Month is 03");
    ASSERT(param_eq(params[2], "15"), "Day is 15");

    size_t count2 = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "2024", STRIDE_BITS(4), params,
                                MAX_PARAMS, &count2) == -1,
           "Fails on short input");
    stride_extractor_destroy(ex);
}

static void test_run_backtrack(void) {
    printf("Test: Run backtrack (${}$[0]${'.'}$'.'${})\n");

    stride_extractor_t *ex =
        extractor_from_pattern("${}$[0]${'.'}$'.'${}", STEP_BYTE);
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "document.pdf",
                                STRIDE_BITS(12), params, MAX_PARAMS,
                                &count) == 0,
           "Run succeeded");
    ASSERT(count == 3, "Three parameters");
    ASSERT(param_eq(params[0], "document.pdf"), "Whole name first");
    ASSERT(param_eq(params[1], "document"), "Stem second");
    ASSERT(param_eq(params[2], "pdf"), "Extension third");
    stride_extractor_destroy(ex);
}

static void test_run_multibyte(void) {
    printf("Test: Run multi-bit capture (UTF-8)\n");

    stride_extractor_t *ex = extractor_from_pattern("${'：'}${}", STEP_BYTE);
    ASSERT(ex != NULL, "Extractor built");

    const char *seg = "用户：alice";
    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, seg, STRIDE_BITS(strlen(seg)),
                                params, MAX_PARAMS, &count) == 0,
           "Run succeeded");
    ASSERT(count == 2, "Two parameters");
    ASSERT(param_eq(params[0], "用户"), "Captures up to the full-width colon");
    ASSERT(param_eq(params[1], "：alice"), "Second captures the rest");
    stride_extractor_destroy(ex);
}

static void test_run_utf16(void) {
    printf("Test: Run UTF-16LE extraction (stride 16)\n");

    /* "中A" = AD 4E 41 00 */
    static const unsigned char seg[] = {0xAD, 0x4E, 0x41, 0x00};

    stride_extractor_t *ex = extractor_from_pattern("${1}$'A\\x00'", 16);
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, 16, seg, STRIDE_BITS(4), params,
                                MAX_PARAMS, &count) == 0,
           "Run succeeded");
    ASSERT(count == 1, "One parameter");
    ASSERT(params[0].bit_len == 16, "Param is one 16-bit code unit");
    const unsigned char *p = (const unsigned char *)params[0].ptr;
    ASSERT(p[0] == 0xAD && p[1] == 0x4E, "Param bytes are AD 4E (中)");
    stride_extractor_destroy(ex);

    /* 段长不是步长整数倍 → 失败 */
    stride_extractor_t *ex2 = extractor_from_pattern("${1}", 16);
    size_t count2 = 0;
    ASSERT(stride_extractor_run(ex2, 16, seg, 24, params, MAX_PARAMS,
                                &count2) == -1,
           "Segment bits not divisible by stride fails");
    stride_extractor_destroy(ex2);
}

static void test_run_records(void) {
    printf("Test: Run fixed-size record extraction (stride 32)\n");

    static const unsigned char seg[] = {0x01, 0x02, 0x03, 0x04, 'O',  'K',
                                        0x00, 0x00, 0x05, 0x06, 0x07, 0x08};

    stride_extractor_t *ex =
        extractor_from_pattern("${1}$'OK\\x00\\x00'${1}", 32);
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, 32, seg, STRIDE_BITS(12), params,
                                MAX_PARAMS, &count) == 0,
           "Run succeeded");
    ASSERT(count == 2, "Two record params");
    ASSERT(params[0].bit_len == 32 && params[1].bit_len == 32,
           "Each param is one 32-bit record");
    ASSERT(((const unsigned char *)params[0].ptr)[0] == 0x01, "Record 0");
    ASSERT(((const unsigned char *)params[1].ptr)[0] == 0x05, "Record 2");
    stride_extractor_destroy(ex);
}

static void test_run_bit_alignment_restriction(void) {
    printf("Test: Capture must start byte-aligned\n");

    /* 步长 4：第 1 步从比特 4 开始，非字节对齐 → 拒绝 */
    static const unsigned char seg[] = {0x12, 0x34};
    stride_extractor_t *ex = extractor_from_pattern("$[>1]${2}", 4);
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, 4, seg, STRIDE_BITS(2), params,
                                MAX_PARAMS, &count) == -1,
           "Byte-unaligned capture rejected");
    stride_extractor_destroy(ex);

    /* 从第 0 步开始则允许 */
    ex = extractor_from_pattern("${2}", 4);
    count = 0;
    ASSERT(stride_extractor_run(ex, 4, seg, STRIDE_BITS(2), params,
                                MAX_PARAMS, &count) == 0,
           "Byte-aligned nibble capture accepted");
    ASSERT(count == 1 && params[0].bit_len == 8, "Param is 8 bits");
    stride_extractor_destroy(ex);
}

static void test_run_full(void) {
    printf("Test: Run multi-segment full extractor\n");

    stride_extractor_t *ex0 = extractor_from_pattern("${2}", STEP_BYTE);
    stride_extractor_t *ex1 = extractor_from_pattern("${}", STEP_BYTE);
    ASSERT(ex0 != NULL && ex1 != NULL, "Both segment extractors built");

    stride_extractor_t *arr[2] = {ex0, ex1};
    stride_full_extractor_t *full = stride_full_extractor_create(arr, 2);
    ASSERT(full != NULL, "Full extractor created");
    ASSERT(full->total_params == 2, "Two total params");

    const void *segments[2] = {"ab", "cde"};
    size_t seg_bit_lens[2] = {STRIDE_BITS(2), STRIDE_BITS(3)};
    stride_param_t params[MAX_PARAMS];
    size_t count = 0;
    ASSERT(stride_full_extractor_run(full, STEP_BYTE, segments, seg_bit_lens, 2,
                                     params, MAX_PARAMS, &count) == 0,
           "Full run succeeded");
    ASSERT(count == 2, "Two parameters across segments");
    ASSERT(param_eq(params[0], "ab"), "Param 0 from segment 0");
    ASSERT(param_eq(params[1], "cde"), "Param 1 from segment 1");

    size_t count2 = 0;
    ASSERT(stride_full_extractor_run(full, STEP_BYTE, segments, seg_bit_lens, 1,
                                     params, MAX_PARAMS, &count2) == -1,
           "Fails when segment count mismatches");

    /* destroy 负责释放 ex0/ex1 */
    stride_full_extractor_destroy(full);
}

static void test_run_capacity(void) {
    printf("Test: Run with insufficient param capacity\n");

    stride_extractor_t *ex = extractor_from_pattern("${2}${2}", STEP_BYTE);
    ASSERT(ex != NULL, "Extractor built");

    stride_param_t params[1];
    size_t count = 0;
    ASSERT(stride_extractor_run(ex, STEP_BYTE, "abcd", STRIDE_BITS(4), params, 1,
                                &count) == -1,
           "Fails when capacity is too small");
    stride_extractor_destroy(ex);
}

int main(void) {
    printf("=== Stride Extract Sequence Tests ===\n\n");

    test_basic_capture_with_optimization();
    test_capture_until_no_merge();
    test_const_move_merge();
    test_find_interrupt_merge();
    test_forward_backward_cancel();
    test_zero_net_merge_dropped();
    test_unknown_stride_no_cross_merge();
    test_extractor_object();

    test_run_len_and_end();
    test_run_date();
    test_run_backtrack();
    test_run_multibyte();
    test_run_utf16();
    test_run_records();
    test_run_bit_alignment_restriction();
    test_run_full();
    test_run_capacity();

    return test_summary("Extractor");
}
