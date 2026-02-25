/* sdsalloc.h -- 内存分配底层抽象
 * 作用: 将 SDS 的内存管理与具体的系统实现解耦。
 */

#ifndef __SDSALLOC_H
#define __SDSALLOC_H

#include <stdlib.h>

/* 思考笔记：为什么不直接用 malloc？
 * 这里加一层宏定义的封装，不仅仅是为了好看，主要是为了给自己留条后路：
 * 1. 灵活性：如果以后觉得系统自带的分配器慢了，想换成 jemalloc 或 tcmalloc，
 * 只需要改这几个宏，不用去 sds.c 里一行行查找替换。
 * 2. 调试便利：可以在这层宏里挂钩子（Hook），监控每次内存申请和释放，
 * 这对排查 Web 服务器长时间运行后的内存泄漏（Memory Leak）非常有用。
 * 这算是 C 语言项目里解耦底层资源的标准写法。
 */

#define s_malloc malloc
#define s_realloc realloc
#define s_free free

#endif
