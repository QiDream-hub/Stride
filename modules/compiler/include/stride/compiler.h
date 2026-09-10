#ifndef STRIDE_COMPILER_H
#define STRIDE_COMPILER_H

#include "stride/core.h"
#include "stride/extractor.h"
#include "stride/feature.h"
#include "stride/grammar.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 序列编译器（Sequence Compiler）
 *
 * 编译入口：把单个段模式一次编译为两个独立产物
 *
 *     模式字符串
 *        │  词法分析（grammar）
 *        ▼
 *     操作符序列
 *        ├──► 特征序列编译（feature）   → 用于匹配
 *        └──► 提取序列编译（extractor） → 用于参数提取
 *
 * 本模块负责编排 grammar / feature / extractor 三个部分，
 * 并统一管理产物生命周期。
 * ============================================================ */

/**
 * 编译结果
 *
 * features / extractors 由本结构拥有，通过 stride_compile_free 释放。
 * error_msg 为静态字符串，不需要释放。
 */
typedef struct {
    stride_status_t status;

    /* 特征序列 */
    stride_feature_t *features;
    size_t feature_count;

    /* 提取序列（已优化）*/
    stride_extractor_op_t *extractors;
    size_t extractor_count;
    size_t param_count; /* 参数数量 */

    /* 错误信息 */
    const char *error_msg;
    size_t error_pos; /* 错误位置（当前实现仅语法错误时为 0）*/
} stride_compile_result_t;

/**
 * 编译单个段模式
 * @param pattern 模式字符串（单个段的内容，不包含分隔符）
 * @return 编译结果；status == STRIDE_OK 表示成功
 *
 * 即使失败也返回一个合法结构：status 为错误码，error_msg 为描述。
 * 调用者始终应调用 stride_compile_free 释放。
 */
stride_compile_result_t stride_compile(const char *pattern);

/**
 * 释放编译结果（含特征关键字与提取序列），并清零结构
 * @param result 编译结果指针，可为 NULL
 */
void stride_compile_free(stride_compile_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_COMPILER_H */
