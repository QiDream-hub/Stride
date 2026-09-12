#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"
#include "stride/matcher.h"

/* ============================================================
 * 匹配序列测试：编译（状态机）+ 匹配（步长 / 比特）
 *
 * 单位：步长以比特计；文本示例用步长 8（1 字节/步）。
 * ============================================================ */

#define STEP_BYTE 8u

/* ---------------- 编译 ---------------- */

static stride_match_op_t *compile_match(const char *pattern, size_t stride,
                                        size_t *out_count) {
    stride_match_op_t *m = NULL;
    size_t count = 0, capacity = 0;

    if (stride_compile_match(pattern, 0, stride, &m, &count, &capacity) != 0) {
        return NULL;
    }
    *out_count = count;
    return m;
}

static void test_const_addition(void) {
    printf("Test: Constant addition (${1}${1}${1}$'key')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("${1}${1}${1}$'key'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One tuple output");
    ASSERT(m[0].type == STRIDE_MT_STEP_FWD, "Type is STEP_FWD");
    ASSERT(m[0].steps == 3, "Steps is 3 (1+1+1)");
    ASSERT(m[0].expect.bit_len == 24, "Expect is 24 bits");
    ASSERT(memcmp(m[0].expect.data, "key", 3) == 0, "Expect is 'key'");

    stride_match_free(m, count);
}

static void test_find_interrupt(void) {
    printf("Test: Find interrupt (${2}${'a'}${3}$'b')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("${2}${'a'}${3}$'b'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three tuples output");
    ASSERT(m[0].type == STRIDE_MT_STEP_FWD && m[0].steps == 2, "First STEP_FWD(2)");
    ASSERT(m[1].type == STRIDE_MT_FIND_FWD, "Second is FIND_FWD");
    ASSERT(m[1].delimiter.bit_len == 8, "Delimiter is 8 bits");
    ASSERT(memcmp(m[1].delimiter.data, "a", 1) == 0, "Delimiter is 'a'");
    ASSERT(m[2].type == STRIDE_MT_STEP_FWD && m[2].steps == 3, "Third STEP_FWD(3)");
    ASSERT(m[2].expect.bit_len == 8, "Third carries expect 'b'");

    stride_match_free(m, count);
}

static void test_abs_head_addition(void) {
    printf("Test: HEAD absolute addition ($[5]${2}$'key')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("$[5]${2}$'key'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One tuple output");
    ASSERT(m[0].type == STRIDE_MT_ABS_HEAD, "Type is ABS_HEAD");
    ASSERT(m[0].steps == 7, "Steps is 7 (5+2)");

    stride_match_free(m, count);
}

static void test_end_merge(void) {
    printf("Test: END merge (${}$[<4]$'dddd')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("${}$[<4]$'dddd'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One tuple output");
    ASSERT(m[0].type == STRIDE_MT_ABS_END, "Type is ABS_END");
    ASSERT(m[0].steps == 4, "Steps is 4 (END-4，非负幅度)");

    stride_match_free(m, count);
}

static void test_find_literal_merge(void) {
    printf("Test: Find + literal merge (${'a'}$'key')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("${'a'}$'key'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One tuple output");
    ASSERT(m[0].type == STRIDE_MT_FIND_FWD, "Type is FIND_FWD");
    ASSERT(m[0].delimiter.bit_len == 8, "Delimiter is 'a'");
    ASSERT(m[0].expect.bit_len == 24, "Expect is 'key'");

    stride_match_free(m, count);
}

static void test_find_rev_literal_merge(void) {
    printf("Test: Reverse find + literal ($[<'=']$'key')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("$[<'=']$'key'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One tuple output");
    ASSERT(m[0].type == STRIDE_MT_FIND_REV, "Type is FIND_REV");
    ASSERT(m[0].delimiter.bit_len == 8, "Delimiter is '='");
    ASSERT(m[0].expect.bit_len == 24, "Expect is 'key'");

    stride_match_free(m, count);
}

static void test_consecutive_literals(void) {
    printf("Test: Consecutive literals ($'dd'$'aaa')\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("$'dd'$'aaa'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two tuples output");
    ASSERT(m[0].steps == 0 && m[1].steps == 0, "Both move 0 steps");

    stride_match_free(m, count);
}

static void test_stride_independent_artifact(void) {
    printf("Test: Artifact is stride-independent (compile with stride 0)\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("${1}$'ab'", 0, &count);
    ASSERT(m != NULL, "Compiled with unknown stride");
    ASSERT(count == 1, "One tuple");
    ASSERT(m[0].expect.bit_len == 16, "Expect is 16 bits regardless of stride");

    /* 同一份产物按步长 8 执行 */
    const char seg8[] = "xab"; /* 24 比特，步长 8 → N=3 */
    ASSERT(stride_match_run(m, count, 8, seg8, STRIDE_BITS(3)) == 0,
           "Matches with stride 8");

    stride_match_free(m, count);
}

static void test_multibyte_compile(void) {
    printf("Test: Multi-bit literal (UTF-8)\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("$'用户'", STEP_BYTE, &count);

    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One tuple");
    ASSERT(m[0].expect.bit_len == 48, "48 bits (2 x 3 bytes)");

    stride_match_free(m, count);

    /* 多比特定界 */
    m = compile_match("$[>'：']", STEP_BYTE, &count);
    ASSERT(m != NULL, "Full-width colon compiles");
    ASSERT(m[0].delimiter.bit_len == 24, "Full-width colon is 24 bits");
    stride_match_free(m, count);
}

/* ---------------- 匹配 ---------------- */

static int match_pattern(const char *pattern, const char *segment) {
    size_t count = 0;
    stride_match_op_t *m = compile_match(pattern, STEP_BYTE, &count);
    if (!m) {
        return -2;
    }
    int rc = stride_match_run(m, count, STEP_BYTE, segment,
                              STRIDE_BITS(strlen(segment)));
    stride_match_free(m, count);
    return rc;
}

static void test_match_basic(void) {
    printf("Test: Basic matching\n");

    ASSERT(match_pattern("$'user'", "user") == 0, "Exact segment matches");
    ASSERT(match_pattern("$'user'", "usera") == -1,
           "Trailing byte breaks segment-end alignment");
    ASSERT(match_pattern("$'user'", "us") == -1, "Too short fails");

    ASSERT(match_pattern("${2}", "ab") == 0, "Exactly 2 steps matches");
    ASSERT(match_pattern("${2}", "abc") == -1, "3 bytes fails alignment");
    ASSERT(match_pattern("${2}", "a") == -1, "1 byte fails movement");

    ASSERT(match_pattern("${}", "alice") == 0, "Any segment matches");
    ASSERT(match_pattern("${}", "") == 0, "Empty segment matches");

    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "v2.0") == 0, "v2.0 matches");
    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "v2") == -1, "v2 fails");
    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "v2.0.1") == 0,
           "Trailing bytes absorbed by capture-end");
    ASSERT(match_pattern("$[<'=']${}", "name=alice") == 0, "Reverse find works");
    ASSERT(match_pattern("$[<'=']${}", "namealice") == -1, "No '=' fails");
}

static void test_match_multibyte(void) {
    printf("Test: Multi-bit matching (UTF-8)\n");

    ASSERT(match_pattern("$'用户'", "用户") == 0, "Whole UTF-8 literal matches");
    ASSERT(match_pattern("$'用户'${}", "用户：alice") == 0,
           "Literal + capture-end matches");
    ASSERT(match_pattern("$'用户'${}", "用卢：alice") == -1,
           "Different literal fails");

    ASSERT(match_pattern("$'用户'${'：'}$'：'${}", "用户：alice") == 0,
           "Multi-bit delimiter captured then consumed");
    ASSERT(match_pattern("$[>'：']$[>3]${}", "用户：alice") == 0,
           "Multi-bit find + skip");
    ASSERT(match_pattern("$[>'：']$[>3]${}", "用户alice") == -1,
           "Missing full-width colon fails");
}

static void test_match_utf16(void) {
    printf("Test: UTF-16LE matching (stride 16)\n");

    /* "中A" = AD 4E 41 00 */
    static const unsigned char seg[] = {0xAD, 0x4E, 0x41, 0x00};
    size_t count = 0;

    stride_match_op_t *m = compile_match("${1}$'A\\x00'", 16, &count);
    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(stride_match_run(m, count, 16, seg, STRIDE_BITS(4)) == 0,
           "UTF-16LE segment matches");
    ASSERT(stride_match_run(m, count, 16, seg, 24) == -1,
           "Segment length not a multiple of stride fails");
    stride_match_free(m, count);

    count = 0;
    m = compile_match("$'A\\x00'", 16, &count);
    ASSERT(stride_match_run(m, count, 16, seg + 2, STRIDE_BITS(2)) == 0,
           "Second code unit matches 'A'");
    stride_match_free(m, count);
}

static void test_match_records(void) {
    printf("Test: Fixed-size records (stride 32)\n");

    /* 3 条记录，每条 4 字节；记录 1 = "OK\0\0" */
    static const unsigned char seg[] = {0x01, 0x02, 0x03, 0x04, 'O',  'K',
                                        0x00, 0x00, 0x05, 0x06, 0x07, 0x08};
    size_t count = 0;

    stride_match_op_t *m = compile_match("${1}$'OK\\x00\\x00'${1}", 32, &count);
    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(stride_match_run(m, count, 32, seg, STRIDE_BITS(12)) == 0,
           "Record pattern matches");
    stride_match_free(m, count);

    /* 同一模式按步长 8（字节）解释时长度不匹配 */
    count = 0;
    m = compile_match("${1}$'OK\\x00\\x00'${1}", 8, &count);
    ASSERT(stride_match_run(m, count, 8, seg, STRIDE_BITS(12)) == -1,
           "Byte stride misaligns with record layout");
    stride_match_free(m, count);
}

static void test_match_bit_level(void) {
    printf("Test: Non-byte-aligned bit comparison (stride 4)\n");

    /* 0x12 0x34 = 0001 0010 0011 0100，步长 4 → 4 步 */
    static const unsigned char seg[] = {0x12, 0x34};
    size_t count = 0;

    /* 第 1 步（比特偏移 4）起的 8 比特 = 0010 0011 = 0x23 */
    stride_match_op_t *m = compile_match("$[>1]$'\\x23'${1}", 4, &count);
    ASSERT(m != NULL, "Compilation succeeded");
    ASSERT(stride_match_run(m, count, 4, seg, STRIDE_BITS(2)) == 0,
           "Unaligned 8-bit literal matches at bit offset 4");
    stride_match_free(m, count);

    /* 同一模式按步长 8 解释时，第 1 步越界 */
    count = 0;
    m = compile_match("$[>1]$'\\x23'${1}", 8, &count);
    ASSERT(stride_match_run(m, count, 8, seg, STRIDE_BITS(2)) == -1,
           "Byte stride rejects the same pattern");
    stride_match_free(m, count);

    /* 正向查找也可以落在非字节边界：0x34 的比特起点是第 2 步 */
    count = 0;
    m = compile_match("$[>'\\x34']$[>2]", 4, &count);
    ASSERT(m != NULL, "Find pattern compiles");
    ASSERT(stride_match_run(m, count, 4, seg, STRIDE_BITS(2)) == 0,
           "Find locates the nibble-aligned byte");
    stride_match_free(m, count);
}

static void test_match_alignment(void) {
    printf("Test: Run-time alignment errors\n");

    size_t count = 0;
    stride_match_op_t *m = compile_match("${1}", 0, &count);
    ASSERT(m != NULL, "Compiled with unknown stride");

    /* 24 比特既不是 16 的整数倍 → 对齐失败 */
    ASSERT(stride_match_run(m, count, 16, "abc", STRIDE_BITS(3)) == -1,
           "Segment bits not divisible by stride fails");
    /* 24 比特是 8 的整数倍，但 ${1} 后游标 1 != N=3 → 段尾未对齐 */
    ASSERT(stride_match_run(m, count, 8, "abc", STRIDE_BITS(3)) == -1,
           "Segment-end alignment still enforced");
    ASSERT(stride_match_run(m, count, 8, "a", STRIDE_BITS(1)) == 0,
           "Aligned single byte matches");

    stride_match_free(m, count);
}

static void test_match_manual_array(void) {
    printf("Test: Match a hand-built tuple array (no compiler)\n");

    /* 证明匹配能力只依赖 matcher.h 定义的数据结构 */
    stride_match_op_t m[2];
    memset(m, 0, sizeof(m));

    m[0].type = STRIDE_MT_STEP_FWD;
    m[0].steps = 0;
    m[0].expect.data = "user";
    m[0].expect.bit_len = 32;

    m[1].type = STRIDE_MT_ABS_END;
    m[1].steps = 0;

    ASSERT(stride_match_run(m, 1, 8, "user", 32) == 0,
           "Hand-built literal-only tuple matches");
    ASSERT(stride_match_run(m, 1, 8, "userx", 40) == -1,
           "Literal-only rejects trailing bytes");
    ASSERT(stride_match_run(m, 1, 8, "use", 24) == -1,
           "Literal-only rejects short input");
    ASSERT(stride_match_run(m, 2, 8, "userx", 40) == 0,
           "Trailing ABS_END absorbs the suffix");
}

static void test_match_detail(void) {
    printf("Test: Match diagnostics (stride_match_run_ex)\n");

    size_t count = 0;
    stride_match_detail_t d;
    stride_match_op_t *m = compile_match("${2}", STEP_BYTE, &count);

    ASSERT(stride_match_run_ex(m, count, STEP_BYTE, "abc", STRIDE_BITS(3),
                               &d) == -1,
           "Mismatch reported");
    ASSERT(d.matched == 0, "matched is 0");
    ASSERT(d.fail_index == count, "fail_index == count (段尾未对齐)");
    ASSERT(d.cursor == 2, "cursor is 2 steps");

    ASSERT(stride_match_run_ex(m, count, STEP_BYTE, "ab", STRIDE_BITS(2),
                               &d) == 0,
           "Match OK");
    ASSERT(d.matched == 1, "matched is 1");
    ASSERT(d.cursor == 2, "cursor at segment end");
    stride_match_free(m, count);

    /* 字面量不符时 fail_index 指向该元组 */
    m = compile_match("$'user'", STEP_BYTE, &count);
    ASSERT(stride_match_run_ex(m, count, STEP_BYTE, "usex", STRIDE_BITS(4),
                               &d) == -1,
           "Literal mismatch reported");
    ASSERT(d.fail_index == 0, "fail_index is 0");
    stride_match_free(m, count);

    /* 移动越界时 fail_index 指向该元组 */
    m = compile_match("$[>5]", STEP_BYTE, &count);
    ASSERT(stride_match_run_ex(m, count, STEP_BYTE, "ab", STRIDE_BITS(2),
                               &d) == -1,
           "Movement overflow reported");
    ASSERT(d.fail_index == 0, "fail_index is 0 for movement overflow");
    stride_match_free(m, count);
}

static void test_match_edge_cases(void) {
    printf("Test: Match edge cases\n");

    ASSERT(stride_match_run(NULL, 0, 8, "", 0) == 0,
           "Empty sequence matches empty segment");
    ASSERT(stride_match_run(NULL, 0, 8, "x", 8) == -1,
           "Empty sequence rejects non-empty segment");
    ASSERT(stride_match_run(NULL, 1, 8, "x", 8) == -1,
           "NULL tuples with non-zero count rejected");
    ASSERT(stride_match_run(NULL, 0, 8, NULL, 0) == -1,
           "NULL segment rejected");
    ASSERT(stride_match_run(NULL, 0, 0, "", 0) == 0,
           "Stride 0 treated as 1");
}

int main(void) {
    printf("=== Stride Match Sequence Tests ===\n\n");

    test_const_addition();
    test_find_interrupt();
    test_abs_head_addition();
    test_end_merge();
    test_find_literal_merge();
    test_find_rev_literal_merge();
    test_consecutive_literals();
    test_stride_independent_artifact();
    test_multibyte_compile();

    test_match_basic();
    test_match_multibyte();
    test_match_utf16();
    test_match_records();
    test_match_bit_level();
    test_match_alignment();
    test_match_manual_array();
    test_match_detail();
    test_match_edge_cases();

    return test_summary("Matcher");
}
