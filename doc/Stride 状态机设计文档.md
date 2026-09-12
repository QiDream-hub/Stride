# Stride 状态机设计文档

**文档版本**：2.0
**更新日期**：2026-09-12
**适用模块**：`stride/matcher.h`（匹配序列编译）、`stride/extractor.h`（提取序列编译）

---

> **范围**：本文档只描述**编译期状态机**——匹配序列编译的 IDLE/HOLD 状态机，以及提取序列编译的常量合并状态机。
> 词法分析、操作符 IR、编译编排、数据结构与错误码见
> [`Stride 编译器设计文档.md`](Stride%20编译器设计文档.md)。
>
> **单位**：步长的单位是**比特**；位置与偏移以**步**计（比特偏移 = 步 × 步长）。
> 详见 [`Stride 段模式语法规范.md`](Stride%20段模式语法规范.md) 第七节。

---

## 一、概述

编译器对**单个段**的操作符序列做线性扫描，产出两个产物，各由一个编译期状态机驱动：

| 状态机 | 输入 | 输出 | 形态 |
|--------|------|------|------|
| **匹配序列编译状态机** | 操作符序列 | 匹配序列 | IDLE / HOLD 两状态 |
| **提取序列常量合并状态机** | 基础转换后的提取操作 | 优化后的提取序列 | IDLE / MERGING 两状态 |

两个状态机都只依赖操作符序列，彼此独立；调用方可以只运行其中一个（只编译匹配序列或只编译提取序列）。

两个状态机都**与步长无关**：它们只处理「步数」与「比特长度」，不把步长折算成字节。

---

## 二、匹配序列编译状态机

对**单个段**的操作符序列做一次线性扫描，把「移动 + 在该处比对什么」压缩成最少的元组。

### 2.1 状态定义

| 状态 | 含义 |
|------|------|
| **IDLE** | 空闲，无持有元组 |
| **HOLD** | 持有一个步进元组，等待继续相加、被查找打断或被字面量收尾 |

### 2.2 操作分类与事件

| 类别 | 操作符 | 事件 | 说明 |
|------|--------|------|------|
| 步进 | `STRIDE_OP_CAPTURE_STEPS`、`STRIDE_OP_JUMP_FWD` | `STEP_POS` | 向段尾的常量步进（+n 步） |
| 步进 | `STRIDE_OP_JUMP_BACK` | `STEP_NEG` | 向段首的常量步进（−n 步） |
| 步进 | `STRIDE_OP_JUMP_ABS` | `STEP_ABS_HEAD` | 绝对步位置（基于 HEAD） |
| 步进 | `STRIDE_OP_JUMP_END` | `STEP_ABS_END` | 绝对步位置（基于 END） |
| 查找 | `STRIDE_OP_CAPTURE_UNTIL`、`STRIDE_OP_FIND_FWD` | `FIND_FWD` | 向段尾查找比特串 |
| 查找 | `STRIDE_OP_FIND_REV` | `FIND_REV` | 向段首查找比特串 |
| 比对 | `STRIDE_OP_MATCH` | `LITERAL` | 比对字面量 |

### 2.3 基础元组映射

| 操作符 | 基础元组 |
|--------|---------|
| `STRIDE_OP_MATCH` | `(0, literal)` |
| `STRIDE_OP_CAPTURE_STEPS` | `(steps, NULL)` |
| `STRIDE_OP_JUMP_FWD` | `(steps, NULL)` |
| `STRIDE_OP_JUMP_BACK` | `(-steps, NULL)` |
| `STRIDE_OP_JUMP_ABS` | `(HEAD+steps, NULL)` |
| `STRIDE_OP_JUMP_END`（END） | `(END, NULL)` |
| `STRIDE_OP_JUMP_END`（END−n） | `(END−n, NULL)` |
| `STRIDE_OP_CAPTURE_UNTIL` | `("S", NULL)` |
| `STRIDE_OP_FIND_FWD` | `("S", NULL)` |
| `STRIDE_OP_FIND_REV` | `("<", "S", NULL)` |

记法中 `n` 为**步数**，`S` 为**比特串**（可为多比特，如 UTF-8 中文）。

### 2.4 状态转换表

