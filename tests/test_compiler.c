#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"

/* ============================================================
 * 序列编译器集成测试
 *
 * 验证 stride_compile 一次产出特征序列与提取序列，
 * 并验证两者的参数计数一致。
 * ============================================================ */

/* 统计提取序列中产生参数的操作数 */
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

/* 文档示例：$'api'（单段，纯关键字） */
static void test_compile_keyword_only(void) {
    printf("Test: Compile keyword-only segment ($'api')\n");

    stride_compile_result_t r = stride_compile("$'api'");

    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.features != NULL && r.feature_count == 1, "One feature tuple");
    ASSERT(r.features[0].type == STRIDE_FT_CONST_REL_FWD,
           "Feature type is CONST_REL_FWD");
    ASSERT(r.features[0].value == 0, "Feature value is 0");
    ASSERT(r.features[0].keyword_len == 3, "Keyword length is 3");
    ASSERT(strncmp(r.features[0].keyword, "api", 3) == 0, "Keyword is 'api'");

    ASSERT(r.extractor_count == 1, "One extractor op");
    ASSERT(r.extractors[0].type == STRIDE_EX_SKIP_LEN,
           "Match optimized to EX_SKIP_LEN");
    ASSERT(r.extractors[0].data.skip_len.length == 3, "Skip length is 3");
    ASSERT(r.param_count == 0, "No parameters");

    stride_compile_free(&r);
    ASSERT(r.features == NULL && r.extractors == NULL, "Free clears result");
}

/* 文档完整示例：$'v'${'.'}$'.'${} */
static void test_compile_version_segment(void) {
    printf("Test: Compile version segment ($'v'${'.'}$'.'${})\n");

    stride_compile_result_t r = stride_compile("$'v'${'.'}$'.'${}");

    ASSERT(r.status == STRIDE_OK, "Compile OK");

    /* 特征序列：[(0,"v"), ('.',"."), (END,NULL)] */
    ASSERT(r.feature_count == 3, "Three feature tuples");
    ASSERT(r.features[0].type == STRIDE_FT_CONST_REL_FWD, "F0 is REL_FWD");
    ASSERT(r.features[0].keyword_len == 1, "F0 keyword len 1");
    ASSERT(r.features[1].type == STRIDE_FT_DYNAMIC_FIND_FWD, "F1 is FIND_FWD");
    ASSERT(r.features[1].value == (int)'.', "F1 char is '.'");
    ASSERT(r.features[1].keyword_len == 1, "F1 keyword len 1");
    ASSERT(r.features[2].type == STRIDE_FT_CONST_ABS_END, "F2 is ABS_END");
    ASSERT(r.features[2].keyword == NULL, "F2 has no keyword");

    /* 提取序列：[SKIP(1), CAPTURE_CHR('.'), SKIP(1), CAPTURE_END] */
    ASSERT(r.extractor_count == 4, "Four extractor ops");
    ASSERT(r.extractors[0].type == STRIDE_EX_SKIP_LEN, "E0 is SKIP_LEN");
    ASSERT(r.extractors[1].type == STRIDE_EX_CAPTURE_CHR, "E1 is CAPTURE_CHR");
    ASSERT(r.extractors[1].data.capture_chr.ch == '.', "E1 char is '.'");
    ASSERT(r.extractors[2].type == STRIDE_EX_SKIP_LEN, "E2 is SKIP_LEN");
    ASSERT(r.extractors[3].type == STRIDE_EX_CAPTURE_END, "E3 is CAPTURE_END");

    ASSERT(r.param_count == 2, "Two parameters");
    ASSERT(count_capture_ops(&r) == r.param_count,
           "param_count matches capture ops");

    stride_compile_free(&r);
}

/* 编译 {$4}$'a' */
static void test_compile_len_capture(void) {
    printf("Test: Compile length capture (${4}$'a')\n");

    stride_compile_result_t r = stride_compile("${4}$'a'");

    ASSERT(r.status == STRIDE_OK, "Compile OK");
    ASSERT(r.feature_count == 1, "One feature tuple (4, 'a')");
    ASSERT(r.features[0].value == 4, "Feature value is 4");
    ASSERT(r.features[0].keyword_len == 1, "Keyword len is 1");

    ASSERT(r.param_count == 1, "One parameter");
    ASSERT(count_capture_ops(&r) == r.param_count,
           "param_count matches capture ops");

    stride_compile_free(&r);
}

/* 错误：空模式 */
static void test_compile_empty(void) {
    printf("Test: Compile empty pattern\n");

    stride_compile_result_t r = stride_compile("");
    ASSERT(r.status == STRIDE_E_EMPTY_SEGMENT, "Status is E_EMPTY_SEGMENT");
    ASSERT(r.error_msg != NULL, "error_msg provided");
    ASSERT(r.features == NULL && r.extractors == NULL, "No partial output");
    stride_compile_free(&r);

    stride_compile_result_t r2 = stride_compile(NULL);
    ASSERT(r2.status == STRIDE_E_EMPTY_SEGMENT, "NULL pattern is empty");
    stride_compile_free(&r2);
}

/* 错误：语法错误 */
static void test_compile_syntax_error(void) {
    printf("Test: Compile syntax error\n");

    stride_compile_result_t r = stride_compile("$'unclosed");
    ASSERT(r.status == STRIDE_E_INVALID_PATTERN, "Status is E_INVALID_PATTERN");
    ASSERT(r.error_msg != NULL, "error_msg provided");
    ASSERT(r.features == NULL && r.extractors == NULL, "No partial output");
    stride_compile_free(&r);
}

/* 状态码字符串 */
static void test_status_str(void) {
    printf("Test: Status strings\n");

    ASSERT(strcmp(stride_status_str(STRIDE_OK), "ok") == 0, "OK string");
    ASSERT(stride_status_str(STRIDE_E_EMPTY_SEGMENT) != NULL,
           "Empty segment string non-NULL");
    ASSERT(stride_status_str((stride_status_t)9999) != NULL,
           "Unknown status still returns a string");
}

int main(void) {
    printf("=== Stride Compiler Integration Tests ===\n\n");

    test_compile_keyword_only();
    test_compile_version_segment();
    test_compile_len_capture();
    test_compile_empty();
    test_compile_syntax_error();
    test_status_str();

    return test_summary("Compiler");
}
