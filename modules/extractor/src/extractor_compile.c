#include "stride/extractor.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_EXTRACTOR_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 提取序列模块 - 编译实现
 *
 * 两项优化：
 *   1. 匹配操作转换为常量偏移跳过（OP_MATCH → EX_SKIP_LEN）
 *   2. 连续常量移动操作合并
 * 见《Stride 编译器设计文档》5.3 / 5.4 节
 * ============================================================ */

/* ==================== 工具函数 ==================== */

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

/* ==================== 辅助判断函数 ==================== */

/**
 * 判断操作是否产生参数
 */
static int extractor_op_produces_param(stride_extractor_op_type_t type) {
    return (type == STRIDE_EX_CAPTURE_LEN ||
            type == STRIDE_EX_CAPTURE_CHR ||
            type == STRIDE_EX_CAPTURE_END);
}

/**
 * 判断是否是可合并的常量移动操作
 *
 * 可合并的操作：EX_SKIP_LEN、EX_JUMP_FWD、EX_JUMP_BACK
 */
static int extractor_is_const_move(stride_extractor_op_type_t type) {
    return (type == STRIDE_EX_SKIP_LEN ||
            type == STRIDE_EX_JUMP_FWD ||
            type == STRIDE_EX_JUMP_BACK);
}

/* ==================== 提取序列编译主函数 ==================== */

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
                /* 优化：OP_MATCH → EX_SKIP_LEN（常量偏移跳过）*/
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
    int merge_value = 0;  /* 合并值（正向为正，负向为负）*/
    int has_skip_len = 0; /* 是否包含 EX_SKIP_LEN */

    for (size_t i = 0; i < temp_count; i++) {
        stride_extractor_op_t *op = &temp_ops[i];

        /* 确保有足够空间 */
        if (*out_count >= capacity) {
            extractor_array_grow(out_extractors, &capacity);
        }

        /* 检查是否可以合并 */
        if (extractor_is_const_move(op->type) &&
            !extractor_op_produces_param(op->type)) {
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
                    op_value = 0;
                    break;
            }

            if (merge_start < 0) {
                /* 开始新的合并段 */
                merge_start = (int)i;
                merge_value = op_value;
                has_skip_len = (op->type == STRIDE_EX_SKIP_LEN);
            } else {
                /* 继续合并（EX_SKIP_LEN 已在上面置位 has_skip_len）*/
                merge_value += op_value;
            }
        } else {
            /* 非合并操作或产生参数的操作：打断合并 */
            if (merge_start >= 0) {
                /* 输出之前的合并结果 */
                if (*out_count >= capacity) {
                    extractor_array_grow(out_extractors, &capacity);
                }
                stride_extractor_op_t *merge_op = &(*out_extractors)[*out_count];

                /* 含 EX_SKIP_LEN 且结果为正 → EX_SKIP_LEN；
                 * 否则按符号输出 EX_JUMP_FWD / EX_JUMP_BACK */
                if (has_skip_len && merge_value > 0) {
                    merge_op->type = STRIDE_EX_SKIP_LEN;
                    merge_op->data.skip_len.length = (size_t)merge_value;
                } else if (merge_value > 0) {
                    merge_op->type = STRIDE_EX_JUMP_FWD;
                    merge_op->data.jump_fwd.offset = (size_t)merge_value;
                } else if (merge_value < 0) {
                    merge_op->type = STRIDE_EX_JUMP_BACK;
                    merge_op->data.jump_back.offset = (size_t)(-merge_value);
                }
                /* 净位移为 0 的合并段是无操作，直接丢弃 */
                if (merge_value != 0) {
                    (*out_count)++;
                }
                merge_start = -1;
                has_skip_len = 0;
            }

            /* 输出当前操作（产生参数的操作或不可合并的操作）*/
            if (!extractor_is_const_move(op->type) ||
                extractor_op_produces_param(op->type)) {
                stride_extractor_op_t *dst_op = &(*out_extractors)[*out_count];
                memcpy(dst_op, op, sizeof(stride_extractor_op_t));
                (*out_count)++;
            }
        }
    }

    /* 处理剩余的合并段 */
    if (merge_start >= 0) {
        if (*out_count >= capacity) {
            extractor_array_grow(out_extractors, &capacity);
        }
        stride_extractor_op_t *merge_op = &(*out_extractors)[*out_count];

        if (has_skip_len && merge_value > 0) {
            merge_op->type = STRIDE_EX_SKIP_LEN;
            merge_op->data.skip_len.length = (size_t)merge_value;
        } else if (merge_value > 0) {
            merge_op->type = STRIDE_EX_JUMP_FWD;
            merge_op->data.jump_fwd.offset = (size_t)merge_value;
        } else if (merge_value < 0) {
            merge_op->type = STRIDE_EX_JUMP_BACK;
            merge_op->data.jump_back.offset = (size_t)(-merge_value);
        }

        if (merge_value != 0) {
            (*out_count)++;
        }
    }

    free(temp_ops);
    return 0;
}

/* ==================== 提取器对象生命周期 ==================== */

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

    /* 计算参数数量 */
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
