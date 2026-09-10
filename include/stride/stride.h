#ifndef STRIDE_H
#define STRIDE_H

/* ============================================================
 * Stride —— 轻量级序列模式编译器
 *
 * 总入口头文件，聚合全部公共 API：
 *   - stride/types.h      共享类型：操作符 IR 与状态码
 *   - stride/compiler.h   编译：词法分析 + 序列编译编排
 *   - stride/feature.h    特征序列：编译 + 匹配
 *   - stride/extractor.h  提取序列：编译 + 执行
 *
 * 三件事彼此解绑：可以只用特征序列做匹配，也可以只用提取序列做提取，
 * 互不依赖；compiler.h 提供“一次编译出两者”的便捷入口。
 *
 * 典型用法：
 *
 *     stride_compile_result_t r = stride_compile("$'v'${'.'}$'.'${}");
 *     if (r.status == STRIDE_OK) {
 *         int ok = stride_feature_match(r.features, r.feature_count,
 *                                       segment, segment_len);
 *         // 或用 r.extractors 做提取
 *     }
 *     stride_compile_free(&r);
 * ============================================================ */

#include "stride/types.h"
#include "stride/compiler.h"
#include "stride/extractor.h"
#include "stride/feature.h"

#define STRIDE_VERSION_MAJOR 1
#define STRIDE_VERSION_MINOR 0
#define STRIDE_VERSION_PATCH 0
#define STRIDE_VERSION_STRING "1.0.0"

#endif /* STRIDE_H */
