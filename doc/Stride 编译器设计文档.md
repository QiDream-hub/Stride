# Stride 编译器设计文档

**文档版本**：1.0
**更新日期**：2026-09-04
**适用模块**：`stride/grammar.h`（词法分析）、`stride/compiler.h`（序列编译）、`stride/feature.h`（特征序列）、`stride/extractor.h`（提取序列）
**内容来源**：URLRouter 2.2 编译器设计

---

> **输入模型说明（重要）**
>
> Stride 是序列模式编译器（sequence-pattern compiler）子系统，只编译**单个输入单元（段 / segment）**的模式。段是一段不透明的字符数组。
>
> 按 `/` 等分隔符切分输入是**调用方**的职责，不属于 Stride。Stride 内部不存在路由器、路由树、HTTP 方法、路由匹配、回调等概念；编译器只做"给定一个段模式字符串，产出该段的特征序列与提取序列"这一件事。多个段由调用方分别编译、分别执行。
>
> 本文档示例中的复合模式（如 `/$'api'/$'v'${'.'}$'.'${}`）只为便于阅读，`/` 是调用方切分边界；文中凡涉及状态机演示处，均已明确标注实际处理的单段子模式。
>
> **模块归属**：编译器模块同时承担**词法分析（grammar / lexer）**职责。`stride/grammar.h` 提供模式字符串 → 操作符序列的词法分析接口，`stride/compiler.h` 在其之上提供两阶段编译入口。

---

## 一、概述

编译器将**单个段**的模式字符串转换为两个独立的数据结构：

1. **特征序列**：用于匹配阶段，包含移动操作和关键字
2. **提取序列**：用于参数提取阶段，保留完整的捕获语义

```
段模式字符串 → 词法分析 → 操作符序列 → 特征序列编译 → 特征序列
                              ↓
                        提取序列编译 → 提取操作序列（经编译时优化）
```

两个阶段的产物彼此独立：调用方既可以使用特征序列做纯匹配（不关心捕获），也可以使用提取序列在匹配成功之后做参数提取。

---

## 二、词法分析

### 2.1 操作符识别规则

词法分析器将段模式字符串切分为操作符令牌序列。

| 操作符 | 正则模式 | 示例 |
|--------|---------|------|
| 精确匹配 | `\$'[^']*'` | `$'user'` |
| 定长捕获 | `\$\{[\d]+\}` | `${4}` |
| 捕获到字符 | `\$\{'[^']'\}` | `${'='}` |
| 捕获到结尾 | `\$\{\}` | `${}` |
| 绝对跳转 | `\$\[[\d]+\]` | `$[5]` |
| 绝对跳转(END) | `\$\[END(?:-[\d]+)?\]` | `$[END]`、`$[END-4]` |
| 正向移动 | `\$\[>[\d]+\]` | `$[>3]` |
| 负向移动 | `\$\[<[\d]+\]` | `$[<2]` |
| 正向查找 | `\$\[>'[^']'\]` | `$[>'=']` |
| 负向查找 | `\$\[<'[^']'\]` | `$[<'=']` |

### 2.2 错误处理

| 错误类型 | 示例 | 处理 |
|---------|------|------|
| 未闭合引号 | `$'user` | 编译失败 |
| 无效数字 | `${abc}` | 编译失败 |
| 无效位置 | `$[abc]` | 编译失败 |
| 空模式 | `""` | 编译失败（`STRIDE_E_EMPTY_SEGMENT`） |
| 非法字符（含分隔符 `/`） | `/user`、`user/profile` | 编译失败（Stride 只编译单个段，切分由调用方完成） |

---

## 三、操作符序列

### 3.1 操作符类型定义

```c
/* stride/core.h */

typedef enum {
    STRIDE_OP_MATCH = 0,      /* 精确匹配 $'text' */
    STRIDE_OP_CAPTURE_LEN,    /* 定长捕获 ${n} */
    STRIDE_OP_CAPTURE_CHR,    /* 捕获到字符 ${'c'} */
    STRIDE_OP_CAPTURE_END,    /* 捕获到结尾 ${} */
    STRIDE_OP_JUMP_ABS,       /* 绝对跳转 $[n] */
    STRIDE_OP_JUMP_END,       /* END跳转 $[END] / $[END-n] */
    STRIDE_OP_JUMP_FWD,       /* 正向移动 $[>n] */
    STRIDE_OP_JUMP_BACK,      /* 负向移动 $[<n] */
    STRIDE_OP_FIND_FWD,       /* 正向查找 $[>'c'] */
    STRIDE_OP_FIND_REV        /* 负向查找 $[<'c'] */
} stride_op_type_t;

typedef struct {
    stride_op_type_t type;
    union {
        struct { const char *text; size_t len; } match;  /* STRIDE_OP_MATCH */
        size_t length;                                    /* STRIDE_OP_CAPTURE_LEN, STRIDE_OP_JUMP_* */
        size_t pos;                                       /* STRIDE_OP_JUMP_ABS */
        size_t offset;                                    /* STRIDE_OP_JUMP_FWD / STRIDE_OP_JUMP_BACK */
        struct { char ch; } find;                         /* STRIDE_OP_CAPTURE_CHR, STRIDE_OP_FIND_* */
        struct { int is_end; int offset; } jump_end;      /* STRIDE_OP_JUMP_END */
    } data;
} stride_op_t;
```

