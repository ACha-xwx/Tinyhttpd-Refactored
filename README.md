# Tinyhttpd-Refactored (2026寒假专业实践)

## 项目简介
这是一个基于 Tinyhttpd 的改进版本，主要完成了以下升级：
1. **SDS (Simple Dynamic String)**：替换了原有的 C 风格字符串，解决了缓冲区溢出风险。
2. **线程池 (Thread Pool)**：引入预创建线程池，提升了服务器并发处理能力。
3. **详细注释**：对核心代码进行了深度剖析和注释。

## 如何编译运行
在 Linux 环境下执行：
```bash
make
./httpd
```
