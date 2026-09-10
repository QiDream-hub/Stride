#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/feature.h"
#include "stride/grammar.h"

/* ============================================================
 * 特征序列模块单元测试
 * ============================================================ */

/* 辅助函数：词法分析并编译特征序列 */
static stride_feature_t *compile_features(const char *pattern,
                                          size_t *out_count) {
    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;
    stride_feature_t *features = NULL;
    size_t feature_count = 0, feature_capacity = 0;

    if (stride_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        return NULL;
    }

    if (stride_feature_compile(ops, op_count, &features, &feature_count,
                               &feature_capacity) != 0) {
        stride_ops_free(ops);
        return NULL;
    }

    stride_ops_free(ops);
    *out_count = feature_count;
    return features;
}

/* 测试示例 1：常量操作相加 ${1}${1}${1}$'key' → [(3, "key")] */
static void test_const_addition(void) {
    printf("Test: Constant addition (${1}${1}${1}$'key')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${1}${1}${1}$'key'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].value == 3, "Value is 3 (1+1+1)");
    ASSERT(features[0].type == STRIDE_FT_CONST_REL_FWD,
           "Type is CONST_REL_FWD");
    ASSERT(features[0].keyword != NULL, "Keyword is present");
    ASSERT(features[0].keyword_len == 3, "Keyword length is 3");
    ASSERT(strncmp(features[0].keyword, "key", 3) == 0, "Keyword is 'key'");

    stride_feature_free(features, count);
}

/* 测试示例 2：动态操作打断 ${2}${'a'}${3}$'b' */
static void test_dynamic_interrupt(void) {
    printf("Test: Dynamic interrupt (${2}${'a'}${3}$'b')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${2}${'a'}${3}$'b'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three feature tuples output");
    ASSERT(features[0].type == STRIDE_FT_CONST_REL_FWD,
           "First is CONST_REL_FWD");
    ASSERT(features[0].value == 2, "First value is 2");
    ASSERT(features[1].type == STRIDE_FT_DYNAMIC_FIND_FWD,
           "Second is DYNAMIC_FIND_FWD");
    ASSERT(features[1].value == (int)'a', "Second char is 'a'");
    ASSERT(features[2].value == 3, "Third value is 3");

    stride_feature_free(features, count);
}

/* 测试示例 3：绝对移动与常量相加 $[5]${2}$'key' */
static void test_abs_head_addition(void) {
    printf("Test: HEAD absolute addition ($[5]${2}$'key')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("$[5]${2}$'key'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].type == STRIDE_FT_CONST_ABS_HEAD,
           "Type is FT_CONST_ABS_HEAD");
    ASSERT(features[0].value == 7, "Value is 7 (5+2)");

    stride_feature_free(features, count);
}

/* 测试示例 4：END 合并 ${}$[<4]$'dddd' */
static void test_end_merge(void) {
    printf("Test: END merge (${}$[<4]$'dddd')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${}$[<4]$'dddd'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].type == STRIDE_FT_CONST_ABS_END,
           "Type is FT_CONST_ABS_END");
    ASSERT(features[0].value == -4, "Value is -4 (END-4)");

    stride_feature_free(features, count);
}

/* 测试示例 5：动态操作与关键字合并 ${'a'}$'key' */
static void test_dynamic_keyword_merge(void) {
    printf("Test: Dynamic + keyword merge (${'a'}$'key')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${'a'}$'key'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].type == STRIDE_FT_DYNAMIC_FIND_FWD,
           "Type is DYNAMIC_FIND_FWD");
    ASSERT(features[0].value == (int)'a', "Char is 'a'");
    ASSERT(features[0].keyword != NULL, "Keyword is present");

    stride_feature_free(features, count);
}

/* 测试示例 6：混合场景 ${2}${'a'}${3}$'b' */
static void test_mixed_scenario(void) {
    printf("Test: Mixed scenario (${2}${'a'}${3}$'b')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${2}${'a'}${3}$'b'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three feature tuples output");

    stride_feature_free(features, count);
}

/* 测试示例 7：连续关键字 $'dd'$'aaa' */
static void test_consecutive_keywords(void) {
    printf("Test: Consecutive keywords ($'dd'$'aaa')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("$'dd'$'aaa'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two feature tuples output");
    ASSERT(features[0].type == STRIDE_FT_CONST_REL_FWD,
           "First type is CONST_REL_FWD");
    ASSERT(features[0].value == 0, "First value is 0");
    ASSERT(features[1].value == 0, "Second value is 0");

    stride_feature_free(features, count);
}

/* 测试：反向查找合并 $[<'=']$'key' */
static void test_find_rev_keyword_merge(void) {
    printf("Test: Reverse find + keyword ($[<'=']$'key')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("$[<'=']$'key'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].type == STRIDE_FT_DYNAMIC_FIND_REV,
           "Type is DYNAMIC_FIND_REV");
    ASSERT(features[0].value == (int)'=', "Char is '='");
    ASSERT(features[0].keyword_len == 3, "Keyword is 'key'");

    stride_feature_free(features, count);
}

/* 测试：纯关键字 $'hello' */
static void test_single_keyword(void) {
    printf("Test: Single keyword ($'hello')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("$'hello'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].value == 0, "Value is 0");
    ASSERT(features[0].keyword_len == 5, "Keyword length is 5");

    stride_feature_free(features, count);
}

/* 测试：捕获到结尾 ${} */
static void test_capture_end_only(void) {
    printf("Test: Capture end only (${})\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${}", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].type == STRIDE_FT_CONST_ABS_END,
           "Type is FT_CONST_ABS_END");
    ASSERT(features[0].value == 0, "Value is 0 (pure END)");
    ASSERT(features[0].keyword == NULL, "No keyword");

    stride_feature_free(features, count);
}

/* 测试：相对回退 ${2}$[<3]$'k' → CONST_REL_BACK(-1) */
static void test_const_rel_back(void) {
    printf("Test: Relative backward (${2}$[<3]$'k')\n");

    size_t count = 0;
    stride_feature_t *features = compile_features("${2}$[<3]$'k'", &count);

    ASSERT(features != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(features[0].type == STRIDE_FT_CONST_REL_BACK,
           "Type is CONST_REL_BACK");
    ASSERT(features[0].value == -1, "Value is -1 (2-3)");

    stride_feature_free(features, count);
}

int main(void) {
    printf("=== Stride Feature Sequence Unit Tests ===\n\n");

    test_const_addition();
    test_dynamic_interrupt();
    test_abs_head_addition();
    test_end_merge();
    test_dynamic_keyword_merge();
    test_mixed_scenario();
    test_consecutive_keywords();
    test_find_rev_keyword_merge();
    test_single_keyword();
    test_capture_end_only();
    test_const_rel_back();

    return test_summary("Feature");
}
