#include "stride/extractor.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_EXTRACTOR_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 提取序列 - 实现
 *
 * 1. 编译：操作符序列 → 提取序列（基础转换 + 常量合并）
 * 2. 执行：在段上运行提取序列，产出零拷贝参数
 *
 * 单位：步长以比特计；steps 为步数；bit_len 为比特长度。
 * ============================================================ */

/* ==================== 通用辅助 ==================== */

/* 取第 bit 个比特（MSB 优先） */
static int bit_at(const unsigned char *base, size_t bit) {
    return (base[bit >> 3] >> (7u - (bit & 7u))) & 1;
}

/* 比较 base 从 bit_off 起、长度为 bit_len 的比特串与 data */
static int bits_eq(const unsigned char *base, size_t bit_off, const void *data,
                   size_t bit_len) {
    if (bit_len == 0) {
        return 1;
    }
    if ((bit_off & 7u) == 0 && (bit_len & 7u) == 0) {
        return memcmp(base + (bit_off >> 3), data, bit_len >> 3) == 0;
    }
    const unsigned char *d = (const unsigned char *)data;
    for (size_t i = 0; i < bit_len; i++) {
        if (bit_at(base, bit_off + i) != bit_at(d, i)) {
            return 0;
        }
    }
    return 1;
}

static int blob_copy(stride_blob_t *dst, const stride_blob_t *src) {
    dst->data = NULL;
    dst->bit_len = src->bit_len;
    if (!src->data || src->bit_len == 0) {
        return 0;
    }
    size_t nbytes = (src->bit_len + 7u) / 8u;
    void *buf = malloc(nbytes);
    if (!buf) {
        return -1;
    }
    memcpy(buf, src->data, nbytes);
    dst->data = buf;
    return 0;
}

static int extractor_op_produces_param(stride_extractor_op_type_t type) {
    return (type == STRIDE_EX_CAPTURE_STEPS ||
            type == STRIDE_EX_CAPTURE_UNTIL ||
            type == STRIDE_EX_CAPTURE_END);
}

/* 追加一个操作，返回其指针（数组不足时扩容） */
static stride_extractor_op_t *ex_push(stride_extractor_op_t **arr,
                                      size_t *capacity, size_t *count) {
    if (*count >= *capacity) {
        size_t new_cap = *capacity * 2;
        stride_extractor_op_t *na = (stride_extractor_op_t *)realloc(
            *arr, new_cap * sizeof(stride_extractor_op_t));
        if (!na) {
            return NULL;
        }
        memset(na + *capacity, 0,
               (new_cap - *capacity) * sizeof(stride_extractor_op_t));
        *arr = na;
        *capacity = new_cap;
    }
    stride_extractor_op_t *e = &(*arr)[*count];
    memset(e, 0, sizeof(*e));
    (*count)++;
    return e;
}

/* ==================== 常量合并状态机 ==================== */

/**
 * 合并累计量（产物与步长无关）
 *
 * 只允许**同单位**累计：
 *   - 字面量跳过以比特累计（MERGE_BITS）
 *   - 常量移动以步累计（MERGE_STEPS，有符号）
 * 单位切换时先冲刷。因此提取序列中不会保存任何由步长换算出来的值，
 * 同一份产物可在不同步长下执行。
 */
typedef enum { MERGE_NONE = 0, MERGE_BITS, MERGE_STEPS } merge_kind_t;

typedef struct {
    merge_kind_t kind;
    size_t       bits;  /* kind == MERGE_BITS */
    long         steps; /* kind == MERGE_STEPS，可为负 */
} merge_t;

static void merge_add_bits(merge_t *m, size_t bit_len) {
    m->kind = MERGE_BITS;
    m->bits += bit_len;
}

static void merge_add_steps(merge_t *m, long delta) {
    m->kind = MERGE_STEPS;
    m->steps += delta;
}

static int merge_flush(merge_t *m, stride_extractor_op_t **arr,
                       size_t *capacity, size_t *count) {
    if (m->kind == MERGE_NONE) {
        return 0;
    }
    merge_kind_t kind = m->kind;
    size_t bits = m->bits;
    long steps = m->steps;

    m->kind = MERGE_NONE;
    m->bits = 0;
    m->steps = 0;

    if (kind == MERGE_BITS) {
        if (bits == 0) {
            return 0;
        }
        stride_extractor_op_t *e = ex_push(arr, capacity, count);
        if (!e) {
            return -1;
        }
        e->type = STRIDE_EX_SKIP_BITS;
        e->data.skip_bits.bit_len = bits;
        return 0;
    }

    if (steps == 0) {
        return 0;
    }
    stride_extractor_op_t *e = ex_push(arr, capacity, count);
    if (!e) {
        return -1;
    }
    if (steps > 0) {
        e->type = STRIDE_EX_JUMP_FWD;
        e->data.jump_fwd.steps = (size_t)steps;
    } else {
        e->type = STRIDE_EX_JUMP_BACK;
        e->data.jump_back.steps = (size_t)(-steps);
    }
    return 0;
}

