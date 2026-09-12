#ifndef STRIDE_H
#define STRIDE_H

/* ============================================================
 * Stride —— 轻量级步进式比特串匹配库
 *
 * 两个原语：步长（每步多少比特）+ 比特串（比什么）。
 * 不含语法、不含编译器：模式 → 序列的翻译由调用方（如 URLRouter）完成，
 * Stride 只提供函数式的序列构建与通用执行引擎。
 *
 * 公共 API：
 *   - stride/types.h     类型：比特串、参数、步进节点与序列、状态码
 *   - stride/sequence.h  构建（尾部合并）+ 通用执行引擎
 *   - stride/matcher.h   匹配执行入口
 *   - stride/extractor.h 提取执行入口（单段 / 多段）
 *
 * 典型用法（调用方自行把模式翻译成构建函数调用）：
 *
 *     stride_seq_t *m = stride_seq_new();
 *     stride_seq_step_fwd(m, 3);                     // $[>3]
 *     stride_blob_t lit = { "ab", STRIDE_BITS(2) };
 *     stride_seq_compare(m, &lit);                   // $'ab'
 *     int hit = stride_match_run(m, 8, segment, STRIDE_BITS(n));
 *     stride_seq_free(m);
 * ============================================================ */

#include "stride/types.h"
#include "stride/sequence.h"
#include "stride/matcher.h"
#include "stride/extractor.h"

#define STRIDE_VERSION_MAJOR 3
#define STRIDE_VERSION_MINOR 0
#define STRIDE_VERSION_PATCH 0
#define STRIDE_VERSION_STRING "3.0.0"

#endif /* STRIDE_H */
