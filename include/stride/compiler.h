#ifndef STRIDE_COMPILER_H
#define STRIDE_COMPILER_H

#include "stride/extractor.h"
#include "stride/matcher.h"
#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 编译器（序列编译）
 *
 * 职责一：词法分析 —— 模式 → 操作符序列（stride_op_t）
 * 职责二：编排 —— 把操作符序列一次编译为两个独立产物
 *
 *     模式
 *        │  stride_lex()
 *        ▼
 *     操作符序列
 *        ├──► stride_match_compile()      → 匹配序列（用于匹配）
 *        └──► stride_extractor_compile()  → 提取序列（用于参数提取）
 *
 * 单位：步长以比特计；位置以步计；长度以比特计。
 *
 * 注意：本模块只处理“单个段”的内容，不包含分隔符。段如何切分由调用者负责。
 * ============================================================ */

/* ==================== 词法分析 ==================== */

/**
 * 词法分析：模式 → 操作符序列
 * @param pattern       模式（单个段的内容）
 * @param pattern_len   模式字节长度；0 表示按 '\0' 结尾
 * @param out_ops       输出操作符数组（调用者通过 stride_ops_free 释放）
 * @param out_count     输出操作符数量
 * @param out_capacity  输出数组容量
 * @return 0 成功，-1 失败（语法错误）
 *
 * 支持的语法：
 * - $'比特串'      -> STRIDE_OP_MATCH
 * - ${步数}        -> STRIDE_OP_CAPTURE_STEPS
 * - ${'比特串'}    -> STRIDE_OP_CAPTURE_UNTIL
 * - ${}            -> STRIDE_OP_CAPTURE_END
 * - $[步位置]      -> STRIDE_OP_JUMP_ABS
 * - $[END]         -> STRIDE_OP_JUMP_END (is_end=1, back_steps=0)
 * - $[END-n]       -> STRIDE_OP_JUMP_END (is_end=1, back_steps=n)
 * - $[>步数]       -> STRIDE_OP_JUMP_FWD
 * - $[<步数]       -> STRIDE_OP_JUMP_BACK
 * - $[>'比特串']   -> STRIDE_OP_FIND_FWD
 * - $[<'比特串']   -> STRIDE_OP_FIND_REV
 *
 * 字面量支持 \\、\'、\xNN 转义；语法字符恒为单字节 ASCII。
 */
int stride_lex(const void *pattern, size_t pattern_len, stride_op_t **out_ops,
               size_t *out_count, size_t *out_capacity);

/**
 * 释放操作符数组（含每个字面量操作符的比特串副本）
 * @param ops   操作符数组，可为 NULL
 * @param count 操作符数量
 */
void stride_ops_free(stride_op_t *ops, size_t count);

/* ==================== 一步编译 ==================== */

/**
 * 编译结果
 *
 * match / extract 由本结构拥有，通过 stride_compile_free 释放。
 * error_msg 为静态字符串，不需要释放。
 */
typedef struct {
  stride_status_t status;

  /* 匹配序列 */
  stride_match_op_t *match;
  size_t match_count;

  /* 提取序列（已优化）*/
  stride_extractor_op_t *extract;
  size_t extract_count;
  size_t param_count; /* 参数数量 */

  /* 错误信息 */
  const char *error_msg;
  size_t error_pos;
} stride_compile_result_t;

/**
 * 编译单个段模式，同时产出匹配序列与提取序列
 * @param pattern     模式（单个段的内容）
 * @param pattern_len 模式字节长度；0 表示按 '\0' 结尾
 * @param stride      编译期步长（比特/步）；0 表示未知（不校验对齐），≥1
 * 表示已知
 * @return 编译结果；status == STRIDE_OK 表示成功
 */
stride_compile_result_t stride_compile(const void *pattern, size_t pattern_len,
                                       size_t stride);

/**
 * 释放编译结果（含匹配序列与提取序列），并清零结构
 */
void stride_compile_free(stride_compile_result_t *result);

/* ==================== 只编译其中一种序列 ==================== */

/**
 * 只编译匹配序列（不需要提取序列时使用，调用者只需链接 matcher 相关能力）
 * @return 0 成功，-1 失败
 */
int stride_compile_match(const void *pattern, size_t pattern_len, size_t stride,
                         stride_match_op_t **out_ops, size_t *out_count,
                         size_t *out_capacity);

/**
 * 只编译提取序列
 * @return 0 成功，-1 失败
 */
int stride_compile_extract(const void *pattern, size_t pattern_len,
                           size_t stride, stride_extractor_op_t **out_ops,
                           size_t *out_count, size_t *out_param_count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_COMPILER_H */
