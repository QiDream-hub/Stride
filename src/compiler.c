#include "stride/compiler.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_LEX_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 编译器 - 实现
 *
 * 词法分析 + 序列编译编排
 * ============================================================ */

/* ==================== 词法分析 ==================== */

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static size_t parse_number(const char *str, size_t *num) {
    size_t result = 0;
    size_t i = 0;

    while (is_digit(str[i])) {
        result = result * 10 + (size_t)(str[i] - '0');
        i++;
    }

    *num = result;
    return i;
}

static void op_array_grow(stride_op_t **ops, size_t *capacity) {
    size_t new_cap = *capacity * 2;
    stride_op_t *new_arr = realloc(*ops, new_cap * sizeof(stride_op_t));
    if (new_arr) {
        *ops = new_arr;
        *capacity = new_cap;
    }
}

void stride_ops_free(stride_op_t *ops) {
    free(ops);
}

int stride_lex(const char *pattern, stride_op_t **out_ops,
               size_t *out_count, size_t *out_capacity) {
    if (!pattern || !out_ops || !out_count || !out_capacity) {
        return -1;
    }

    *out_ops = NULL;
    *out_count = 0;
    *out_capacity = STRIDE_LEX_INITIAL_CAPACITY;

    *out_ops = calloc(*out_capacity, sizeof(stride_op_t));
    if (!*out_ops) {
        return -1;
    }

    const char *p = pattern;
    size_t count = 0;

    while (*p) {
        /* 确保有足够空间 */
        if (count >= *out_capacity) {
            op_array_grow(out_ops, out_capacity);
        }

        stride_op_t *op = &(*out_ops)[count];
        memset(op, 0, sizeof(stride_op_t));

        if (*p == '$') {
            p++; /* 跳过 '$' */

            if (*p == '\'') {
                /* $'文本' - 精确匹配 */
                p++; /* 跳过开头的 '\'' */
                const char *start = p;

                while (*p && *p != '\'') {
                    p++;
                }

                if (*p != '\'') {
                    free(*out_ops);
                    *out_ops = NULL;
                    return -1; /* 未闭合的单引号 */
                }

                op->type = STRIDE_OP_MATCH;
                op->data.match.text = start;
                op->data.match.len = (size_t)(p - start);
                p++; /* 跳过闭合的 '\'' */
                count++;

            } else if (*p == '{') {
                p++; /* 跳过 '{' */

                if (*p == '}') {
                    /* ${} - 捕获到结尾 */
                    op->type = STRIDE_OP_CAPTURE_END;
                    p++; /* 跳过 '}' */
                    count++;

                } else if (*p == '\'') {
                    /* ${'字符'} - 捕获到字符 */
                    p++; /* 跳过开头的 '\'' */

                    if (*p == '\0') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }

                    char ch = *p;
                    p++;

                    if (*p != '\'') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1; /* 期望闭合的单引号 */
                    }
                    p++; /* 跳过闭合的 '\'' */

                    if (*p != '}') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1; /* 期望 '}' */
                    }
                    p++; /* 跳过 '}' */

                    op->type = STRIDE_OP_CAPTURE_CHR;
                    op->data.find.ch = ch;
                    count++;

                } else if (is_digit(*p)) {
                    /* ${数字} - 定长捕获 */
                    size_t len;
                    size_t consumed = parse_number(p, &len);
                    p += consumed;

                    if (*p != '}') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 '}' */

                    if (len == 0) {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1; /* 长度必须为正整数 */
                    }

                    op->type = STRIDE_OP_CAPTURE_LEN;
                    op->data.length = len;
                    count++;

                } else {
                    /* 无效的 ${} 内容 */
                    free(*out_ops);
                    *out_ops = NULL;
                    return -1;
                }

            } else if (*p == '[') {
                p++; /* 跳过 '[' */

                if (*p == '>') {
                    p++; /* 跳过 '>' */

                    if (*p == '\'') {
                        /* $[>'字符'] - 向结尾查找字符 */
                        p++; /* 跳过开头的 '\'' */

                        if (*p == '\0') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }

                        char ch = *p;
                        p++;

                        if (*p != '\'') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过闭合的 '\'' */

                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */

                        op->type = STRIDE_OP_FIND_FWD;
                        op->data.find.ch = ch;
                        count++;

                    } else {
                        /* $[>偏移] - 向结尾移动 */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;

                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */

                        op->type = STRIDE_OP_JUMP_FWD;
                        op->data.offset = offset;
                        count++;
                    }

                } else if (*p == '<') {
                    p++; /* 跳过 '<' */

                    if (*p == '\'') {
                        /* $[<'字符'] - 向开头查找字符 */
                        p++; /* 跳过开头的 '\'' */

                        if (*p == '\0') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }

                        char ch = *p;
                        p++;

                        if (*p != '\'') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过闭合的 '\'' */

                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */

                        op->type = STRIDE_OP_FIND_REV;
                        op->data.find.ch = ch;
                        count++;

                    } else {
                        /* $[<偏移] - 向开头移动 */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;

                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */

                        op->type = STRIDE_OP_JUMP_BACK;
                        op->data.offset = offset;
                        count++;
                    }

                } else if (strncmp(p, "END", 3) == 0) {
                    /* $[END] 或 $[END-n] - END 跳转 */
                    p += 3; /* 跳过 'END' */

                    op->type = STRIDE_OP_JUMP_END;
                    op->data.jump_end.is_end = 1;

                    if (*p == '-') {
                        p++; /* 跳过 '-' */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;

                        op->data.jump_end.offset = (int)offset;

                    } else {
                        op->data.jump_end.offset = 0;
                    }

                    if (*p != ']') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 ']' */
                    count++;

                } else if (is_digit(*p)) {
                    /* $[位置] - 绝对跳转（基于 HEAD）*/
                    size_t pos;
                    size_t consumed = parse_number(p, &pos);
                    p += consumed;

                    if (*p != ']') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 ']' */

                    op->type = STRIDE_OP_JUMP_ABS;
                    op->data.pos = pos;
                    count++;

                } else {
                    free(*out_ops);
                    *out_ops = NULL;
                    return -1;
                }

            } else {
                free(*out_ops);
                *out_ops = NULL;
                return -1; /* 无效的 $ 操作符 */
            }

        } else {
            free(*out_ops);
            *out_ops = NULL;
            return -1; /* 非 $ 开头的字符，视为无效语法 */
        }
    }

    *out_count = count;
    return 0;
}

