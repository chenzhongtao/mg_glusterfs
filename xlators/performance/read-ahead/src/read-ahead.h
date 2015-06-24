/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __READ_AHEAD_H
#define __READ_AHEAD_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "read-ahead-mem-types.h"

struct ra_conf;
struct ra_local;
struct ra_page;
struct ra_file;
struct ra_waitq;


struct ra_waitq {
        struct ra_waitq *next;
        void            *data;
};


struct ra_fill {
        struct ra_fill *next;
        struct ra_fill *prev;
        off_t           offset;
        size_t          size;
        struct iovec   *vector;
        int32_t         count;
        struct iobref  *iobref;
};


struct ra_local {
        mode_t            mode;
        struct ra_fill    fill;
        off_t             offset;
        size_t            size;
        int32_t           op_ret;
        int32_t           op_errno;
        off_t             pending_offset;
        size_t            pending_size;
        fd_t             *fd;
        int32_t           wait_count; 
        pthread_mutex_t   local_lock;
};


struct ra_page {
        struct ra_page   *next;
        struct ra_page   *prev;
        struct ra_file   *file;     //属于哪个file
        char              dirty;    /* Internal request, not from user. */
        char              poisoned; /* Pending read invalidated by write. 在读的期间，由于有写入使数据无效*/
        char              ready;    // 数据更新后置1
        struct iovec     *vector;   // 存放数据的数组
        int32_t           count;    // iovecc数组的长度
        off_t             offset;   //page的偏移量
        size_t            size;     //page 数据的大小
        struct ra_waitq  *waitq;
        struct iobref    *iobref;
        char              stale;   //page的数据是旧的
};


struct ra_file {
        struct ra_file    *next;
        struct ra_file    *prev;
        struct ra_conf    *conf;     //对应的ra_conf结构体
        fd_t              *fd;       //对应的fd,表明这个缓存结构体是属于哪个打开的文件的
        int                disabled; // 是否需要缓存
        size_t             expected; //期望读完的偏移量
        struct ra_page     pages;    // page的头节点
        off_t              offset;   //file 当前的偏移量
        size_t             size;
        int32_t            refcount;
        pthread_mutex_t    file_lock;
        struct iatt        stbuf;
        uint64_t           page_size; // 等于对应ra_conf的page_size
        uint32_t           page_count;// 占用了多少个page
};


struct ra_conf {
        uint64_t          page_size;  //页大小，默认131072
        uint32_t          page_count; //页数，默认4
        void             *cache_block;
        struct ra_file    files; //file链表头节点
        gf_boolean_t      force_atime_update; //是否强制更新，默认false
        pthread_mutex_t   conf_lock;
};


typedef struct ra_conf ra_conf_t;
typedef struct ra_local ra_local_t;
typedef struct ra_page ra_page_t;
typedef struct ra_file ra_file_t;
typedef struct ra_waitq ra_waitq_t;
typedef struct ra_fill ra_fill_t;

ra_page_t *
ra_page_get (ra_file_t *file,
             off_t offset);

ra_page_t *
ra_page_create (ra_file_t *file,
                off_t offset);

void
ra_page_fault (ra_file_t *file,
               call_frame_t *frame,
               off_t offset);
void
ra_wait_on_page (ra_page_t *page,
                 call_frame_t *frame);

ra_waitq_t *
ra_page_wakeup (ra_page_t *page);

void
ra_page_flush (ra_page_t *page);

ra_waitq_t *
ra_page_error (ra_page_t *page,
               int32_t op_ret,
               int32_t op_errno);
void
ra_page_purge (ra_page_t *page);

void
ra_frame_return (call_frame_t *frame);

void
ra_frame_fill (ra_page_t *page,
               call_frame_t *frame);

void
ra_file_destroy (ra_file_t *file);

static inline void
ra_file_lock (ra_file_t *file)
{
        pthread_mutex_lock (&file->file_lock);
}

static inline void
ra_file_unlock (ra_file_t *file)
{
        pthread_mutex_unlock (&file->file_lock);
}

static inline void
ra_conf_lock (ra_conf_t *conf)
{
        pthread_mutex_lock (&conf->conf_lock);
}

static inline void
ra_conf_unlock (ra_conf_t *conf)
{
        pthread_mutex_unlock (&conf->conf_lock);
}
static inline void
ra_local_lock (ra_local_t *local)
{
        pthread_mutex_lock (&local->local_lock);
}

static inline void
ra_local_unlock (ra_local_t *local)
{
        pthread_mutex_unlock (&local->local_lock);
}

#endif /* __READ_AHEAD_H */
