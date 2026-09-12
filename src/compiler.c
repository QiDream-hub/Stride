#include "stride/compiler.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_LEX_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 编译器 - 实现
 *
 * 词法分析（含 \\、\'、\xNN 转义）+ 序列编译编排
 * ============================================================ */

/* ==================== 词法辅助 ==================== */

static int is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static int hex_val(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_hex(unsigned char c) {
    return hex_val(c) >= 0;
}

/* 解析十进制数字，返回消费的字节数 */
static size_t parse_number(const unsigned char *p, const unsigned char *end,
                           size_t *out) {
    size_t result = 0;
    size_t i = 0;

    while (p + i < end && is_digit(p[i])) {
        result = result * 10 + (size_t)(p[i] - '0');
        i++;
    }

    *out = result;
    return i;
}

static int op_array_grow(stride_op_t **ops, size_t *capacity) {
    size_t new_cap = *capacity * 2;
    stride_op_t *arr = realloc(*ops, new_cap * sizeof(stride_op_t));
    if (!arr) {
        return -1;
    }
    memset(arr + *capacity, 0, (new_cap - *capacity) * sizeof(stride_op_t));
    *ops = arr;
    *capacity = new_cap;
    return 0;
}

/* 释放 [0, n) 范围内的操作符及其拥有的字面量 */
static void ops_free_range(stride_op_t *ops, size_t n) {
    if (!ops) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (stride_op_has_literal(ops[i].type)) {
            free((void *)ops[i].data.literal.data);
        }
    }
    free(ops);
}

void stride_ops_free(stride_op_t *ops, size_t count) {
    ops_free_range(ops, count);
}

/**
 * 解析引号内的比特串字面量。
 * @param pp   输入输出：进入时指向开引号后的第一个字节，成功时越过闭引号
 * @param end  模式末尾
 * @param out  输出比特串（data 为 malloc 的副本）
 * @return 0 成功，-1 失败
 *
 * 支持转义：\\ -> 0x5C，\' -> 0x27，\xNN -> 任意字节。
 * 空字面量视为非法。
 */
static int parse_literal(const unsigned char **pp, const unsigned char *end,
                         stride_blob_t *out) {
    const unsigned char *p = *pp;
    const unsigned char *q = p;
    size_t nbytes = 0;

    /* 第一遍：定位闭引号并统计解码后的字节数 */
    while (q < end && *q != '\'') {
        if (*q == '\\') {
            if ((size_t)(end - q) < 2) {
                return -1;
            }
            unsigned char e = q[1];
            if (e == '\\' || e == '\'') {
                nbytes += 1;
                q += 2;
            } else if (e == 'x') {
                if ((size_t)(end - q) < 4 || !is_hex(q[2]) || !is_hex(q[3])) {
                    return -1;
                }
                nbytes += 1;
                q += 4;
            } else {
                return -1; /* 未知转义 */
            }
        } else {
            nbytes += 1;
            q++;
        }
    }

    if (q >= end || *q != '\'') {
        return -1; /* 未闭合 */
    }
    if (nbytes == 0) {
        return -1; /* 空字面量非法 */
    }

    unsigned char *buf = (unsigned char *)malloc(nbytes);
    if (!buf) {
        return -1;
    }

    /* 第二遍：解码 */
    size_t i = 0;
    while (p < q) {
        if (*p == '\\') {
            unsigned char e = p[1];
            if (e == '\\') {
                buf[i++] = '\\';
                p += 2;
            } else if (e == '\'') {
                buf[i++] = '\'';
                p += 2;
            } else {
                buf[i++] = (unsigned char)(hex_val(p[2]) * 16 + hex_val(p[3]));
                p += 4;
            }
        } else {
            buf[i++] = *p++;
        }
    }

    out->data = buf;
    out->bit_len = STRIDE_BITS(nbytes);
    *pp = q + 1; /* 越过闭引号 */
    return 0;
}

/* ==================== 词法分析 ==================== */

