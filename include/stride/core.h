#ifndef STRIDE_CORE_H
#define STRIDE_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 核心定义（共享契约）
 *
 * 本头文件是三个模块（feature / extractor / compiler）共同依赖的
 * 唯一中立层，只包含：
 *   1. 操作符（operator）序列的数据结构 —— 语法的中间表示（IR）
 *   2. 统一的状态码 / 错误码
 *
 * 依赖方向（无环）：
 *
 *              core.h
 *             /   |   \
 *   feature.h  extractor.h  grammar.h
 *             \   |   /
 *            compiler.h
 *
 * 注意：Stride 与分隔符无关。段（segment）在 Stride 中就是一个
 * 不透明的字符数组，如何切分输入（例如按 '/' 切分 URL）由调用者负责。
 * ============================================================ */

/* ==================== 操作符定义 ==================== */

/**
 * 操作符类型 —— 对应语法规范中的 10 种操作符
 */
typedef enum {
    STRIDE_OP_MATCH = 0,    /* $'文本'     - 精确匹配固定字符串 */
    STRIDE_OP_CAPTURE_LEN,  /* ${长度}     - 捕获指定长度字符 */
    STRIDE_OP_CAPTURE_CHR,  /* ${'字符'}   - 捕获到指定字符前 */
    STRIDE_OP_CAPTURE_END,  /* ${}         - 捕获到段尾 */
    STRIDE_OP_JUMP_ABS,     /* $[位置]     - 绝对跳转（基于 HEAD）*/
    STRIDE_OP_JUMP_END,     /* $[END] / $[END-n] - END 跳转 */
    STRIDE_OP_JUMP_FWD,     /* $[>偏移]    - 向结尾方向移动 */
    STRIDE_OP_JUMP_BACK,    /* $[<偏移]    - 向开头方向移动 */
    STRIDE_OP_FIND_FWD,     /* $[>'字符']  - 向结尾方向查找字符 */
    STRIDE_OP_FIND_REV      /* $[<'字符']  - 向开头方向查找字符 */
} stride_op_type_t;

/**
 * 操作符
 *
 * 词法分析（grammar 模块）的输出，也是特征序列编译器与
 * 提取序列编译器的输入。
 *
 * 注意：对于 STRIDE_OP_MATCH，data.match.text 指向调用者传入的
 * pattern 字符串内部，不拥有所有权。
 */
typedef struct {
    stride_op_type_t type;
    union {
        /* STRIDE_OP_MATCH */
        struct {
            const char *text;  /* 指向 pattern 内部 */
            size_t len;
        } match;

        /* STRIDE_OP_CAPTURE_LEN, STRIDE_OP_JUMP_ABS,
         * STRIDE_OP_JUMP_FWD, STRIDE_OP_JUMP_BACK */
        size_t length;
        size_t pos;
        size_t offset;

        /* STRIDE_OP_CAPTURE_CHR, STRIDE_OP_FIND_FWD, STRIDE_OP_FIND_REV */
        struct {
            char ch;
        } find;

        /* STRIDE_OP_JUMP_END */
        struct {
            int is_end;    /* 是否为 END */
            int offset;    /* END-n 中的 n，0 表示纯 END */
        } jump_end;
    } data;
} stride_op_t;

/* ==================== 状态码 / 错误码 ==================== */

/**
 * 编译状态码
 * 见设计文档《Stride 编译器设计文档》错误码定义一节
 */
typedef enum {
    STRIDE_OK = 0,               /* 成功 */
    STRIDE_E_INVALID_PATTERN,    /* 模式格式无效 */
    STRIDE_E_UNCLOSED_QUOTE,     /* 未闭合的引号 */
    STRIDE_E_INVALID_NUMBER,     /* 无效的数字 */
    STRIDE_E_INVALID_POSITION,   /* 无效的位置表达式 */
    STRIDE_E_EMPTY_SEGMENT,      /* 空的段模式 */
    STRIDE_E_NO_LEADING_SLASH,   /* 模式不以 / 开头（保留，Stride 不检查）*/
    STRIDE_E_END_CONFLICT,       /* END 后存在其他操作 */
    STRIDE_E_END_POSITIVE_OFFSET,/* END 与正数相加 */
    STRIDE_E_END_DUPLICATE,      /* 同一持有单元内 END 重复 */
    STRIDE_E_ROUTE_CONFLICT      /* 路由特征序列冲突（保留，Stride 未使用）*/
} stride_status_t;

/**
 * 状态码转可读字符串
 * @param status 状态码
 * @return 静态字符串，永不为 NULL
 *
 * 以 static inline 提供，避免为共享层引入额外的编译单元，
 * 使 feature / extractor / compiler 三个模块保持自足。
 */
static inline const char *stride_status_str(stride_status_t status) {
    switch (status) {
        case STRIDE_OK:                    return "ok";
        case STRIDE_E_INVALID_PATTERN:     return "invalid pattern";
        case STRIDE_E_UNCLOSED_QUOTE:      return "unclosed quote";
        case STRIDE_E_INVALID_NUMBER:      return "invalid number";
        case STRIDE_E_INVALID_POSITION:    return "invalid position";
        case STRIDE_E_EMPTY_SEGMENT:       return "empty segment";
        case STRIDE_E_NO_LEADING_SLASH:    return "pattern does not start with '/'";
        case STRIDE_E_END_CONFLICT:        return "operation after END";
        case STRIDE_E_END_POSITIVE_OFFSET: return "END with positive offset";
        case STRIDE_E_END_DUPLICATE:       return "duplicate END in hold unit";
        case STRIDE_E_ROUTE_CONFLICT:      return "route feature conflict";
        default:                           return "unknown status";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_CORE_H */