**所有权说明**：`STRIDE_OP_MATCH` 的 `data.match.text` 指向调用方传入的 `pattern` 字符串内部，词法分析不复制文本；操作符数组由调用方通过 `stride_ops_free()` 释放。

### 3.2 编译示例

```
输入: /$'user'/${}
      （以 '/' 为调用方切分边界）

段0: $'user'   → STRIDE_OP_MATCH, text="user"
段1: ${}       → STRIDE_OP_CAPTURE_END
```

---

## 四、特征序列编译

### 4.1 状态机

特征序列编译采用两状态状态机，对**单个段**的操作符序列做一次线性扫描（多个段由调用方分别编译，或按段循环调用）。

#### 状态定义

| 状态 | 含义 |
|------|------|
| **IDLE** | 空闲，无持有操作 |
| **HOLD** | 持有一个移动元组，等待合并或输出 |

#### 操作分类

| 类别 | 操作符 | 事件类型 | 说明 |
|------|--------|---------|------|
| 常量操作 | `STRIDE_OP_CAPTURE_LEN`、`STRIDE_OP_JUMP_FWD` | `CONST_POS` | 正向常量移动 |
| 常量操作 | `STRIDE_OP_JUMP_BACK` | `CONST_NEG` | 负向常量移动 |
| 常量操作 | `STRIDE_OP_JUMP_ABS` | `CONST_ABS_HEAD` | 绝对常量移动（基于 HEAD） |
| 常量操作 | `STRIDE_OP_JUMP_END` | `CONST_ABS_END` | 绝对常量移动（基于 END） |
| 动态操作 | `STRIDE_OP_CAPTURE_CHR`、`STRIDE_OP_FIND_FWD` | `DYNAMIC_FIND_FWD` | 动态正向查找 |
| 动态操作 | `STRIDE_OP_FIND_REV` | `DYNAMIC_FIND_REV` | 动态反向查找 |
| 关键字 | `STRIDE_OP_MATCH` | `KEYWORD` | 关键字匹配 |

#### 基础元组映射

| 操作符 | 基础元组 |
|--------|---------|
| `STRIDE_OP_MATCH` | `(0, text)` |
| `STRIDE_OP_CAPTURE_LEN` | `(length, NULL)` |
| `STRIDE_OP_JUMP_FWD` | `(offset, NULL)` |
| `STRIDE_OP_JUMP_BACK` | `(-offset, NULL)` |
| `STRIDE_OP_JUMP_ABS` | `(HEAD+n, NULL)` |
| `STRIDE_OP_JUMP_END`（END） | `(END, NULL)` |
| `STRIDE_OP_JUMP_END`（END-n） | `(END-n, NULL)` |
| `STRIDE_OP_CAPTURE_CHR` | `(ch, NULL)` |
| `STRIDE_OP_FIND_FWD` | `(ch, NULL)` |
| `STRIDE_OP_FIND_REV` | `('<', ch, NULL)` |

#### 状态转换表

| 当前状态 | 事件 | 动作 | 下一状态 |
|---------|------|------|---------|
| IDLE | 常量操作 | 持有 = 基础元组 | HOLD |
| IDLE | 动态操作 | 持有 = 基础元组 | HOLD |
| IDLE | 关键字 | 输出 `(0, kw)` | IDLE |
| HOLD | 常量操作 | 若可相加：持有 += 常量值<br>否则：输出持有，持有 = 新元组 | HOLD |
| HOLD | 动态操作 | 输出持有，持有 = 动态元组 | HOLD |
| HOLD | 关键字 | 输出 (持有值, kw)，清空持有 | IDLE |
| 扫描结束 | — | 若在 HOLD：输出持有 | IDLE |

#### 常量相加规则

常量操作可以数值相加，但需满足基准兼容性：

| 持有类型 | 可相加事件 | 结果 | 约束 |
|---------|-----------|------|------|
| `(n, NULL)` | `CONST_POS` | `(n+m, NULL)` | 无 |
| `(n, NULL)` | `CONST_NEG` | `(n-m, NULL)` | 结果可为负 |
| `(-n, NULL)` | `CONST_POS` | `(-n+m, NULL)` | 无 |
| `(-n, NULL)` | `CONST_NEG` | `(-n-m, NULL)` | 无 |
| `(HEAD+n, NULL)` | `CONST_POS` | `(HEAD+n+m, NULL)` | 结果 `n+m ≥ 0` |
| `(HEAD+n, NULL)` | `CONST_NEG` | `(HEAD+n-m, NULL)` | 结果 `n-m ≥ 0` |
| `(END-n, NULL)` | `CONST_NEG` | `(END-n-m, NULL)` | 结果 `n+m ≥ 0` |
| `(END-n, NULL)` | `CONST_POS` | **无效** | END 不能加正数 |
| 动态操作 | 任何 | **不可相加** | 先输出再处理 |
| 不同基准 | 任何 | **不可相加** | HEAD 与 END 不能混用 |

