#include <stdio.h>
#include <string.h>

#include "stride/stride.h"

/* ============================================================
 * Stride 使用示例
 *
 * 演示：编译一个段模式 → 打印特征序列 / 提取序列 → 在段上提取参数。
 *
 * 注意：Stride 只处理单个段的内容。如何把输入切分为段（例如按 '/'
 * 切分 URL 路径）由调用者负责。
 * ============================================================ */

static const char *feature_type_name(stride_feature_type_t t) {
    switch (t) {
        case STRIDE_FT_CONST_REL_FWD:    return "CONST_REL_FWD";
        case STRIDE_FT_CONST_REL_BACK:   return "CONST_REL_BACK";
        case STRIDE_FT_CONST_ABS_HEAD:   return "CONST_ABS_HEAD";
        case STRIDE_FT_CONST_ABS_END:    return "CONST_ABS_END";
        case STRIDE_FT_DYNAMIC_FIND_FWD: return "DYNAMIC_FIND_FWD";
        case STRIDE_FT_DYNAMIC_FIND_REV: return "DYNAMIC_FIND_REV";
        default:                         return "?";
    }
}

static const char *extractor_type_name(stride_extractor_op_type_t t) {
    switch (t) {
        case STRIDE_EX_CAPTURE_LEN: return "CAPTURE_LEN";
        case STRIDE_EX_CAPTURE_CHR: return "CAPTURE_CHR";
        case STRIDE_EX_CAPTURE_END: return "CAPTURE_END";
        case STRIDE_EX_SKIP_LEN:    return "SKIP_LEN";
        case STRIDE_EX_JUMP_ABS:    return "JUMP_ABS";
        case STRIDE_EX_JUMP_END:    return "JUMP_END";
        case STRIDE_EX_JUMP_FWD:    return "JUMP_FWD";
        case STRIDE_EX_JUMP_BACK:   return "JUMP_BACK";
        case STRIDE_EX_FIND_FWD:    return "FIND_FWD";
        case STRIDE_EX_FIND_REV:    return "FIND_REV";
        default:                    return "?";
    }
}

static void print_compile_result(const stride_compile_result_t *r) {
    printf("特征序列（%zu 个元组）:\n", r->feature_count);
    for (size_t i = 0; i < r->feature_count; i++) {
        const stride_feature_t *f = &r->features[i];
        printf("  [%zu] %-18s value=%-4d", i, feature_type_name(f->type),
               f->value);
        if (f->keyword) {
            printf(" keyword=\"%.*s\"", (int)f->keyword_len, f->keyword);
        }
        printf("\n");
    }

    printf("提取序列（%zu 个操作，%zu 个参数）:\n", r->extractor_count,
           r->param_count);
    for (size_t i = 0; i < r->extractor_count; i++) {
        const stride_extractor_op_t *e = &r->extractors[i];
        printf("  [%zu] %s", i, extractor_type_name(e->type));
        switch (e->type) {
            case STRIDE_EX_CAPTURE_LEN:
                printf(" length=%zu", e->data.capture_len.length);
                break;
            case STRIDE_EX_CAPTURE_CHR:
                printf(" ch='%c'", e->data.capture_chr.ch);
                break;
            case STRIDE_EX_SKIP_LEN:
                printf(" length=%zu", e->data.skip_len.length);
                break;
            case STRIDE_EX_JUMP_ABS:
                printf(" pos=%zu", e->data.jump_abs.pos);
                break;
            case STRIDE_EX_JUMP_END:
                printf(" END-%d", e->data.jump_end.offset);
                break;
            case STRIDE_EX_JUMP_FWD:
                printf(" offset=%zu", e->data.jump_fwd.offset);
                break;
            case STRIDE_EX_JUMP_BACK:
                printf(" offset=%zu", e->data.jump_back.offset);
                break;
            case STRIDE_EX_FIND_FWD:
                printf(" ch='%c'", e->data.find_fwd.ch);
                break;
            case STRIDE_EX_FIND_REV:
                printf(" ch='%c'", e->data.find_rev.ch);
                break;
            default:
                break;
        }
        printf("\n");
    }
}

/* 编译模式，并在给定段上提取参数 */
static void demo(const char *pattern, const char *segment) {
    printf("--------------------------------------------------\n");
    printf("模式: %s\n", pattern);
    printf("段:   \"%s\"\n\n", segment);

    stride_compile_result_t r = stride_compile(pattern);
    if (r.status != STRIDE_OK) {
        printf("编译失败: %s (%s)\n\n", stride_status_str(r.status),
               r.error_msg ? r.error_msg : "?");
        stride_compile_free(&r);
        return;
    }

    print_compile_result(&r);

    /* 特征序列匹配：只判断段是否命中，不做提取 */
    int matched = stride_feature_match(r.features, r.feature_count, segment,
                                       strlen(segment));
    printf("\n特征序列匹配: %s\n", matched == 0 ? "命中" : "未命中");

    if (matched != 0) {
        stride_compile_free(&r);
        printf("\n");
        return;
    }

    stride_extractor_t *ex =
        stride_extractor_create(r.extractors, r.extractor_count);
    printf("运行提取:\n");

    if (ex) {
        stride_param_t params[16];
        size_t count = 0;
        size_t len = strlen(segment);

        if (stride_extractor_execute(ex, segment, len, params, 16, &count) == 0) {
            printf("  共 %zu 个参数\n", count);
            for (size_t i = 0; i < count; i++) {
                printf("    [%zu] \"%.*s\" (len=%zu)\n", i,
                       (int)params[i].len, params[i].ptr, params[i].len);
            }
        } else {
            printf("  提取失败（段与模式不匹配？）\n");
        }
        stride_extractor_destroy(ex);
    }

    stride_compile_free(&r);
    printf("\n");
}

int main(void) {
    printf("Stride %s —— 序列模式编译器示例\n\n", STRIDE_VERSION_STRING);

    /* 版本号段：$'v'${'.'}$'.'${}  → 提取 ["2", "0"] */
    demo("$'v'${'.'}$'.'${}", "v2.0");

    /* 日期段：${4}$'-'${2}$'-'${2}  → 提取 ["2024", "03", "15"] */
    demo("${4}$'-'${2}$'-'${2}", "2024-03-15");

    /* 文件名段：${'.'}$'.'${}  → 提取 ["document", "pdf"] */
    demo("${'.'}$'.'${}", "document.pdf");

    /* 反向查找：$[<'=']${}  → 提取 ["=alice"] */
    demo("$[<'=']${}", "name=alice");

    /* 回溯捕获：${}$[0]${'.'}$'.'${}  → 提取 ["document.pdf","document","pdf"] */
    demo("${}$[0]${'.'}$'.'${}", "document.pdf");

    /* 匹配失败示例：段尾未对齐 */
    demo("${4}$'-'${2}$'-'${2}", "20240315");

    /* 编译错误示例 */
    demo("$'unclosed", "whatever");

    return 0;
}
