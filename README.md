# Stride

**Stride** 是一个用 C99 编写的轻量级**序列模式编译器**，零外部依赖，仅使用标准 C 库。

它把一段“序列模式”（例如 `$'v'${'.'}$'.'${}`）一次编译为两个相互独立的产物，并提供各自的运行时：

| 能力 | 产物 | 用途 |
|------|------|------|
| 编译 | 特征序列 + 提取序列 | 把模式翻译成两份可执行的指令序列 |
| **特征序列匹配** | 特征序列 | 只判断段是否命中，快速、不产生参数 |
| **提取序列提取** | 提取序列 | 命中后按需提取参数（零拷贝） |

匹配与提取分离：匹配阶段无需关心捕获细节，提取阶段无需重复验证关键字。

> **与分隔符无关**：Stride 编译的是**单个段（segment）**——一个不透明的字符数组。
> 如何把输入切分为段（按 `/` 切 URL 路径、按 `.` 切域名、按 `,` 切 CSV 行……）
> 完全由调用者负责。因此 Stride 既可用于 URL 路由，也可用于任何“按固定分隔符切分后
> 逐段匹配并提取”的场景。

---

## 三件彼此解绑的能力

解绑体现在**头文件与 API**，而不是目录：只 include 需要的头文件，就能独立使用对应能力。

| 能力 | 头文件 | 你能独立完成的事 | 完全不需要用到 |
|------|--------|-----------------|---------------|
| 编译 | `stride/compiler.h` | 词法分析、把模式编译成特征序列 / 提取序列 | — |
| 特征序列 | `stride/feature.h` | 编译特征序列、**用特征序列匹配段** | 提取序列 |
| 提取序列 | `stride/extractor.h` | 编译提取序列、执行提取 | 特征序列 |

`feature.h` 与 `extractor.h` 只依赖共享的 `stride/types.h`，**互不依赖**；
`compiler.h` 在两者之上提供“一次编译出两个序列”的编排。

```
                types.h              （操作符 IR + 状态码）
               /   |   \
     feature.h  extractor.h  compiler.h
```

---

## 目录结构

```
Stride/
├── include/stride/
│   ├── stride.h        # 总入口（聚合全部公共头文件）
│   ├── types.h         # 共享类型：操作符 IR、状态码
│   ├── compiler.h      # 编译：词法分析 + 序列编译编排
│   ├── feature.h       # 特征序列：类型 + 编译 + 匹配
│   └── extractor.h     # 提取序列：类型 + 编译 + 执行
├── src/
│   ├── compiler.c      # 词法分析 + 编排
│   ├── feature.c       # 特征序列编译 + 匹配
│   └── extractor.c     # 提取序列编译（含优化）+ 执行
├── tests/
│   ├── test_compiler.c
│   ├── test_feature.c      # 编译 + 匹配
│   └── test_extractor.c    # 编译 + 执行
├── examples/example.c
├── doc/                # 设计文档（中文）
├── Makefile
└── README.md
```

---

## 快速开始

### 构建

```bash
make                    # 构建静态库 build/libstride.a
make test               # 构建并运行全部测试
make example && make run # 构建并运行示例
make compile-commands   # 用 bear 生成 compile_commands.json（供 clangd 使用）
make clean
```

### 最小示例

```c
#include <stdio.h>
#include "stride/stride.h"

int main(void) {
    /* 编译单个段模式：$'v'${'.'}$'.'${}  */
    stride_compile_result_t r = stride_compile("$'v'${'.'}$'.'${}");
    if (r.status != STRIDE_OK) {
        printf("compile error: %s\n", stride_status_str(r.status));
        stride_compile_free(&r);
        return 1;
    }

    /* ① 匹配：用特征序列判断段是否命中 */
    const char *segment = "v2.0";
    if (stride_feature_match(r.features, r.feature_count,
                             segment, 4) != 0) {
        printf("no match\n");
        stride_compile_free(&r);
        return 1;
    }

    /* ② 提取：命中后用提取序列取出参数（零拷贝） */
    stride_extractor_t *ex =
        stride_extractor_create(r.extractors, r.extractor_count);

    stride_param_t params[8];
    size_t count = 0;
    if (stride_extractor_execute(ex, segment, 4, params, 8, &count) == 0) {
        for (size_t i = 0; i < count; i++)
            printf("[%zu] %.*s\n", i, (int)params[i].len, params[i].ptr);
        /* 输出：[0] 2    [1] 0 */
    }

    stride_extractor_destroy(ex);
    stride_compile_free(&r);
    return 0;
}
```

