#ifndef STRIDE_H
#define STRIDE_H

/* ============================================================
 * Stride —— 轻量级序列模式编译器
 *
 * 总入口头文件，聚合三个模块的公共 API：
 *   - stride/compiler.h   序列编译器（含语法/词法分析入口）
 *   - stride/feature.h    特征序列（用于匹配）
 *   - stride/extractor.h  提取序列（用于参数提取）
 *
 * 以及共享契约：
 *   - stride/core.h       操作符 IR 与状态码
 *
 * 典型用法：
 *
 *     stride_compile_result_t r = stride_compile("$'v'${'.'}$'.'${}");
 *     if (r.status == STRIDE_OK) {
 *         // 使用 r.features 做匹配，或用 r.extractors 做提取
 *     }
 *     stride_compile_free(&r);
 * ============================================================ */

#include "stride/core.h"
#include "stride/compiler.h"
#include "stride/extractor.h"
#include "stride/feature.h"
#include "stride/grammar.h"

#define STRIDE_VERSION_MAJOR 1
#define STRIDE_VERSION_MINOR 0
#define STRIDE_VERSION_PATCH 0
#define STRIDE_VERSION_STRING "1.0.0"

#endif /* STRIDE_H */
