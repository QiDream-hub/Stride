#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"

/* ============================================================
 * 编译器测试：词法分析 + 一步编译
 * ============================================================ */

/* ---------------- 词法分析 ---------------- */

static void test_op_match(void) {
    printf("Test: OP_MATCH ($'text')\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("'hello'", &ops, &count, &capacity) == -1,
           "Reject pattern without $ prefix");

    ASSERT(stride_lex("$'hello'", &ops, &count, &capacity) == 0,
           "Parse $'hello' successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_MATCH, "Operator type is OP_MATCH");
    ASSERT(ops[0].data.match.len == 5, "Match length is 5");
    ASSERT(strncmp(ops[0].data.match.text, "hello", 5) == 0,
           "Match text is 'hello'");

    stride_ops_free(ops);
}

static void test_op_capture_len(void) {
    printf("Test: OP_CAPTURE_LEN (${n})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${4}", &ops, &count, &capacity) == 0,
           "Parse ${4} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_LEN, "Type is OP_CAPTURE_LEN");
    ASSERT(ops[0].data.length == 4, "Capture length is 4");

    stride_ops_free(ops);
}

static void test_op_capture_chr(void) {
    printf("Test: OP_CAPTURE_CHR (${'c'})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${'.'}", &ops, &count, &capacity) == 0,
           "Parse ${'.'} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_CHR, "Type is OP_CAPTURE_CHR");
    ASSERT(ops[0].data.find.ch == '.', "Capture char is '.'");

    stride_ops_free(ops);
}

static void test_op_capture_end(void) {
    printf("Test: OP_CAPTURE_END (${})\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${}", &ops, &count, &capacity) == 0,
           "Parse ${} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_END, "Type is OP_CAPTURE_END");

    stride_ops_free(ops);
}

static void test_op_jump_abs(void) {
    printf("Test: OP_JUMP_ABS ($[n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[5]", &ops, &count, &capacity) == 0,
           "Parse $[5] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_ABS, "Type is OP_JUMP_ABS");
    ASSERT(ops[0].data.pos == 5, "Jump position is 5");

    stride_ops_free(ops);
}

static void test_op_jump_end(void) {
    printf("Test: OP_JUMP_END ($[END] / $[END-n])\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[END]", &ops, &count, &capacity) == 0,
           "Parse $[END] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_END, "Type is OP_JUMP_END");
    ASSERT(ops[0].data.jump_end.is_end == 1, "is_end flag is set");
    ASSERT(ops[0].data.jump_end.offset == 0, "END offset is 0");
    stride_ops_free(ops);

    ASSERT(stride_lex("$[END-4]", &ops, &count, &capacity) == 0,
           "Parse $[END-4] successfully");
    ASSERT(ops[0].data.jump_end.offset == 4, "END-4 offset is 4");
    stride_ops_free(ops);
}

static void test_op_jump_fwd_back(void) {
    printf("Test: OP_JUMP_FWD / OP_JUMP_BACK\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[>3]", &ops, &count, &capacity) == 0,
           "Parse $[>3] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_FWD, "Type is OP_JUMP_FWD");
    ASSERT(ops[0].data.offset == 3, "Forward offset is 3");
    stride_ops_free(ops);

    ASSERT(stride_lex("$[<2]", &ops, &count, &capacity) == 0,
           "Parse $[<2] successfully");
    ASSERT(ops[0].type == STRIDE_OP_JUMP_BACK, "Type is OP_JUMP_BACK");
    ASSERT(ops[0].data.offset == 2, "Back offset is 2");
    stride_ops_free(ops);
}

static void test_op_find(void) {
    printf("Test: OP_FIND_FWD / OP_FIND_REV\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("$[>'=']", &ops, &count, &capacity) == 0,
           "Parse $[>'='] successfully");
    ASSERT(ops[0].type == STRIDE_OP_FIND_FWD, "Type is OP_FIND_FWD");
    ASSERT(ops[0].data.find.ch == '=', "Find char is '='");
    stride_ops_free(ops);

    ASSERT(stride_lex("$[<'=']", &ops, &count, &capacity) == 0,
           "Parse $[<'='] successfully");
    ASSERT(ops[0].type == STRIDE_OP_FIND_REV, "Type is OP_FIND_REV");
    ASSERT(ops[0].data.find.ch == '=', "Find char is '='");
    stride_ops_free(ops);
}

static void test_lex_complex_and_errors(void) {
    printf("Test: complex pattern and syntax errors\n");

    stride_op_t *ops = NULL;
    size_t count = 0, capacity = 0;

    ASSERT(stride_lex("${1}${'a'}${2}$'key'", &ops, &count, &capacity) == 0,
           "Parse complex pattern successfully");
    ASSERT(count == 4, "Four operators parsed");
    ASSERT(ops[0].type == STRIDE_OP_CAPTURE_LEN, "First is CAPTURE_LEN");
    ASSERT(ops[1].type == STRIDE_OP_CAPTURE_CHR, "Second is CAPTURE_CHR");
    ASSERT(ops[2].type == STRIDE_OP_CAPTURE_LEN, "Third is CAPTURE_LEN");
    ASSERT(ops[3].type == STRIDE_OP_MATCH, "Fourth is MATCH");
    stride_ops_free(ops);

    ASSERT(stride_lex("$'hello", &ops, &count, &capacity) == -1,
           "Reject unclosed quote");
    ASSERT(stride_lex("${'.", &ops, &count, &capacity) == -1,
           "Reject unclosed quote in capture");
    ASSERT(stride_lex("${abc}", &ops, &count, &capacity) == -1,
           "Reject invalid number");
    ASSERT(stride_lex("${0}", &ops, &count, &capacity) == -1,
           "Reject zero length capture");
    ASSERT(stride_lex("user", &ops, &count, &capacity) == -1,
           "Reject bare text without $");
    ASSERT(stride_lex("$", &ops, &count, &capacity) == -1, "Reject lone $");
    ASSERT(stride_lex("$[abc]", &ops, &count, &capacity) == -1,
           "Reject invalid position expression");
}

/* ---------------- 一步编译 ---------------- */

static size_t count_capture_ops(const stride_compile_result_t *r) {
    size_t n = 0;
    for (size_t i = 0; i < r->extractor_count; i++) {
        stride_extractor_op_type_t t = r->extractors[i].type;
        if (t == STRIDE_EX_CAPTURE_LEN || t == STRIDE_EX_CAPTURE_CHR ||
            t == STRIDE_EX_CAPTURE_END) {
            n++;
        }
    }
    return n;
}

static void test_compile_keyword_only(void) {
    printf("Test: Compile keyword-only segment ($'api')\n");

    stride_compile_result_t r = stride_compile("$'api'");

    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.features != NULL && r.feature_count == 1, "One feature tuple");
    ASSERT(r.features[0].type == STRIDE_FT_CONST_REL_FWD, "Type is REL_FWD");
    ASSERT(r.features[0].value == 0, "Value is 0");
    ASSERT(strncmp(r.features[0].keyword, "api", 3) == 0, "Keyword is 'api'");

    ASSERT(r.extractor_count == 1, "One extractor op");
    ASSERT(r.extractors[0].type == STRIDE_EX_SKIP_LEN, "Optimized to SKIP_LEN");
    ASSERT(r.extractors[0].data.skip_len.length == 3, "Skip length is 3");
    ASSERT(r.param_count == 0, "No parameters");

    stride_compile_free(&r);
    ASSERT(r.features == NULL && r.extractors == NULL, "Free clears result");
}

