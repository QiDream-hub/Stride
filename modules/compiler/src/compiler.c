#include "stride/compiler.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Stride 序列编译器 - 编译入口实现
 *
 * 协调词法分析、特征序列编译、提取序列编译
 * ============================================================ */

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