只做匹配、不需要提取时，可以只编译特征序列：

```c
stride_feature_t *feats = NULL;
size_t n = 0, cap = 0;

if (stride_compile_features("$'user'", &feats, &n, &cap) != 0) { /* ... */ }
/* 注意：传入的是单个段模式，不含分隔符 */
int hit = stride_feature_match(feats, n, "user", 4);
stride_feature_free(feats, n);
```

参数以 `stride_param_t { const char *ptr; size_t len; }` 返回，**零拷贝**：
`ptr` 直接指向输入段内部，调用者只需保证输入在使用期间有效。

---

## 语法

段模式由以下操作符按书写顺序组成（共 10 个 `stride_op_type_t` 枚举值）：

| 类别 | 操作符 | 说明 |
|------|--------|------|
| 匹配 | `$'文本'` | 精确匹配固定字符串 |
| 捕获 | `${数字}` | 捕获指定长度字符 |
| 捕获 | `${'字符'}` | 捕获到指定字符前（不含该字符） |
| 捕获 | `${}` | 捕获到段尾 |
| 移动 | `$[位置]` | 绝对跳转到某位置（基于 HEAD） |
| 移动 | `$[END]` / `$[END-n]` | 跳到段尾 / 从段尾向前偏移 n |
| 移动 | `$[>偏移]` | 向段尾方向移动 |
| 移动 | `$[<偏移]` | 向段首方向移动 |
| 移动 | `$[>'字符']` | 向段尾方向查找字符 |
| 移动 | `$[<'字符']` | 向段首方向查找字符 |

匹配成功需同时满足：**操作耗尽** 且 **指针恰好位于段尾**（段尾对齐，避免部分匹配）。

完整语法、边界规则与示例见 [`doc/Stride 语法规范.md`](doc/Stride%20语法规范.md)。

---

## API 速览

### 编译（`stride/compiler.h`）

| 函数 | 说明 |
|------|------|
| `int stride_lex(const char *pattern, stride_op_t **ops, size_t *n, size_t *cap)` | 词法分析：模式 → 操作符序列 |
| `void stride_ops_free(stride_op_t *ops)` | 释放操作符序列 |
| `stride_compile_result_t stride_compile(const char *pattern)` | 一次编译出特征序列与提取序列 |
| `void stride_compile_free(stride_compile_result_t *r)` | 释放编译结果 |
| `int stride_compile_features(const char *pattern, ...)` | 只编译特征序列 |
| `int stride_compile_extractors(const char *pattern, ...)` | 只编译提取序列 |

`stride_compile_result_t` 字段：`status`、`features`/`feature_count`、
`extractors`/`extractor_count`、`param_count`、`error_msg`、`error_pos`。

> `STRIDE_OP_MATCH` 的 `data.match.text` 指向传入的 `pattern`，不复制；
> 调用者需保证 `pattern` 在操作符序列使用期间有效。

### 特征序列（`stride/feature.h`）

| 函数 | 说明 |
|------|------|
| `int stride_feature_compile(const stride_op_t *ops, size_t n, ...)` | 操作符序列 → 特征序列 |
| `void stride_feature_free(stride_feature_t *f, size_t n)` | 释放特征序列（含关键字副本） |
| `int stride_feature_match(const stride_feature_t *f, size_t n, const char *segment, size_t len)` | 匹配：0 命中 / -1 未命中 |
| `int stride_feature_match_ex(..., stride_match_detail_t *out)` | 带诊断的匹配（失败元组下标、游标） |

特征元组 6 种类型：`STRIDE_FT_CONST_REL_FWD`、`STRIDE_FT_CONST_REL_BACK`、
`STRIDE_FT_CONST_ABS_HEAD`、`STRIDE_FT_CONST_ABS_END`、
`STRIDE_FT_DYNAMIC_FIND_FWD`、`STRIDE_FT_DYNAMIC_FIND_REV`。

`value` 统一为**非负幅度**，方向由 `type` 表达：

