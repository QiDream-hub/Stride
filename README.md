# Stride

**Stride** 是一个用 C99 编写的轻量级**步进式比特串匹配编译器**，零外部依赖，仅使用标准 C 库。

它把一段“段模式”（例如 `$'v'${'.'}$'.'${}`）一次编译为两个相互独立的产物，并提供各自的运行时：

| 能力 | 产物 | 用途 |
|------|------|------|
| 编译 | 匹配序列 + 提取序列 | 把模式翻译成两份可执行的指令序列 |
| **匹配序列匹配** | 匹配序列 | 只判断段是否命中，快速、不产生参数 |
| **提取序列提取** | 提取序列 | 命中后按需提取参数（零拷贝） |

匹配与提取分离：匹配阶段无需关心捕获细节，提取阶段无需重复验证字面量。

> **本项目名即核心原语**：`stride` 指**步长**——每一步跨越多少**比特**。
> Stride 把输入看作**不透明二进制**，用「步长寻址 + 比特串比对」完成匹配，
> **完全不解释数据的编码或结构**。

---

## 两个正交原语

整套设计只有两个原语，理解它们就理解了 Stride：

| 原语 | 回答的问题 | 作用 |
|------|-----------|------|
| **步长 stride** | 指针“怎么走” | 游标以**步**计数，比特偏移 = `步 × 步长`（步长单位：比特） |
| **比特串 blob** | “比什么” | 比对与查找的目标是任意长度的**二进制比特串** |

二者合起来即：**在 `步 × 步长` 得到的比特偏移处，比对一段比特串**。引擎不关心里面是
ASCII、UTF-8、UTF-16 还是二进制记录，因此同一套机制可以：

- 以步长 8 处理字节流（UTF-8 / Latin-1 / 任意二进制），以步长 4 处理半字节；
- 以步长 1 处理位域，以步长 16 / 32 处理 UTF-16 / UTF-32 或定长记录；
- 用**多比特长字面量**匹配中文等内容（如 `$'用户'`、`${'：'}`）。

> **为什么不需要理解编码**：比特串比对是可选的“全有或全无”匹配。要匹配中文，
> 就把中文那几个字节写进字面量即可；UTF-8 的自同步性还保证了整段多字节序列
> 不可能匹配到某个字符的内部，无需解码就天然按字符对齐。

> **与分隔符无关**：Stride 编译的是**单个段（segment）**——一个不透明的二进制。
> 如何把输入切分为段（按 `/` 切 URL 路径、按 `.` 切域名、按 `,` 切 CSV 行……）
> 完全由调用者负责。因此 Stride 既可用于 URL 路由，也可用于任何“按固定分隔符切分后
> 逐段匹配并提取”的场景，还可用于定长二进制记录/协议的字段解析。

---

## 单位与量纲

| 量 | 单位 | 说明 |
|----|------|------|
| 步长 `stride` | **比特/步** | 每一步跨越的比特数，`≥ 1` |
| 段长 `segment_bit_len` | **比特** | `= 8 × 字节数` |
| 字面量长度 `bit_len` | **比特** | `= 8 × 字节数`，可用 `STRIDE_BITS(nbytes)` 换算 |
| 位置 / 偏移 `steps` | **步** | 比特偏移 = `steps × stride` |
| 参数长度 `bit_len` | **比特** | 零拷贝切片，调用方按比特解释 |

**对齐约束**：段比特长度与每个字面量比特长度都必须是步长的整数倍，否则报 `STRIDE_E_ALIGN`。
例如 `$'abc'` = 24 比特：步长 8 合法（3 步），步长 16 失败，步长 4 合法（6 步）。

---

## 三件彼此解绑的能力

解绑体现在**头文件与 API**，而不是目录：只 include 需要的头文件，就能独立使用对应能力。

