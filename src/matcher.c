#include "stride/matcher.h"

/* ============================================================
 * Stride 匹配便捷层 - 实现
 *
 * 直接复用通用执行引擎：匹配 = 不带参数缓冲的一次序列执行。
 * ============================================================ */

int stride_match_run(const stride_seq_t *seq, size_t stride,
                     const void *segment, size_t segment_bit_len) {
    return stride_seq_run(seq, stride, segment, segment_bit_len, NULL, 0, NULL);
}