int stride_lex(const void *pattern, size_t pattern_len, stride_op_t **out_ops,
               size_t *out_count, size_t *out_capacity) {
    if (!pattern || !out_ops || !out_count || !out_capacity) {
        return -1;
    }

    const unsigned char *base = (const unsigned char *)pattern;
    size_t len = pattern_len ? pattern_len
                             : strlen((const char *)pattern);
    const unsigned char *end = base + len;
    const unsigned char *p = base;

    size_t capacity = STRIDE_LEX_INITIAL_CAPACITY;
    stride_op_t *ops = (stride_op_t *)calloc(capacity, sizeof(stride_op_t));
    if (!ops) {
        return -1;
    }
    size_t count = 0;

    while (p < end) {
        if (count >= capacity) {
            if (op_array_grow(&ops, &capacity) != 0) {
                ops_free_range(ops, capacity);
                return -1;
            }
        }

        stride_op_t *op = &ops[count];
        memset(op, 0, sizeof(stride_op_t));

        if (*p != '$') {
            goto fail;
        }
        p++; /* 跳过 '$' */
        if (p >= end) {
            goto fail;
        }

        if (*p == '\'') {
            /* $'比特串' */
            p++;
            op->type = STRIDE_OP_MATCH;
            if (parse_literal(&p, end, &op->data.literal) != 0) {
                goto fail;
            }
            count++;

        } else if (*p == '{') {
            p++;
            if (p >= end) {
                goto fail;
            }

            if (*p == '}') {
                /* ${} */
                op->type = STRIDE_OP_CAPTURE_END;
                p++;
                count++;

            } else if (*p == '\'') {
                /* ${'比特串'} */
                p++;
                op->type = STRIDE_OP_CAPTURE_UNTIL;
                if (parse_literal(&p, end, &op->data.literal) != 0) {
                    goto fail;
                }
                if (p >= end || *p != '}') {
                    goto fail;
                }
                p++;
                count++;

            } else if (is_digit(*p)) {
                /* ${步数} */
                size_t steps;
                size_t consumed = parse_number(p, end, &steps);
                p += consumed;
                if (p >= end || *p != '}') {
                    goto fail;
                }
                p++;
                if (steps == 0) {
                    goto fail; /* 步数必须为正 */
                }
                op->type = STRIDE_OP_CAPTURE_STEPS;
                op->data.steps = steps;
                count++;

            } else {
                goto fail;
            }

        } else if (*p == '[') {
            p++;
            if (p >= end) {
                goto fail;
            }

            if (*p == '>') {
                p++;
                if (p >= end) {
                    goto fail;
                }

                if (*p == '\'') {
                    /* $[>'比特串'] */
                    p++;
                    op->type = STRIDE_OP_FIND_FWD;
                    if (parse_literal(&p, end, &op->data.literal) != 0) {
                        goto fail;
                    }
                    if (p >= end || *p != ']') {
                        goto fail;
                    }
                    p++;
                    count++;

                } else if (is_digit(*p)) {
                    /* $[>步数] */
                    size_t steps;
                    size_t consumed = parse_number(p, end, &steps);
                    p += consumed;
                    if (p >= end || *p != ']') {
                        goto fail;
                    }
                    p++;
                    op->type = STRIDE_OP_JUMP_FWD;
                    op->data.steps = steps;
                    count++;

                } else {
                    goto fail;
                }

            } else if (*p == '<') {
                p++;
                if (p >= end) {
                    goto fail;
                }

                if (*p == '\'') {
                    /* $[<'比特串'] */
                    p++;
                    op->type = STRIDE_OP_FIND_REV;
                    if (parse_literal(&p, end, &op->data.literal) != 0) {
                        goto fail;
                    }
                    if (p >= end || *p != ']') {
                        goto fail;
                    }
                    p++;
                    count++;

                } else if (is_digit(*p)) {
                    /* $[<步数] */
                    size_t steps;
                    size_t consumed = parse_number(p, end, &steps);
                    p += consumed;
                    if (p >= end || *p != ']') {
                        goto fail;
                    }
                    p++;
                    op->type = STRIDE_OP_JUMP_BACK;
                    op->data.steps = steps;
                    count++;

                } else {
                    goto fail;
                }

            } else if ((size_t)(end - p) >= 3 && memcmp(p, "END", 3) == 0) {
                /* $[END] 或 $[END-n] */
                p += 3;
                op->type = STRIDE_OP_JUMP_END;
                op->data.jump_end.is_end = 1;
                op->data.jump_end.back_steps = 0;

                if (p < end && *p == '-') {
                    p++;
                    size_t back;
                    size_t consumed = parse_number(p, end, &back);
                    if (consumed == 0) {
                        goto fail;
                    }
                    p += consumed;
                    op->data.jump_end.back_steps = back;
                }

                if (p >= end || *p != ']') {
                    goto fail;
                }
                p++;
                count++;

            } else if (is_digit(*p)) {
                /* $[步位置] */
                size_t pos;
                size_t consumed = parse_number(p, end, &pos);
                p += consumed;
                if (p >= end || *p != ']') {
                    goto fail;
                }
                p++;
                op->type = STRIDE_OP_JUMP_ABS;
                op->data.steps = pos;
                count++;

            } else {
                goto fail;
            }

        } else {
            goto fail;
        }
    }

    *out_ops = ops;
    *out_count = count;
    *out_capacity = capacity;
    return 0;

fail:
    /* 当前 op 可能已分配字面量，故按容量整体释放 */
    ops_free_range(ops, capacity);
    *out_ops = NULL;
    *out_count = 0;
    *out_capacity = 0;
    return -1;
}

