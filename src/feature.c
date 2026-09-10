#include "stride/feature.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_FEATURE_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 特征序列 - 实现
 *
 * 1. 编译：操作符序列 → 特征序列（IDLE / HOLD 两状态状态机）
 * 2. 匹配：用特征序列匹配一个段
 * ============================================================ */

/* ==================== 编译实现 ==================== */

static void feature_array_grow(stride_feature_t **features, size_t *capacity) {
    size_t new_cap = *capacity * 2;
    stride_feature_t *new_arr =
        realloc(*features, new_cap * sizeof(stride_feature_t));
    if (new_arr) {
        *features = new_arr;
        *capacity = new_cap;
    }
}

void stride_feature_free(stride_feature_t *features, size_t count) {
    if (!features) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (features[i].keyword) {
            free((char *)features[i].keyword);
        }
    }
    free(features);
}

/**
 * 操作分类（用于状态机）
 */
typedef enum {
    OP_CLASS_CONST_POS,      /* 正向常量移动 */
    OP_CLASS_CONST_NEG,      /* 负向常量移动 */
    OP_CLASS_CONST_ABS_HEAD, /* 绝对常量移动（基于 HEAD）*/
    OP_CLASS_CONST_ABS_END,  /* 绝对常量移动（基于 END）*/
    OP_CLASS_DYNAMIC_FWD,    /* 动态正向查找 */
    OP_CLASS_DYNAMIC_REV,    /* 动态反向查找 */
    OP_CLASS_KEYWORD         /* 关键字匹配 */
} op_class_t;

/**
 * 持有元组（状态机内部使用）
 * value 为有符号累计值：纯偏移按方向加减，END 基准为负累计。
 */
typedef struct {
    int is_dynamic;   /* 是否是动态操作 */
    int is_end_based; /* 是否基于 END */
    int is_head_abs;  /* 是否是绝对位置（基于 HEAD）*/
    int value;        /* 偏移量或字符 ASCII */
    int is_reverse;   /* 是否是反向查找 */
} hold_tuple_t;

/**
 * 获取操作符的分类和基础元组值
 */
static int get_op_class(const stride_op_t *op, op_class_t *out_class,
                        hold_tuple_t *out_tuple) {
    memset(out_tuple, 0, sizeof(hold_tuple_t));

    switch (op->type) {
        case STRIDE_OP_CAPTURE_LEN:
            /* ${n} - 定长捕获：正向常量移动 */
            *out_class = OP_CLASS_CONST_POS;
            out_tuple->value = (int)op->data.length;
            return 0;

        case STRIDE_OP_CAPTURE_CHR:
            /* ${'c'} - 捕获到字符：动态正向查找 */
            *out_class = OP_CLASS_DYNAMIC_FWD;
            out_tuple->is_dynamic = 1;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case STRIDE_OP_CAPTURE_END:
            /* ${} - 捕获到结尾：绝对位置基于 END（纯 END，累计 0）*/
            *out_class = OP_CLASS_CONST_ABS_END;
            out_tuple->is_end_based = 1;
            out_tuple->value = 0;
            return 0;

        case STRIDE_OP_JUMP_FWD:
            /* $[>n] - 向结尾移动：正向常量移动 */
            *out_class = OP_CLASS_CONST_POS;
            out_tuple->value = (int)op->data.offset;
            return 0;

        case STRIDE_OP_JUMP_BACK:
            /* $[<n] - 向开头移动：负向常量移动 */
            *out_class = OP_CLASS_CONST_NEG;
            out_tuple->value = -(int)op->data.offset;
            return 0;

        case STRIDE_OP_JUMP_ABS:
            /* $[n] - 绝对跳转：绝对位置基于 HEAD */
            *out_class = OP_CLASS_CONST_ABS_HEAD;
            out_tuple->is_head_abs = 1;
            out_tuple->value = (int)op->data.pos;
            return 0;

        case STRIDE_OP_JUMP_END:
            /* $[END] / $[END-n] - END 跳转 */
            *out_class = OP_CLASS_CONST_ABS_END;
            out_tuple->is_end_based = 1;
            out_tuple->value = -op->data.jump_end.offset;
            return 0;

        case STRIDE_OP_FIND_FWD:
            /* $[>'c'] - 向结尾查找：动态正向查找 */
            *out_class = OP_CLASS_DYNAMIC_FWD;
            out_tuple->is_dynamic = 1;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case STRIDE_OP_FIND_REV:
            /* $[<'c'] - 向开头查找：动态反向查找 */
            *out_class = OP_CLASS_DYNAMIC_REV;
            out_tuple->is_dynamic = 1;
            out_tuple->is_reverse = 1;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case STRIDE_OP_MATCH:
            /* $'text' - 关键字匹配 */
            *out_class = OP_CLASS_KEYWORD;
            return 0;

        default:
            return -1;
    }
}

