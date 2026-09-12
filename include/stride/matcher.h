#ifndef STRIDE_MATCHER_H
#define STRIDE_MATCHER_H

#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 匹配序列（Match Sequence）
 *
 * 匹配序列用于匹配阶段：每个元组描述一个“按步移动”和在该位置需要
 * 比对的“比特串”。只依赖 types.h，不涉及提取序列。
 *
 * 本头文件提供两件事：
 *   1. 编译：操作符序列 → 匹配序列（IDLE / HOLD 两状态机）
 *   2. 匹配：用匹配序列匹配一个段（stride_match_run）
 *
 * 单位：步长以比特计；steps 为步数；bit_len 为比特长度。
 * ============================================================ */

/* ==================== 数据结构 ==================== */

/**
 * 匹配元组类型（6 种）
 */
typedef enum {
  STRIDE_MT_STEP_FWD = 0, /* 步进向段尾：(n 步, expect) */
  STRIDE_MT_STEP_BACK,    /* 步进向段首：(n 步, expect) */
  STRIDE_MT_ABS_HEAD,     /* 绝对步位置（基于 HEAD）：(HEAD+n, expect) */
  STRIDE_MT_ABS_END,      /* 绝对步位置（基于 END）：(END-n, expect) */
  STRIDE_MT_FIND_FWD,     /* 向段尾查找比特串：("S", expect) */
  STRIDE_MT_FIND_REV      /* 向段首查找比特串：("<", "S", expect) */
} stride_match_type_t;

/**
 * 匹配元组 —— (移动操作, 比对字面量) 对
 *
 * steps 语义（一律非负，方向由 type 表达）：
 * | type          | 含义                                   |
 * |---------------|----------------------------------------|
 * | STEP_FWD      | 向段尾移动的步数                        |
 * | STEP_BACK     | 向段首移动的步数                        |
 * | ABS_HEAD      | 目标步位置 = HEAD + steps               |
 * | ABS_END       | 目标步位置 = END - steps（0 表示段尾）  |
 * | FIND_FWD/REV  | 未使用；查找目标见 delimiter            |
 *
 * delimiter / expect 由本模块拥有（stride_match_compile 会复制），
 * 通过 stride_match_free 释放。
 */
typedef struct {
  stride_match_type_t type;
  size_t steps;            /* 非负步数 */
  stride_blob_t delimiter; /* FIND_* 的查找目标；其他类型为空 */
  stride_blob_t expect;    /* 在该处要比对的字面量；可为空 */
} stride_match_op_t;

/* ==================== 编译 ==================== */

/**
 * 匹配序列编译：操作符序列 → 匹配序列
 * @param ops           操作符数组（单个段的词法分析结果）
 * @param op_count      操作符数量
 * @param stride        编译期步长（比特/步）；0 表示未知（不校验对齐），≥1
 * 表示已知
 * @param out_ops       输出匹配元组数组（调用者通过 stride_match_free 释放）
 * @param out_count     输出元组数量
 * @param out_capacity  输出数组容量
 * @return 0 成功，-1 失败
 *
 * 状态机规则：
 * - IDLE + 步进/查找 → HOLD
 * - IDLE + 比对字面量 → 输出 (0, literal)，保持 IDLE
 * - HOLD + 步进       → 尝试相加，否则输出持有
 * - HOLD + 查找       → 输出持有，持有新元组
 * - HOLD + 比对字面量 → 输出 (持有值, literal)，清空持有
 */
int stride_match_compile(const stride_op_t *ops, size_t op_count, size_t stride,
                         stride_match_op_t **out_ops, size_t *out_count,
                         size_t *out_capacity);

/**
 * 释放匹配元组数组（含每个元组的 delimiter 与 expect）
 */
void stride_match_free(stride_match_op_t *ops, size_t count);

/* ==================== 匹配 ==================== */

/**
 * 用匹配序列匹配一个段
 * @param ops              匹配元组数组
 * @param count            元组数量
 * @param stride           执行期步长（比特/步，必须 ≥ 1；0 视为 1）
 * @param segment          段数据（不透明二进制）
 * @param segment_bit_len  段比特长度
 * @return 0 匹配成功，负数表示第几步失败
 */
int stride_match_run(const stride_match_op_t *ops, size_t count, size_t stride,
                     const void *segment, size_t segment_bit_len);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_MATCHER_H */
