#include "stride/extractor.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_EXTRACTOR_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 提取序列 - 实现
 *
 * 1. 编译：操作符序列 → 提取序列（两项编译时优化）
 * 2. 执行：在段上运行提取序列，产出零拷贝参数
 * ============================================================ */

/* ==================== 辅助判断 ==================== */

static int extractor_op_produces_param(stride_extractor_op_type_t type) {
    return (type == STRIDE_EX_CAPTURE_LEN ||
            type == STRIDE_EX_CAPTURE_CHR ||
            type == STRIDE_EX_CAPTURE_END);
}

/**
 * 可合并的常量移动操作：SKIP_LEN、JUMP_FWD、JUMP_BACK
 */
static int extractor_is_const_move(stride_extractor_op_type_t type) {
    return (type == STRIDE_EX_SKIP_LEN ||
            type == STRIDE_EX_JUMP_FWD ||
            type == STRIDE_EX_JUMP_BACK);
}

static void extractor_array_grow(stride_extractor_op_t **extractors,
                                 size_t *capacity) {
    size_t new_cap = *capacity * 2;
    stride_extractor_op_t *new_arr =
        realloc(*extractors, new_cap * sizeof(stride_extractor_op_t));
    if (new_arr) {
        *extractors = new_arr;
        *capacity = new_cap;
    }
}

/**
 * 输出一个合并段：含 SKIP_LEN 且结果为正时输出 SKIP_LEN，
 * 否则按符号输出 JUMP_FWD / JUMP_BACK；净位移为 0 时为无操作，不输出。
 * @return 实际写入的操作数（0 或 1）
 */
static size_t extractor_emit_merge(stride_extractor_op_t *dst,
                                   int merge_value, int has_skip_len) {
    if (merge_value == 0) {
        return 0;
    }
    if (has_skip_len && merge_value > 0) {
        dst->type = STRIDE_EX_SKIP_LEN;
        dst->data.skip_len.length = (size_t)merge_value;
    } else if (merge_value > 0) {
        dst->type = STRIDE_EX_JUMP_FWD;
        dst->data.jump_fwd.offset = (size_t)merge_value;
    } else {
        dst->type = STRIDE_EX_JUMP_BACK;
        dst->data.jump_back.offset = (size_t)(-merge_value);
    }
    return 1;
}

/* ==================== 编译 ==================== */