/**
 * 尝试将新的常量事件合并进持有元组
 * @return 0 成功，-1 不可相加
 *
 * 常量相加规则：
 * - 动态操作不能与任何操作相加
 * - END 基准只能与负数或 CONST_ABS_END 相加
 * - HEAD 基准可以与 CONST_POS / CONST_NEG / CONST_ABS_HEAD 相加
 * - 不同基准（HEAD 与 END）不能混用
 */
static int try_add_constants(hold_tuple_t *hold, op_class_t new_class,
                             int new_value) {
    if (hold->is_dynamic) {
        return -1;
    }

    if (hold->is_end_based) {
        /* END 基准：只能与负数或 END 相加 */
        if (new_class == OP_CLASS_CONST_POS) {
            return -1; /* END 不能加正数 */
        } else if (new_class == OP_CLASS_CONST_NEG ||
                   new_class == OP_CLASS_CONST_ABS_END) {
            hold->value += new_value; /* new_value 为负 */
            return 0;
        }
        return -1;
    }

    /* HEAD 基准或纯偏移 */
    if (new_class == OP_CLASS_CONST_POS || new_class == OP_CLASS_CONST_NEG) {
        hold->value += new_value;
        return 0;
    } else if (new_class == OP_CLASS_CONST_ABS_HEAD) {
        hold->value = new_value; /* 绝对位置覆盖累计偏移 */
        return 0;
    }
    return -1;
}

/**
 * 输出持有元组到特征数组
 * value 统一输出为非负幅度，方向由 type 表达。
 */
static void output_hold_tuple(stride_feature_t **features, size_t *capacity,
                              size_t *count, hold_tuple_t *hold,
                              const char *keyword, size_t keyword_len) {
    if (*count >= *capacity) {
        feature_array_grow(features, capacity);
    }

    stride_feature_t *ft = &(*features)[*count];
    memset(ft, 0, sizeof(stride_feature_t));

    if (hold->is_dynamic) {
        ft->type = hold->is_reverse ? STRIDE_FT_DYNAMIC_FIND_REV
                                    : STRIDE_FT_DYNAMIC_FIND_FWD;
        ft->value = hold->value;
    } else if (hold->is_head_abs) {
        ft->type = STRIDE_FT_CONST_ABS_HEAD;
        ft->value = hold->value;
    } else if (hold->is_end_based) {
        ft->type = STRIDE_FT_CONST_ABS_END;
        ft->value = -hold->value; /* 累计值 ≤ 0 → 非负的 END 偏移量 */
    } else if (hold->value >= 0) {
        ft->type = STRIDE_FT_CONST_REL_FWD;
        ft->value = hold->value;
    } else {
        ft->type = STRIDE_FT_CONST_REL_BACK;
        ft->value = -hold->value;
    }

    if (keyword) {
        ft->keyword_len = keyword_len;
        ft->keyword = malloc(keyword_len + 1);
        if (ft->keyword) {
            memcpy((char *)ft->keyword, keyword, keyword_len);
            ((char *)ft->keyword)[keyword_len] = '\0';
        }
    }

    (*count)++;
}