| type | 含义 |
|------|------|
| `CONST_REL_FWD` / `CONST_REL_BACK` | 向段尾 / 段首移动的距离 |
| `CONST_ABS_HEAD` | 目标位置 = HEAD + value |
| `CONST_ABS_END` | 目标位置 = END − value（0 表示段尾） |
| `DYNAMIC_FIND_FWD` / `DYNAMIC_FIND_REV` | 要查找字符的 ASCII 值 |

### 提取序列（`stride/extractor.h`）

| 函数 | 说明 |
|------|------|
| `int stride_extractor_compile(const stride_op_t *ops, size_t n, ...)` | 操作符序列 → 提取序列（含优化） |
| `stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops, size_t n)` | 创建单段提取器 |
| `void stride_extractor_destroy(stride_extractor_t *ex)` | 销毁单段提取器 |
| `int stride_extractor_execute(const stride_extractor_t *ex, const char *segment, size_t len, stride_param_t *params, size_t cap, size_t *count)` | 在段上执行提取 |
| `stride_full_extractor_t *stride_full_extractor_create(stride_extractor_t **segs, size_t n)` | 组合多段提取器 |
| `void stride_full_extractor_destroy(stride_full_extractor_t *full)` | 销毁多段提取器（含各段） |
| `int stride_full_extractor_execute(...)` | 多段提取，参数按段顺序连接 |

提取操作 10 种：`STRIDE_EX_CAPTURE_LEN/CHR/END`（产生参数）、
`STRIDE_EX_SKIP_LEN`、`STRIDE_EX_JUMP_ABS/END/FWD/BACK`、
`STRIDE_EX_FIND_FWD/REV`（不产生参数）。

---

## 编译与匹配模型

```
模式字符串
   │  ① stride_lex()            词法分析
   ▼
操作符序列（stride_op_t）
   ├──② stride_feature_compile()  → 特征序列 ──③ stride_feature_match()  → 命中 / 未命中
   └──④ stride_extractor_compile()→ 提取序列 ──⑤ stride_extractor_execute()→ 参数
```

- **特征序列编译**：IDLE / HOLD 两状态机。常量操作可相加，关键字与当前 HOLD
  元组合并，动态操作打断合并。只保留匹配所需信息，丢弃捕获边界。
- **特征序列匹配**：按顺序执行每个元组的移动；带关键字的元组在移动后的游标处
  验证关键字并前进其长度；全部执行完毕后游标必须正好等于段长度。
- **提取序列编译**：两阶段。① 基础转换：`OP_MATCH → STRIDE_EX_SKIP_LEN`
  （匹配阶段已验证关键字，提取阶段无需重复验证）；② 常量移动合并：连续的
  `EX_SKIP_LEN`/`EX_JUMP_FWD`/`EX_JUMP_BACK` 相加抵消为一个操作。

详细状态转换表、常量相加 / END 约束 / 关键字合并规则见
[`doc/Stride 编译器设计文档.md`](doc/Stride%20编译器设计文档.md) 与
[`doc/Stride 特征序列设计文档.md`](doc/Stride%20特征序列设计文档.md)。

---

## 测试

```bash
make test
```

| 测试 | 覆盖内容 |
|------|---------|
| `tests/test_compiler.c` | 10 种操作符的词法分析、语法错误、一步编译、只编译单序列 |
| `tests/test_feature.c` | 常量相加、动态打断、关键字合并、元组值语义；**匹配**（含诊断与边界） |
| `tests/test_extractor.c` | 匹配优化、常量合并、抵消、对象生命周期；运行时提取与零拷贝 |

---

## 工具链

生成 `compile_commands.json` 供 clangd / IDE 使用（该文件已被 `.gitignore` 忽略）：

```bash
make compile-commands      # 等价于 bear -- make clean all
```

---

## 设计文档

- [Stride 语法规范](doc/Stride%20语法规范.md) —— 操作符语法、执行模型、边界检查、完整示例
- [Stride 特征序列设计文档](doc/Stride%20特征序列设计文档.md) —— 六种元组、HEAD/END 基准、编译与匹配规则
- [Stride 编译器设计文档](doc/Stride%20编译器设计文档.md) —— 词法表、状态机、两阶段编译、状态码

---

## 许可证

本项目采用 MIT 许可证，见 [LICENSE](LICENSE)。