#### 关键字合并规则

关键字与当前 HOLD 元组合并，输出 `(元组值, kw)`：

| 持有类型 | 合并结果 |
|---------|---------|
| `(n, NULL)` | `(n, kw)` |
| `(-n, NULL)` | `(-n, kw)` |
| `(HEAD+n, NULL)` | `(HEAD+n, kw)` |
| `(END-n, NULL)` | `(END-n, kw)` |
| `(ch, NULL)` | `(ch, kw)` |
| `('<', ch, NULL)` | `('<', ch, kw)` |

### 4.2 编译示例

以下示例均为**单个段**的操作符序列。

#### 示例1：常量操作相加

```
输入：${1}${1}${1}$'key'

操作符序列：STRIDE_OP_CAPTURE_LEN(1), STRIDE_OP_CAPTURE_LEN(1), STRIDE_OP_CAPTURE_LEN(1), STRIDE_OP_MATCH("key")

状态机：
  IDLE + CONST_POS(1) → HOLD(1, NULL)
  HOLD + CONST_POS(1) → HOLD(2, NULL)
  HOLD + CONST_POS(1) → HOLD(3, NULL)
  HOLD + KEYWORD("key") → 输出 (3, "key") → IDLE

输出：[(3, "key")]
```

#### 示例2：动态操作打断

```
输入：${2}${'a'}${3}$'b'

操作符序列：STRIDE_OP_CAPTURE_LEN(2), STRIDE_OP_CAPTURE_CHR('a'), STRIDE_OP_CAPTURE_LEN(3), STRIDE_OP_MATCH("b")

状态机：
  IDLE + CONST_POS(2) → HOLD(2, NULL)
  HOLD + DYNAMIC('a') → 输出 (2, NULL)，HOLD('a', NULL)
  HOLD + CONST_POS(3) → 输出 ('a', NULL)，HOLD(3, NULL)
  HOLD + KEYWORD("b") → 输出 (3, "b") → IDLE

输出：[(2, NULL), ('a', NULL), (3, "b")]
```

#### 示例3：绝对移动与常量相加

```
输入：$[5]${2}$'key'

操作符序列：STRIDE_OP_JUMP_ABS(5), STRIDE_OP_CAPTURE_LEN(2), STRIDE_OP_MATCH("key")

状态机：
  IDLE + CONST_ABS_HEAD(5) → HOLD(HEAD+5, NULL)
  HOLD + CONST_POS(2) → HOLD(HEAD+7, NULL)
  HOLD + KEYWORD("key") → 输出 (HEAD+7, "key") → IDLE

输出：[(HEAD+7, "key")]
```

#### 示例4：END 合并

```
输入：${}$[<4]$'dddd'

操作符序列：STRIDE_OP_CAPTURE_END, STRIDE_OP_JUMP_BACK(4), STRIDE_OP_MATCH("dddd")

状态机：
  IDLE + CONST_ABS_END(END) → HOLD(END, NULL)
  HOLD + CONST_NEG(4) → HOLD(END-4, NULL)
  HOLD + KEYWORD("dddd") → 输出 (END-4, "dddd") → IDLE

输出：[(END-4, "dddd")]
```

#### 示例5：动态操作与关键字合并

```
输入：${'a'}$'key'

操作符序列：STRIDE_OP_CAPTURE_CHR('a'), STRIDE_OP_MATCH("key")

状态机：
  IDLE + DYNAMIC('a') → HOLD('a', NULL)
  HOLD + KEYWORD("key") → 输出 ('a', "key") → IDLE

输出：[('a', "key")]
```

#### 示例6：混合场景

```
输入：${2}${'a'}${3}$'b'

输出：[(2, NULL), ('a', NULL), (3, "b")]
```

#### 示例7：连续关键字（不推荐）

```
输入：$'dd'$'aaa'

操作符序列：STRIDE_OP_MATCH("dd"), STRIDE_OP_MATCH("aaa")

状态机：
  IDLE + KEYWORD("dd") → 输出 (0, "dd") → IDLE
  IDLE + KEYWORD("aaa") → 输出 (0, "aaa") → IDLE

输出：[(0, "dd"), (0, "aaa")]
```

**建议**：用户应主动合并相邻关键字为 `$'ddaaa'`，以获得更优性能。

#### 示例8：END 位置约束（报错）

```
输入：$[END]${'a'}

操作符序列：STRIDE_OP_JUMP_END(END), STRIDE_OP_CAPTURE_CHR('a')

状态机：
  IDLE + CONST_ABS_END(END) → HOLD(END, NULL)
  HOLD + DYNAMIC('a') → 尝试输出 (END, NULL) 然后 HOLD('a', NULL)
  
  ❌ 错误：纯 END 元组后不能有其他操作（STRIDE_E_END_CONFLICT）
```

---

## 五、提取序列编译

### 5.1 设计原则

