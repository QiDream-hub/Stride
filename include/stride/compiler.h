#ifndef STRIDE_COMPILER_H
#define STRIDE_COMPILER_H

#include "stride/extractor.h"
#include "stride/feature.h"
#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 编译器（序列编译）
 *
 * 职责一：词法分析 —— 模式字符串 → 操作符序列（stride_op_t）
 * 职责二：编排 —— 把操作符序列一次编译为两个独立产物
 *
 *     模式字符串
 *        │  stride_lex()
 *        ▼
 *     操作符序列
 *        ├──► stride_feature_compile()    → 特征序列（用于匹配）
 *        └──► stride_extractor_compile()  → 提取序列（用于参数提取）
 *
 * 注意：本模块只处理“单个段”的内容，不包含分隔符。段如何切分由调用者负责。
 * ============================================================ */

/* ==================== 词法分析 ==================== */

/**
 * 词法分析：模式字符串 → 操作符序列
 * @param pattern       模式字符串（单个段的内容）
 * @param out_ops       输出操作符数组（调用者通过 stride_ops_free 释放）
 * @param out_count     输出操作符数量
 * @param out_capacity  输出数组容量
 * @return 0 成功，-1 失败（语法错误）
 *
 * 支持的语法：
 * - $'文本'     -> STRIDE_OP_MATCH
 * - ${数字}     -> STRIDE_OP_CAPTURE_LEN
 * - ${'字符'}   -> STRIDE_OP_CAPTURE_CHR
 * - ${}         -> STRIDE_OP_CAPTURE_END
 * - $[位置]     -> STRIDE_OP_JUMP_ABS
 * - $[END]      -> STRIDE_OP_JUMP_END (is_end=1, offset=0)
 * - $[END-n]    -> STRIDE_OP_JUMP_END (is_end=1, offset=n)
 * - $[>偏移]    -> STRIDE_OP_JUMP_FWD
 * - $[<偏移]    -> STRIDE_OP_JUMP_BACK
 * - $[>'字符']  -> STRIDE_OP_FIND_FWD
 * - $[<'字符']  -> STRIDE_OP_FIND_REV
 *
 * 注意：STRIDE_OP_MATCH 的 data.match.text 指向 pattern 内部，
 * 调用者须保证 pattern 在操作符序列使用期间保持有效。
 */
int stride_lex(const char *pattern, stride_op_t **out_ops,
               size_t *out_count, size_t *out_capacity);

/**
 * 释放操作符数组
 * @param ops 操作符数组，可为 NULL
 */
void stride_ops_free(stride_op_t *ops);

/* ==================== 一步编译 ==================== */

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
    size_t error_pos;
} stride_compile_result_t;

/**
 * 编译单个段模式，同时产出特征序列与提取序列
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

/* ==================== 只编译其中一种序列 ==================== */

/**
 * 只编译特征序列（不需要提取序列时使用，调用者只需链接 feature 相关能力）
 * @param pattern      模式字符串
 * @param out_features 输出特征数组（调用者通过 stride_feature_free 释放）
 * @param out_count    输出特征数量
 * @param out_capacity 输出数组容量
 * @return 0 成功，-1 失败
 */
int stride_compile_features(const char *pattern,
                            stride_feature_t **out_features,
                            size_t *out_count, size_t *out_capacity);

/**
 * 只编译提取序列
 * @param pattern         模式字符串
 * @param out_extractors  输出提取操作数组（调用者 free）
 * @param out_count       输出提取操作数量
 * @param out_param_count 输出参数数量
 * @return 0 成功，-1 失败
 */
int stride_compile_extractors(const char *pattern,
                              stride_extractor_op_t **out_extractors,
                              size_t *out_count, size_t *out_param_count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_COMPILER_H */
