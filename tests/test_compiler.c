#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"

/* ============================================================
 * 编译器测试：词法分析（含转义）+ 一步编译 + 对齐校验
 *
 * 单位：步长以比特计；文本示例用步长 8（1 字节/步）。
 * ============================================================ */

#define STEP_BYTE 8u

/* ---------------- 词法分析 ---------------- */

static void test_op_match(void) {
    printf("Test: OP_MATCH ($'text')\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("'hello'", 0, &ops, &count, &capacity) == -1,
           "Reject pattern without $ prefix");

    ASSERT(stride_lex("$'hello'", 0, &ops, &count, &capacity) == 0,
           "Parse $'hello' successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_MATCH, "Operator type is OP_MATCH");
    ASSERT(ops[0].data.literal.bit_len == 40, "Literal is 40 bits");
    ASSERT(memcmp(ops[0].data.literal.data, "hello", 5) == 0,
           "Literal bytes are 'hello'");

    stride_ops_free(ops, count);
}

static void test_op_escapes(void) {
    printf("Test: Literal escapes (\\\\ \\' \\xNN)\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    /* $'a\x00b' -> 3 字节（含 0x00） */
    ASSERT(stride_lex("$'a\\x00b'", 0, &ops, &count, &capacity) == 0,
           "Parse \\xNN escape");
    ASSERT(ops[0].data.literal.bit_len == 24, "Decoded literal is 24 bits");
    const unsigned char *b = (const unsigned char *)ops[0].data.literal.data;
    ASSERT(b[0] == 'a' && b[1] == 0x00 && b[2] == 'b', "Bytes are a,0x00,b");
    stride_ops_free(ops, count);

    /* $'it\'s' -> 4 字节 */
    ASSERT(stride_lex("$'it\\'s'", 0, &ops, &count, &capacity) == 0,
           "Parse escaped quote");
    ASSERT(ops[0].data.literal.bit_len == 32, "Decoded literal is 32 bits");
    ASSERT(memcmp(ops[0].data.literal.data, "it's", 4) == 0,
           "Bytes are \"it's\"");
    stride_ops_free(ops, count);

    /* $'\\' -> 单反斜杠 */
    ASSERT(stride_lex("$'\\\\'", 0, &ops, &count, &capacity) == 0,
           "Parse escaped backslash");
    ASSERT(ops[0].data.literal.bit_len == 8, "One escaped byte");
    ASSERT(((const char *)ops[0].data.literal.data)[0] == '\\',
           "Byte is a backslash");
    stride_ops_free(ops, count);

    /* 未知转义与残缺转义都要拒绝 */
    ASSERT(stride_lex("$'a\\q'", 0, &ops, &count, &capacity) == -1,
           "Reject unknown escape");
    ASSERT(stride_lex("$'a\\x1'", 0, &ops, &count, &capacity) == -1,
           "Reject short hex escape");
    ASSERT(stride_lex("$'\\'", 0, &ops, &count, &capacity) == -1,
           "Reject dangling backslash");
}

static void test_op_capture_steps(void) {
    printf("Test: OP_CAPTURE_STEPS (${n})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${4}", 0, &ops, &count, &capacity) == 0,
           "Parse ${4} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_STEPS, "Type is OP_CAPTURE_STEPS");
    ASSERT(ops[0].data.steps == 4, "Capture steps is 4");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("${0}", 0, &ops, &count, &capacity) == -1,
           "Reject zero-step capture");
}

static void test_op_capture_until(void) {
    printf("Test: OP_CAPTURE_UNTIL (${'S'})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${'.'}", 0, &ops, &count, &capacity) == 0,
           "Parse ${'.'} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_UNTIL, "Type is OP_CAPTURE_UNTIL");
    ASSERT(ops[0].data.literal.bit_len == 8, "Delimiter is 8 bits");
    ASSERT(((const char *)ops[0].data.literal.data)[0] == '.',
           "Delimiter is '.'");
    stride_ops_free(ops, count);

    /* 多比特定界 */
    ASSERT(stride_lex("${'用户'}", 0, &ops, &count, &capacity) == 0,
           "Multi-bit delimiter parses");
    ASSERT(ops[0].data.literal.bit_len == 48, "Delimiter is 48 bits");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("${''}", 0, &ops, &count, &capacity) == -1,
           "Reject empty delimiter");
}

static void test_op_capture_end(void) {
    printf("Test: OP_CAPTURE_END (${})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${}", 0, &ops, &count, &capacity) == 0,
           "Parse ${} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_END, "Type is OP_CAPTURE_END");
    stride_ops_free(ops, count);
}