/* ==================== 编译 ==================== */

int stride_extractor_compile(const stride_op_t *ops, size_t op_count,
                             size_t stride, stride_extractor_op_t **out_ops,
                             size_t *out_count, size_t *out_param_count) {
    if (!ops || !out_ops || !out_count || !out_param_count) {
        return -1;
    }

    *out_ops = NULL;
    *out_count = 0;
    *out_param_count = 0;
    size_t capacity = STRIDE_EXTRACTOR_INITIAL_CAPACITY;

    stride_extractor_op_t *arr = (stride_extractor_op_t *)calloc(
        capacity, sizeof(stride_extractor_op_t));
    if (!arr) {
        return -1;
    }
    size_t count = 0;
    size_t params = 0;

    merge_t merge;
    memset(&merge, 0, sizeof(merge));

    for (size_t i = 0; i < op_count; i++) {
        const stride_op_t *op = &ops[i];

        /* 字面量与常量移动进入合并段（只在同单位内合并） */
        if (op->type == STRIDE_OP_MATCH) {
            if (stride != 0 && op->data.literal.bit_len % stride != 0) {
                goto fail;
            }
            if (merge.kind == MERGE_STEPS &&
                merge_flush(&merge, &arr, &capacity, &count) != 0) {
                goto fail;
            }
            merge_add_bits(&merge, op->data.literal.bit_len);
            continue;
        }
        if (op->type == STRIDE_OP_JUMP_FWD) {
            if (merge.kind == MERGE_BITS &&
                merge_flush(&merge, &arr, &capacity, &count) != 0) {
                goto fail;
            }
            merge_add_steps(&merge, (long)op->data.steps);
            continue;
        }
        if (op->type == STRIDE_OP_JUMP_BACK) {
            if (merge.kind == MERGE_BITS &&
                merge_flush(&merge, &arr, &capacity, &count) != 0) {
                goto fail;
            }
            merge_add_steps(&merge, -(long)op->data.steps);
            continue;
        }

        /* 非常量操作：先冲刷合并段，再输出自身 */
        if (merge_flush(&merge, &arr, &capacity, &count) != 0) {
            goto fail;
        }

        stride_extractor_op_t *e = ex_push(&arr, &capacity, &count);
        if (!e) {
            goto fail;
        }

        switch (op->type) {
            case STRIDE_OP_CAPTURE_STEPS:
                e->type = STRIDE_EX_CAPTURE_STEPS;
                e->data.capture_steps.steps = op->data.steps;
                params++;
                break;

            case STRIDE_OP_CAPTURE_UNTIL:
                if (stride != 0 && op->data.literal.bit_len % stride != 0) {
                    goto fail;
                }
                e->type = STRIDE_EX_CAPTURE_UNTIL;
                if (blob_copy(&e->data.capture_until, &op->data.literal) != 0) {
                    goto fail;
                }
                params++;
                break;

            case STRIDE_OP_CAPTURE_END:
                e->type = STRIDE_EX_CAPTURE_END;
                params++;
                break;

            case STRIDE_OP_JUMP_ABS:
                e->type = STRIDE_EX_JUMP_ABS;
                e->data.jump_abs.steps = op->data.steps;
                break;

            case STRIDE_OP_JUMP_END:
                e->type = STRIDE_EX_JUMP_END;
                e->data.jump_end.is_end = op->data.jump_end.is_end;
                e->data.jump_end.back_steps = op->data.jump_end.back_steps;
                break;

            case STRIDE_OP_FIND_FWD:
                if (stride != 0 && op->data.literal.bit_len % stride != 0) {
                    goto fail;
                }
                e->type = STRIDE_EX_FIND_FWD;
                if (blob_copy(&e->data.find_fwd, &op->data.literal) != 0) {
                    goto fail;
                }
                break;

            case STRIDE_OP_FIND_REV:
                if (stride != 0 && op->data.literal.bit_len % stride != 0) {
                    goto fail;
                }
                e->type = STRIDE_EX_FIND_REV;
                if (blob_copy(&e->data.find_rev, &op->data.literal) != 0) {
                    goto fail;
                }
                break;

            default:
                goto fail;
        }
    }

    if (merge_flush(&merge, &arr, &capacity, &count) != 0) {
        goto fail;
    }

    *out_ops = arr;
    *out_count = count;
    *out_param_count = params;
    return 0;

fail:
    stride_extractor_free(arr, count + 1);
    *out_ops = NULL;
    *out_count = 0;
    *out_param_count = 0;
    return -1;
}