提取序列保留操作语义用于参数提取，并经过编译时优化：

- **保留捕获边界**：每个捕获操作独立存在
- **匹配操作优化**：匹配操作转换为常量偏移跳过（无需重复验证）
- **常量移动合并**：连续的常量移动操作合并为一个
- **保留动态操作**：查找操作无法在编译时优化，保持原样

### 5.2 提取操作类型定义

```c
/* stride/extractor.h */

typedef enum {
    /* 捕获操作（产生参数） */
    STRIDE_EX_CAPTURE_LEN,  /* 定长捕获 */
    STRIDE_EX_CAPTURE_CHR,  /* 捕获到字符 */
    STRIDE_EX_CAPTURE_END,  /* 捕获到结尾 */
    
    /* 移动操作（不产生参数） */
    STRIDE_EX_SKIP_LEN,     /* 跳过固定长度（由 STRIDE_OP_MATCH 优化而来） */
    STRIDE_EX_JUMP_ABS,     /* 绝对跳转 */
    STRIDE_EX_JUMP_END,     /* END 跳转 */
    STRIDE_EX_JUMP_FWD,     /* 正向移动 */
    STRIDE_EX_JUMP_BACK,    /* 负向移动 */
    STRIDE_EX_FIND_FWD,     /* 正向查找 */
    STRIDE_EX_FIND_REV      /* 反向查找 */
} stride_extractor_op_type_t;

typedef struct {
    stride_extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;      /* STRIDE_EX_CAPTURE_LEN */
        struct { char ch; } capture_chr;            /* STRIDE_EX_CAPTURE_CHR */
        struct { size_t length; } skip_len;         /* STRIDE_EX_SKIP_LEN */
        struct { size_t pos; } jump_abs;            /* STRIDE_EX_JUMP_ABS */
        struct { int is_end; int offset; } jump_end;/* STRIDE_EX_JUMP_END */
        struct { size_t offset; } jump_fwd;         /* STRIDE_EX_JUMP_FWD */
        struct { size_t offset; } jump_back;        /* STRIDE_EX_JUMP_BACK */
        struct { char ch; } find_fwd;               /* STRIDE_EX_FIND_FWD */
        struct { char ch; } find_rev;               /* STRIDE_EX_FIND_REV */
    } data;
} stride_extractor_op_t;
```

### 5.3 基础转换规则

| 操作符 | 基础提取操作 | 产生参数 | 说明 |
|--------|------------|---------|------|
| `STRIDE_OP_MATCH` | `STRIDE_EX_SKIP_LEN` | 否 | 优化为常量偏移，偏移量 = 文本长度 |
| `STRIDE_OP_CAPTURE_LEN` | `STRIDE_EX_CAPTURE_LEN` | 是 | 保持不变 |
| `STRIDE_OP_CAPTURE_CHR` | `STRIDE_EX_CAPTURE_CHR` | 是 | 保持不变 |
| `STRIDE_OP_CAPTURE_END` | `STRIDE_EX_CAPTURE_END` | 是 | 保持不变 |
| `STRIDE_OP_JUMP_ABS` | `STRIDE_EX_JUMP_ABS` | 否 | 保持不变 |
| `STRIDE_OP_JUMP_END` | `STRIDE_EX_JUMP_END` | 否 | 保持不变 |
| `STRIDE_OP_JUMP_FWD` | `STRIDE_EX_JUMP_FWD` | 否 | 保持不变 |
| `STRIDE_OP_JUMP_BACK` | `STRIDE_EX_JUMP_BACK` | 否 | 保持不变 |
| `STRIDE_OP_FIND_FWD` | `STRIDE_EX_FIND_FWD` | 否 | 保持不变 |
| `STRIDE_OP_FIND_REV` | `STRIDE_EX_FIND_REV` | 否 | 保持不变 |

### 5.4 编译时优化

提取序列在基础转换后执行两项优化：匹配关键字转常量偏移、常量移动合并。

#### 5.4.1 匹配关键字 → 常量偏移

由于匹配阶段已验证所有关键字的正确性，提取阶段无需重复验证。

| 原转换 | 优化后转换 |
|--------|-----------|
| `STRIDE_OP_MATCH` → `STRIDE_EX_SKIP_MATCH` | `STRIDE_OP_MATCH` → `STRIDE_EX_SKIP_LEN`（偏移量 = 文本长度） |

优化后提取序列不再包含 `STRIDE_EX_SKIP_MATCH` 类型，匹配操作全部转换为常量偏移跳过。

#### 5.4.2 常量移动合并

连续的常量移动操作（不产生参数）在编译时合并为一个操作。

**可合并的操作**：
- `STRIDE_EX_SKIP_LEN`
- `STRIDE_EX_JUMP_FWD`
- `STRIDE_EX_JUMP_BACK`

