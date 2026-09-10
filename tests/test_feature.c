#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/compiler.h"
#include "stride/feature.h"

/* ============================================================
 * 特征序列测试：编译 + 匹配
 * ============================================================ */

/* ---------------- 编译 ---------------- */

static stride_feature_t *compile_features(const char *pattern,
                                          size_t *out_count) {
    stride_feature_t *features = NULL;
    size_t count = 0, capacity = 0;

    if (stride_compile_features(pattern, &features, &count, &capacity) != 0) {
        return NULL;
    }
    *out_count = count;
    return features;
}

static void test_const_addition(void) {
    printf("Test: Constant addition (${1}${1}${1}$'key')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("${1}${1}${1}$'key'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(f[0].type == STRIDE_FT_CONST_REL_FWD, "Type is CONST_REL_FWD");
    ASSERT(f[0].value == 3, "Value is 3 (1+1+1)");
    ASSERT(f[0].keyword != NULL && f[0].keyword_len == 3, "Keyword is 3 bytes");
    ASSERT(strncmp(f[0].keyword, "key", 3) == 0, "Keyword is 'key'");

    stride_feature_free(f, count);
}

static void test_dynamic_interrupt(void) {
    printf("Test: Dynamic interrupt (${2}${'a'}${3}$'b')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("${2}${'a'}${3}$'b'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three feature tuples output");
    ASSERT(f[0].type == STRIDE_FT_CONST_REL_FWD && f[0].value == 2,
           "First is REL_FWD(2)");
    ASSERT(f[1].type == STRIDE_FT_DYNAMIC_FIND_FWD, "Second is FIND_FWD");
    ASSERT(f[1].value == (int)'a', "Second char is 'a'");
    ASSERT(f[2].value == 3, "Third value is 3");

    stride_feature_free(f, count);
}

static void test_abs_head_addition(void) {
    printf("Test: HEAD absolute addition ($[5]${2}$'key')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("$[5]${2}$'key'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(f[0].type == STRIDE_FT_CONST_ABS_HEAD, "Type is ABS_HEAD");
    ASSERT(f[0].value == 7, "Value is 7 (5+2)");

    stride_feature_free(f, count);
}

static void test_end_merge(void) {
    printf("Test: END merge (${}$[<4]$'dddd')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("${}$[<4]$'dddd'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(f[0].type == STRIDE_FT_CONST_ABS_END, "Type is ABS_END");
    ASSERT(f[0].value == 4, "Value is 4 (END-4，非负幅度)");

    stride_feature_free(f, count);
}

static void test_dynamic_keyword_merge(void) {
    printf("Test: Dynamic + keyword merge (${'a'}$'key')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("${'a'}$'key'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(f[0].type == STRIDE_FT_DYNAMIC_FIND_FWD, "Type is FIND_FWD");
    ASSERT(f[0].value == (int)'a', "Char is 'a'");
    ASSERT(f[0].keyword != NULL, "Keyword is present");

    stride_feature_free(f, count);
}

static void test_consecutive_keywords(void) {
    printf("Test: Consecutive keywords ($'dd'$'aaa')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("$'dd'$'aaa'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two feature tuples output");
    ASSERT(f[0].value == 0 && f[1].value == 0, "Both values are 0");

    stride_feature_free(f, count);
}

static void test_find_rev_keyword_merge(void) {
    printf("Test: Reverse find + keyword ($[<'=']$'key')\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("$[<'=']$'key'", &count);

    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple output");
    ASSERT(f[0].type == STRIDE_FT_DYNAMIC_FIND_REV, "Type is FIND_REV");
    ASSERT(f[0].value == (int)'=', "Char is '='");
    ASSERT(f[0].keyword_len == 3, "Keyword is 'key'");

    stride_feature_free(f, count);
}

static void test_rel_back_and_capture_end(void) {
    printf("Test: Relative back and capture-end\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("${2}$[<3]$'k'", &count);
    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple");
    ASSERT(f[0].type == STRIDE_FT_CONST_REL_BACK, "Type is REL_BACK");
    ASSERT(f[0].value == 1, "Value is 1 (3-2，非负幅度)");
    stride_feature_free(f, count);

    f = compile_features("${}", &count);
    ASSERT(f != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One feature tuple");
    ASSERT(f[0].type == STRIDE_FT_CONST_ABS_END, "Type is ABS_END");
    ASSERT(f[0].value == 0, "Value is 0 (pure END)");
    ASSERT(f[0].keyword == NULL, "No keyword");
    stride_feature_free(f, count);
}

/* ---------------- 匹配 ---------------- */

/* 编译模式并直接对段做匹配；-2 表示编译失败 */
static int match_pattern(const char *pattern, const char *segment) {
    size_t count = 0;
    stride_feature_t *f = compile_features(pattern, &count);
    if (!f) {
        return -2;
    }
    int rc = stride_feature_match(f, count, segment, strlen(segment));
    stride_feature_free(f, count);
    return rc;
}

static void test_match_keyword(void) {
    printf("Test: Match keyword ($'user')\n");

    ASSERT(match_pattern("$'user'", "user") == 0, "Exact segment matches");
    ASSERT(match_pattern("$'user'", "usera") == -1,
           "Trailing byte breaks segment-end alignment");
    ASSERT(match_pattern("$'user'", "us") == -1, "Too short fails");
    ASSERT(match_pattern("$'user'", "userX") == -1, "Longer fails");
}

static void test_match_capture_len(void) {
    printf("Test: Match length capture (${2})\n");

    ASSERT(match_pattern("${2}", "ab") == 0, "Exactly 2 matches");
    ASSERT(match_pattern("${2}", "abc") == -1, "3 bytes fails alignment");
    ASSERT(match_pattern("${2}", "a") == -1, "1 byte fails movement");
}

static void test_match_capture_end(void) {
    printf("Test: Match capture-end (${})\n");

    ASSERT(match_pattern("${}", "alice") == 0, "Any segment matches");
    ASSERT(match_pattern("${}", "") == 0, "Empty segment matches");
    ASSERT(match_pattern("${}$[<4]$'dddd'", "aaaadddd") == 0,
           "END-4 keyword matches");
    ASSERT(match_pattern("${}$[<4]$'dddd'", "aaaaxxxx") == -1,
           "END-4 keyword mismatch fails");
}

static void test_match_version(void) {
    printf("Test: Match version segment ($'v'${'.'}$'.'${})\n");

    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "v2.0") == 0,
           "v2.0 matches");
    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "v2") == -1, "v2 fails");
    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "v2.0.1") == 0,
           "Trailing bytes absorbed by capture-end");
    ASSERT(match_pattern("$'v'${'.'}$'.'${}", "x2.0") == -1,
           "Leading keyword mismatch fails");
    ASSERT(match_pattern("$'v'${2}", "v2.0") == -1,
           "Tail not aligned to segment end fails");
}

static void test_match_reverse_find(void) {
    printf("Test: Match reverse find ($[<'=']${})\n");

    ASSERT(match_pattern("$[<'=']${}", "name=alice") == 0, "Finds '='");
    ASSERT(match_pattern("$[<'=']${}", "namealice") == -1, "No '=' fails");
}

static void test_match_backtrack(void) {
    printf("Test: Match backtrack (${}$[0]${'.'}$'.'${})\n");

    ASSERT(match_pattern("${}$[0]${'.'}$'.'${}", "document.pdf") == 0,
           "Backtrack pattern matches");
    ASSERT(match_pattern("${}$[0]${'.'}$'.'${}", "documentpdf") == -1,
           "Without '.' fails");
}

static void test_match_manual_array(void) {
    printf("Test: Match a hand-built feature array (no compiler)\n");

    /* 证明匹配能力只依赖 feature.h 定义的数据结构 */
    stride_feature_t f[2];
    memset(f, 0, sizeof(f));

    f[0].type = STRIDE_FT_CONST_REL_FWD;
    f[0].value = 0; /* 关键字元组不移动，直接在该位置验证 */
    f[0].keyword = "user";
    f[0].keyword_len = 4;

    f[1].type = STRIDE_FT_CONST_ABS_END;
    f[1].value = 0;

    ASSERT(stride_feature_match(f, 1, "user", 4) == 0,
           "Hand-built keyword-only matches");
    ASSERT(stride_feature_match(f, 1, "userx", 5) == -1,
           "Keyword-only rejects trailing bytes");
    ASSERT(stride_feature_match(f, 1, "use", 3) == -1,
           "Keyword-only rejects short input");
    ASSERT(stride_feature_match(f, 2, "userx", 5) == 0,
           "Trailing ABS_END absorbs the suffix");
}

static void test_match_detail(void) {
    printf("Test: Match diagnostics (stride_feature_match_ex)\n");

    size_t count = 0;
    stride_feature_t *f = compile_features("${2}", &count);
    stride_match_detail_t d;

    ASSERT(stride_feature_match_ex(f, count, "abc", 3, &d) == -1,
           "Mismatch reported");
    ASSERT(d.matched == 0, "matched is 0");
    ASSERT(d.fail_index == count, "fail_index == count (段尾未对齐)");
    ASSERT(d.cursor == 2, "cursor is 2 after consuming 2 bytes");

    ASSERT(stride_feature_match_ex(f, count, "ab", 2, &d) == 0, "Match OK");
    ASSERT(d.matched == 1, "matched is 1");
    ASSERT(d.cursor == 2, "cursor at segment end");
    stride_feature_free(f, count);

    /* 关键字不符时 fail_index 指向该元组 */
    f = compile_features("$'user'", &count);
    ASSERT(stride_feature_match_ex(f, count, "usex", 4, &d) == -1,
           "Keyword mismatch reported");
    ASSERT(d.fail_index == 0, "fail_index is 0");
    stride_feature_free(f, count);

    /* 移动越界时 fail_index 指向该元组 */
    f = compile_features("$[>5]", &count);
    ASSERT(stride_feature_match_ex(f, count, "ab", 2, &d) == -1,
           "Movement overflow reported");
    ASSERT(d.fail_index == 0, "fail_index is 0 for movement overflow");
    stride_feature_free(f, count);
}

static void test_match_edge_cases(void) {
    printf("Test: Match edge cases\n");

    ASSERT(stride_feature_match(NULL, 0, "", 0) == 0,
           "Empty feature sequence matches empty segment");
    ASSERT(stride_feature_match(NULL, 0, "x", 1) == -1,
           "Empty feature sequence rejects non-empty segment");
    ASSERT(stride_feature_match(NULL, 1, "x", 1) == -1,
           "NULL features with non-zero count rejected");
    ASSERT(stride_feature_match(NULL, 0, NULL, 0) == -1,
           "NULL segment rejected");
}

int main(void) {
    printf("=== Stride Feature Sequence Tests ===\n\n");

    test_const_addition();
    test_dynamic_interrupt();
    test_abs_head_addition();
    test_end_merge();
    test_dynamic_keyword_merge();
    test_consecutive_keywords();
    test_find_rev_keyword_merge();
    test_rel_back_and_capture_end();

    test_match_keyword();
    test_match_capture_len();
    test_match_capture_end();
    test_match_version();
    test_match_reverse_find();
    test_match_backtrack();
    test_match_manual_array();
    test_match_detail();
    test_match_edge_cases();

    return test_summary("Feature");
}
