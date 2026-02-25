/* SDSLib 2.0 头文件
 * 作用: 定义 SDS 的核心数据结构。
 * 核心思想：把字符串长度存在 Header 里，不用每次都遍历，同时兼容标准 C 的 char*。
 */

#ifndef __SDS_H
#define __SDS_H

/* 预分配最大阈值：1MB
 * 策略：空间换时间。防止 append 数据时频繁 malloc，
 * 但也不能无限制浪费内存，所以设了个 1MB 的上限。
 */
#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

/* 定义 sds 为 char* 类型
 * 这样设计很巧妙，sds 字符串能直接传给 printf、strcat 这些标准库函数，
 * 以前的老代码（tinyhttpd）改起来就很省事，不用重写所有逻辑。
 */
typedef char *sds;

/* * 关键点：__attribute__ ((__packed__))
 * 必须告诉编译器取消字节对齐。
 * 原因：我们需要 flags 字段紧挨着 buf 数组。如果不取消对齐，
 * 编译器可能会在中间插填充字节，导致通过 (s-1) 访问 flags 时读到脏数据。
 */

struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 低3位存类型，高5位存长度 */
    char buf[];          /* 柔性数组，直接指向后面的数据区 */
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len;         /* 已用长度，获取长度复杂度降为 O(1) */
    uint8_t alloc;       /* 总分配长度（不含头和\0） */
    unsigned char flags; /* 类型标记 */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len;
    uint16_t alloc;
    unsigned char flags;
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len;
    uint32_t alloc;
    unsigned char flags;
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len;
    uint64_t alloc;
    unsigned char flags;
    char buf[];
};

#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3

/* 指针运算宏
 * 原理：s 指向 buf 的开头，减去结构体的大小，就能指回 Header 的开头。
 * 这是 C 语言指针操作的精髓，实现了从数据反推元数据。
 */
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

/* * 内联函数：获取长度
 * 相比 strlen 的 O(N) 遍历，这里直接读内存，速度是 O(1)。
 * 在高并发服务器里，频繁调用 strlen 是性能杀手，改用这个效率提升巨大。
 */
static inline size_t sdslen(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

/* 获取剩余可用空间 */
static inline size_t sdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* 导出函数声明：
 * 这些是给 httpd.c 用的接口。
 */
sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);
sds sdsdup(const sds s);
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdsrange(sds s, int start, int end);
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* 底层 API：
 * sdsMakeRoomFor 是核心，在 append 数据前必须调用它检查并扩容。
 */
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);
void *sdsAllocPtr(sds s);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

/* -------------------------------------------------------------------------
 * 填坑记录：补全内存分配函数
 * 之前编译的时候报 sdsalloc 和 sdssetalloc 未定义，这里手动补上。
 * ------------------------------------------------------------------------- */

static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8: return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16: return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32: return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64: return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: break;
        case SDS_TYPE_8: SDS_HDR(8,s)->alloc = newlen; break;
        case SDS_TYPE_16: SDS_HDR(16,s)->alloc = newlen; break;
        case SDS_TYPE_32: SDS_HDR(32,s)->alloc = newlen; break;
        case SDS_TYPE_64: SDS_HDR(64,s)->alloc = newlen; break;
    }
}

#endif
