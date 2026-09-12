# Stride

**Stride** 是一个用 C99 编写的**步进式比特串匹配 / 提取库**，零外部依赖。

> **名字即原语**：`stride` = **步长**，即每一步跨越多少**比特**。
> Stride 把输入看作**不透明二进制**：游标以「步」计数，比特偏移 = `步 × 步长`；
> 比对与查找的目标是任意长度的**比特串**。引擎不解释任何编码。

**Stride 不含语法，也不含编译器。** 把 `$'…'`、`${…}`、`$[…]` 这类模式翻译成
序列构建函数调用，是调用方（例如 [URLRouter](../URLRouter)）的职责。

---

## 两个原语

| 原语 | 回答的问题 | 说明 |
|------|-----------|------|
| **步长 stride** | 指针怎么走 | 每步比特数（`≥1`）；比特偏移 = `步 × 步长` |
| **比特串 blob** | 比什么 | `stride_blob_t { const void *data; size_t bit_len; }` |

于是同一套机制可以：步长 8 处理字节流（UTF-8 / 任意二进制）、步长 4 处理半字节、
步长 1 处理位域、步长 16/32 处理 UTF-16/32 或定长记录；
用多比特字面量匹配中文等多字节内容。

| 量 | 单位 |
|----|------|
| 步长 `stride` | 比特/步 |
| 段长 `segment_bit_len` | 比特（`= 8 × 字节数`） |
| 字面量长度 `bit_len` | 比特，可用 `STRIDE_BITS(nbytes)` 换算 |
| 位置 / 偏移 `steps` | 步 |
| 参数长度 `bit_len` | 比特 |

段比特长度必须是步长的整数倍，否则执行失败。

---

## 核心结构：单链表 + 尾部合并

序列是**单链表**，每个节点表达「**先偏移、再执行动作**」：

```c
typedef struct stride_step {
    stride_move_t move;        /* STEP_FWD/BACK、ABS_HEAD/END、SKIP_BITS、FIND_FWD/REV */
    size_t        move_value;  /* 步数（或 SKIP_BITS 的比特数） */
    stride_blob_t move_target; /* FIND_* 的查找目标 */

    stride_act_t  act;         /* COMPARE、CAPTURE_STEPS/UNTIL/END */
    stride_blob_t act_target;  /* COMPARE / CAPTURE_UNTIL 的目标 */
    size_t        act_value;   /* CAPTURE_STEPS 的步数 */

    struct stride_step *next;
} stride_step_t;
```

构建是**函数式**的：每次调用把一个新的偏移或动作追加到**尾节点**，
**能与尾节点合并就地合并**，否则新建尾节点。没有状态机。

合并规则（同单位常量相加）：

| 已有 \ 新增 | STEP_FWD | STEP_BACK | SKIP_BITS | FIND_* |
|---|---|---|---|---|
| `STEP_FWD` | 相加 | 抵消（可为负→BACK） | 不合并（单位不同） | 不合并 |
| `STEP_BACK` | 抵消 | 相加 | 不合并 | 不合并 |
| `ABS_HEAD` | 相加 | 非负时相加 | 不合并 | 不合并 |
| `ABS_END` | 不合并 | 相加 | 不合并 | 不合并 |
| `SKIP_BITS` | 不合并 | 不合并 | 相加 | 不合并 |

动作（`COMPARE` / `CAPTURE_*`）在尾节点尚无动作时**绑定到尾节点**
（例如把「比对 `v`」绑到刚追加的偏移上），否则新建尾节点。

---

## 通用执行引擎

匹配序列与提取序列是**同一结构**的不同用法，共用 `stride_seq_run()`：

- **匹配**：只加偏移 + `COMPARE`，执行时不带参数缓冲；
- **提取**：只加偏移 + 捕获动作，执行时带参数缓冲。

执行过程：从 `head` 起逐节点「偏移 → 动作」，最后要求游标**恰好落在段尾**。

```
匹配：  stride_match_run(seq, stride, segment, segment_bit_len)
提取：  stride_extract_run(seq, stride, segment, segment_bit_len, params, cap, &count)
通用：  stride_seq_run(seq, stride, segment, segment_bit_len, params, cap, &count)
```