| 当前状态 | 事件 | 动作 | 下一状态 |
|---------|------|------|---------|
| IDLE | 步进 | 持有 = 基础元组 | HOLD |
| IDLE | 查找 | 持有 = 基础元组 | HOLD |
| IDLE | 比对 | 输出 `(0, literal)` | IDLE |
| HOLD | 步进 | 可相加：持有 += 步数<br>否则：输出持有，持有 = 新元组 | HOLD |
| HOLD | 查找 | 输出持有，持有 = 查找元组 | HOLD |
| HOLD | 比对 | 输出 `(持有值, literal)`，清空持有 | IDLE |
| 扫描结束 | — | 若在 HOLD：输出持有 | IDLE |

### 2.5 步进相加规则

同类型的步进元组按**步数**相加：

| 持有类型 | 可相加事件 | 结果 | 约束 |
|---------|-----------|------|------|
| `(n, NULL)` | `STEP_POS` | `(n+m, NULL)` | 无 |
| `(n, NULL)` | `STEP_NEG` | `(n−m, NULL)` | 结果可为负 |
| `(−n, NULL)` | `STEP_POS` | `(−n+m, NULL)` | 无 |
| `(−n, NULL)` | `STEP_NEG` | `(−n−m, NULL)` | 无 |
| `(HEAD+n, NULL)` | `STEP_POS` | `(HEAD+n+m, NULL)` | 结果 `n+m ≥ 0` |
| `(HEAD+n, NULL)` | `STEP_NEG` | `(HEAD+n−m, NULL)` | 结果 `n−m ≥ 0` |
| `(END−n, NULL)` | `STEP_NEG` | `(END−n−m, NULL)` | 结果 `n+m ≥ 0` |
| `(END−n, NULL)` | `STEP_POS` | **无效**（打断） | END 不能加正数 |
| 查找 | 任何 | **不可相加**（打断） | 先输出再处理 |
| 不同基准 | 任何 | **不可相加**（打断） | HEAD 与 END 不能混用 |

### 2.6 字面量合并规则

比对事件与当前 HOLD 元组合并后输出：

| 持有类型 | 合并结果 |
|---------|---------|
| `(n, NULL)` | `(n, literal)` |
| `(−n, NULL)` | `(−n, literal)` |
| `(HEAD+n, NULL)` | `(HEAD+n, literal)` |
| `(END−n, NULL)` | `(END−n, literal)` |
| `("S", NULL)` | `("S", literal)` |
| `("<","S", NULL)` | `("<","S", literal)` |

### 2.7 对齐校验

若编译期已知步长 `s`（`stride != 0`），对每个非空字面量校验 `literal.bit_len % s == 0`，
违反则编译失败（`STRIDE_E_ALIGN`）。执行期会再校验一次段长。

---

## 三、匹配序列编译示例

以下示例均为**单个段**的操作符序列；元组中的数值为**步数**。
为便于阅读，示例给出「步长 8（= 1 字节）」下的字节数换算，但状态机本身不依赖步长。

### 3.1 步进相加

```
输入：${1}${1}${1}$'key'
步长：8（1 字节/步）

事件序列：STEP_POS(1), STEP_POS(1), STEP_POS(1), LITERAL("key")

状态机：
  IDLE + STEP_POS(1) → HOLD(1, NULL)
  HOLD + STEP_POS(1) → HOLD(2, NULL)
  HOLD + STEP_POS(1) → HOLD(3, NULL)
  HOLD + LITERAL("key") → 输出 (3, "key") → IDLE

输出：[(3, "key")]     // "key" = 24 比特 = 3 步
```

### 3.2 查找打断

```
输入：${2}${'a'}${3}$'b'

事件序列：STEP_POS(2), FIND_FWD("a"), STEP_POS(3), LITERAL("b")

状态机：
  IDLE + STEP_POS(2) → HOLD(2, NULL)
  HOLD + FIND_FWD("a") → 输出 (2, NULL)，HOLD("a", NULL)
  HOLD + STEP_POS(3) → 输出 ("a", NULL)，HOLD(3, NULL)
  HOLD + LITERAL("b") → 输出 (3, "b") → IDLE

输出：[(2, NULL), ("a", NULL), (3, "b")]
```

### 3.3 绝对定位与步进相加

```
输入：$[5]${2}$'key'

状态机：
  IDLE + STEP_ABS_HEAD(5) → HOLD(HEAD+5, NULL)
  HOLD + STEP_POS(2) → HOLD(HEAD+7, NULL)
  HOLD + LITERAL("key") → 输出 (HEAD+7, "key") → IDLE

输出：[(HEAD+7, "key")]
```