void stride_extractor_free(stride_extractor_op_t *ops, size_t count) {
    if (!ops) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        switch (ops[i].type) {
            case STRIDE_EX_CAPTURE_UNTIL:
                free((void *)ops[i].data.capture_until.data);
                break;
            case STRIDE_EX_FIND_FWD:
                free((void *)ops[i].data.find_fwd.data);
                break;
            case STRIDE_EX_FIND_REV:
                free((void *)ops[i].data.find_rev.data);
                break;
            default:
                break;
        }
    }
    free(ops);
}

/* ==================== 单段提取器生命周期 ==================== */

stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops,
                                            size_t op_count) {
    stride_extractor_t *ex =
        (stride_extractor_t *)calloc(1, sizeof(stride_extractor_t));
    if (!ex) {
        return NULL;
    }
    if (op_count == 0) {
        ex->ops = NULL;
        ex->op_count = 0;
        ex->param_count = 0;
        return ex;
    }
    if (!ops) {
        free(ex);
        return NULL;
    }

    ex->ops = (stride_extractor_op_t *)calloc(
        op_count, sizeof(stride_extractor_op_t));
    if (!ex->ops) {
        free(ex);
        return NULL;
    }
    ex->op_count = op_count;
    ex->param_count = 0;

    for (size_t i = 0; i < op_count; i++) {
        ex->ops[i] = ops[i]; /* 浅拷贝，随后重建拥有的比特串 */

        const stride_blob_t *src = NULL;
        stride_blob_t *dst = NULL;
        switch (ops[i].type) {
            case STRIDE_EX_CAPTURE_UNTIL:
                src = &ops[i].data.capture_until;
                dst = &ex->ops[i].data.capture_until;
                break;
            case STRIDE_EX_FIND_FWD:
                src = &ops[i].data.find_fwd;
                dst = &ex->ops[i].data.find_fwd;
                break;
            case STRIDE_EX_FIND_REV:
                src = &ops[i].data.find_rev;
                dst = &ex->ops[i].data.find_rev;
                break;
            default:
                break;
        }
        if (src && dst) {
            if (blob_copy(dst, src) != 0) {
                stride_extractor_destroy(ex);
                return NULL;
            }
        }
        if (extractor_op_produces_param(ops[i].type)) {
            ex->param_count++;
        }
    }

    return ex;
}

void stride_extractor_destroy(stride_extractor_t *ex) {
    if (!ex) {
        return;
    }
    stride_extractor_free(ex->ops, ex->op_count);
    free(ex);
}

/* ==================== 查找辅助 ==================== */

static int seg_find_fwd(const unsigned char *seg, size_t stride, size_t total,
                        size_t from, const stride_blob_t *d, size_t *out_pos) {
    if (d->bit_len == 0 || d->bit_len % stride != 0) {
        return -1;
    }
    size_t ds = d->bit_len / stride;
    for (size_t k = from; k + ds <= total; k++) {
        if (bits_eq(seg, k * stride, d->data, d->bit_len)) {
            *out_pos = k;
            return 0;
        }
    }
    return -1;
}

static int seg_find_rev(const unsigned char *seg, size_t stride, size_t total,
                        size_t from, const stride_blob_t *d, size_t *out_pos) {
    if (total == 0 || d->bit_len == 0 || d->bit_len % stride != 0) {
        return -1;
    }
    size_t ds = d->bit_len / stride;
    size_t k = (from == 0) ? total - 1 : from - 1;
    for (;;) {
        if (k + ds <= total && bits_eq(seg, k * stride, d->data, d->bit_len)) {
            *out_pos = k;
            return 0;
        }
        if (k == 0) {
            return -1;
        }
        k--;
    }
}

/* ==================== 单段执行 ==================== */