**合并规则**：
1. 同类操作直接相加：`STRIDE_EX_JUMP_FWD(a) + STRIDE_EX_JUMP_FWD(b) = STRIDE_EX_JUMP_FWD(a+b)`
2. 正向与负向抵消：`STRIDE_EX_JUMP_FWD(a) + STRIDE_EX_JUMP_BACK(b) = STRIDE_EX_JUMP_FWD(a-b)`（结果可能为负，转换为 `STRIDE_EX_JUMP_BACK`）
3. `STRIDE_EX_SKIP_LEN` 视为正向移动，可与 `STRIDE_EX_JUMP_FWD`、`STRIDE_EX_JUMP_BACK` 合并

**打断条件**：
- 遇到产生参数的操作（`STRIDE_EX_CAPTURE_*`）
- 遇到动态操作（`STRIDE_EX_FIND_FWD`、`STRIDE_EX_FIND_REV`）
- 遇到绝对跳转（`STRIDE_EX_JUMP_ABS`、`STRIDE_EX_JUMP_END`）

**合并示例**：
```
输入序列：STRIDE_EX_JUMP_FWD(3), STRIDE_EX_JUMP_BACK(1), STRIDE_EX_SKIP_LEN(4)
合并后：STRIDE_EX_JUMP_FWD(6)  // 3 - 1 + 4 = 6
```

#### 5.4.3 优化流程

提取序列编译采用两阶段处理：

1. **基础转换**：将操作符序列按 5.3 规则转换为提取操作序列（暂不输出）
2. **优化合并**：遍历提取操作序列，按 5.4.2 规则合并连续常量移动

```
操作符序列 → 基础转换 → 临时序列 → 常量合并 → 最终提取序列
```

### 5.5 编译示例

以下示例均为**单个段**的操作符序列。

#### 示例1：基础捕获（含匹配优化）

```
输入：${4}$'a'

操作符序列：
  [0] STRIDE_OP_CAPTURE_LEN, length=4
  [1] STRIDE_OP_MATCH, text="a"

基础转换：
  [0] STRIDE_EX_CAPTURE_LEN, length=4
  [1] STRIDE_EX_SKIP_LEN, length=1   // STRIDE_OP_MATCH 优化为 STRIDE_EX_SKIP_LEN

优化合并：无可合并的连续常量移动

最终提取序列：
  [0] STRIDE_EX_CAPTURE_LEN, length=4  (参数1)
  [1] STRIDE_EX_SKIP_LEN, length=1     (不产生参数)

参数数量：1
```

#### 示例2：捕获到字符（无优化）

```
输入：${'='}$'='${}

操作符序列：
  [0] STRIDE_OP_CAPTURE_CHR, ch='='
  [1] STRIDE_OP_MATCH, text="="
  [2] STRIDE_OP_CAPTURE_END

基础转换：
  [0] STRIDE_EX_CAPTURE_CHR, ch='='
  [1] STRIDE_EX_SKIP_LEN, length=1
  [2] STRIDE_EX_CAPTURE_END

优化合并：无可合并的连续常量移动（捕获操作打断）

最终提取序列：
  [0] STRIDE_EX_CAPTURE_CHR, ch='='     (参数1)
  [1] STRIDE_EX_SKIP_LEN, length=1      (不产生参数)
  [2] STRIDE_EX_CAPTURE_END             (参数2)

参数数量：2
```

#### 示例3：常量移动合并

```
输入：${2}$[>3]$'abc'

操作符序列：
  [0] STRIDE_OP_CAPTURE_LEN, length=2
  [1] STRIDE_OP_JUMP_FWD, offset=3
  [2] STRIDE_OP_MATCH, text="abc"

基础转换：
  [0] STRIDE_EX_CAPTURE_LEN, length=2
  [1] STRIDE_EX_JUMP_FWD, offset=3
  [2] STRIDE_EX_SKIP_LEN, length=3

优化合并：STRIDE_EX_JUMP_FWD(3) + STRIDE_EX_SKIP_LEN(3) = STRIDE_EX_JUMP_FWD(6)

最终提取序列：
  [0] STRIDE_EX_CAPTURE_LEN, length=2  (参数1)
  [1] STRIDE_EX_JUMP_FWD, offset=6     (不产生参数)

参数数量：1
运行时操作数：从 3 个减少到 2 个
```

#### 示例4：动态操作打断合并

```
输入：${2}$[>'=']$[>3]$'abc'

操作符序列：
  [0] STRIDE_OP_CAPTURE_LEN, length=2
  [1] STRIDE_OP_FIND_FWD, ch='='
  [2] STRIDE_OP_JUMP_FWD, offset=3
  [3] STRIDE_OP_MATCH, text="abc"

基础转换：
  [0] STRIDE_EX_CAPTURE_LEN, length=2
  [1] STRIDE_EX_FIND_FWD, ch='='
  [2] STRIDE_EX_JUMP_FWD, offset=3
  [3] STRIDE_EX_SKIP_LEN, length=3

优化合并：STRIDE_EX_JUMP_FWD(3) + STRIDE_EX_SKIP_LEN(3) = STRIDE_EX_JUMP_FWD(6)

最终提取序列：
  [0] STRIDE_EX_CAPTURE_LEN, length=2  (参数1)
  [1] STRIDE_EX_FIND_FWD, ch='='       (不产生参数)
  [2] STRIDE_EX_JUMP_FWD, offset=6     (不产生参数)

参数数量：1
运行时操作数：从 4 个减少到 3 个
```