### 3.4 END 合并

```
输入：${}$[<4]$'dddd'

状态机：
  IDLE + STEP_ABS_END(END) → HOLD(END, NULL)
  HOLD + STEP_NEG(4) → HOLD(END−4, NULL)
  HOLD + LITERAL("dddd") → 输出 (END−4, "dddd") → IDLE

输出：[(END−4, "dddd")]
```

### 3.5 查找与字面量合并

```
输入：${'a'}$'key'

状态机：
  IDLE + FIND_FWD("a") → HOLD("a", NULL)
  HOLD + LITERAL("key") → 输出 ("a", "key") → IDLE

输出：[("a", "key")]
```

### 3.6 多字节查找（UTF-8，步长 8）

```
输入：${'：'}$'：'
（"：" = EF BC 9A，24 比特）

事件序列：FIND_FWD("："), LITERAL("：")

状态机：
  IDLE + FIND_FWD("：") → HOLD("：", NULL)
  HOLD + LITERAL("：") → 输出 ("：", "：") → IDLE

输出：[("：", "：")]
```

### 3.7 END 后接查找

```
输入：$[END]${'a'}

状态机：
  IDLE + STEP_ABS_END(END) → HOLD(END, NULL)
  HOLD + FIND_FWD("a") → 不可相加，输出 (END, NULL)，HOLD("a", NULL)
  扫描结束 → 输出 ("a", NULL)

输出：[(END, NULL), ("a", NULL)]
```

### 3.8 连续字面量（不推荐）

```
输入：$'dd'$'aaa'

状态机：
  IDLE + LITERAL("dd") → 输出 (0, "dd") → IDLE
  IDLE + LITERAL("aaa") → 输出 (0, "aaa") → IDLE

输出：[(0, "dd"), (0, "aaa")]
```

两个元组会产生两次「移动 0 步 + 比对」的冗余。**建议合并为 `$'ddaaa'`。**

---

## 四、提取序列编译状态机

### 4.1 两阶段

```
操作符序列 →（阶段一：基础转换）→ 临时提取序列 →（阶段二：常量合并状态机）→ 提取序列
```

- **阶段一 基础转换**：逐操作符一一映射，不做优化（规则见 4.2）。
- **阶段二 常量合并**：扫描临时序列，把连续的常量移动压成一个（状态机见 4.3）。

### 4.2 基础转换规则

| 操作符 | 基础提取操作 | 产生参数 | 说明 |
|--------|------------|---------|------|
| `STRIDE_OP_MATCH` | `STRIDE_EX_SKIP_STEPS` | 否 | 比对已在匹配阶段完成，此处只跳过 |
| `STRIDE_OP_CAPTURE_STEPS` | `STRIDE_EX_CAPTURE_STEPS` | 是 | 捕获 n 步 |
| `STRIDE_OP_CAPTURE_UNTIL` | `STRIDE_EX_CAPTURE_UNTIL` | 是 | 捕获到定界串前 |
| `STRIDE_OP_CAPTURE_END` | `STRIDE_EX_CAPTURE_END` | 是 | 捕获到段尾 |
| `STRIDE_OP_JUMP_ABS` | `STRIDE_EX_JUMP_ABS` | 否 | 绝对定位 |
| `STRIDE_OP_JUMP_END` | `STRIDE_EX_JUMP_END` | 否 | END 定位 |
| `STRIDE_OP_JUMP_FWD` | `STRIDE_EX_JUMP_FWD` | 否 | 向段尾移动 |
| `STRIDE_OP_JUMP_BACK` | `STRIDE_EX_JUMP_BACK` | 否 | 向段首移动 |
| `STRIDE_OP_FIND_FWD` | `STRIDE_EX_FIND_FWD` | 否 | 向段尾查找 |
| `STRIDE_OP_FIND_REV` | `STRIDE_EX_FIND_REV` | 否 | 向段首查找 |

> `STRIDE_OP_MATCH` 转成 `STRIDE_EX_SKIP_STEPS` 时，**跳过步数 = `literal.bit_len / 步长`**。
> 编译期已知步长时可预先算出；否则执行期换算。

### 4.3 常量合并状态机

#### 状态定义