/* ==================== 一步编译 ==================== */

stride_compile_result_t stride_compile(const char *pattern) {
    stride_compile_result_t result;
    memset(&result, 0, sizeof(result));

    if (!pattern || *pattern == '\0') {
        result.status = STRIDE_E_EMPTY_SEGMENT;
        result.error_msg = "empty pattern";
        return result;
    }

    /* 第一阶段：词法分析 */
    stride_op_t *ops = NULL;
    size_t op_count = 0;
    size_t op_capacity = 0;

    if (stride_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        result.status = STRIDE_E_INVALID_PATTERN;
        result.error_msg = "syntax error in pattern";
        return result;
    }

    /* 第二阶段：生成特征序列 */
    stride_feature_t *features = NULL;
    size_t feature_count = 0;
    size_t feature_capacity = 0;

    if (stride_feature_compile(ops, op_count, &features, &feature_count,
                               &feature_capacity) != 0) {
        stride_ops_free(ops);
        result.status = STRIDE_E_INVALID_PATTERN;
        result.error_msg = "failed to generate features";
        return result;
    }

    /* 第三阶段：生成提取序列 */
    stride_extractor_op_t *extractors = NULL;
    size_t extractor_count = 0;
    size_t param_count = 0;

    if (stride_extractor_compile(ops, op_count, &extractors, &extractor_count,
                                 &param_count) != 0) {
        stride_ops_free(ops);
        stride_feature_free(features, feature_count);
        result.status = STRIDE_E_INVALID_PATTERN;
        result.error_msg = "failed to generate extractors";
        return result;
    }

    /* 释放操作符数组 */
    stride_ops_free(ops);

    /* 填充结果 */
    result.status = STRIDE_OK;
    result.features = features;
    result.feature_count = feature_count;
    result.extractors = extractors;
    result.extractor_count = extractor_count;
    result.param_count = param_count;

    return result;
}

void stride_compile_free(stride_compile_result_t *result) {
    if (!result) {
        return;
    }
    if (result->features) {
        stride_feature_free(result->features, result->feature_count);
    }
    free(result->extractors);
    memset(result, 0, sizeof(stride_compile_result_t));
}

/* ==================== 只编译其中一种序列 ==================== */

int stride_compile_features(const char *pattern,
                            stride_feature_t **out_features,
                            size_t *out_count, size_t *out_capacity) {
    if (!pattern || *pattern == '\0') {
        return -1;
    }
    if (!out_features || !out_count || !out_capacity) {
        return -1;
    }

    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;

    if (stride_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        return -1;
    }

    int rc = stride_feature_compile(ops, op_count, out_features, out_count,
                                    out_capacity);
    stride_ops_free(ops);
    return rc;
}

int stride_compile_extractors(const char *pattern,
                              stride_extractor_op_t **out_extractors,
                              size_t *out_count, size_t *out_param_count) {
    if (!pattern || *pattern == '\0') {
        return -1;
    }
    if (!out_extractors || !out_count || !out_param_count) {
        return -1;
    }

    stride_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;

    if (stride_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        return -1;
    }

    int rc = stride_extractor_compile(ops, op_count, out_extractors,
                                      out_count, out_param_count);
    stride_ops_free(ops);
    return rc;
}