#### 示例5：正向与负向抵消

```
输入：$[>5]$[<3]$'key'

操作符序列：
  [0] STRIDE_OP_JUMP_FWD, offset=5
  [1] STRIDE_OP_JUMP_BACK, offset=3
  [2] STRIDE_OP_MATCH, text="key"

基础转换：
  [0] STRIDE_EX_JUMP_FWD, offset=5
  [1] STRIDE_EX_JUMP_BACK, offset=3
  [2] STRIDE_EX_SKIP_LEN, length=3

优化合并：STRIDE_EX_JUMP_FWD(5) + STRIDE_EX_JUMP_BACK(3) = STRIDE_EX_JUMP_FWD(2)
          STRIDE_EX_JUMP_FWD(2) + STRIDE_EX_SKIP_LEN(3) = STRIDE_EX_JUMP_FWD(5)

最终提取序列：
  [0] STRIDE_EX_JUMP_FWD, offset=5     (不产生参数)

参数数量：0
运行时操作数：从 3 个减少到 1 个
```

#### 示例6：完整复合示例

```
调用方复合模式：/$'api'/$'v'${'.'}$'.'${}/$'users'
（以 '/' 为调用方切分边界）

所编译的段模式（段1）：$'v'${'.'}$'.'${}

操作符序列（段1）：
  [0] STRIDE_OP_MATCH, text="v"
  [1] STRIDE_OP_CAPTURE_CHR, ch='.'
  [2] STRIDE_OP_MATCH, text="."
  [3] STRIDE_OP_CAPTURE_END

基础转换：
  [0] STRIDE_EX_SKIP_LEN, length=1
  [1] STRIDE_EX_CAPTURE_CHR, ch='.'
  [2] STRIDE_EX_SKIP_LEN, length=1
  [3] STRIDE_EX_CAPTURE_END

优化合并：无可合并（捕获操作打断）

最终提取序列（段1）：
  [0] STRIDE_EX_SKIP_LEN, length=1     (不产生参数)
  [1] STRIDE_EX_CAPTURE_CHR, ch='.'    (参数1)
  [2] STRIDE_EX_SKIP_LEN, length=1     (不产生参数)
  [3] STRIDE_EX_CAPTURE_END            (参数2)

参数数量：2
```

### 5.6 提取器接口

提取序列在编译完成后可交给提取器执行。提取器在**单个段**上运行，产出零拷贝参数（指针 + 长度，直接指向原始段数据）。

```c
/* stride/extractor.h */

/**
 * 参数：零拷贝的（指针，长度）对，直接指向原始段数据
 */
typedef struct {
    const char *ptr;
    size_t len;
} stride_param_t;

/**
 * 提取序列编译：操作符序列 → 提取操作序列（含编译时优化）
 * @param ops            操作符数组（单个段的词法分析结果）
 * @param op_count       操作符数量
 * @param out_ops        输出提取操作数组
 * @param out_count      输出提取操作数量
 * @param out_param_count 输出参数数量（产生参数的提取操作个数）
 * @return STRIDE_OK（0）成功，负值错误码失败
 */
int stride_extractor_compile(const stride_op_t *ops, size_t op_count,
                             stride_extractor_op_t **out_ops,
                             size_t *out_count, size_t *out_param_count);

/**
 * 由提取操作序列创建提取器实例
 * @return 成功返回提取器指针，失败返回 NULL
 */
stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops, size_t op_count);

/** 销毁提取器实例 */
void stride_extractor_destroy(stride_extractor_t *ex);

/**
 * 在单个段上执行提取
 * @param ex               提取器实例
 * @param segment          段数据（不透明字符数组）
 * @param segment_len      段长度
 * @param params           参数输出数组
 * @param param_capacity   params 的容量
 * @param param_count      输出实际参数数量
 * @return STRIDE_OK（0）成功，负值错误码失败
 */
int stride_extractor_execute(const stride_extractor_t *ex, const char *segment, size_t segment_len,
                             stride_param_t *params, size_t param_capacity, size_t *param_count);
```

---

## 六、完整编译流程

### 6.1 编译入口

语法 / 词法模块（`stride/grammar.h`）负责模式字符串 → 操作符序列：

```c
/* stride/grammar.h */

/**
 * 词法分析：段模式字符串 → 操作符序列
 * @param pattern       段模式字符串（不含分隔符）
 * @param out_ops       输出操作符数组（调用方通过 stride_ops_free 释放）
 * @param out_count     输出操作符数量
 * @param out_capacity  输出数组容量
 * @return STRIDE_OK（0）成功，负值错误码失败
 */
int stride_lex(const char *pattern, stride_op_t **out_ops, size_t *out_count, size_t *out_capacity);

/** 释放操作符数组 */
void stride_ops_free(stride_op_t *ops);
```

编译模块（`stride/compiler.h`）负责操作符序列 → 两阶段编译结果：

