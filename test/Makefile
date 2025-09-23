CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I../src -I../src/memory

# 测试程序目标（无后缀）
TEST_TARGET = segment block type_descriptor handle

# 内存模块源码
MEMORY_SOURCES = ../src/memory/segment.c ../src/memory/block.c ../src/type/type_descriptor.c ../src/zora/handle.c

# 默认目标
all: $(TEST_TARGET)

# 通用规则：make test_xxx → 编译 test_xxx.c + MEMORY_SOURCES → 输出 xxx.exe
test_%: test_%.c $(MEMORY_SOURCES)
	$(CC) $(CFLAGS) -o $@.exe $^

# 别名：make xxx → make test_xxx
%: test_%
	@echo "Built $<.exe"

# 运行测试（现在运行的是 .exe 文件）
run: $(TEST_TARGET)
	.\$@.exe

# 清理
clean:
	del /Q *.o *.exe test_*.exe 2> NUL || true

.PHONY: all run clean