int stride_extractor_compile(const stride_op_t *ops, size_t op_count,
                             stride_extractor_op_t **out_extractors,
                             size_t *out_count, size_t *out_param_count) {
    if (!ops || !out_extractors || !out_count || !out_param_count) {
        return -1;
    }

    *out_extractors = NULL;
    *out_count = 0;
    *out_param_count = 0;
    size_t capacity = STRIDE_EXTRACTOR_INITIAL_CAPACITY;

    *out_extractors = calloc(capacity, sizeof(stride_extractor_op_t));
    if (!*out_extractors) {
        return -1;
    }

    /* 第一阶段：基础转换，暂存到临时数组 */
    stride_extractor_op_t *temp_ops =
        calloc(op_count * 2, sizeof(stride_extractor_op_t));
    if (!temp_ops) {
        free(*out_extractors);
        *out_extractors = NULL;
        return -1;
    }
    size_t temp_count = 0;

    for (size_t i = 0; i < op_count; i++) {
        const stride_op_t *op = &ops[i];
        stride_extractor_op_t *ex_op = &temp_ops[temp_count++];
        memset(ex_op, 0, sizeof(stride_extractor_op_t));

        switch (op->type) {
            case STRIDE_OP_CAPTURE_LEN:
                ex_op->type = STRIDE_EX_CAPTURE_LEN;
                ex_op->data.capture_len.length = op->data.length;
                (*out_param_count)++;
                break;

            case STRIDE_OP_CAPTURE_CHR:
                ex_op->type = STRIDE_EX_CAPTURE_CHR;
                ex_op->data.capture_chr.ch = op->data.find.ch;
                (*out_param_count)++;
                break;

            case STRIDE_OP_CAPTURE_END:
                ex_op->type = STRIDE_EX_CAPTURE_END;
                (*out_param_count)++;
                break;

            case STRIDE_OP_MATCH:
                /* 匹配阶段已验证关键字，提取阶段只需跳过其长度 */
                ex_op->type = STRIDE_EX_SKIP_LEN;
                ex_op->data.skip_len.length = op->data.match.len;
                break;

            case STRIDE_OP_JUMP_ABS:
                ex_op->type = STRIDE_EX_JUMP_ABS;
                ex_op->data.jump_abs.pos = op->data.pos;
                break;

            case STRIDE_OP_JUMP_END:
                ex_op->type = STRIDE_EX_JUMP_END;
                ex_op->data.jump_end.is_end = op->data.jump_end.is_end;
                ex_op->data.jump_end.offset = op->data.jump_end.offset;
                break;

            case STRIDE_OP_JUMP_FWD:
                ex_op->type = STRIDE_EX_JUMP_FWD;
                ex_op->data.jump_fwd.offset = op->data.offset;
                break;

            case STRIDE_OP_JUMP_BACK:
                ex_op->type = STRIDE_EX_JUMP_BACK;
                ex_op->data.jump_back.offset = op->data.offset;
                break;

            case STRIDE_OP_FIND_FWD:
                ex_op->type = STRIDE_EX_FIND_FWD;
                ex_op->data.find_fwd.ch = op->data.find.ch;
                break;

            case STRIDE_OP_FIND_REV:
                ex_op->type = STRIDE_EX_FIND_REV;
                ex_op->data.find_rev.ch = op->data.find.ch;
                break;

            default:
                temp_count--; /* 跳过无效操作 */
                break;
        }
    }

    /* 第二阶段：常量移动合并 */
    int merge_start = -1; /* 合并段起始索引，-1 表示无合并中 */
    int merge_value = 0;  /* 合并净位移（正向为正，负向为负）*/
    int has_skip_len = 0; /* 合并段是否包含 SKIP_LEN */

    for (size_t i = 0; i < temp_count; i++) {
        stride_extractor_op_t *op = &temp_ops[i];

        if (extractor_is_const_move(op->type)) {
            int op_value = 0;
            switch (op->type) {
                case STRIDE_EX_SKIP_LEN:
                    op_value = (int)op->data.skip_len.length;
                    has_skip_len = 1;
                    break;
                case STRIDE_EX_JUMP_FWD:
                    op_value = (int)op->data.jump_fwd.offset;
                    break;
                case STRIDE_EX_JUMP_BACK:
                    op_value = -(int)op->data.jump_back.offset;
                    break;
                default:
                    break;
            }

            if (merge_start < 0) {
                merge_start = (int)i;
                merge_value = op_value;
                has_skip_len = (op->type == STRIDE_EX_SKIP_LEN);
            } else {
                merge_value += op_value;
            }
            continue;
        }

        /* 非合并操作：先冲刷未完成的合并段，再输出自身 */
        if (merge_start >= 0) {
            if (*out_count >= capacity) {
                extractor_array_grow(out_extractors, &capacity);
            }
            *out_count += extractor_emit_merge(&(*out_extractors)[*out_count],
                                               merge_value, has_skip_len);
            merge_start = -1;
            has_skip_len = 0;
        }

        if (*out_count >= capacity) {
            extractor_array_grow(out_extractors, &capacity);
        }
        memcpy(&(*out_extractors)[*out_count], op,
               sizeof(stride_extractor_op_t));
        (*out_count)++;
    }

    /* 冲刷最后一个合并段 */
    if (merge_start >= 0) {
        if (*out_count >= capacity) {
            extractor_array_grow(out_extractors, &capacity);
        }
        *out_count += extractor_emit_merge(&(*out_extractors)[*out_count],
                                           merge_value, has_skip_len);
    }

    free(temp_ops);
    return 0;
}

/* ==================== 单段提取器生命周期 ==================== */

stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops,
                                            size_t op_count) {
    if (!ops || op_count == 0) {
        return NULL;
    }

    stride_extractor_t *ext = calloc(1, sizeof(stride_extractor_t));
    if (!ext) {
        return NULL;
    }

    ext->ops = calloc(op_count, sizeof(stride_extractor_op_t));
    if (!ext->ops) {
        free(ext);
        return NULL;
    }

    ext->op_count = op_count;
    memcpy(ext->ops, ops, op_count * sizeof(stride_extractor_op_t));

    ext->param_count = 0;
    for (size_t i = 0; i < op_count; i++) {
        if (extractor_op_produces_param(ops[i].type)) {
            ext->param_count++;
        }
    }

    return ext;
}

void stride_extractor_destroy(stride_extractor_t *extractor) {
    if (!extractor) {
        return;
    }
    free(extractor->ops);
    free(extractor);
}

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
                size_t end_pos =
                    found ? (size_t)(found - segment) : segment_len;
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
                size_t new_pos = op->data.jump_abs.pos;
                if (new_pos > segment_len) {
                    return -1;
                }
                cursor = new_pos;
                break;
            }

            case STRIDE_EX_JUMP_END: {
                if (op->data.jump_end.is_end) {
                    int offset = op->data.jump_end.offset;
                    if (offset == 0) {
                        cursor = segment_len;
                    } else {
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
                if (cursor + op->data.jump_fwd.offset > segment_len) {
                    return -1;
                }
                cursor += op->data.jump_fwd.offset;
                break;
            }

            case STRIDE_EX_JUMP_BACK: {
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
                /* 从游标前一个字符向段首查找；游标在段首时从段尾开始 */
                char target = op->data.find_rev.ch;
                if (segment_len == 0) {
                    return -1;
                }
                size_t k = (cursor == 0) ? segment_len - 1 : cursor - 1;
                for (;;) {
                    if (segment[k] == target) {
                        cursor = k;
                        break;
                    }
                    if (k == 0) {
                        return -1;
                    }
                    k--;
                }
                break;
            }
        }
    }

    *param_count = out_idx;
    return 0;
}

/* ==================== 多段提取器 ==================== */

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