```c
/* stride/compiler.h */

typedef struct {
    stride_feature_t      *features;        /* 特征序列（匹配阶段用） */
    size_t                 feature_count;
    stride_extractor_op_t *extractors;      /* 提取序列（已优化，参数提取用） */
    size_t                 extractor_count;
    size_t                 param_count;     /* 参数数量 */
} stride_compile_result_t;

/**
 * 编译入口：一次性完成词法分析、特征序列编译与提取序列编译
 * @param pattern 段模式字符串（单个段，不含分隔符）
 * @return 编译结果；失败时返回全零结果（features / extractors 为 NULL，计数为 0）
 */
stride_compile_result_t stride_compile(const char *pattern);

/**
 * 释放编译结果（特征序列与提取序列）
 * @param result 编译结果，可为 NULL
 */
void stride_compile_free(stride_compile_result_t *result);
```

同样地，状态码使用统一命名空间 `stride_status_t`：成功为 `STRIDE_OK`，各类失败为 `STRIDE_E_*`；可调用 `stride_status_str()` 取得可读描述。

### 6.2 完整示例

#### 输入
```
模式: /$'api'/$'v'${'.'}$'.'${}
输入:  /api/v2.0
      （以 '/' 为调用方切分边界，此处仅编译段1）
```

#### 词法分析
```
段0: $'api'
段1: $'v' ${'.'} $'.' ${}
```

#### 操作符序列
```
段0:
  [0] STRIDE_OP_MATCH, text="api"

段1:
  [0] STRIDE_OP_MATCH, text="v"
  [1] STRIDE_OP_CAPTURE_CHR, ch='.'
  [2] STRIDE_OP_MATCH, text="."
  [3] STRIDE_OP_CAPTURE_END
```

#### 特征序列编译（段1）
```
状态机：
  IDLE + KEYWORD("v") → 输出 (0, "v") → IDLE
  IDLE + DYNAMIC('.') → HOLD('.', NULL)
  HOLD + KEYWORD(".") → 输出 ('.', ".") → IDLE
  IDLE + CONST_ABS_END(END) → HOLD(END, NULL)
  扫描结束 → 输出 (END, NULL)

输出：[(0, "v"), ('.', "."), (END, NULL)]
```

#### 提取序列编译（段1）
```
基础转换：
  [0] STRIDE_EX_SKIP_LEN, length=1    // STRIDE_OP_MATCH "v"
  [1] STRIDE_EX_CAPTURE_CHR, ch='.'   // STRIDE_OP_CAPTURE_CHR
  [2] STRIDE_EX_SKIP_LEN, length=1    // STRIDE_OP_MATCH "."
  [3] STRIDE_EX_CAPTURE_END           // STRIDE_OP_CAPTURE_END

优化合并：无可合并的连续常量移动

最终提取序列：
  [0] STRIDE_EX_SKIP_LEN, length=1    (不产生参数)
  [1] STRIDE_EX_CAPTURE_CHR, ch='.'   (参数1)
  [2] STRIDE_EX_SKIP_LEN, length=1    (不产生参数)
  [3] STRIDE_EX_CAPTURE_END           (参数2)
```

#### 最终结果
```
特征序列:
  段0: [(0, "api")]
  段1: [(0, "v"), ('.', "."), (END, NULL)]

提取序列:
  段0: [STRIDE_EX_SKIP_LEN(3)]
  段1: [STRIDE_EX_SKIP_LEN(1), STRIDE_EX_CAPTURE_CHR('.'), STRIDE_EX_SKIP_LEN(1), STRIDE_EX_CAPTURE_END]

参数数量: 2
```

---

## 七、数据结构定义汇总

### 7.1 特征序列

```c
/* stride/feature.h */

typedef enum {
    STRIDE_FT_CONST_REL_FWD = 0, /* 常量相对向结尾移动：(n, kw) */
    STRIDE_FT_CONST_REL_BACK,    /* 常量相对向开头移动：(-n, kw) */
    STRIDE_FT_CONST_ABS_HEAD,    /* 常量绝对位置（基于 HEAD）：(HEAD+n, kw) */
    STRIDE_FT_CONST_ABS_END,     /* 常量绝对位置（基于 END）：(END-n, kw) */
    STRIDE_FT_DYNAMIC_FIND_FWD,  /* 动态向结尾查找：('c', kw) */
    STRIDE_FT_DYNAMIC_FIND_REV   /* 动态向开头查找：('<', 'c', kw) */
} stride_feature_type_t;

typedef struct {
    stride_feature_type_t type;
    int value;              /* 偏移量或字符 ASCII */
    const char *keyword;    /* 关键字，可为 NULL */
    size_t keyword_len;
} stride_feature_t;
```

### 7.2 提取序列