---

## 快速开始

```bash
make                     # 构建 build/libstride.a
make test                # 运行测试
make example && make run # 运行示例
make compile-commands    # bear -- make clean all，生成 compile_commands.json
```

手工翻译 `$'v'${'.'}$'.'${}`（步长 8）并匹配 `"v2.0"`：

```c
#include "stride/stride.h"

stride_blob_t b(const char *s) {
    stride_blob_t x = { s, STRIDE_BITS(strlen(s)) };
    return x;
}

stride_seq_t *m = stride_seq_new();
stride_blob_t v = b("v"), dot = b(".");

stride_seq_compare(m, &v);     /* $'v'  */
stride_seq_find_fwd(m, &dot);  /* ${'.'} 匹配阶段 = 查找 */
stride_seq_compare(m, &dot);   /* $'.'  绑到上一节点 */
stride_seq_abs_end(m, 0);      /* ${}   */

int hit = stride_match_run(m, 8, "v2.0", STRIDE_BITS(4)); /* 0 = 命中 */
stride_seq_free(m);
```

提取 `2024-03-15`：

```c
stride_seq_t *e = stride_seq_new();
stride_seq_capture_steps(e, 4);
stride_seq_skip_bits(e, STRIDE_BITS(1));
stride_seq_capture_steps(e, 2);
stride_seq_skip_bits(e, STRIDE_BITS(1));
stride_seq_capture_steps(e, 2);

stride_param_t p[8];
size_t n = 0;
stride_extract_run(e, 8, "2024-03-15", STRIDE_BITS(10), p, 8, &n);
/* p[0]="2024" p[1]="03" p[2]="15"（零拷贝，指向输入内部）*/
stride_seq_free(e);
```

参数以 `stride_param_t { const void *ptr; size_t bit_len; }` 返回，**零拷贝**；
起始位置必须字节对齐（比特偏移为 8 的整数倍），否则捕获失败。

---

## API 速览

| 头文件 | 内容 |
|--------|------|
| `stride/types.h` | `stride_blob_t`、`stride_param_t`、`stride_step_t`、`stride_seq_t`、状态码 |
| `stride/sequence.h` | `stride_seq_new/free/clear/count/param_count`；构建函数；`stride_seq_run` |
| `stride/matcher.h` | `stride_match_run` |
| `stride/extractor.h` | `stride_extract_run`、`stride_full_extractor_*`（多段） |

构建函数：

| 偏移 | 动作 |
|------|------|
| `stride_seq_step_fwd(seq, n)` | `stride_seq_compare(seq, &literal)` |
| `stride_seq_step_back(seq, n)` | `stride_seq_capture_steps(seq, n)` |
| `stride_seq_abs_head(seq, n)` | `stride_seq_capture_until(seq, &delim)` |
| `stride_seq_abs_end(seq, n)` | `stride_seq_capture_end(seq)` |
| `stride_seq_skip_bits(seq, bits)` | |
| `stride_seq_find_fwd/rev(seq, &target)` | |

`stride_seq_run` 返回 `0` 成功、负数失败（`-(i+1)` 表示第 `i` 个节点失败，
`-(count+1)` 表示段尾未对齐）。

---

## 目录结构

```
Stride/
├── include/stride/{types,sequence,matcher,extractor,stride}.h
├── src/{sequence,matcher,extractor}.c
├── tests/{test_sequence,test_extractor}.c
├── examples/example.c
├── doc/                 # 设计文档
├── Makefile
└── README.md
```

---

## 与 URLRouter 的分工

| | Stride | URLRouter |
|---|---|---|
| 语法（`$''` / `${}` / `$[]` 词法） | — | ✅ |
| 模式 → 序列的翻译（编译器） | — | ✅ |
| 序列构建（函数 + 尾部合并） | ✅ | 调用方 |
| 匹配 / 提取执行引擎 | ✅ | 调用方 |
| 路由树、特征序列合并、优先级 | — | ✅ |

---

## 许可证

MIT，见 [LICENSE](LICENSE)。