int stride_feature_compile(const stride_op_t *ops, size_t op_count,
                           stride_feature_t **out_features,
                           size_t *out_count, size_t *out_capacity) {
    if (!ops || !out_features || !out_count || !out_capacity) {
        return -1;
    }

    *out_features = NULL;
    *out_count = 0;
    *out_capacity = STRIDE_FEATURE_INITIAL_CAPACITY;

    *out_features = calloc(*out_capacity, sizeof(stride_feature_t));
    if (!*out_features) {
        return -1;
    }

    hold_tuple_t hold;
    memset(&hold, 0, sizeof(hold));
    int state = 0; /* 0 = IDLE, 1 = HOLD */
    size_t count = 0;

    for (size_t i = 0; i < op_count; i++) {
        const stride_op_t *op = &ops[i];
        op_class_t op_class;
        hold_tuple_t tuple;

        if (get_op_class(op, &op_class, &tuple) != 0) {
            continue; /* 跳过无效操作 */
        }

        if (state == 0) { /* IDLE */
            if (op_class == OP_CLASS_KEYWORD) {
                /* IDLE + 关键字：直接输出 (0, kw)，保持 IDLE */
                output_hold_tuple(out_features, out_capacity, &count, &hold,
                                  op->data.match.text, op->data.match.len);
            } else {
                hold = tuple;
                state = 1; /* HOLD */
            }
        } else { /* HOLD */
            if (op_class == OP_CLASS_KEYWORD) {
                /* HOLD + 关键字：合并输出 (持有值, kw) */
                output_hold_tuple(out_features, out_capacity, &count, &hold,
                                  op->data.match.text, op->data.match.len);
                memset(&hold, 0, sizeof(hold));
                state = 0; /* IDLE */
            } else if (try_add_constants(&hold, op_class, tuple.value) == 0) {
                /* 常量可相加，保持 HOLD */
            } else {
                /* 不可相加（动态操作或基准不兼容）：先输出持有，再持有新元组 */
                output_hold_tuple(out_features, out_capacity, &count, &hold,
                                  NULL, 0);
                hold = tuple;
                state = 1; /* HOLD */
            }
        }
    }

    /* 扫描结束：若在 HOLD，输出持有 */
    if (state == 1) {
        output_hold_tuple(out_features, out_capacity, &count, &hold, NULL, 0);
    }

    *out_count = count;
    return 0;
}

/* ==================== 匹配实现 ==================== */

int stride_feature_match_ex(const stride_feature_t *features, size_t count,
                            const char *segment, size_t segment_len,
                            stride_match_detail_t *out) {
    if (out) {
        out->matched = 0;
        out->fail_index = 0;
        out->cursor = 0;
    }
    if (!segment || (count > 0 && !features)) {
        return -1;
    }

    size_t cursor = 0; /* 始终满足 cursor <= segment_len */

    for (size_t i = 0; i < count; i++) {
        const stride_feature_t *f = &features[i];
        int value = f->value;

        switch (f->type) {
            case STRIDE_FT_CONST_REL_FWD:
                if (value < 0 || (size_t)value > segment_len - cursor) {
                    goto fail;
                }
                cursor += (size_t)value;
                break;

            case STRIDE_FT_CONST_REL_BACK:
                if (value < 0 || (size_t)value > cursor) {
                    goto fail;
                }
                cursor -= (size_t)value;
                break;

            case STRIDE_FT_CONST_ABS_HEAD:
                if (value < 0 || (size_t)value > segment_len) {
                    goto fail;
                }
                cursor = (size_t)value;
                break;

            case STRIDE_FT_CONST_ABS_END:
                if (value < 0 || (size_t)value > segment_len) {
                    goto fail;
                }
                cursor = segment_len - (size_t)value;
                break;

            case STRIDE_FT_DYNAMIC_FIND_FWD: {
                const char *found = memchr(segment + cursor, (char)value,
                                           segment_len - cursor);
                if (!found) {
                    goto fail;
                }
                cursor = (size_t)(found - segment);
                break;
            }

            case STRIDE_FT_DYNAMIC_FIND_REV: {
                /* 从游标前一个字符向段首查找；游标在段首时从段尾开始 */
                if (segment_len == 0) {
                    goto fail;
                }
                size_t k = (cursor == 0) ? segment_len - 1 : cursor - 1;
                for (;;) {
                    if (segment[k] == (char)value) {
                        cursor = k;
                        break;
                    }
                    if (k == 0) {
                        goto fail;
                    }
                    k--;
                }
                break;
            }

            default:
                goto fail;
        }

        /* 关键字在移动后的游标处验证，并消耗其长度 */
        if (f->keyword) {
            if (f->keyword_len > segment_len - cursor ||
                memcmp(segment + cursor, f->keyword, f->keyword_len) != 0) {
                goto fail;
            }
            cursor += f->keyword_len;
        }
        continue;

    fail:
        if (out) {
            out->matched = 0;
            out->fail_index = i;
            out->cursor = cursor;
        }
        return -1;
    }

    /* 段尾对齐 */
    if (cursor != segment_len) {
        if (out) {
            out->matched = 0;
            out->fail_index = count;
            out->cursor = cursor;
        }
        return -1;
    }

    if (out) {
        out->matched = 1;
        out->fail_index = count;
        out->cursor = cursor;
    }
    return 0;
}

int stride_feature_match(const stride_feature_t *features, size_t count,
                         const char *segment, size_t segment_len) {
    return stride_feature_match_ex(features, count, segment, segment_len, NULL);
}
