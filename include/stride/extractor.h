#ifndef STRIDE_EXTRACTOR_H
#define STRIDE_EXTRACTOR_H

#include "stride/sequence.h"
#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 提取便捷层
 *
 * 提取序列同样是一个步进序列，节点为「偏移 + 捕获动作」：
 *
 *     stride_seq_t *e = stride_seq_new();
 *     stride_seq_capture_bytes(e, 2);   // 捕获 2 字节
 *
 * 与匹配序列共用 stride_seq_run() 这一通用执行引擎，
 * 区别只在于执行时带参数缓冲。本头文件提供单段/多段执行入口。
 * ============================================================ */

/** 提取序列 —— 与步进序列同构 */
typedef stride_seq_t stride_extractor_t;

/**
 * 在单个段上执行提取
 * @param ex               提取序列
 * @param segment          段数据
 * @param segment_len      段总字节数
 * @param params           参数数组
 * @param param_capacity   参数数组容量
 * @param param_count      入参：已写入的参数数；出参：写入后的参数总数
 * @return 0 成功，-1 失败
 */
int stride_extract_run(const stride_extractor_t *ex, const void *segment,
                       size_t segment_len, stride_param_t *params,
                       size_t param_capacity, size_t *param_count);
#ifdef __cplusplus
}
#endif

#endif /* STRIDE_EXTRACTOR_H */
