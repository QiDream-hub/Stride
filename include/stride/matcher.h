#ifndef STRIDE_MATCHER_H
#define STRIDE_MATCHER_H

#include "stride/sequence.h"
#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 匹配便捷层
 *
 * 匹配序列就是一个只含「偏移 + STRIDE_ACT_COMPARE」的步进序列，
 * 由 stride/sequence.h 的构建函数组装：
 *
 *     stride_seq_t *m = stride_seq_new();
 *     stride_seq_step_fwd(m, 3);                    // 跳过 3 字节
 *     stride_seq_compare(m, &(stride_blob_t){...}); // 在该处比对字面量
 *
 * 本头文件只提供执行入口。
 * ============================================================ */

/**
 * 用匹配序列匹配一个段
 * @param seq              匹配序列（只应含偏移与 COMPARE 动作）
 * @param segment          段数据
 * @param segment_len      段总字节数
 * @return 0 匹配成功；负数表示第 |r|-1 个节点失败，或段尾未对齐
 */
int stride_match_run(const stride_seq_t *seq, const void *segment, size_t segment_len);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_MATCHER_H */
