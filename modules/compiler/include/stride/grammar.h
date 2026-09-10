#ifndef STRIDE_GRAMMAR_H
#define STRIDE_GRAMMAR_H

#include "stride/core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 语法模块（词法分析 / Grammar）
 *
 * 将模式字符串解析为操作符序列（stride_op_t）。
 * 支持 10 种操作符，见《Stride 序列模式语法规范》。
 *
 * 注意：本模块只处理“单个段”的内容，不包含分隔符（例如 '/'）。
 * 输入如何切分为段由调用者负责。
 * ============================================================ */

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

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_GRAMMAR_H */