static void test_compile_version_segment(void) {
    printf("Test: Compile version segment ($'v'${'.'}$'.'${})\n");

    stride_compile_result_t r = stride_compile("$'v'${'.'}$'.'${}");

    ASSERT(r.status == STRIDE_OK, "Compile OK");

    ASSERT(r.feature_count == 3, "Three feature tuples");
    ASSERT(r.features[0].type == STRIDE_FT_CONST_REL_FWD, "F0 is REL_FWD");
    ASSERT(r.features[1].type == STRIDE_FT_DYNAMIC_FIND_FWD, "F1 is FIND_FWD");
    ASSERT(r.features[1].value == (int)'.', "F1 char is '.'");
    ASSERT(r.features[2].type == STRIDE_FT_CONST_ABS_END, "F2 is ABS_END");
    ASSERT(r.features[2].value == 0, "F2 value 0 (pure END)");
    ASSERT(r.features[2].keyword == NULL, "F2 has no keyword");

    ASSERT(r.extractor_count == 4, "Four extractor ops");
    ASSERT(r.extractors[0].type == STRIDE_EX_SKIP_LEN, "E0 is SKIP_LEN");
    ASSERT(r.extractors[1].type == STRIDE_EX_CAPTURE_CHR, "E1 is CAPTURE_CHR");
    ASSERT(r.extractors[3].type == STRIDE_EX_CAPTURE_END, "E3 is CAPTURE_END");

    ASSERT(r.param_count == 2, "Two parameters");
    ASSERT(count_capture_ops(&r) == r.param_count,
           "param_count matches capture ops");

    stride_compile_free(&r);
}

