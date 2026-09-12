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
    b.bit_len = STRIDE_BITS(strlen(s));
    return b;
}

/* 手工翻译 $'v'${'.'}$'.'${}（步长 8）的匹配序列 */
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

/* 手工翻译 ${4}$'-'${2}$'-'${2}（步长 8）的提取序列 */
static stride_seq_t *build_extract_date(void) {
    stride_seq_t *e = stride_seq_new();
    stride_seq_capture_steps(e, 4);          /* ${4} */
    stride_seq_skip_bits(e, STRIDE_BITS(1)); /* $'-' */
    stride_seq_capture_steps(e, 2);          /* ${2} */
    stride_seq_skip_bits(e, STRIDE_BITS(1)); /* $'-' */
    stride_seq_capture_steps(e, 2);          /* ${2} */
    return e;
}

static void dump_node(const stride_step_t *n) {
    static const char *move_names[] = {"NONE", "FWD",  "BACK", "ABS_HEAD",
                                       "ABS_END", "SKIP_BITS", "FIND_FWD",
                                       "FIND_REV"};
    static const char *act_names[] = {"NONE", "COMPARE", "CAP_STEPS",
                                      "CAP_UNTIL", "CAP_END"};
    printf("  move=%-10s value=%-4zu act=%-10s", move_names[n->move],
           n->move_value, act_names[n->act]);
    if (n->move_target.bit_len) {
        printf(" find=\"%.*s\"", (int)(n->move_target.bit_len / 8),
               (const char *)n->move_target.data);
    }
    if (n->act == STRIDE_ACT_COMPARE && n->act_target.bit_len) {
        printf(" expect=\"%.*s\"", (int)(n->act_target.bit_len / 8),
               (const char *)n->act_target.data);
    }
    printf("\n");
}

int main(void) {
    printf("Stride %s —— 步进式比特串匹配库示例\n\n", STRIDE_VERSION_STRING);

    /* ---------- ① 匹配 ---------- */
    stride_seq_t *m = build_match_version();
    printf("匹配序列 $'v'${'.'}$'.'${}（步长 8）\n");
    for (const stride_step_t *n = m->head; n; n = n->next) {
        dump_node(n);
    }

    const char *seg = "v2.0";
    printf("匹配 \"%s\" → %s\n", seg,
           stride_match_run(m, 8, seg, STRIDE_BITS(4)) == 0 ? "命中"
                                                            : "未命中");
    printf("匹配 \"v2\"   → %s\n",
           stride_match_run(m, 8, "v2", STRIDE_BITS(2)) == 0 ? "命中"
                                                             : "未命中");
    stride_seq_free(m);

    /* ---------- ② 提取 ---------- */
    stride_seq_t *e = build_extract_date();
    const char *date = "2024-03-15";
    printf("\n提取序列 ${4}$'-'${2}$'-'${2}（步长 8，%zu 个参数）\n",
           stride_seq_param_count(e));

    stride_param_t params[8];
    size_t count = 0;
    if (stride_extract_run(e, 8, date, STRIDE_BITS(strlen(date)), params, 8,
                           &count) == 0) {
        for (size_t i = 0; i < count; i++) {
            printf("  [%zu] %.*s\n", i, (int)(params[i].bit_len / 8),
                   (const char *)params[i].ptr);
        }
    }
    stride_seq_free(e);

    /* ---------- ③ 比特级：步长 1 ---------- */
    printf("\n比特级匹配（步长 1）：段 = 0xB0 = 1011 0000\n");
    const unsigned char raw[] = {0xB0};
    stride_seq_t *bits = stride_seq_new();
    stride_seq_step_fwd(bits, 4); /* 跳过高 4 位 */
    stride_blob_t low = {(const void *)"\x00", 4};
    stride_seq_compare(bits, &low);
    printf("  跳 4 位后比对低 4 位 0000 → %s\n",
           stride_match_run(bits, 1, raw, 8) == 0 ? "命中" : "未命中");
    stride_seq_free(bits);

    return 0;
}
