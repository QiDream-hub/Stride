#include "stride/extractor.h"

#include <stdlib.h>

/* ============================================================
 * Stride 提取便捷层 - 实现
 * ============================================================ */

int stride_extract_run(const stride_extractor_t *ex, const void *segment,
                       size_t segment_len, stride_param_t *params,
                       size_t param_capacity, size_t *param_count) {
    if (!params || !param_count) {
        return -1;
    }
    return stride_seq_run(ex, segment, segment_len, params, param_capacity,
                          param_count) == 0
               ? 0
               : -1;
}