| 状态 | 含义 |
|------|------|
| **IDLE** | 当前没有正在累计的合并段 |
| **MERGING** | 正在累计一个合并段 |

#### 累计量

| 累计量 | 含义 |
|--------|------|
| `merge_value` | 合并段净位移（步），正向为正、负向为负 |
| `has_skip_len` | 合并段是否包含 `SKIP_STEPS`（决定结果输出成 `SKIP_STEPS` 还是 `JUMP_*`） |

#### 事件分类

| 事件 | 操作 | 处理 |
|------|------|------|
| **可合并** | `STRIDE_EX_SKIP_STEPS`、`STRIDE_EX_JUMP_FWD`、`STRIDE_EX_JUMP_BACK` | 累加净位移 |
| **打断** | `STRIDE_EX_CAPTURE_STEPS/UNTIL/END`、`STRIDE_EX_JUMP_ABS`、`STRIDE_EX_JUMP_END`、`STRIDE_EX_FIND_FWD`、`STRIDE_EX_FIND_REV` | 先冲刷合并段，再原样输出本操作 |

#### 状态转换表

| 当前状态 | 事件 | 动作 | 下一状态 |
|---------|------|------|---------|
| IDLE | 可合并 | `merge_value = v`；`has_skip_len` 按类型设置 | MERGING |
| IDLE | 打断 | 原样输出操作 | IDLE |
| MERGING | 可合并 | `merge_value += v` | MERGING |
| MERGING | 打断 | 冲刷合并段（净位移为 0 则不输出），再原样输出操作 | IDLE |
| 扫描结束 | — | 若在 MERGING：冲刷合并段 | IDLE |

#### 冲刷规则

净位移为 0 时**不输出**任何操作；否则：

| 条件 | 输出 |
|------|------|
| `merge_value > 0` 且 `has_skip_len` | `STRIDE_EX_SKIP_STEPS(steps=merge_value, bit_len=0)` |
| `merge_value > 0` | `STRIDE_EX_JUMP_FWD(merge_value)` |
| `merge_value < 0` | `STRIDE_EX_JUMP_BACK(-merge_value)` |

> **编译期步长未知（`stride == 0`）时**：步数（来自 `$[>n]` 等）与比特数（来自字面量跳过）
> 无法换算，因此只允许**同单位**的累计——遇到单位切换就先冲刷。此时字面量跳过输出为
> `STRIDE_EX_SKIP_STEPS(steps=0, bit_len=比特数)`，由执行期按实际步长折算为步数。
> 步长已知时全部折算为步数，可跨单位完全合并。

---

## 五、提取序列编译示例

### 5.1 基础捕获

```
输入：${4}$'a'     步长：8

阶段一（基础转换）：
  [0] STRIDE_EX_CAPTURE_STEPS, steps=4
  [1] STRIDE_EX_SKIP_STEPS, bit_len=8     // "a" = 8 比特 = 1 步

阶段二（常量合并）：
  IDLE + CAPTURE_STEPS(打断) → 原样输出 [0] → IDLE
  IDLE + SKIP_STEPS(可合并) → MERGING(1, skip)
  扫描结束 → 冲刷 → 输出 [1]

最终提取序列：
  [0] STRIDE_EX_CAPTURE_STEPS(4)   (参数1)
  [1] STRIDE_EX_SKIP_STEPS(1)      (不产生参数)

参数数量：1
```

### 5.2 捕获到定界串（打断合并）

```
输入：${'='}$'='${}

阶段一：
  [0] STRIDE_EX_CAPTURE_UNTIL, "="
  [1] STRIDE_EX_SKIP_STEPS, bit_len=8
  [2] STRIDE_EX_CAPTURE_END

阶段二：
  IDLE + CAPTURE_UNTIL(打断) → 输出 [0] → IDLE
  IDLE + SKIP_STEPS(可合并) → MERGING(1, skip)
  MERGING + CAPTURE_END(打断) → 冲刷 [1]，输出 [2] → IDLE

最终提取序列：
  [0] STRIDE_EX_CAPTURE_UNTIL("=")  (参数1)
  [1] STRIDE_EX_SKIP_STEPS(1)       (不产生参数)
  [2] STRIDE_EX_CAPTURE_END         (参数2)

参数数量：2
```

### 5.3 常量移动合并