static void test_op_jump_abs(void) {
    printf("Test: OP_JUMP_ABS ($[n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[5]", 0, &ops, &count, &capacity) == 0,
           "Parse $[5] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_ABS, "Type is OP_JUMP_ABS");
    ASSERT(ops[0].data.steps == 5, "Jump step position is 5");
    stride_ops_free(ops, count);
}

static void test_op_jump_end(void) {
    printf("Test: OP_JUMP_END ($[END] / $[END-n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[END]", 0, &ops, &count, &capacity) == 0,
           "Parse $[END] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_END, "Type is OP_JUMP_END");
    ASSERT(ops[0].data.jump_end.is_end == 1, "is_end flag is set");
    ASSERT(ops[0].data.jump_end.back_steps == 0, "END back_steps is 0");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("$[END-4]", 0, &ops, &count, &capacity) == 0,
           "Parse $[END-4] successfully");
    ASSERT(ops[0].data.jump_end.back_steps == 4, "END-4 back_steps is 4");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("$[END-]", 0, &ops, &count, &capacity) == -1,
           "Reject END without offset digits");
}

static void test_op_jump_fwd_back(void) {
    printf("Test: OP_JUMP_FWD / OP_JUMP_BACK\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[>3]", 0, &ops, &count, &capacity) == 0,
           "Parse $[>3] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_FWD, "Type is OP_JUMP_FWD");
    ASSERT(ops[0].data.steps == 3, "Forward steps is 3");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("$[<2]", 0, &ops, &count, &capacity) == 0,
           "Parse $[<2] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_BACK, "Type is OP_JUMP_BACK");
    ASSERT(ops[0].data.steps == 2, "Back steps is 2");
    stride_ops_free(ops, count);
}

static void test_op_find(void) {
    printf("Test: OP_FIND_FWD / OP_FIND_REV\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[>'=']", 0, &ops, &count, &capacity) == 0,
           "Parse $[>'='] successfully");
    ASSERT(ops[0].type == STRIDE_OP_FIND_FWD, "Type is OP_FIND_FWD");
    ASSERT(ops[0].data.literal.bit_len == 8, "Find target is 8 bits");
    ASSERT(((const char *)ops[0].data.literal.data)[0] == '=', "Target is '='");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("$[<'=']", 0, &ops, &count, &capacity) == 0,
           "Parse $[<'='] successfully");
    ASSERT(ops[0].type == STRIDE_OP_FIND_REV, "Type is OP_FIND_REV");
    stride_ops_free(ops, count);

    /* 多比特查找目标 */
    ASSERT(stride_lex("$[>'：']", 0, &ops, &count, &capacity) == 0,
           "Parse multi-bit find target");
    ASSERT(ops[0].data.literal.bit_len == 24, "Target is 24 bits");
    stride_ops_free(ops, count);
}

static void test_lex_len_delimited_binary(void) {
    printf("Test: Length-delimited binary pattern\n");

    /* 模式字节：$ ' a 0x00 b '  —— 含真实 NUL */
    static const char pattern[] = "$'a\0b'";
    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex(pattern, sizeof(pattern) - 1, &ops, &count, &capacity) ==
               0,
           "Parse pattern containing NUL");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_MATCH, "Type is OP_MATCH");
    ASSERT(ops[0].data.literal.bit_len == 24, "Literal is 24 bits");
    const unsigned char *b = (const unsigned char *)ops[0].data.literal.data;
    ASSERT(b[1] == 0x00, "NUL preserved in literal");
    stride_ops_free(ops, count);
}

static void test_lex_complex_and_errors(void) {
    printf("Test: complex pattern and syntax errors\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${1}${'a'}${2}$'key'", 0, &ops, &count, &capacity) == 0,
           "Parse complex pattern successfully");
    ASSERT(count == 4, "Four operators parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_STEPS, "First is CAPTURE_STEPS");
    ASSERT(ops[1].type == STRIDE_OP_CAPTURE_UNTIL, "Second is CAPTURE_UNTIL");
    ASSERT(ops[2].type == STRIDE_OP_CAPTURE_STEPS, "Third is CAPTURE_STEPS");
    ASSERT(ops[3].type == STRIDE_OP_MATCH, "Fourth is MATCH");
    stride_ops_free(ops, count);

    ASSERT(stride_lex("$'hello", 0, &ops, &count, &capacity) == -1,
           "Reject unclosed quote");
    ASSERT(stride_lex("${'.", 0, &ops, &count, &capacity) == -1,
           "Reject unclosed quote in capture");
    ASSERT(stride_lex("${abc}", 0, &ops, &count, &capacity) == -1,
           "Reject invalid number");
    ASSERT(stride_lex("user", 0, &ops, &count, &capacity) == -1,
           "Reject bare text without $");
    ASSERT(stride_lex("$", 0, &ops, &count, &capacity) == -1, "Reject lone $");
    ASSERT(stride_lex("$[abc]", 0, &ops, &count, &capacity) == -1,
           "Reject invalid position expression");
    ASSERT(stride_lex("$'a'/${}", 0, &ops, &count, &capacity) == -1,
           "Reject free-standing bytes (the '/')");

    /* '/' 在引号内只是数据 */
    ASSERT(stride_lex("$'////'", 0, &ops, &count, &capacity) == 0,
           "Slash inside a literal is valid");
    ASSERT(ops[0].data.literal.bit_len == 32, "Four slashes = 32 bits");
    stride_ops_free(ops, count);
}

