#ifndef STRIDE_SEQUENCE_H
#define STRIDE_SEQUENCE_H

#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 步进序列（Step Sequence）
 *
 * 构建：函数式。每次调用把一个新的「偏移」或「动作」追加到**尾节点**，
 *       若可与尾节点合并则就地合并，否则新建尾节点。
 *       —— 没有状态机；合并规则就是「同单位常量相加」。
 *
 * 执行：stride_seq_run()，从 head 起逐节点「偏移 → 动作」。
 *
 * 匹配序列与提取序列是同一结构的不同用法：
 *   - 匹配：只加偏移 + STRIDE_ACT_COMPARE，run 时不带参数缓冲
 *   - 提取：只加偏移 + 捕获动作，run 时带参数缓冲
 * ============================================================ */

/* ==================== 生命周期 ==================== */

/** 创建空序列 */
stride_seq_t *stride_seq_new(void);

/** 释放序列及其全部节点与字节串副本 */
void stride_seq_free(stride_seq_t *seq);

/** 清空序列（保留容器），复用前调用 */
void stride_seq_clear(stride_seq_t *seq);

size_t stride_seq_count(const stride_seq_t *seq);
size_t stride_seq_param_count(const stride_seq_t *seq);

/* ==================== 偏移 ==================== */

/**
 * 追加一个偏移。可与尾节点合并（同单位常量相加）时就地合并，
 * 否则新建尾节点。
 *
 * 合并规则：
 *   STEP_FWD + STEP_FWD/BACK      → 相加/抵消
 *   STEP_BACK + STEP_FWD/BACK     → 相加/抵消
 *   ABS_HEAD + STEP_FWD/BACK      → 结果非负时相加，否则不合并
 *   ABS_END + STEP_BACK           → 相加
 *   ABS_END + STEP_FWD            → 不合并
 *   FIND_FWD/REV                  → 从不合并
 *
 * 尾节点已有动作时也不合并，直接新建节点。
 *
 * @return 0 成功，-1 失败
 */
int stride_seq_step_fwd(stride_seq_t *seq, size_t bytes);
int stride_seq_step_back(stride_seq_t *seq, size_t bytes);
int stride_seq_abs_head(stride_seq_t *seq, size_t bytes);
int stride_seq_abs_end(stride_seq_t *seq, size_t bytes);
/** 查找目标字节串；target 会被复制，调用后即可释放 */
int stride_seq_find_fwd(stride_seq_t *seq, const stride_blob_t *target);
int stride_seq_find_rev(stride_seq_t *seq, const stride_blob_t *target);

/* ==================== 动作 ==================== */

/**
 * 追加一个动作。尾节点尚无动作时绑定到尾节点（例如把「比对字面量」
 * 绑到刚追加的偏移上），否则新建尾节点。
 *
 * @return 0 成功，-1 失败
 */
int stride_seq_compare(stride_seq_t *seq, const stride_blob_t *literal);
int stride_seq_capture_bytes(stride_seq_t *seq, size_t bytes);
int stride_seq_capture_until(stride_seq_t *seq, const stride_blob_t *target);
int stride_seq_capture_end(stride_seq_t *seq);

/* ==================== 通用执行引擎 ==================== */

/**
 * 从 head 起逐节点执行「偏移 → 动作」。
 *
 * @param seq              步进序列
 * @param segment          段数据（不透明二进制）
 * @param segment_len      段总字节数
 * @param params           参数缓冲；为 NULL 表示纯匹配（遇到捕获动作即失败）。
 *                         每个参数的 stride_param_t.len 为**字节长度**
 * @param param_capacity   params 容量
 * @param param_count      入参：已写入参数个数；出参：执行后的总数。纯匹配时可为 NULL
 * @return 0 成功；负数表示失败（-1 表示一般失败，-(i+1) 表示第 i 个节点失败）
 *
 * 无论匹配还是提取，都要求结束时游标恰好位于段尾（段尾对齐）。
 */
int stride_seq_run(const stride_seq_t *seq, const void *segment,
                   size_t segment_len, stride_param_t *params,
                   size_t param_capacity, size_t *param_count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_SEQUENCE_H */