| 能力 | 头文件 | 你能独立完成的事 | 完全不需要用到 |
|------|--------|-----------------|---------------|
| 编译 | `stride/compiler.h` | 词法分析、把模式编译成匹配序列 / 提取序列 | — |
| 匹配序列 | `stride/matcher.h` | 编译匹配序列、**用匹配序列匹配段** | 提取序列 |
| 提取序列 | `stride/extractor.h` | 编译提取序列、执行提取 | 匹配序列 |

`matcher.h` 与 `extractor.h` 只依赖共享的 `stride/types.h`，**互不依赖**；
`compiler.h` 在两者之上提供“一次编译出两个序列”的编排。

```
                types.h              （操作符 IR + 比特串 + 状态码）
               /   |   \
     matcher.h  extractor.h  compiler.h
```

---

## 文档状态

> **v2 概念重构已落地**：以「比特步长寻址 + 比特串比对」替代原先的
> 「按字节偏移 + 单字节字符」模型。`include/`、`src/`、`tests/`、`examples/`
> 与本文档命名一致；旧名对照见[「术语与命名对照」](#术语与命名对照)。

---

## 目录结构

```
Stride/
├── include/stride/
│   ├── stride.h        # 总入口（聚合全部公共头文件）
│   ├── types.h         # 共享类型：操作符 IR、比特串、状态码
│   ├── compiler.h      # 编译：词法分析 + 序列编译编排
│   ├── matcher.h       # 匹配序列：类型 + 编译 + 匹配
│   └── extractor.h     # 提取序列：类型 + 编译 + 执行
├── src/
│   ├── compiler.c      # 词法分析 + 编排
│   ├── matcher.c       # 匹配序列编译 + 匹配
│   └── extractor.c     # 提取序列编译（含优化）+ 执行
├── tests/
│   ├── test_compiler.c
│   ├── test_matcher.c      # 编译 + 匹配
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

    /* ① 匹配：用匹配序列判断段是否命中（步长 8 = 1 字节/步） */
    const char *segment = "v2.0";          /* 4 字节 = 32 比特 */
    if (stride_match_run(r.match, r.match_count, 8,
                         segment, STRIDE_BITS(4)) != 0) {
        printf("no match\n");
        stride_compile_free(&r);
        return 1;
    }

    /* ② 提取：命中后用提取序列取出参数（零拷贝，长度以比特计） */
    stride_extractor_t *ex =
        stride_extractor_create(r.extract, r.extract_count);

    stride_param_t params[8];
    size_t count = 0;
    if (stride_extractor_run(ex, 8, segment, STRIDE_BITS(4),
                             params, 8, &count) == 0) {
        for (size_t i = 0; i < count; i++)
            printf("[%zu] %.*s\n", i, (int)(params[i].bit_len / 8),
                   (const char *)params[i].ptr);
        /* 输出：[0] 2    [1] 0 */
    }

    stride_extractor_destroy(ex);
    stride_compile_free(&r);
    return 0;
}
```

只做匹配、不需要提取时，可以只编译匹配序列：

```c
stride_match_op_t *seq = NULL;
size_t n = 0, cap = 0;

if (stride_compile_match("$'user'", 0, 8, &seq, &n, &cap) != 0) { /* ... */ }
/* 注意：传入的是单个段模式 */
int hit = stride_match_run(seq, n, 8, "user", STRIDE_BITS(4));
stride_match_free(seq, n);
```

参数以 `stride_param_t { const void *ptr; size_t bit_len; }` 返回，**零拷贝**：
`ptr` 直接指向输入段内部，调用者只需保证输入在使用期间有效。

---

## 语法

段模式由以下操作符按书写顺序组成（共 10 个 `stride_op_type_t` 枚举值）：

| 类别 | 操作符 | 说明 |
|------|--------|------|
| 比对 | `$'比特串'` | 在当前步精确比对一段二进制 |
| 捕获 | `${步数}` | 捕获指定**步数**（`步数 × 步长` 比特） |
| 捕获 | `${'比特串'}` | 捕获到指定比特串前（不含该比特串） |
| 捕获 | `${}` | 捕获到段尾 |
| 移动 | `$[步位置]` | 绝对定位到第 N 步（基于 HEAD） |
| 移动 | `$[END]` / `$[END-n]` | 定位到段尾 / 从段尾向前 n 步 |
| 移动 | `$[>步数]` | 向段尾方向移动 n 步 |
| 移动 | `$[<步数]` | 向段首方向移动 n 步 |
| 移动 | `$[>'比特串']` | 向段尾方向查找比特串 |
| 移动 | `$[<'比特串']` | 向段首方向查找比特串 |

**位置与偏移的单位都是「步」；长度的单位都是「比特」。** 步长由 API 指定。

匹配成功需同时满足：**操作耗尽** 且 **游标恰好位于段尾**（段尾对齐，避免部分匹配）。

字面量是**任意二进制串**，支持 `\\`、`\'`、`\xNN` 转义；因此 `$'中文'`（UTF-8 源码）
即为 48 比特字面量，无需引擎理解编码。

完整语法、边界规则与示例见 [`doc/Stride 段模式语法规范.md`](doc/Stride%20段模式语法规范.md)。

---

## API 速览

### 编译（`stride/compiler.h`）

| 函数 | 说明 |
|------|------|
| `int stride_lex(const void *pattern, size_t pattern_len, stride_op_t **ops, size_t *n, size_t *cap)` | 词法分析：模式 → 操作符序列 |
| `void stride_ops_free(stride_op_t *ops, size_t n)` | 释放操作符序列（含各字面量的比特串副本） |
| `stride_compile_result_t stride_compile_ex(const void *pattern, size_t pattern_len, size_t stride)` | 一次编译出匹配序列与提取序列（可指定编译期步长用于校验） |
| `stride_compile_result_t stride_compile(const void *pattern)` | 便捷入口：`pattern_len = 0`、`stride = 0` |
| `void stride_compile_free(stride_compile_result_t *r)` | 释放编译结果 |
| `int stride_compile_match(const void *pattern, size_t pattern_len, size_t stride, stride_match_op_t **ops, size_t *n, size_t *cap)` | 只编译匹配序列 |
| `int stride_compile_extract(const void *pattern, size_t pattern_len, size_t stride, stride_extractor_op_t **ops, size_t *n, size_t *params)` | 只编译提取序列 |

`stride_compile_result_t` 字段：`status`、`match`/`match_count`、
`extract`/`extract_count`、`param_count`、`error_msg`、`error_pos`。

> `stride_lex` 的 `pattern_len` 为 0 时按 `'\0'` 结尾处理；非 0 时允许模式中含
> `'\0'`，用于二进制字面量。
>
> 携带字面量的操作符（`MATCH` / `CAPTURE_UNTIL` / `FIND_FWD` / `FIND_REV`）
> 拥有词法阶段**解码后**的比特串（`\\`、`\'`、`\xNN` 转义已展开），
> 由 `stride_ops_free(ops, count)` 统一释放；`pattern` 本身不要求继续有效。

### 匹配序列（`stride/matcher.h`）

| 函数 | 说明 |
|------|------|
| `int stride_match_compile(const stride_op_t *ops, size_t n, size_t stride, ...)` | 操作符序列 → 匹配序列 |
| `void stride_match_free(stride_match_op_t *seq, size_t n)` | 释放匹配序列（含比特串副本） |
| `int stride_match_run(const stride_match_op_t *seq, size_t n, size_t stride, const void *segment, size_t segment_bit_len)` | 匹配：0 命中 / -1 未命中 |
| `int stride_match_run_ex(..., stride_match_detail_t *out)` | 带诊断的匹配（失败元组下标、游标） |

匹配元组 6 种类型：`STRIDE_MT_STEP_FWD`、`STRIDE_MT_STEP_BACK`、
`STRIDE_MT_ABS_HEAD`、`STRIDE_MT_ABS_END`、
`STRIDE_MT_FIND_FWD`、`STRIDE_MT_FIND_REV`。

`steps` 统一为**非负步数**，方向由 `type` 表达：

| type | 含义 |
|------|------|
| `STEP_FWD` / `STEP_BACK` | 向段尾 / 段首移动的**步数** |
| `ABS_HEAD` | 目标 = 第 `steps` 步（`HEAD + steps`） |
| `ABS_END` | 目标 = `END − steps` 步（0 表示段尾） |
| `FIND_FWD` / `FIND_REV` | 要查找的**比特串**（`delimiter`，可为多比特） |

每个元组可附带一段**匹配字面量** `expect`：移动完成后在游标处比对它。

### 提取序列（`stride/extractor.h`）

| 函数 | 说明 |
|------|------|
| `int stride_extractor_compile(const stride_op_t *ops, size_t n, size_t stride, ...)` | 操作符序列 → 提取序列（含优化） |
| `void stride_extractor_free(stride_extractor_op_t *ops, size_t n)` | 释放提取操作数组（含比特串副本） |
| `stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops, size_t n)` | 创建单段提取器 |
| `void stride_extractor_destroy(stride_extractor_t *ex)` | 销毁单段提取器 |
| `int stride_extractor_run(const stride_extractor_t *ex, size_t stride, const void *segment, size_t segment_bit_len, stride_param_t *params, size_t cap, size_t *count)` | 在段上执行提取 |
| `stride_full_extractor_t *stride_full_extractor_create(stride_extractor_t **segs, size_t n)` | 组合多段提取器 |
| `void stride_full_extractor_destroy(stride_full_extractor_t *full)` | 销毁多段提取器（含各段） |
| `int stride_full_extractor_run(const stride_full_extractor_t *full, size_t stride, const void *const *segments, const size_t *seg_bit_lens, size_t n, stride_param_t *params, size_t cap, size_t *count)` | 多段提取，参数按段顺序连接 |

提取操作 10 种：`STRIDE_EX_CAPTURE_STEPS/UNTIL/END`（产生参数）、
`STRIDE_EX_SKIP_STEPS`、`STRIDE_EX_JUMP_ABS/END/FWD/BACK`、
`STRIDE_EX_FIND_FWD/REV`（不产生参数）。

---

## 编译与匹配模型

```
模式
   │  ① stride_lex()               词法分析
   ▼
操作符序列（stride_op_t）
   ├──② stride_match_compile()      → 匹配序列 ──③ stride_match_run()      → 命中 / 未命中
   └──④ stride_extractor_compile()  → 提取序列 ──⑤ stride_extractor_run()  → 参数
```

- **匹配序列编译**：IDLE / HOLD 两状态机。常量移动按「步」相加，字面量与当前 HOLD
  元组合并，查找操作打断合并。只保留匹配所需信息，丢弃捕获边界。
- **匹配序列匹配**：按顺序执行每个元组的移动；带字面量的元组在移动后的游标处
  比对并前进其步数；全部执行完毕后游标必须正好等于段尾。
- **提取序列编译**：两阶段。① 基础转换：`OP_MATCH → STRIDE_EX_SKIP_STEPS`
  （匹配阶段已验证字面量，提取阶段无需重复验证）；② 常量移动合并：连续的
  `EX_SKIP_STEPS`/`EX_JUMP_FWD`/`EX_JUMP_BACK` 相加抵消为一个操作。

状态转换表、相加/合并规则见 [`doc/Stride 状态机设计文档.md`](doc/Stride%20状态机设计文档.md)。

### 步长语义一览

设步长 `s`（比特/步）、段比特长度 `Lb`，则总步数 `N = Lb / s`（要求 `Lb % s == 0`）。

| 操作 | 语义 |
|------|------|
| `${n}` | 捕获 `n` 步 = `n·s` 比特 |
| `$[>n]` / `$[<n]` | 前 / 后 `n` 步 |
| `$[n]` | 定位到第 `n` 步 |
| `$[END]` / `$[END-n]` | `N` 步 / `N−n` 步 |
| `$'S'` | 在第 `pos` 步处比对 `S`（`bit_len` 比特），随后 `pos += bit_len / s` |
| `${'S'}` / `$[>'S']` / `$[<'S']` | 在**步对齐**位置查找比特串 `S` |
| 段尾对齐 | `pos == N`（即 `pos·s == Lb`） |

---

## 多比特与定长数据

因为引擎只认「步」和「比特串」，复杂编码与二进制结构都不需要额外支持。

### 例 1：UTF-8 中文（步长 8）

```c
/* 段 "用户：alice"（UTF-8），多比特长字面量直接写在模式里 */
stride_compile_result_t r = stride_compile_ex("$'用户：'${}", 0, 8);
/* $'用户：' 比对 72 比特（用户=48 + 全角冒号=24），${} 捕获 "alice" */
```

用多比特“定界串”查找：

```c
/* $[>'：'] 定位到全角冒号，$[>3] 跳过它（步长 8 时 3 步 = 24 比特），再捕获 */
stride_compile_result_t r = stride_compile_ex("$[>'：']$[>3]${}", 0, 8);
```

### 例 2：UTF-16（步长 16）

```c
/* 段 "中A" = AD 4E 41 00（32 比特 = 2 步），步长 16 */
const char seg[] = { (char)0xAD, 0x4E, 0x41, 0x00 };
stride_compile_result_t r = stride_compile_ex("${1}$'A\\x00'", 0, 16);
/* ${1} 捕获第 0 步 = "中"；$'A\x00' 比对第 1 步 */
```

全程只做 `pos × 16` 与比特串比对：没有代理对、没有字节序、没有 BOM 概念。

### 例 3：定长二进制记录（步长 = 记录位宽）

```c
/* 每条记录 4 字节 = 32 比特，步长 32 */
stride_compile_result_t r =
    stride_compile_ex("$'HDR1'$[>1]${1}", 0, 32);
/* 校验 32 比特头 → 跳 1 步（= 1 条记录）→ 捕获 1 步（= 32 比特） */
```

此时提取器就是一个定长结构体字段抽取器，参数零拷贝指回记录内部。

### 边界说明（属于模型本身，不是缺陷）

1. **“恰好 N 个字符”无法表达**：`${n}` 数的是**步**。变长编码下要按“字”切分，
   应改用 `${'定界串'}`（多比特长字面量）而非定步捕获。
2. **非自同步编码**：GBK / Big5 / Shift-JIS 的多字节序列可能匹配到另一字符的内部
   字节（这些编码的尾字节可落入 ASCII 或前导区间）。这是“不解释内容”的固有代价；
   UTF-8 与定宽编码无此问题。

---

## 术语与命名对照

v2 重构采用了贴合设计的新命名。下表用于把本文档与旧版接口对应起来。

### 概念

| 旧名 | 新名 | 说明 |
|------|------|------|
| 段（字符数组） | 段 / 数据段 | 不透明**二进制** + 比特长度 |
| — | **步长（stride）** | 每步比特数；核心原语 |
| — | **步（step）** | 游标单位；比特偏移 = 步 × 步长 |
| 字符 / 字节 | 比特 / 比特串 | 最小单位是比特；长度以比特计 |
| 关键字 | 匹配字面量（literal） | `$'...'` 的比特串 |
| 查找字符 | 定界字面量 | `${'...'}` / `$[>'...']` / `$[<'...']` 的比特串 |
| 特征序列 | **匹配序列（match sequence）** | 匹配阶段产物 |
| 提取序列 | 提取序列（extract sequence） | 提取阶段产物 |
| 特征元组 | 匹配元组 | 匹配序列中的一条 |
| 定长捕获 | 定步捕获 | `${n}` 捕获 n 步 |

### 文件与标识符

| 旧 | 新 |
|----|----|
| `include/stride/feature.h` | `include/stride/matcher.h` |
| `src/feature.c` | `src/matcher.c` |
| `tests/test_feature.c` | `tests/test_matcher.c` |
| `stride_feature_t` | `stride_match_op_t` |
| `stride_feature_type_t` | `stride_match_type_t` |
| `STRIDE_FT_*` | `STRIDE_MT_*` |
| `stride_feature_compile` | `stride_match_compile` |
| `stride_feature_free` | `stride_match_free` |
| `stride_feature_match` | `stride_match_run` |
| `stride_feature_match_ex` | `stride_match_run_ex` |
| `stride_compile_features` | `stride_compile_match` |
| `stride_compile_extractors` | `stride_compile_extract` |
| `stride_extractor_execute` | `stride_extractor_run` |
| `stride_ops_free(ops)` | `stride_ops_free(ops, count)`（字面量需逐个释放） |
| `free(extractors)` | `stride_extractor_free(ops, count)` |
| `result.features` / `feature_count` | `result.match` / `match_count` |
| `result.extractors` / `extractor_count` | `result.extract` / `extract_count` |
| `keyword` / `keyword_len` | `expect`（`stride_blob_t`，`bit_len`） |
| `data.find.ch`（`char`） | `data.literal`（`stride_blob_t`，`const void *data`） |
| `stride_blob_t.bytes`（`char *`） | `stride_blob_t.data`（**`const void *`**） |
| `len`（字面量 / 参数长度） | `bit_len`（**比特**） |
| `segment_len`（字节） | `segment_bit_len`（**比特**） |
| `stride_param_t.ptr`（`char *`） | `stride_param_t.ptr`（**`const void *`**） |
| `data.length`（捕获长度） | `data.steps` |
| `STRIDE_OP_CAPTURE_LEN` | `STRIDE_OP_CAPTURE_STEPS` |
| `STRIDE_OP_CAPTURE_CHR` | `STRIDE_OP_CAPTURE_UNTIL` |
| `STRIDE_EX_CAPTURE_LEN` | `STRIDE_EX_CAPTURE_STEPS` |
| `STRIDE_EX_CAPTURE_CHR` | `STRIDE_EX_CAPTURE_UNTIL` |
| `STRIDE_EX_SKIP_LEN` | `STRIDE_EX_SKIP_STEPS` |
| — | `STRIDE_BITS(nbytes)`（字节数 → 比特数） |
| — | `STRIDE_E_ALIGN`（新）：段长或字面量长度不是步长整数倍 |

---

## 测试

```bash
make test
```

| 测试 | 覆盖内容 |
|------|---------|
| `tests/test_compiler.c` | 10 种操作符的词法分析、转义、语法错误、一步编译、只编译单序列 |
| `tests/test_matcher.c` | 常量相加、查找打断、字面量合并、元组值语义；**匹配**（含诊断与边界） |
| `tests/test_extractor.c` | 匹配优化、常量合并、抵消、对象生命周期；运行时提取与零拷贝 |

覆盖的 v2 要点：多比特长字面量（UTF-8）比对/查找/定界、步长 1/4/8/16/32 的定宽数据、
段长与字面量的对齐报错（`STRIDE_E_ALIGN`）、含 `\0` 的二进制模式、`\\`/`\'`/`\xNN` 转义、
以及非字节对齐步长的比特级比较。当前共 336 条断言全部通过。

---

## 工具链

生成 `compile_commands.json` 供 clangd / IDE 使用（该文件已被 `.gitignore` 忽略）：

```bash
make compile-commands      # 等价于 bear -- make clean all
```

---

## 设计文档

- [Stride 段模式语法规范](doc/Stride%20段模式语法规范.md) —— 操作符语法、步长与步、边界检查、完整示例
- [Stride 匹配序列设计文档](doc/Stride%20匹配序列设计文档.md) —— 六种元组、步长寻址、比特串比对、匹配算法
- [Stride 编译器设计文档](doc/Stride%20编译器设计文档.md) —— 词法表、操作符 IR、编译编排、数据结构、状态码
- [Stride 状态机设计文档](doc/Stride%20状态机设计文档.md) —— 匹配序列 IDLE/HOLD、提取序列常量合并、编译示例

---

## 许可证

本项目采用 MIT 许可证，见 [LICENSE](LICENSE)。
