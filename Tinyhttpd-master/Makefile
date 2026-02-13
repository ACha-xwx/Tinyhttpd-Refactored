# 编译器定义
CC = gcc

# 深度剖析编译标志 (CFLAGS):
# -g:  生成调试符号信息。这在重构过程中至关重要，能让我们在 GDB 中精准定位到指针偏移错误。
# -W -Wall: 开启编译器最严苛的警告检查。作为工程级代码，我们不应忽略任何一个潜在的类型转换或内存溢出风险。
CFLAGS = -g -W -Wall

# 链接库：POSIX 线程库，用于处理并发 HTTP 请求
LIBS = -lpthread

# 默认构建目标
all: httpd client

# 核心服务器构建逻辑：
# 此处将主程序 httpd.c 与我们引入的二进制安全库 sds.c 进行联合编译。
# 只有这样，主程序在调用 get_line_sds 时才能正确链接到 SDS 的内存管理逻辑。
httpd: httpd.c sds.c
	$(CC) $(CFLAGS) -o httpd httpd.c sds.c $(LIBS)

# 辅助测试客户端
client: simpleclient.c
	$(CC) $(CFLAGS) -o client simpleclient.c

# 环境维护逻辑：
# clean 目标不仅是为了节省空间，更是为了在代码逻辑（如 sds.h 结构体）发生变化后，
# 强制系统重新生成二进制文件，避免因旧的 .o 文件导致难以排查的内存对齐 Bug。
clean:
	rm -f httpd client *.o
