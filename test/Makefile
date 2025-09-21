CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I../src -I../src/memory

# 测试程序目标
TEST_TARGET = test_segment.exe test_block.exe
TEST_SOURCE_SEGMENT = test_segment.c
TEST_SOURCE_BLOCK = test_block.c
MEMORY_SOURCES = ../src/memory/segment.c ../src/memory/block.c

# 默认目标
all: $(TEST_TARGET)

# 编译测试程序
test_segment.exe: $(TEST_SOURCE_SEGMENT) $(MEMORY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

test_block.exe: $(TEST_SOURCE_BLOCK) $(MEMORY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

# 运行测试
run: $(TEST_TARGET)
	.\test_segment.exe
	.\test_block.exe

# 清理编译产物
clean:
	del $(TEST_TARGET)

.PHONY: all run clean