/* ==================== 对齐校验 ==================== */

/* 编译期已知步长时，校验每个字面量的比特长度能被步长整除 */
static int validate_alignment(const stride_op_t *ops, size_t count,
                              size_t stride) {
    if (stride == 0) {
        return 0; /* 编译期未知步长，推迟到执行期 */
    }
    for (size_t i = 0; i < count; i++) {
        if (stride_op_has_literal(ops[i].type) &&
            ops[i].data.literal.bit_len % stride != 0) {
            return -1;
        }
    }
    return 0;
}

/* ==================== 编译 ==================== */

stride_compile_result_t stride_compile(const void *pattern,
                                          size_t pattern_len, size_t stride) {
    stride_compile_result_t result;
    memset(&result, 0, sizeof(result));

    if (!pattern) {
        result.status = STRIDE_E_EMPTY_SEGMENT;
        result.error_msg = "empty pattern";
        return result;
    }
    if (!pattern_len && *(const char *)pattern == '\0') {
        result.status = STRIDE_E_EMPTY_SEGMENT;
        result.error_msg = "empty pattern";
        return result;
    }

    /* 第一阶段：词法分析 */
    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;

    if (stride_lex(pattern, pattern_len, &ops, &op_count, &op_capacity) != 0) {
        result.status = STRIDE_E_INVALID_PATTERN;
        result.error_msg = "syntax error in pattern";
        return result;
    }

    if (validate_alignment(ops, op_count, stride) != 0) {
        stride_ops_free(ops, op_count);
        result.status = STRIDE_E_ALIGN;
        result.error_msg = "literal length is not a multiple of stride";
        return result;
    }

    /* 第二阶段：生成匹配序列 */
    stride_match_op_t *match = NULL;
    size_t match_count = 0, match_capacity = 0;

    if (stride_match_compile(ops, op_count, stride, &match, &match_count,
                             &match_capacity) != 0) {
        stride_ops_free(ops, op_count);
        result.status = STRIDE_E_INVALID_PATTERN;
        result.error_msg = "failed to generate match sequence";
        return result;
    }

    /* 第三阶段：生成提取序列 */
    stride_extractor_op_t *extract = NULL;
    size_t extract_count = 0, param_count = 0;

    if (stride_extractor_compile(ops, op_count, stride, &extract,
                                 &extract_count, &param_count) != 0) {
        stride_ops_free(ops, op_count);
        stride_match_free(match, match_count);
        result.status = STRIDE_E_INVALID_PATTERN;
        result.error_msg = "failed to generate extract sequence";
        return result;
    }

    stride_ops_free(ops, op_count);

    result.status = STRIDE_OK;
    result.match = match;
    result.match_count = match_count;
    result.extract = extract;
    result.extract_count = extract_count;
    result.param_count = param_count;
    return result;
}

void stride_compile_free(stride_compile_result_t *result) {
    if (!result) {
        return;
    }
    stride_match_free(result->match, result->match_count);
    stride_extractor_free(result->extract, result->extract_count);
    memset(result, 0, sizeof(stride_compile_result_t));
}

/* ==================== 只编译其中一种序列 ==================== */

int stride_compile_match(const void *pattern, size_t pattern_len, size_t stride,
                         stride_match_op_t **out_ops, size_t *out_count,
                         size_t *out_capacity) {
    if (!pattern || !out_ops || !out_count || !out_capacity) {
        return -1;
    }
    if (!pattern_len && *(const char *)pattern == '\0') {
        return -1;
    }

    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;

    if (stride_lex(pattern, pattern_len, &ops, &op_count, &op_capacity) != 0) {
        return -1;
    }
    if (validate_alignment(ops, op_count, stride) != 0) {
        stride_ops_free(ops, op_count);
        return -1;
    }

    int rc = stride_match_compile(ops, op_count, stride, out_ops, out_count,
                                  out_capacity);
    stride_ops_free(ops, op_count);
    return rc;
}

int stride_compile_extract(const void *pattern, size_t pattern_len,
                           size_t stride, stride_extractor_op_t **out_ops,
                           size_t *out_count, size_t *out_param_count) {
    if (!pattern || !out_ops || !out_count || !out_param_count) {
        return -1;
    }
    if (!pattern_len && *(const char *)pattern == '\0') {
        return -1;
    }

    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;

    if (stride_lex(pattern, pattern_len, &ops, &op_count, &op_capacity) != 0) {
        return -1;
    }
    if (validate_alignment(ops, op_count, stride) != 0) {
        stride_ops_free(ops, op_count);
        return -1;
    }

    int rc = stride_extractor_compile(ops, op_count, stride, out_ops, out_count,
                                      out_param_count);
    stride_ops_free(ops, op_count);
    return rc;
}