/* ---------------- 一步编译 ---------------- */

static size_t count_capture_ops(const stride_compile_result_t *r) {
    size_t n = 0;
    for (size_t i = 0; i < r->extract_count; i++) {
        stride_extractor_op_type_t t = r->extract[i].type;
        if (t == STRIDE_EX_CAPTURE_STEPS || t == STRIDE_EX_CAPTURE_UNTIL ||
            t == STRIDE_EX_CAPTURE_END) {
            n++;
        }
    }
    return n;
}

static void test_compile_keyword_only(void) {
    printf("Test: Compile keyword-only segment ($'api')\n");

    stride_compile_result_t r = stride_compile_ex("$'api'", 0, STEP_BYTE);

    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.match != NULL && r.match_count == 1, "One match tuple");
    ASSERT(r.match[0].type == STRIDE_MT_STEP_FWD, "Type is STEP_FWD");
    ASSERT(r.match[0].steps == 0, "Steps is 0");
    ASSERT(memcmp(r.match[0].expect.data, "api", 3) == 0, "Expect is 'api'");

    ASSERT(r.extract_count == 1, "One extract op");
    ASSERT(r.extract[0].type == STRIDE_EX_SKIP_BITS, "Optimized to SKIP_BITS");
    ASSERT(r.extract[0].data.skip_bits.bit_len == 24,
           "Skip is 24 bits (stride-free)");
    ASSERT(r.param_count == 0, "No parameters");

    stride_compile_free(&r);
    ASSERT(r.match == NULL && r.extract == NULL, "Free clears result");
}

static void test_compile_version_segment(void) {
    printf("Test: Compile version segment ($'v'${'.'}$'.'${})\n");

    stride_compile_result_t r = stride_compile_ex("$'v'${'.'}$'.'${}", 0,
                                                  STEP_BYTE);

    ASSERT(r.status == STRIDE_OK, "Compile OK");

    ASSERT(r.match_count == 3, "Three match tuples");
    ASSERT(r.match[0].type == STRIDE_MT_STEP_FWD, "M0 is STEP_FWD");
    ASSERT(r.match[1].type == STRIDE_MT_FIND_FWD, "M1 is FIND_FWD");
    ASSERT(r.match[1].delimiter.bit_len == 8, "M1 delimiter is '.'");
    ASSERT(r.match[2].type == STRIDE_MT_ABS_END, "M2 is ABS_END");
    ASSERT(r.match[2].steps == 0, "M2 steps 0 (pure END)");

    ASSERT(r.extract_count == 4, "Four extract ops");
    ASSERT(r.extract[0].type == STRIDE_EX_SKIP_BITS, "E0 is SKIP_BITS");
    ASSERT(r.extract[1].type == STRIDE_EX_CAPTURE_UNTIL, "E1 is CAPTURE_UNTIL");
    ASSERT(r.extract[3].type == STRIDE_EX_CAPTURE_END, "E3 is CAPTURE_END");

    ASSERT(r.param_count == 2, "Two parameters");
    ASSERT(count_capture_ops(&r) == r.param_count,
           "param_count matches capture ops");

    stride_compile_free(&r);
}

static void test_compile_multibyte(void) {
    printf("Test: Compile multi-bit pattern (UTF-8)\n");

    stride_compile_result_t r = stride_compile_ex("$'用户：'${}", 0, STEP_BYTE);

    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.match_count == 2, "Two match tuples");
    ASSERT(r.match[0].expect.bit_len == 72, "Literal is 72 bits");
    ASSERT(r.param_count == 1, "One parameter");

    stride_compile_free(&r);
}

