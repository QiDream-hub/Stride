#include <stdio.h>
#include <string.h>

#include "stride/stride.h"

/* ============================================================
 * Stride v3 示例：函数式构建序列 + 通用执行引擎
 *
 * Stride 不含语法：下面「模式字符串 → 序列」的翻译由调用方完成，
 * URLRouter 就是这样一个调用方（它提供 $'' / ${} / $[] 的词法）。
 * ============================================================ */

static stride_blob_t blob(const char *s) {
    stride_blob_t b;
    b.data = s;
    b.len = strlen(s);
    return b;
}

/* 手工翻译 $'v'${'.'}$'.'${} 的匹配序列 */
static stride_seq_t *build_match_version(void) {
    stride_seq_t *m = stride_seq_new();
    stride_blob_t v = blob("v");
    stride_blob_t dot = blob(".");

    stride_seq_compare(m, &v);    /* $'v' */
    stride_seq_find_fwd(m, &dot); /* ${'.'} 匹配阶段 = 查找 */
    stride_seq_compare(m, &dot);  /* $'.' 绑到上一个节点 */
    stride_seq_abs_end(m, 0);     /* ${} */
    return m;
}

/* 手工翻译 ${4}$'-'${2}$'-'${2} 的提取序列 */
static stride_seq_t *build_extract_date(void) {
    stride_seq_t *e = stride_seq_new();
    stride_blob_t dash = blob("-");
    stride_seq_capture_until(e, &dash);      /* ${4} 捕获到 '-' */
    stride_seq_capture_until(e, &dash);      /* ${2} 捕获到 '-' */
    stride_seq_capture_end(e);               /* ${2} 捕获到段尾 */
    return e;
}

static void dump_node(const stride_step_t *n) {
    static const char *move_names[] = {"NONE", "FWD",  "BACK", "ABS_HEAD",
                                       "ABS_END", "FIND_FWD", "FIND_REV"};
    static const char *act_names[] = {"NONE", "COMPARE", "CAP_BYTES",
                                      "CAP_UNTIL", "CAP_END"};
    printf("  move=%-10s value=%-4zu act=%-10s", move_names[n->move],
           n->move_value, act_names[n->act]);
    if (n->move_target.len) {
        printf(" find=\"%.*s\"", (int)n->move_target.len,
               (const char *)n->move_target.data);
    }
    if (n->act == STRIDE_ACT_COMPARE && n->act_target.len) {
        printf(" expect=\"%.*s\"", (int)n->act_target.len,
               (const char *)n->act_target.data);
    }
    printf("\n");
}

int main(void) {
    printf("Stride %s —— 步进式字节串匹配库示例\n\n", STRIDE_VERSION_STRING);

    /* ---------- ① 匹配 ---------- */
    stride_seq_t *m = build_match_version();
    printf("匹配序列 $'v'${'.'}$'.'${}\n");
    for (const stride_step_t *n = m->head; n; n = n->next) {
        dump_node(n);
    }

    const char *seg = "v2.0";
    printf("匹配 \"%s\" → %s\n", seg,
           stride_match_run(m, seg, 4) == 0 ? "命中" : "未命中");
    printf("匹配 \"v2\"   → %s\n",
           stride_match_run(m, "v2", 2) == 0 ? "命中" : "未命中");
    stride_seq_free(m);

    /* ---------- ② 提取 ---------- */
    stride_seq_t *e = build_extract_date();
    const char *date = "2024-03-15";
    printf("\n提取序列 ${4}$'-'${2}$'-'${2}（%zu 个参数）\n",
           stride_seq_param_count(e));

    stride_param_t params[8];
    size_t count = 0;
    if (stride_extract_run(e, date, 10, params, 8, &count) == 0) {
        for (size_t i = 0; i < count; i++) {
            printf("  [%zu] %.*s\n", i, (int)params[i].len,
                   (const char *)params[i].ptr);
        }
    }
    stride_seq_free(e);

    return 0;
}
