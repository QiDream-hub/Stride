#include "stride/extractor.h"

#include <stdlib.h>

/* ============================================================
 * Stride 提取便捷层 - 实现
 * ============================================================ */

int stride_extract_run(const stride_extractor_t *ex, size_t stride,
                       const void *segment, size_t segment_bit_len,
                       stride_param_t *params, size_t param_capacity,
                       size_t *param_count) {
    if (!params || !param_count) {
        return -1;
    }
    return stride_seq_run(ex, stride, segment, segment_bit_len, params,
                          param_capacity, param_count) == 0
               ? 0
               : -1;
}

/* ==================== 多段提取器 ==================== */

stride_full_extractor_t *stride_full_extractor_create(
    stride_extractor_t **seg_extractors, size_t segment_count) {
    if (!seg_extractors || segment_count == 0) {
        return NULL;
    }

    stride_full_extractor_t *full =
        (stride_full_extractor_t *)calloc(1, sizeof(stride_full_extractor_t));
    if (!full) {
        return NULL;
    }

    full->segments =
        (stride_extractor_t **)calloc(segment_count, sizeof(stride_extractor_t *));
    if (!full->segments) {
        free(full);
        return NULL;
    }

    full->segment_count = segment_count;
    full->total_params = 0;
    for (size_t i = 0; i < segment_count; i++) {
        full->segments[i] = seg_extractors[i];
        if (seg_extractors[i]) {
            full->total_params += stride_seq_param_count(seg_extractors[i]);
        }
    }

    return full;
}

void stride_full_extractor_destroy(stride_full_extractor_t *full) {
    if (!full) {
        return;
    }
    for (size_t i = 0; i < full->segment_count; i++) {
        stride_seq_free(full->segments[i]);
    }
    free(full->segments);
    free(full);
}

int stride_full_extractor_run(const stride_full_extractor_t *full, size_t stride,
                              const void *const *segments,
                              const size_t *seg_bit_lens, size_t segment_count,
                              stride_param_t *params, size_t param_capacity,
                              size_t *out_count) {
    if (!full || !segments || !seg_bit_lens || !params || !out_count) {
        return -1;
    }
    if (full->segment_count != segment_count) {
        return -1;
    }

    size_t param_idx = 0;
    for (size_t i = 0; i < segment_count; i++) {
        if (!full->segments[i]) {
            continue;
        }
        if (stride_extract_run(full->segments[i], stride, segments[i],
                               seg_bit_lens[i], params, param_capacity,
                               &param_idx) != 0) {
            return -1;
        }
    }

    *out_count = param_idx;
    return 0;
}