int stride_extractor_run(const stride_extractor_t *ex, size_t stride,
                         const void *segment, size_t segment_bit_len,
                         stride_param_t *params, size_t param_capacity,
                         size_t *param_count) {
    if (!ex || !segment || !params || !param_count) {
        return -1;
    }
    if (stride == 0) {
        stride = 1;
    }
    if (segment_bit_len % stride != 0) {
        return -1;
    }

    const unsigned char *seg = (const unsigned char *)segment;
    size_t total = segment_bit_len / stride;
    size_t pos = 0;
    size_t out_idx = *param_count;

    for (size_t i = 0; i < ex->op_count; i++) {
        const stride_extractor_op_t *op = &ex->ops[i];

        switch (op->type) {
            case STRIDE_EX_CAPTURE_STEPS: {
                size_t n = op->data.capture_steps.steps;
                if (n > total - pos) {
                    return -1;
                }
                size_t start_bit = pos * stride;
                if (start_bit % 8 != 0) {
                    return -1; /* 起始未字节对齐，无法用 (ptr, bit_len) 表达 */
                }
                if (out_idx >= param_capacity) {
                    return -1;
                }
                params[out_idx].ptr = seg + start_bit / 8;
                params[out_idx].bit_len = n * stride;
                out_idx++;
                pos += n;
                break;
            }

            case STRIDE_EX_CAPTURE_UNTIL: {
                const stride_blob_t *d = &op->data.capture_until;
                size_t start = pos;
                size_t start_bit = start * stride;
                if (start_bit % 8 != 0) {
                    return -1;
                }
                size_t end = total;
                size_t hit;
                if (seg_find_fwd(seg, stride, total, start, d, &hit) == 0) {
                    end = hit;
                }
                if (out_idx >= param_capacity) {
                    return -1;
                }
                params[out_idx].ptr = seg + start_bit / 8;
                params[out_idx].bit_len = (end - start) * stride;
                out_idx++;
                pos = end;
                break;
            }

            case STRIDE_EX_CAPTURE_END: {
                size_t start_bit = pos * stride;
                if (start_bit % 8 != 0) {
                    return -1;
                }
                if (out_idx >= param_capacity) {
                    return -1;
                }
                params[out_idx].ptr = seg + start_bit / 8;
                params[out_idx].bit_len = segment_bit_len - start_bit;
                out_idx++;
                pos = total;
                break;
            }

            case STRIDE_EX_SKIP_BITS: {
                if (op->data.skip_bits.bit_len % stride != 0) {
                    return -1; /* 字面量比特长度不是步长整数倍 */
                }
                size_t skip = op->data.skip_bits.bit_len / stride;
                if (skip > total - pos) {
                    return -1;
                }
                pos += skip;
                break;
            }

            case STRIDE_EX_JUMP_ABS:
                if (op->data.jump_abs.steps > total) {
                    return -1;
                }
                pos = op->data.jump_abs.steps;
                break;

            case STRIDE_EX_JUMP_END:
                if (op->data.jump_end.is_end) {
                    if (op->data.jump_end.back_steps > total) {
                        return -1;
                    }
                    pos = total - op->data.jump_end.back_steps;
                }
                break;

            case STRIDE_EX_JUMP_FWD:
                if (op->data.jump_fwd.steps > total - pos) {
                    return -1;
                }
                pos += op->data.jump_fwd.steps;
                break;

            case STRIDE_EX_JUMP_BACK:
                if (op->data.jump_back.steps > pos) {
                    return -1;
                }
                pos -= op->data.jump_back.steps;
                break;

            case STRIDE_EX_FIND_FWD: {
                size_t hit;
                if (seg_find_fwd(seg, stride, total, pos,
                                 &op->data.find_fwd, &hit) != 0) {
                    return -1;
                }
                pos = hit;
                break;
            }

            case STRIDE_EX_FIND_REV: {
                size_t hit;
                if (seg_find_rev(seg, stride, total, pos,
                                 &op->data.find_rev, &hit) != 0) {
                    return -1;
                }
                pos = hit;
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

    stride_full_extractor_t *full =
        (stride_full_extractor_t *)calloc(1, sizeof(stride_full_extractor_t));
    if (!full) {
        return NULL;
    }

    full->segments = (stride_extractor_t **)calloc(
        segment_count, sizeof(stride_extractor_t *));
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
        if (full->segments[i]) {
            if (stride_extractor_run(full->segments[i], stride, segments[i],
                                     seg_bit_lens[i], params, param_capacity,
                                     &param_idx) != 0) {
                return -1;
            }
        }
    }

    *out_count = param_idx;
    return 0;
}