```
输入：${2}$[>3]$'abc'     步长：8

阶段一：
  [0] STRIDE_EX_CAPTURE_STEPS(2)
  [1] STRIDE_EX_JUMP_FWD(3)
  [2] STRIDE_EX_SKIP_STEPS, bit_len=24     // "abc" = 24 比特 = 3 步

阶段二：
  ... [0] 输出 → IDLE
  IDLE + JUMP_FWD(3) → MERGING(3, 非 skip)
  MERGING + SKIP_STEPS(3) → MERGING(6, skip)
  扫描结束 → 冲刷 → 输出 JUMP_FWD? 见下

冲刷：merge_value=6>0，has_skip_len=1 → SKIP_STEPS(6)
```

> 由于合并段**包含** `SKIP_STEPS`，冲刷按 `has_skip_len` 规则输出 `SKIP_STEPS`；
> `SKIP_STEPS` 与 `JUMP_FWD` 在运行时的语义完全相同（向段尾移动 n 步），
> 两者只是来源标记不同。

```
最终提取序列：
  [0] STRIDE_EX_CAPTURE_STEPS(2)  (参数1)
  [1] STRIDE_EX_SKIP_STEPS(6)     (不产生参数)

参数数量：1；运行时操作数：3 → 2
```

### 5.4 查找操作打断合并

```
输入：${2}$[>'=']$[>3]$'abc'     步长：8

阶段一：
  [0] STRIDE_EX_CAPTURE_STEPS(2)
  [1] STRIDE_EX_FIND_FWD("=")
  [2] STRIDE_EX_JUMP_FWD(3)
  [3] STRIDE_EX_SKIP_STEPS, bit_len=24

阶段二：
  [0] 输出 → IDLE；[1] FIND_FWD 打断，原样输出 → IDLE
  [2] JUMP_FWD 进入 MERGING(3)；[3] SKIP_STEPS 累加 → MERGING(6, skip)
  扫描结束 → 冲刷 → SKIP_STEPS(6)

最终提取序列：
  [0] STRIDE_EX_CAPTURE_STEPS(2)  (参数1)
  [1] STRIDE_EX_FIND_FWD("=")     (不产生参数)
  [2] STRIDE_EX_SKIP_STEPS(6)     (不产生参数)

参数数量：1；运行时操作数：4 → 3
```

### 5.5 正负抵消

```
输入：$[>5]$[<3]$'key'     步长：8

阶段一：
  [0] STRIDE_EX_JUMP_FWD(5)
  [1] STRIDE_EX_JUMP_BACK(3)
  [2] STRIDE_EX_SKIP_STEPS, bit_len=24

阶段二：
  IDLE + JUMP_FWD(5) → MERGING(5)
  MERGING + JUMP_BACK(3) → MERGING(2)
  MERGING + SKIP_STEPS(3) → MERGING(5, skip)
  扫描结束 → 冲刷 → SKIP_STEPS(5)

最终提取序列：
  [0] STRIDE_EX_SKIP_STEPS(5)     (不产生参数)

参数数量：0；运行时操作数：3 → 1
```

### 5.6 合并段净位移为 0（不输出）

```
输入：$[>4]$[<4]     步长：8

阶段一：
  [0] STRIDE_EX_JUMP_FWD(4)
  [1] STRIDE_EX_JUMP_BACK(4)

阶段二：
  IDLE + JUMP_FWD(4) → MERGING(4)
  MERGING + JUMP_BACK(4) → MERGING(0)
  扫描结束 → 冲刷：净位移 0 → 不输出

最终提取序列：[]（空）
参数数量：0；运行时操作数：2 → 0
```

---

## 六、设计原则

1. **两状态**：匹配序列编译用 IDLE/HOLD，提取序列合并用 IDLE/MERGING；状态少、可线性扫描
2. **步进与查找分离**：步进可相加，查找打断
3. **字面量收尾**：比对事件把 HOLD 元组与字面量合并后输出
4. **END 约束**：END 基准只能加负数（结果为 `END − n`，`n ≥ 0`）
5. **HEAD 约束**：最终结果必须为 `HEAD + n`，`n ≥ 0`，否则打断
6. **比对免验证**：提取阶段把比对字面量转为定步跳过，不重复验证
7. **常量合并**：连续常量步进压成一个，净位移为 0 则不输出
8. **步长无关**：状态机只处理步数与比特长度，不把步长折算成字节

---

**文档版本**：2.0
**更新日期**：2026-09-12