static void test_compile_capture_and_end(void) {
    printf("Test: Compile ${4}$'a' and ${}$[<4]$'dddd'\n");

    stride_compile_result_t r = stride_compile("${4}$'a'");
    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.features[0].value == 4, "Feature value is 4");
    ASSERT(r.features[0].keyword_len == 1, "Keyword len is 1");
    ASSERT(r.param_count == 1, "One parameter");
    stride_compile_free(&r);

    /* END 基准的 value 统一为非负幅度：END-4 → 4 */
    r = stride_compile("${}$[<4]$'dddd'");
    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.feature_count == 1, "One feature tuple");
    ASSERT(r.features[0].type == STRIDE_FT_CONST_ABS_END, "Type is ABS_END");
    ASSERT(r.features[0].value == 4, "Value is 4 (END-4, 非负幅度)");
    stride_compile_free(&r);
}

static void test_compile_errors(void) {
    printf("Test: Compile errors\n");

    stride_compile_result_t r = stride_compile("");
    ASSERT(r.status == STRIDE_E_EMPTY_SEGMENT, "Empty → E_EMPTY_SEGMENT");
    ASSERT(r.error_msg != NULL, "error_msg provided");
    ASSERT(r.features == NULL && r.extractors == NULL, "No partial output");
    stride_compile_free(&r);

    r = stride_compile(NULL);
    ASSERT(r.status == STRIDE_E_EMPTY_SEGMENT, "NULL → E_EMPTY_SEGMENT");
    stride_compile_free(&r);

    r = stride_compile("$'unclosed");
    ASSERT(r.status == STRIDE_E_INVALID_PATTERN, "Bad syntax → E_INVALID_PATTERN");
    ASSERT(r.features == NULL && r.extractors == NULL, "No partial output");
    stride_compile_free(&r);

    ASSERT(strcmp(stride_status_str(STRIDE_OK), "ok") == 0, "OK string");
    ASSERT(stride_status_str((stride_status_t)9999) != NULL,
           "Unknown status still returns a string");
}

/* ---------------- 只编译一种序列 ---------------- */

static void test_compile_features_only(void) {
    printf("Test: stride_compile_features\n");

    stride_feature_t *f = NULL;
    size_t count = 0, cap = 0;

    ASSERT(stride_compile_features("$'v'${'.'}$'.'${}", &f, &count, &cap) == 0,
           "Compile features OK");
    ASSERT(count == 3, "Three feature tuples");
    ASSERT(f[0].keyword_len == 1, "Keyword copied");
    stride_feature_free(f, count);

    ASSERT(stride_compile_features("", &f, &count, &cap) == -1,
           "Empty pattern rejected");
    ASSERT(stride_compile_features(NULL, &f, &count, &cap) == -1,
           "NULL pattern rejected");
}

static void test_compile_extractors_only(void) {
    printf("Test: stride_compile_extractors\n");

    stride_extractor_op_t *e = NULL;
    size_t count = 0, params = 0;

    ASSERT(stride_compile_extractors("${4}$'a'", &e, &count, &params) == 0,
           "Compile extractors OK");
    ASSERT(count == 2, "Two extractor ops");
    ASSERT(params == 1, "One parameter");
    ASSERT(e[0].type == STRIDE_EX_CAPTURE_LEN, "E0 is CAPTURE_LEN");
    free(e);

    ASSERT(stride_compile_extractors("", &e, &count, &params) == -1,
           "Empty pattern rejected");
}

int main(void) {
    printf("=== Stride Compiler Tests ===\n\n");

    test_op_match();
    test_op_capture_len();
    test_op_capture_chr();
    test_op_capture_end();
    test_op_jump_abs();
    test_op_jump_end();
    test_op_jump_fwd_back();
    test_op_find();
    test_lex_complex_and_errors();

    test_compile_keyword_only();
    test_compile_version_segment();
    test_compile_capture_and_end();
    test_compile_errors();

    test_compile_features_only();
    test_compile_extractors_only();

    return test_summary("Compiler");
}
