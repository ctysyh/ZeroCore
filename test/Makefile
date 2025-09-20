CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../src -I../src/memory

# 测试程序目标
TEST_TARGET = test_segment
TEST_SOURCE = test_segment.c
MEMORY_SOURCES = ../src/memory/segment.c

# 默认目标
all: $(TEST_TARGET)

# 编译测试程序
$(TEST_TARGET): $(TEST_SOURCE) $(MEMORY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

# 运行测试
run: $(TEST_TARGET)
	./$(TEST_TARGET)

# 清理编译产物
clean:
	rm -f $(TEST_TARGET)

.PHONY: all run clean