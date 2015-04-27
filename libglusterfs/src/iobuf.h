/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _IOBUF_H_
#define _IOBUF_H_

#include "list.h"
#include "common-utils.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/uio.h>

#define GF_VARIABLE_IOBUF_COUNT 32

/* Lets try to define the new anonymous mapping
 * flag, in case the system is still using the
 * now deprecated MAP_ANON flag.
 *
 * Also, this should ideally be in a centralized/common
 * header which can be used by other source files also.
 */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define GF_ALIGN_BUF(ptr,bound) ((void *)((unsigned long)(ptr + bound - 1) & \
                                          (unsigned long)(~(bound - 1))))

#define GF_IOBUF_ALIGN_SIZE 512

/* one allocatable unit for the consumers of the IOBUF API */
/* each unit hosts @page_size bytes of memory */
struct iobuf;

/* one region of memory MMAPed from the operating system */
/* each region MMAPs @arena_size bytes of memory */
/* each arena hosts @arena_size / @page_size IOBUFs */
struct iobuf_arena;

/* expandable and contractable pool of memory, internally broken into arenas */
struct iobuf_pool;

struct iobuf_init_config {
        size_t   pagesize;  // 页大小
        int32_t  num_pages; // 页数
};

//一个页对应一个iobuf
struct iobuf {
        union {
                struct list_head      list;
                struct {
                        struct iobuf *next;
                        struct iobuf *prev;
                };
        };
        struct iobuf_arena  *iobuf_arena; //对应的iobuf arena

        gf_lock_t            lock; /* for ->ptr and ->ref */
        int                  ref;  /* 0 == passive, >0 == active 被引用次数*/

        void                *ptr;  /* usable memory region by the consumer 页的起始地址*/

        void                *free_ptr; /* in case of stdalloc, this is the
                                          one to be freed 对应于大的iobuf请求*/
};


struct iobuf_arena {
        union {
                struct list_head            list;
                struct {
                        struct iobuf_arena *next;
                        struct iobuf_arena *prev;
                };
        };

        size_t              page_size;  /* size of all iobufs in this arena 页数*/
        size_t              arena_size; /* this is equal to
                                           (iobuf_pool->arena_size / page_size)
                                           * page_size 
                                           是 page_size*page_count
                                           */
        size_t              page_count; //页数

        struct iobuf_pool  *iobuf_pool; //对应的iobuf pool

        void               *mem_base;   //内存起始地址
        struct iobuf       *iobufs;     /* allocated iobufs list 所有iobuf列表*/

        int                 active_cnt; //已被使用的iobuf数
        struct iobuf        active;     /* head node iobuf
                                           (unused by itself) */
        int                 passive_cnt; //没被使用的iobuf数
        struct iobuf        passive;    /* head node iobuf
                                           (unused by itself) */
        uint64_t            alloc_cnt;  /* total allocs in this pool 使用了多少次iobuf*/
        int                 max_active; /* max active buffers at a given time 最大使用次数*/
};


struct iobuf_pool {
        pthread_mutex_t     mutex;  //互斥量
        size_t              arena_size; /* size of memory region in
                                           arena  区域大小,不用的页大小乘以对应的页数相加*/
        size_t              default_page_size; /* default size of iobuf 页大小 128*1024 */

        int                 arena_cnt; //已分配的区域数
        struct list_head    arenas[GF_VARIABLE_IOBUF_COUNT]; //所有区域，不同的页大小使用不同的list
        /* array of arenas. Each element of the array is a list of arenas
           holding iobufs of particular page_size */

        struct list_head    filled[GF_VARIABLE_IOBUF_COUNT];//已填充
        /* array of arenas without free iobufs */

        struct list_head    purge[GF_VARIABLE_IOBUF_COUNT];//可清除(可以写数据?)
        /* array of of arenas which can be purged */

        uint64_t            request_misses; /* mostly the requests for higher
                                               value of iobufs 大iobuf的请求次数*/
};


struct iobuf_pool *iobuf_pool_new (void);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
struct iobuf *iobuf_get (struct iobuf_pool *iobuf_pool);
void iobuf_unref (struct iobuf *iobuf);
struct iobuf *iobuf_ref (struct iobuf *iobuf);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
void iobuf_to_iovec(struct iobuf *iob, struct iovec *iov);

#define iobuf_ptr(iob) ((iob)->ptr)
#define iobpool_default_pagesize(iobpool) ((iobpool)->default_page_size)
#define iobuf_pagesize(iob) (iob->iobuf_arena->page_size)

//iobref 记录iobuf使用情况和地址
struct iobref {
        gf_lock_t          lock;  //锁
        int                ref;  //引用计数
        struct iobuf     **iobrefs; //16个iobuf *
	int                alloced;  //初始分配分配数 16，不够时乘2扩大
	int                used;  //使用数
};

struct iobref *iobref_new ();
struct iobref *iobref_ref (struct iobref *iobref);
void iobref_unref (struct iobref *iobref);
int iobref_add (struct iobref *iobref, struct iobuf *iobuf);
int iobref_merge (struct iobref *to, struct iobref *from);
void iobref_clear (struct iobref *iobref);

size_t iobuf_size (struct iobuf *iobuf);
size_t iobref_size (struct iobref *iobref);
void   iobuf_stats_dump (struct iobuf_pool *iobuf_pool);

struct iobuf *
iobuf_get2 (struct iobuf_pool *iobuf_pool, size_t page_size);
#endif /* !_IOBUF_H_ */
