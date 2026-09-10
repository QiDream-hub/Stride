#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/grammar.h"

/* ============================================================
 * 语法模块（词法分析）单元测试
 * ============================================================ */

/* 测试 $'text' - STRIDE_OP_MATCH */
static void test_op_match(void) {
    printf("Test: OP_MATCH ($'text')\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("'hello'", &ops, &count, &capacity);
    ASSERT(ret == -1, "Reject pattern without $ prefix");

    ret = stride_lex("$'hello'", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $'hello' successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_MATCH, "Operator type is OP_MATCH");
    ASSERT(ops[0].data.match.len == 5, "Match length is 5");
    ASSERT(strncmp(ops[0].data.match.text, "hello", 5) == 0,
           "Match text is 'hello'");

    stride_ops_free(ops);
}

/* 测试 ${n} - STRIDE_OP_CAPTURE_LEN */
static void test_op_capture_len(void) {
    printf("Test: OP_CAPTURE_LEN (${n})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("${4}", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse ${4} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_LEN,
           "Operator type is OP_CAPTURE_LEN");
    ASSERT(ops[0].data.length == 4, "Capture length is 4");

    stride_ops_free(ops);
}

/* 测试 ${'c'} - STRIDE_OP_CAPTURE_CHR */
static void test_op_capture_chr(void) {
    printf("Test: OP_CAPTURE_CHR (${'c'})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("${'.'}", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse ${'.'} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_CHR,
           "Operator type is OP_CAPTURE_CHR");
    ASSERT(ops[0].data.find.ch == '.', "Capture char is '.'");

    stride_ops_free(ops);
}

/* 测试 ${} - STRIDE_OP_CAPTURE_END */
static void test_op_capture_end(void) {
    printf("Test: OP_CAPTURE_END (${})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("${}", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse ${} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_END,
           "Operator type is OP_CAPTURE_END");

    stride_ops_free(ops);
}

/* 测试 $[n] - STRIDE_OP_JUMP_ABS */
static void test_op_jump_abs(void) {
    printf("Test: OP_JUMP_ABS ($[n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("$[5]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[5] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_ABS, "Operator type is OP_JUMP_ABS");
    ASSERT(ops[0].data.pos == 5, "Jump position is 5");

    stride_ops_free(ops);
}

/* 测试 $[END] / $[END-n] - STRIDE_OP_JUMP_END */
static void test_op_jump_end(void) {
    printf("Test: OP_JUMP_END ($[END] / $[END-n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("$[END]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[END] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_END, "Operator type is OP_JUMP_END");
    ASSERT(ops[0].data.jump_end.is_end == 1, "is_end flag is set");
    ASSERT(ops[0].data.jump_end.offset == 0, "END offset is 0");
    stride_ops_free(ops);

    ret = stride_lex("$[END-4]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[END-4] successfully");
    ASSERT(ops[0].data.jump_end.offset == 4, "END-4 offset is 4");
    stride_ops_free(ops);
}

/* 测试 $[>n] - STRIDE_OP_JUMP_FWD */
static void test_op_jump_fwd(void) {
    printf("Test: OP_JUMP_FWD ($[>n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("$[>3]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[>3] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_FWD, "Operator type is OP_JUMP_FWD");
    ASSERT(ops[0].data.offset == 3, "Jump forward offset is 3");

    stride_ops_free(ops);
}

/* 测试 $[<n] - STRIDE_OP_JUMP_BACK */
static void test_op_jump_back(void) {
    printf("Test: OP_JUMP_BACK ($[<n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("$[<2]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[<2] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_BACK, "Operator type is OP_JUMP_BACK");
    ASSERT(ops[0].data.offset == 2, "Jump back offset is 2");

    stride_ops_free(ops);
}

/* 测试 $[>'c'] - STRIDE_OP_FIND_FWD */
static void test_op_find_fwd(void) {
    printf("Test: OP_FIND_FWD ($[>'c'])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("$[>'=']", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[>'='] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_FIND_FWD, "Operator type is OP_FIND_FWD");
    ASSERT(ops[0].data.find.ch == '=', "Find char is '='");

    stride_ops_free(ops);
}

/* 测试 $[<'c'] - STRIDE_OP_FIND_REV */
static void test_op_find_rev(void) {
    printf("Test: OP_FIND_REV ($[<'c'])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("$[<'=']", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[<'='] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_FIND_REV, "Operator type is OP_FIND_REV");
    ASSERT(ops[0].data.find.ch == '=', "Find char is '='");

    stride_ops_free(ops);
}

/* 测试复合模式（单个段内的多个操作符）*/
static void test_complex_pattern(void) {
    printf("Test: Complex pattern (multiple ops in single segment)\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    int ret = stride_lex("${1}${'a'}${2}$'key'", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse complex pattern successfully");
    ASSERT(count == 4, "Four operators parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_LEN, "First op is OP_CAPTURE_LEN");
    ASSERT(ops[1].type == STRIDE_OP_CAPTURE_CHR, "Second op is OP_CAPTURE_CHR");
    ASSERT(ops[2].type == STRIDE_OP_CAPTURE_LEN, "Third op is OP_CAPTURE_LEN");
    ASSERT(ops[3].type == STRIDE_OP_MATCH, "Fourth op is OP_MATCH");

    stride_ops_free(ops);
}

/* 测试错误处理 - 未闭合引号 */
static void test_error_unclosed_quote(void) {
    printf("Test: Error - unclosed quote\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$'hello", &ops, &count, &capacity) == -1,
           "Reject unclosed quote");
    ASSERT(stride_lex("${'.", &ops, &count, &capacity) == -1,
           "Reject unclosed quote in capture");
    ASSERT(stride_lex("$[>'=", &ops, &count, &capacity) == -1,
           "Reject unclosed quote in find");
}

/* 测试错误处理 - 无效数字 */
static void test_error_invalid_number(void) {
    printf("Test: Error - invalid number\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${abc}", &ops, &count, &capacity) == -1,
           "Reject invalid number in capture");
}

/* 测试错误处理 - 零长度捕获 */
static void test_error_zero_length(void) {
    printf("Test: Error - zero length capture\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${0}", &ops, &count, &capacity) == -1,
           "Reject zero length capture");
}

/* 测试错误处理 - 非 $ 起始与非法位置表达式 */
static void test_error_bad_syntax(void) {
    printf("Test: Error - bad syntax\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("user", &ops, &count, &capacity) == -1,
           "Reject bare text without $");
    ASSERT(stride_lex("$", &ops, &count, &capacity) == -1,
           "Reject lone $");
    ASSERT(stride_lex("$[abc]", &ops, &count, &capacity) == -1,
           "Reject invalid position expression");
    ASSERT(stride_lex("${}", &ops, &count, &capacity) == 0,
           "Accept ${} as capture-to-end");
    stride_ops_free(ops);
}

int main(void) {
    printf("=== Stride Grammar (Lexer) Unit Tests ===\n\n");

    test_op_match();
    test_op_capture_len();
    test_op_capture_chr();
    test_op_capture_end();
    test_op_jump_abs();
    test_op_jump_end();
    test_op_jump_fwd();
    test_op_jump_back();
    test_op_find_fwd();
    test_op_find_rev();
    test_complex_pattern();
    test_error_unclosed_quote();
    test_error_invalid_number();
    test_error_zero_length();
    test_error_bad_syntax();

    return test_summary("Grammar");
}