```c
/* stride/extractor.h */

typedef enum {
    STRIDE_EX_CAPTURE_LEN, /* 定长捕获（产生参数） */
    STRIDE_EX_CAPTURE_CHR, /* 捕获到字符（产生参数） */
    STRIDE_EX_CAPTURE_END, /* 捕获到结尾（产生参数） */
    STRIDE_EX_SKIP_LEN,    /* 跳过固定长度（不产生参数） */
    STRIDE_EX_JUMP_ABS,    /* 绝对跳转（不产生参数） */
    STRIDE_EX_JUMP_END,    /* END 跳转（不产生参数） */
    STRIDE_EX_JUMP_FWD,    /* 正向移动（不产生参数） */
    STRIDE_EX_JUMP_BACK,   /* 负向移动（不产生参数） */
    STRIDE_EX_FIND_FWD,    /* 正向查找（不产生参数） */
    STRIDE_EX_FIND_REV     /* 反向查找（不产生参数） */
} stride_extractor_op_type_t;

typedef struct {
    stride_extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;      /* STRIDE_EX_CAPTURE_LEN */
        struct { char ch; } capture_chr;            /* STRIDE_EX_CAPTURE_CHR */
        struct { size_t length; } skip_len;         /* STRIDE_EX_SKIP_LEN */
        struct { size_t pos; } jump_abs;            /* STRIDE_EX_JUMP_ABS */
        struct { int is_end; int offset; } jump_end;/* STRIDE_EX_JUMP_END */
        struct { size_t offset; } jump_fwd;         /* STRIDE_EX_JUMP_FWD */
        struct { size_t offset; } jump_back;        /* STRIDE_EX_JUMP_BACK */
        struct { char ch; } find_fwd;               /* STRIDE_EX_FIND_FWD */
        struct { char ch; } find_rev;               /* STRIDE_EX_FIND_REV */
    } data;
} stride_extractor_op_t;
```

---

## 八、错误码定义

状态码类型为 `stride_status_t`，成功为 `STRIDE_OK`；可通过 `stride_status_str()` 获取可读字符串。

| 错误码 | 含义 | 备注 |
|--------|------|------|
| `STRIDE_E_INVALID_PATTERN` | 模式格式无效 | |
| `STRIDE_E_UNCLOSED_QUOTE` | 未闭合的引号 | |
| `STRIDE_E_INVALID_NUMBER` | 无效的数字 | |
| `STRIDE_E_INVALID_POSITION` | 无效的位置表达式 | |
| `STRIDE_E_EMPTY_SEGMENT` | 空的段模式 | |
| `STRIDE_E_NO_LEADING_SLASH` | 模式不以 / 开头 | **保留/未使用**：Stride 编译单个段，段内不含前导分隔符，编译器不产生该错误 |
| `STRIDE_E_END_CONFLICT` | END 后存在其他操作 | |
| `STRIDE_E_END_POSITIVE_OFFSET` | END 与正数相加 | |
| `STRIDE_E_END_DUPLICATE` | 同一持有单元内 END 重复 | |
| `STRIDE_E_ROUTE_CONFLICT` | 特征序列冲突 | **保留/未使用**：跨模式的冲突检测由调用方负责，编译器不产生该错误 |

---

## 九、设计原则总结

1. **两阶段编译**：匹配（特征序列）与提取（提取序列）分离
2. **常量与动态分离**：常量操作可相加，动态操作不可相加
3. **HEAD 与 END 为符号**：必须显式出现，不可省略
4. **状态机驱动**：特征序列编译使用简洁的两状态状态机
5. **关键字合并**：关键字与当前 HOLD 元组合并后输出
6. **动态打断**：动态操作触发当前 HOLD 输出
7. **END 约束**：只能与负数相加，纯 END 只能在末尾
8. **HEAD 约束**：最终结果必须为 `HEAD + n`，`n ≥ 0`
9. **保留语义**：提取序列保留完整操作语义用于参数提取
10. **匹配优化**：匹配操作转换为常量偏移，避免重复验证
11. **常量合并**：连续常量移动在提取序列中合并，减少运行时操作
12. **单段编译**：一次编译只针对一个段；跨段切分、组织与冲突检测属于调用方职责

---

**文档版本**：1.0
**更新日期**：2026-09-04

**主要变更（相对 URLRouter 2.2 源文档）**：
- 类型、错误码与函数统一更名为 `stride_*` / `STRIDE_*` 命名空间（`op_t` → `stride_op_t`、`feature_tuple_t` → `stride_feature_t`、`extractor_op_t` → `stride_extractor_op_t`、`compile_result_t` → `stride_compile_result_t`、`pattern_compile` → `stride_compile` 等）
- 明确编译器模块同时承担词法分析（grammar / lexer）职责，并给出 `stride_lex` / `stride_ops_free` 接口
- 编译入口更新为 `stride_compile()` 返回 `stride_compile_result_t`（含 `features` / `extractors` / `param_count`），配套 `stride_compile_free()`
- 去除路由器 / 路由树 / HTTP 方法 / 回调等框架概念：Stride 只编译单个段，输入切分由调用方负责
- 保留两状态状态机、常量相加规则、关键字合并规则、END/HEAD 约束、全部编译示例、提取序列基础转换与两项编译时优化、完整错误码表
- `STRIDE_E_NO_LEADING_SLASH` 与 `STRIDE_E_ROUTE_CONFLICT` 标注为保留、编译器未使用
