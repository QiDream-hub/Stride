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
 *     stride_seq_capture_steps(e, 2);   // ${2}
 *
 * 与匹配序列共用 stride_seq_run() 这一通用执行引擎，
 * 区别只在于执行时带参数缓冲。本头文件提供单段/多段执行入口。
 * ============================================================ */

/** 提取序列 —— 与步进序列同构 */
typedef stride_seq_t stride_extractor_t;

/**
 * 在单个段上执行提取
 * @param ex               提取序列
 * @param stride           执行期步长（比特/步）；0 视为 1
 * @param segment          段数据
 * @param segment_bit_len  段比特长度
 * @param params           参数数组
 * @param param_capacity   参数数组容量
 * @param param_count      入参：已写入的参数数；出参：写入后的参数总数
 * @return 0 成功，-1 失败
 */
int stride_extract_run(const stride_extractor_t *ex, size_t stride,
                       const void *segment, size_t segment_bit_len,
                       stride_param_t *params, size_t param_capacity,
                       size_t *param_count);

/* ==================== 多段提取 ==================== */

/**
 * 完整提取器 —— 按段顺序组合多个单段提取序列
 *
 * 注意：create 仅复制指针数组，不接管 seg_extractors 数组本身的所有权；
 * 但 destroy 会释放其中每个提取序列。
 */
typedef struct {
    stride_extractor_t **segments;
    size_t segment_count;
    size_t total_params;
} stride_full_extractor_t;

stride_full_extractor_t *stride_full_extractor_create(
    stride_extractor_t **seg_extractors, size_t segment_count);

void stride_full_extractor_destroy(stride_full_extractor_t *full);

/**
 * 执行完整提取（多段，参数按段顺序连接）
 * @param full           完整提取器
 * @param stride         执行期步长（比特/步，所有段共用）
 * @param segments       段数组
 * @param seg_bit_lens   每段比特长度数组（不可为 NULL）
 * @param segment_count  段数
 * @param params         参数数组
 * @param param_capacity 参数数组容量
 * @param out_count      输出参数总数
 * @return 0 成功，-1 失败
 */
int stride_full_extractor_run(const stride_full_extractor_t *full, size_t stride,
                              const void *const *segments,
                              const size_t *seg_bit_lens, size_t segment_count,
                              stride_param_t *params, size_t param_capacity,
                              size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_EXTRACTOR_H */
