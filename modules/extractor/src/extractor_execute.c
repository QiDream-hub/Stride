#include "stride/extractor.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Stride 提取序列模块 - 运行时实现
 *
 * 支持 10 种提取操作类型
 * 见《Stride 编译器设计文档》5.2 节
 * ============================================================ */

/* ==================== 单段执行 ==================== */

int stride_extractor_execute(const stride_extractor_t *ext,
                             const char *segment, size_t segment_len,
                             stride_param_t *params, size_t param_capacity,
                             size_t *param_count) {
    if (!ext || !segment || !params || !param_count) {
        return -1;
    }

    size_t cursor = 0;
    size_t out_idx = *param_count;

    for (size_t i = 0; i < ext->op_count; i++) {
        const stride_extractor_op_t *op = &ext->ops[i];

        switch (op->type) {
            case STRIDE_EX_CAPTURE_LEN: {
                if (out_idx >= param_capacity) {
                    return -1;
                }

                size_t remaining = segment_len - cursor;
                if (op->data.capture_len.length > remaining) {
                    return -1;
                }

                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = op->data.capture_len.length;
                out_idx++;

                cursor += op->data.capture_len.length;
                break;
            }

            case STRIDE_EX_CAPTURE_CHR: {
                if (out_idx >= param_capacity) {
                    return -1;
                }

                char target = op->data.capture_chr.ch;
                const char *found =
                    memchr(segment + cursor, target, segment_len - cursor);

                size_t end_pos;
                if (found) {
                    end_pos = (size_t)(found - segment);
                } else {
                    end_pos = segment_len;
                }

                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = end_pos - cursor;
                out_idx++;

                cursor = end_pos;
                break;
            }

            case STRIDE_EX_CAPTURE_END: {
                if (out_idx >= param_capacity) {
                    return -1;
                }

                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = segment_len - cursor;
                out_idx++;

                cursor = segment_len;
                break;
            }

            case STRIDE_EX_SKIP_LEN: {
                if (cursor + op->data.skip_len.length > segment_len) {
                    return -1;
                }
                cursor += op->data.skip_len.length;
                break;
            }

            case STRIDE_EX_JUMP_ABS: {
                /* 绝对跳转：基于 HEAD（位置 0）*/
                size_t new_pos = op->data.jump_abs.pos;
                if (new_pos > segment_len) {
                    return -1;
                }
                cursor = new_pos;
                break;
            }

            case STRIDE_EX_JUMP_END: {
                /* END 跳转 */
                if (op->data.jump_end.is_end) {
                    int offset = op->data.jump_end.offset;
                    if (offset == 0) {
                        /* 纯 END */
                        cursor = segment_len;
                    } else {
                        /* END-n：从段尾向前偏移 */
                        int new_pos = (int)segment_len - offset;
                        if (new_pos < 0 || (size_t)new_pos > segment_len) {
                            return -1;
                        }
                        cursor = (size_t)new_pos;
                    }
                }
                break;
            }

            case STRIDE_EX_JUMP_FWD: {
                /* $[>n] - 从当前位置前进 n（相对移动）*/
                if (cursor + op->data.jump_fwd.offset > segment_len) {
                    return -1;
                }
                cursor += op->data.jump_fwd.offset;
                break;
            }

            case STRIDE_EX_JUMP_BACK: {
                /* $[<n] - 从当前位置向开头回退 n */
                int new_pos = (int)cursor - (int)op->data.jump_back.offset;
                if (new_pos < 0) {
                    return -1;
                }
                cursor = (size_t)new_pos;
                break;
            }

            case STRIDE_EX_FIND_FWD: {
                char target = op->data.find_fwd.ch;
                const char *found =
                    memchr(segment + cursor, target, segment_len - cursor);
                if (!found) {
                    return -1;
                }
                cursor = (size_t)(found - segment);
                break;
            }

            case STRIDE_EX_FIND_REV: {
                /* 从当前位置向前查找字符
                 * 如果 cursor 在开头 (0)，则从段尾开始查找
                 */
                char target = op->data.find_rev.ch;

                /* 如果 cursor 在开头，从段尾开始查找 */
                size_t start_pos = (cursor == 0) ? segment_len - 1 : cursor - 1;

                if (start_pos >= segment_len) {
                    return -1; /* 段为空 */
                }

                size_t i = start_pos;
                while (1) {
                    if (segment[i] == target) {
                        cursor = i;
                        break;
                    }
                    if (i == 0) {
                        return -1; /* 未找到 */
                    }
                    i--;
                }
                break;
            }
        }
    }

    *param_count = out_idx;
    return 0;
}

/* ==================== 完整提取器 ==================== */

stride_full_extractor_t *stride_full_extractor_create(
    stride_extractor_t **seg_extractors, size_t segment_count) {
    if (!seg_extractors || segment_count == 0) {
        return NULL;
    }

    stride_full_extractor_t *full = calloc(1, sizeof(stride_full_extractor_t));
    if (!full) {
        return NULL;
    }

    full->segments = calloc(segment_count, sizeof(stride_extractor_t *));
    if (!full->segments) {
        free(full);
        return NULL;
    }

    full->segment_count = segment_count;
    full->total_params = 0;

    /* 复制指针数组（不接管 seg_extractors 数组本身的所有权）*/
    for (size_t i = 0; i < segment_count; i++) {
        full->segments[i] = seg_extractors[i];
        if (seg_extractors[i]) {
            full->total_params += seg_extractors[i]->param_count;
        }
    }

    return full;
}

void stride_full_extractor_destroy(stride_full_extractor_t *full) {
    if (!full) {
        return;
    }

    /* 释放每段的提取器 */
    for (size_t i = 0; i < full->segment_count; i++) {
        if (full->segments[i]) {
            stride_extractor_destroy(full->segments[i]);
        }
    }

    free(full->segments);
    free(full);
}

int stride_full_extractor_execute(const stride_full_extractor_t *full,
                                  const char **segments,
                                  const size_t *seg_lens,
                                  size_t segment_count,
                                  stride_param_t *params,
                                  size_t param_capacity,
                                  size_t *out_count) {
    if (!full || !segments || !params || !out_count) {
        return -1;
    }

    if (full->segment_count != segment_count) {
        return -1;
    }

    size_t param_idx = 0;

    /* 对每段执行对应的提取器 */
    for (size_t i = 0; i < segment_count; i++) {
        const char *segment = segments[i];
        size_t seg_len = seg_lens ? seg_lens[i] : strlen(segment);

        if (full->segments[i]) {
            int ret = stride_extractor_execute(full->segments[i], segment,
                                               seg_len, params,
                                               param_capacity, &param_idx);
            if (ret != 0) {
                return -1;
            }
        }
    }

    *out_count = param_idx;
    return 0;
}