static void test_compile_alignment(void) {
    printf("Test: Compile-time alignment (stride in bits)\n");

    /* $'abc' = 24 比特 */
    stride_compile_result_t r = stride_compile_ex("$'abc'", 0, 8);
    ASSERT(r.status == STRIDE_OK, "24 bits aligned to stride 8");
    stride_compile_free(&r);

    r = stride_compile_ex("$'abc'", 0, 3);
    ASSERT(r.status == STRIDE_OK, "24 bits aligned to stride 3 (bit-level)");
    stride_compile_free(&r);

    r = stride_compile_ex("$'abc'", 0, 4);
    ASSERT(r.status == STRIDE_OK, "24 bits aligned to stride 4 (nibble)");
    stride_compile_free(&r);

    r = stride_compile_ex("$'abc'", 0, 16);
    ASSERT(r.status == STRIDE_E_ALIGN, "24 bits NOT aligned to stride 16");
    ASSERT(r.match == NULL && r.extract == NULL, "No partial output");
    ASSERT(strcmp(stride_status_str(r.status), "alignment error") == 0,
           "Alignment status string");
    stride_compile_free(&r);

    r = stride_compile_ex("$'abc'", 0, 5);
    ASSERT(r.status == STRIDE_E_ALIGN, "24 bits NOT aligned to stride 5");
    stride_compile_free(&r);

    /* 编译期未知步长时不校验 */
    r = stride_compile_ex("$'abc'", 0, 0);
    ASSERT(r.status == STRIDE_OK, "Unknown stride skips validation");
    stride_compile_free(&r);
}

static void test_compile_errors(void) {
    printf("Test: Compile errors\n");

    stride_compile_result_t r = stride_compile("");
    ASSERT(r.status == STRIDE_E_EMPTY_SEGMENT, "Empty → E_EMPTY_SEGMENT");
    ASSERT(r.error_msg != NULL, "error_msg provided");
    ASSERT(r.match == NULL && r.extract == NULL, "No partial output");
    stride_compile_free(&r);

    r = stride_compile(NULL);
    ASSERT(r.status == STRIDE_E_EMPTY_SEGMENT, "NULL → E_EMPTY_SEGMENT");
    stride_compile_free(&r);

    r = stride_compile("$'unclosed");
    ASSERT(r.status == STRIDE_E_INVALID_PATTERN,
           "Bad syntax → E_INVALID_PATTERN");
    ASSERT(r.match == NULL && r.extract == NULL, "No partial output");
    stride_compile_free(&r);

    ASSERT(strcmp(stride_status_str(STRIDE_OK), "ok") == 0, "OK string");
    ASSERT(stride_status_str((stride_status_t)9999) != NULL,
           "Unknown status still returns a string");
}

/* ---------------- 只编译一种序列 ---------------- */

static void test_compile_match_only(void) {
    printf("Test: stride_compile_match\n");

    stride_match_op_t *m = NULL;
    size_t count = 0, cap = 0;

    ASSERT(stride_compile_match("$'v'${'.'}$'.'${}", 0, STEP_BYTE, &m, &count,
                                &cap) == 0,
           "Compile match OK");
    ASSERT(count == 3, "Three match tuples");
    ASSERT(m[0].expect.bit_len == 8, "Expect copied");
    stride_match_free(m, count);

    ASSERT(stride_compile_match("", 0, STEP_BYTE, &m, &count, &cap) == -1,
           "Empty pattern rejected");
    ASSERT(stride_compile_match(NULL, 0, STEP_BYTE, &m, &count, &cap) == -1,
           "NULL pattern rejected");
    ASSERT(stride_compile_match("$'abc'", 0, 16, &m, &count, &cap) == -1,
           "Misaligned literal rejected");
}

static void test_compile_extract_only(void) {
    printf("Test: stride_compile_extract\n");

    stride_extractor_op_t *e = NULL;
    size_t count = 0, params = 0;

    ASSERT(stride_compile_extract("${4}$'a'", 0, STEP_BYTE, &e, &count,
                                  &params) == 0,
           "Compile extract OK");
    ASSERT(count == 2, "Two extract ops");
    ASSERT(params == 1, "One parameter");
    ASSERT(e[0].type == STRIDE_EX_CAPTURE_STEPS, "E0 is CAPTURE_STEPS");
    stride_extractor_free(e, count);

    ASSERT(stride_compile_extract("", 0, STEP_BYTE, &e, &count, &params) == -1,
           "Empty pattern rejected");
}

int main(void) {
    printf("=== Stride Compiler Tests ===\n\n");

    test_op_match();
    test_op_escapes();
    test_op_capture_steps();
    test_op_capture_until();
    test_op_capture_end();
    test_op_jump_abs();
    test_op_jump_end();
    test_op_jump_fwd_back();
    test_op_find();
    test_lex_len_delimited_binary();
    test_lex_complex_and_errors();

    test_compile_keyword_only();
    test_compile_version_segment();
    test_compile_multibyte();
    test_compile_alignment();
    test_compile_errors();

    test_compile_match_only();
    test_compile_extract_only();

    return test_summary("Compiler");
}
