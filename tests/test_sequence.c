#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#include "stride/stride.h"

/* ============================================================
 * Stride 步进序列测试：构建（尾部合并）+ 引擎（匹配）
 * ============================================================ */

static stride_blob_t B(const char *s) {
    stride_blob_t b;
    b.data = s;
    b.len = strlen(s);
    return b;
}

/* ---------------- 尾部合并 ---------------- */

static void test_merge_bytes(void) {
    printf("Test: Byte merging\n");

    stride_seq_t *s = stride_seq_new();
    ASSERT(s != NULL, "seq created");

    stride_seq_step_fwd(s, 1);
    stride_seq_step_fwd(s, 1);
    stride_seq_step_fwd(s, 1);
    ASSERT(stride_seq_count(s) == 1, "3 forward bytes merge into one node");
    ASSERT(s->head->move == STRIDE_MOVE_STEP_FWD && s->head->move_value == 3,
           "merged value is 3");

    stride_seq_step_back(s, 2);
    ASSERT(stride_seq_count(s) == 1, "back merges into the same node");
    ASSERT(s->head->move == STRIDE_MOVE_STEP_FWD && s->head->move_value == 1,
           "3 - 2 = 1 forward");

    stride_seq_step_back(s, 5);
    ASSERT(stride_seq_count(s) == 1, "over-cancel stays one node");
    ASSERT(s->head->move == STRIDE_MOVE_STEP_BACK && s->head->move_value == 4,
           "turns into 4 back");

    stride_seq_free(s);
}

static void test_merge_abs(void) {
    printf("Test: Absolute merging\n");

    stride_seq_t *s = stride_seq_new();

    stride_seq_abs_end(s, 0); /* ${} */
    stride_seq_step_back(s, 4);
    ASSERT(stride_seq_count(s) == 1, "END + back merges");
    ASSERT(s->head->move == STRIDE_MOVE_ABS_END && s->head->move_value == 4,
           "END-4");

    stride_seq_free(s);

    s = stride_seq_new();
    stride_seq_abs_head(s, 5);
    stride_seq_step_fwd(s, 2);
    ASSERT(stride_seq_count(s) == 1, "HEAD + fwd merges");
    ASSERT(s->head->move == STRIDE_MOVE_ABS_HEAD && s->head->move_value == 7,
           "HEAD+7");
    stride_seq_free(s);

    s = stride_seq_new();
    stride_seq_abs_end(s, 0);
    stride_seq_step_fwd(s, 3);
    ASSERT(stride_seq_count(s) == 2, "END + fwd does NOT merge");
    stride_seq_free(s);
}

static void test_find(void) {
    printf("Test: Find operations\n");

    stride_seq_t *s = stride_seq_new();

    /* FIND 从不合并 */
    stride_blob_t dot = B(".");
    stride_seq_find_fwd(s, &dot);
    stride_seq_find_fwd(s, &dot);
    ASSERT(stride_seq_count(s) == 2, "two finds stay separate");

    stride_seq_free(s);
}

static void test_action_binding(void) {
    printf("Test: Action binding to tail\n");

    stride_seq_t *s = stride_seq_new();
    stride_blob_t lit = B("user");

    /* 先偏移，再比对 → 绑到同一节点 */
    stride_seq_step_fwd(s, 3);
    stride_seq_compare(s, &lit);
    ASSERT(stride_seq_count(s) == 1, "compare binds to the pending move");
    ASSERT(s->head->move == STRIDE_MOVE_STEP_FWD && s->head->move_value == 3,
           "move preserved");
    ASSERT(s->head->act == STRIDE_ACT_COMPARE, "act is COMPARE");
    ASSERT(s->head->act_target.len == 4, "literal copied");

    /* 尾节点已有动作 → 新建节点 */
    stride_blob_t again = B("x");
    stride_seq_compare(s, &again);
    ASSERT(stride_seq_count(s) == 2, "second compare opens a new node");

    stride_seq_free(s);
}

/* ---------------- 引擎：匹配 ---------------- */

static void test_match_basic(void) {
    printf("Test: Match run (keyword)\n");

    stride_seq_t *m = stride_seq_new();
    stride_blob_t user = B("user");
    stride_seq_compare(m, &user);

    ASSERT(stride_match_run(m, "user", 4) == 0, "user matches");
    ASSERT(stride_match_run(m, "usex", 4) != 0,
           "keyword mismatch");
    ASSERT(stride_match_run(m, "us", 2) != 0, "too short");

    stride_seq_free(m);
}

static void test_match_version(void) {
    printf("Test: Match run (version pattern)\n");

    /* 手工拼出 $'v'${'.'}$'.'${} 的匹配序列 */
    stride_seq_t *m = stride_seq_new();
    stride_blob_t v = B("v");
    stride_blob_t dot = B(".");

    stride_seq_compare(m, &v);          /* $'v' */
    stride_seq_find_fwd(m, &dot);       /* ${'.'} 在匹配阶段等价于查找 */
    stride_seq_compare(m, &dot);        /* $'.' → 绑到上一个节点 */
    stride_seq_abs_end(m, 0);           /* ${} */

    ASSERT(stride_seq_count(m) == 3, "three nodes");
    ASSERT(stride_match_run(m, "v2.0", 4) == 0, "v2.0 matches");
    ASSERT(stride_match_run(m, "v2", 2) != 0, "v2 fails");
    ASSERT(stride_match_run(m, "x2.0", 4) != 0,
           "leading literal fails");

    stride_seq_free(m);
}

static void test_match_failures(void) {
    printf("Test: Match failures\n");

    stride_seq_t *m = stride_seq_new();
    stride_seq_step_fwd(m, 3);

    ASSERT(stride_match_run(m, "abc", 3) == 0, "3 bytes ok");
    ASSERT(stride_match_run(m, "ab", 2) != 0, "over-run fails");

    /* 段尾未对齐 */
    m = stride_seq_new();
    stride_seq_step_fwd(m, 2);
    ASSERT(stride_match_run(m, "abc", 3) != 0,
           "tail not aligned fails");
    stride_seq_free(m);

    /* 查不到 */
    m = stride_seq_new();
    stride_blob_t z = B("z");
    stride_seq_find_fwd(m, &z);
    ASSERT(stride_match_run(m, "abc", 3) != 0, "find fails");
    stride_seq_free(m);
}

int main(void) {
    printf("=== Stride Sequence Tests ===\n\n");

    test_merge_bytes();
    test_merge_abs();
    test_find();
    test_action_binding();

    test_match_basic();
    test_match_version();
    test_match_failures();

    return test_summary("Sequence");
}
