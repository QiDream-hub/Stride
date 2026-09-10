# ============================================================
# Stride — 序列模式编译器
#
#   make              构建静态库 libstride.a
#   make test         构建并运行全部测试
#   make example      构建示例程序
#   make run          运行示例程序
#   make compile-commands  用 bear 生成 compile_commands.json
#   make clean        清理构建产物
# ============================================================

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -Wall -Wextra -O2 -g -std=c99
CPPFLAGS += -Iinclude

BUILD_DIR := build

# ==================== 库 ====================

SRCS := src/compiler.c \
        src/feature.c \
        src/extractor.c

LIB      := $(BUILD_DIR)/libstride.a
LIB_OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)

# ==================== 测试 ====================

TEST_NAMES := test_compiler test_feature test_extractor
TEST_BINS  := $(addprefix $(BUILD_DIR)/,$(TEST_NAMES))

# ==================== 示例 ====================

EXAMPLE_BIN := $(BUILD_DIR)/example

# ==================== 规则 ====================

.PHONY: all lib test example run compile-commands clean

all: lib

lib: $(LIB)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_%: tests/test_%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB)

example: $(EXAMPLE_BIN)

$(EXAMPLE_BIN): examples/example.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB)

run: $(EXAMPLE_BIN)
	$(EXAMPLE_BIN)

test: $(TEST_BINS)
	@failed=0; \
	for t in $(TEST_BINS); do \
		echo "=== $$t ==="; \
		$$t || failed=1; \
		echo ""; \
	done; \
	if [ $$failed -ne 0 ]; then \
		echo "*** 有测试失败 ***"; exit 1; \
	else \
		echo "*** 全部测试通过 ***"; \
	fi

# 生成 compile_commands.json（供 clangd 等工具使用；文件已被 .gitignore 忽略）
compile-commands:
	bear -- $(MAKE) clean all

clean:
	rm -rf $(BUILD_DIR)
