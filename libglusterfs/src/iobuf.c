/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "iobuf.h"
#include "statedump.h"
#include <stdio.h>


/*
  TODO: implement destroy margins and prefetching of arenas
*/

//这里是8
#define IOBUF_ARENA_MAX_INDEX  (sizeof (gf_iobuf_init_config) /         \
                                (sizeof (struct iobuf_init_config)))

/* Make sure this array is sorted based on pagesize */
//初始化使用的配置
struct iobuf_init_config gf_iobuf_init_config[] = {
        /* { pagesize, num_pages }, */
        {128, 1024},
        {512, 512},
        {2 * 1024, 512},
        {8 * 1024, 128},
        {32 * 1024, 64},
        {128 * 1024, 32},
        {256 * 1024, 8},
        {1 * 1024 * 1024, 2},
};

//查找页大小所对应索引
int
gf_iobuf_get_arena_index (size_t page_size)
{
        int i = -1;

        for (i = 0; i < IOBUF_ARENA_MAX_INDEX; i++) {
                if (page_size <= gf_iobuf_init_config[i].pagesize)
                        break;
        }

        if (i >= IOBUF_ARENA_MAX_INDEX)
                i = -1;

        return i;
}

//获取页大小
size_t
gf_iobuf_get_pagesize (size_t page_size)
{
        int    i    = 0;
        size_t size = 0;

        for (i = 0; i < IOBUF_ARENA_MAX_INDEX; i++) {
                size = gf_iobuf_init_config[i].pagesize;
                if (page_size <= size)
                        break;
        }

        if (i >= IOBUF_ARENA_MAX_INDEX)
                size = -1;

        return size;
}

//iobuf区域初始化iobuf,一个页为一个iobuf
void
__iobuf_arena_init_iobufs (struct iobuf_arena *iobuf_arena)
{
        int                 iobuf_cnt = 0;
        struct iobuf       *iobuf = NULL;
        int                 offset = 0;
        int                 i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        iobuf_cnt  = iobuf_arena->page_count;

        iobuf_arena->iobufs = GF_CALLOC (sizeof (*iobuf), iobuf_cnt,
                                         gf_common_mt_iobuf);
        if (!iobuf_arena->iobufs)
                return;

        iobuf = iobuf_arena->iobufs;
        for (i = 0; i < iobuf_cnt; i++) {
                INIT_LIST_HEAD (&iobuf->list);
                LOCK_INIT (&iobuf->lock);

                iobuf->iobuf_arena = iobuf_arena;

                iobuf->ptr = iobuf_arena->mem_base + offset;

                list_add (&iobuf->list, &iobuf_arena->passive.list);
                iobuf_arena->passive_cnt++;

                offset += iobuf_arena->page_size;
                iobuf++;
        }

out:
        return;
}

//释放iobufs内存
void
__iobuf_arena_destroy_iobufs (struct iobuf_arena *iobuf_arena)
{
        int                 iobuf_cnt = 0;
        struct iobuf       *iobuf = NULL;
        int                 i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        iobuf_cnt  = iobuf_arena->page_count;

        if (!iobuf_arena->iobufs) {
                gf_log_callingfn (THIS->name, GF_LOG_ERROR, "iobufs not found");
                return;
        }

        iobuf = iobuf_arena->iobufs;
        for (i = 0; i < iobuf_cnt; i++) {
                GF_ASSERT (iobuf->ref == 0);
                //list的指针指向自己，内存还没释放
                list_del_init (&iobuf->list);
                iobuf++;
        }
        //释放内存
        GF_FREE (iobuf_arena->iobufs);

out:
        return;
}

//释放iobuf_arena内存
void
__iobuf_arena_destroy (struct iobuf_arena *iobuf_arena)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        __iobuf_arena_destroy_iobufs (iobuf_arena);

        if (iobuf_arena->mem_base
            && iobuf_arena->mem_base != MAP_FAILED)
                //关闭内存映射
                munmap (iobuf_arena->mem_base, iobuf_arena->arena_size);

        GF_FREE (iobuf_arena);
out:
        return;
}

//分配iobuf_arena内存和初始化
struct iobuf_arena *
__iobuf_arena_alloc (struct iobuf_pool *iobuf_pool, size_t page_size,
                     int32_t num_iobufs)
{
        struct iobuf_arena *iobuf_arena = NULL;
        size_t              rounded_size = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        iobuf_arena = GF_CALLOC (sizeof (*iobuf_arena), 1,
                                 gf_common_mt_iobuf_arena);
        if (!iobuf_arena)
                goto err;

        INIT_LIST_HEAD (&iobuf_arena->list);
        INIT_LIST_HEAD (&iobuf_arena->active.list);
        INIT_LIST_HEAD (&iobuf_arena->passive.list);
        iobuf_arena->iobuf_pool = iobuf_pool;

        //获取规定的页大小，大于等于page_size
        rounded_size = gf_iobuf_get_pagesize (page_size);

        iobuf_arena->page_size  = rounded_size;
        iobuf_arena->page_count = num_iobufs;

        iobuf_arena->arena_size = rounded_size * num_iobufs;

        //内存映射.这里是匿名映射，映射区不与任何文件关联，所以fd=-1
        iobuf_arena->mem_base = mmap (NULL, iobuf_arena->arena_size,
                                      PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (iobuf_arena->mem_base == MAP_FAILED) {
                gf_log (THIS->name, GF_LOG_WARNING, "maping failed");
                goto err;
        }

        __iobuf_arena_init_iobufs (iobuf_arena);
        if (!iobuf_arena->iobufs) {
                gf_log (THIS->name, GF_LOG_ERROR, "init failed");
                goto err;
        }

        iobuf_pool->arena_cnt++;

        return iobuf_arena;

err:
        __iobuf_arena_destroy (iobuf_arena);

out:
        return NULL;
}

//从purge列表找一个重用
struct iobuf_arena *
__iobuf_arena_unprune (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena  = NULL;
        struct iobuf_arena *tmp          = NULL;
        int                 index        = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        index = gf_iobuf_get_arena_index (page_size);
        if (index == -1) {
                gf_log ("iobuf", GF_LOG_ERROR, "page_size (%zu) of "
                        "iobufs in arena being added is greater than max "
                        "available", page_size);
                return NULL;
        }

        list_for_each_entry (tmp, &iobuf_pool->purge[index], list) {
                list_del_init (&tmp->list);
                iobuf_arena = tmp;
                break;
        }
out:
        return iobuf_arena;
}

//某页大小所对应的区域加到iobuf pool 中
struct iobuf_arena *
__iobuf_pool_add_arena (struct iobuf_pool *iobuf_pool, size_t page_size,
                        int32_t num_pages)
{
        struct iobuf_arena *iobuf_arena  = NULL;
        int                 index        = 0;

        //查找页大小所对应索引
        index = gf_iobuf_get_arena_index (page_size);
        if (index == -1) {
                gf_log ("iobuf", GF_LOG_ERROR, "page_size (%zu) of "
                        "iobufs in arena being added is greater than max "
                        "available", page_size);
                return NULL;
        }

        //从purge列表找一个重用
        iobuf_arena = __iobuf_arena_unprune (iobuf_pool, page_size);

        if (!iobuf_arena)
                //找不到，新分配iobuf_arena内存
                iobuf_arena = __iobuf_arena_alloc (iobuf_pool, page_size,
                                                   num_pages);

        if (!iobuf_arena) {
                gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                return NULL;
        }

        list_add_tail (&iobuf_arena->list, &iobuf_pool->arenas[index]);

        return iobuf_arena;
}

//iobuf pool 添加区域
struct iobuf_arena *
iobuf_pool_add_arena (struct iobuf_pool *iobuf_pool, size_t page_size,
                      int32_t num_pages)
{
        struct iobuf_arena *iobuf_arena = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                iobuf_arena = __iobuf_pool_add_arena (iobuf_pool, page_size,
                                                      num_pages);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return iobuf_arena;
}

//释放iobuf_pool内存
void
iobuf_pool_destroy (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *tmp         = NULL;
        int                 i           = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        for (i = 0; i < IOBUF_ARENA_MAX_INDEX; i++) {
                list_for_each_entry_safe (iobuf_arena, tmp,
                                          &iobuf_pool->arenas[i], list) {
                        list_del_init (&iobuf_arena->list);
                        iobuf_pool->arena_cnt--;
                        __iobuf_arena_destroy (iobuf_arena);
                }

        }

out:
        return;
}

/* 创建一个arena去处理大的iobuf请求*/
static void
iobuf_create_stdalloc_arena (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;

        /* No locking required here as its called only once during init */
        iobuf_arena = GF_CALLOC (sizeof (*iobuf_arena), 1,
                                 gf_common_mt_iobuf_arena);
        if (!iobuf_arena)
                goto err;

        INIT_LIST_HEAD (&iobuf_arena->list);
        INIT_LIST_HEAD (&iobuf_arena->active.list);
        INIT_LIST_HEAD (&iobuf_arena->passive.list);

        iobuf_arena->iobuf_pool = iobuf_pool;

        iobuf_arena->page_size = 0x7fffffff;

        list_add_tail (&iobuf_arena->list,
                       &iobuf_pool->arenas[IOBUF_ARENA_MAX_INDEX]);

err:
        return;
}

//分配iobuf_pool内存
struct iobuf_pool *
iobuf_pool_new (void)
{
        struct iobuf_pool  *iobuf_pool = NULL;
        int                 i          = 0;
        size_t              page_size  = 0;
        size_t              arena_size = 0;
        int32_t             num_pages  = 0;

        //此时iobuf_pool还没有值，已分配地址
        iobuf_pool = GF_CALLOC (sizeof (*iobuf_pool), 1,
                                gf_common_mt_iobuf_pool);
        if (!iobuf_pool)
                goto out;

        pthread_mutex_init (&iobuf_pool->mutex, NULL);
        for (i = 0; i <= IOBUF_ARENA_MAX_INDEX; i++) {
                INIT_LIST_HEAD (&iobuf_pool->arenas[i]);
                INIT_LIST_HEAD (&iobuf_pool->filled[i]);
                INIT_LIST_HEAD (&iobuf_pool->purge[i]);
        }
        // 128 change to 256
        iobuf_pool->default_page_size  = 128 * GF_UNIT_KB;

        arena_size = 0;
        for (i = 0; i < IOBUF_ARENA_MAX_INDEX; i++) {
                page_size = gf_iobuf_init_config[i].pagesize;
                num_pages = gf_iobuf_init_config[i].num_pages;

                iobuf_pool_add_arena (iobuf_pool, page_size, num_pages);

                arena_size += page_size * num_pages;
        }

        /* Need an arena to handle all the bigger iobuf requests */
        iobuf_create_stdalloc_arena (iobuf_pool);

        iobuf_pool->arena_size = arena_size;
out:

        return iobuf_pool;
}

//删除iobuf_arena
void
__iobuf_arena_prune (struct iobuf_pool *iobuf_pool,
                     struct iobuf_arena *iobuf_arena, int index)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        /* code flow comes here only if the arena is in purge list and we can
         * free the arena only if we have atleast one arena in 'arenas' list
         * (ie, at least few iobufs free in arena), that way, there won't
         * be spurious mmap/unmap of buffers
         */
        if (list_empty (&iobuf_pool->arenas[index]))
                goto out;

        /* All cases matched, destroy */
        list_del_init (&iobuf_arena->list);
        iobuf_pool->arena_cnt--;

        __iobuf_arena_destroy (iobuf_arena);

out:
        return;
}

//删除iobuf pool中的所有arena
void
iobuf_pool_prune (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *tmp         = NULL;
        int                 i           = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                for (i = 0; i < IOBUF_ARENA_MAX_INDEX; i++) {
                        if (list_empty (&iobuf_pool->arenas[i])) {
                                continue;
                        }

                        list_for_each_entry_safe (iobuf_arena, tmp,
                                                  &iobuf_pool->purge[i], list) {
                                __iobuf_arena_prune (iobuf_pool, iobuf_arena, i);
                        }
                }
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return;
}

//挑选一个iobuf_arena
struct iobuf_arena *
__iobuf_select_arena (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena  = NULL;
        struct iobuf_arena *trav         = NULL;
        int                 index        = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        index = gf_iobuf_get_arena_index (page_size);
        if (index == -1) {
                gf_log ("iobuf", GF_LOG_ERROR, "page_size (%zu) of "
                        "iobufs in arena being added is greater than max "
                        "available", page_size);
                return NULL;
        }

        /* look for unused iobuf from the head-most arena */
        list_for_each_entry (trav, &iobuf_pool->arenas[index], list) {
                //该区域还有没使用的iobuf
                if (trav->passive_cnt) {
                        iobuf_arena = trav;
                        break;
                }
        }

        if (!iobuf_arena) {
                /* all arenas were full, find the right count to add */
                //如果所有arenas都满了，新建一个arena
                iobuf_arena = __iobuf_pool_add_arena (iobuf_pool, page_size,
                                                      gf_iobuf_init_config[index].num_pages);
        }

out:
        return iobuf_arena;
}

//引用iobuf
struct iobuf *
__iobuf_ref (struct iobuf *iobuf)
{
        iobuf->ref++;

        return iobuf;
}

//撤销引用iobuf
struct iobuf *
__iobuf_unref (struct iobuf *iobuf)
{
        iobuf->ref--;

        return iobuf;
}

//从iobuf arena中选一个没使用的iobuf
struct iobuf *
__iobuf_get (struct iobuf_arena *iobuf_arena, size_t page_size)
{
        struct iobuf      *iobuf        = NULL;
        struct iobuf_pool *iobuf_pool   = NULL;
        int                index        = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        iobuf_pool = iobuf_arena->iobuf_pool;

        list_for_each_entry (iobuf, &iobuf_arena->passive.list, list)
                break;

        list_del (&iobuf->list);
        iobuf_arena->passive_cnt--;

        list_add (&iobuf->list, &iobuf_arena->active.list);
        iobuf_arena->active_cnt++;

        /* no resetting requied for this element */
        iobuf_arena->alloc_cnt++;

        if (iobuf_arena->max_active < iobuf_arena->active_cnt)
                iobuf_arena->max_active = iobuf_arena->active_cnt;

        //如果没有iobuf可以用了，arena移到filledlie列表
        if (iobuf_arena->passive_cnt == 0) {
                index = gf_iobuf_get_arena_index (page_size);
                if (index == -1) {
                        gf_log ("iobuf", GF_LOG_ERROR, "page_size (%zu) of "
                                "iobufs in arena being added is greater "
                                "than max available", page_size);
                        goto out;
                }

                list_del (&iobuf_arena->list);
                list_add (&iobuf_arena->list, &iobuf_pool->filled[index]);
        }

out:
        return iobuf;
}

struct iobuf *
iobuf_get_from_stdalloc (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf       *iobuf       = NULL;
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *trav        = NULL;
        int                 ret         = -1;

        /* The first arena in the 'MAX-INDEX' will always be used for misc */
        list_for_each_entry (trav, &iobuf_pool->arenas[IOBUF_ARENA_MAX_INDEX],
                             list) {
                iobuf_arena = trav;
                break;
        }

        iobuf = GF_CALLOC (1, sizeof (*iobuf), gf_common_mt_iobuf);
        if (!iobuf)
                goto out;

        /* 4096 is the alignment */
        iobuf->free_ptr = GF_CALLOC (1, ((page_size + GF_IOBUF_ALIGN_SIZE) - 1),
                                     gf_common_mt_char);
        if (!iobuf->free_ptr)
                goto out;

        iobuf->ptr = GF_ALIGN_BUF (iobuf->free_ptr, GF_IOBUF_ALIGN_SIZE);
        iobuf->iobuf_arena = iobuf_arena;
        LOCK_INIT (&iobuf->lock);

        /* Hold a ref because you are allocating and using it */
        iobuf->ref = 1;

        ret = 0;
out:
        if (ret && iobuf) {
                GF_FREE (iobuf->free_ptr);
                GF_FREE (iobuf);
                iobuf = NULL;
        }

        return iobuf;
}

//从iobuf pool中选一个iobuf
struct iobuf *
iobuf_get2 (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf       *iobuf        = NULL;
        struct iobuf_arena *iobuf_arena  = NULL;
        size_t              rounded_size = 0;

        if (page_size == 0) {
                page_size = iobuf_pool->default_page_size;
        }

        rounded_size = gf_iobuf_get_pagesize (page_size);
        if (rounded_size == -1) {
                /* make sure to provide the requested buffer with standard
                   memory allocations */
                iobuf = iobuf_get_from_stdalloc (iobuf_pool, page_size);

                gf_log ("iobuf", GF_LOG_DEBUG, "request for iobuf of size %zu "
                        "is serviced using standard calloc() (%p) as it "
                        "exceeds the maximum available buffer size",
                        page_size, iobuf);

                iobuf_pool->request_misses++;
                return iobuf;
        }

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                /* most eligible arena for picking an iobuf */
                iobuf_arena = __iobuf_select_arena (iobuf_pool, rounded_size);
                if (!iobuf_arena)
                        goto unlock;

                iobuf = __iobuf_get (iobuf_arena, rounded_size);
                if (!iobuf)
                        goto unlock;

                __iobuf_ref (iobuf);
         }
unlock:
        pthread_mutex_unlock (&iobuf_pool->mutex);

        return iobuf;
}

//从iobuf pool中选一个iobuf，页大小为默认
struct iobuf *
iobuf_get (struct iobuf_pool *iobuf_pool)
{
        struct iobuf       *iobuf        = NULL;
        struct iobuf_arena *iobuf_arena  = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                /* most eligible arena for picking an iobuf */
                iobuf_arena = __iobuf_select_arena (iobuf_pool,
                                                    iobuf_pool->default_page_size);
                if (!iobuf_arena) {
                        gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                        goto unlock;
                }

                iobuf = __iobuf_get (iobuf_arena,
                                     iobuf_pool->default_page_size);
                if (!iobuf) {
                        gf_log (THIS->name, GF_LOG_WARNING, "iobuf not found");
                        goto unlock;
                }

                __iobuf_ref (iobuf);
        }
unlock:
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return iobuf;
}

//把一个使用完的iobuf放回iobuf arena
void
__iobuf_put (struct iobuf *iobuf, struct iobuf_arena *iobuf_arena)
{
        struct iobuf_pool *iobuf_pool = NULL;
        int                index      = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        iobuf_pool = iobuf_arena->iobuf_pool;

        index = gf_iobuf_get_arena_index (iobuf_arena->page_size);
        if (index == -1) {
                gf_log ("iobuf", GF_LOG_DEBUG, "freeing the iobuf (%p) "
                        "allocated with standard calloc()", iobuf);

                /* free up properly without bothering about lists and all */
                LOCK_DESTROY (&iobuf->lock);
                GF_FREE (iobuf->free_ptr);
                GF_FREE (iobuf);
                return;
        }

        if (iobuf_arena->passive_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add_tail (&iobuf_arena->list, &iobuf_pool->arenas[index]);
        }

        list_del_init (&iobuf->list);
        iobuf_arena->active_cnt--;

        list_add (&iobuf->list, &iobuf_arena->passive.list);
        iobuf_arena->passive_cnt++;

        if (iobuf_arena->active_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add_tail (&iobuf_arena->list, &iobuf_pool->purge[index]);
                __iobuf_arena_prune (iobuf_pool, iobuf_arena, index);
        }
out:
        return;
}

//把一个使用完的iobuf放回
void
iobuf_put (struct iobuf *iobuf)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_pool  *iobuf_pool = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        iobuf_arena = iobuf->iobuf_arena;
        if (!iobuf_arena) {
                gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                return;
        }

        iobuf_pool = iobuf_arena->iobuf_pool;
        if (!iobuf_pool) {
                gf_log (THIS->name, GF_LOG_WARNING, "iobuf pool not found");
                return;
        }

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                __iobuf_put (iobuf, iobuf_arena);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return;
}

//撤销对iobuf的引用，如果引用为0，放回iobuf池
void
iobuf_unref (struct iobuf *iobuf)
{
        int  ref = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        LOCK (&iobuf->lock);
        {
                __iobuf_unref (iobuf);
                ref = iobuf->ref;
        }
        UNLOCK (&iobuf->lock);

        if (!ref)
                iobuf_put (iobuf);

out:
        return;
}


//返回iobuf,iobuf->ref加1
struct iobuf *
iobuf_ref (struct iobuf *iobuf)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        LOCK (&iobuf->lock);
        {
                __iobuf_ref (iobuf);
        }
        UNLOCK (&iobuf->lock);

out:
        return iobuf;
}

//新建一个iobref
struct iobref *
iobref_new ()
{
        struct iobref *iobref = NULL;

        iobref = GF_CALLOC (sizeof (*iobref), 1,
                            gf_common_mt_iobref);
        if (!iobref)
                return NULL;

	iobref->iobrefs = GF_CALLOC (sizeof (*iobref->iobrefs),
				     16, gf_common_mt_iobrefs);
	if (!iobref->iobrefs) {
		GF_FREE (iobref);
		return NULL;
	}

	iobref->alloced = 16;
	iobref->used = 0;

        LOCK_INIT (&iobref->lock);

        iobref->ref++;

        return iobref;
}

//引用iobref
struct iobref *
iobref_ref (struct iobref *iobref)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        LOCK (&iobref->lock);
        {
                iobref->ref++;
        }
        UNLOCK (&iobref->lock);

out:
        return iobref;
}

//释放iobref内存
void
iobref_destroy (struct iobref *iobref)
{
        int            i = 0;
        struct iobuf  *iobuf = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        for (i = 0; i < iobref->alloced; i++) {
                iobuf = iobref->iobrefs[i];

                iobref->iobrefs[i] = NULL;
                if (iobuf)
                        iobuf_unref (iobuf);
        }

	GF_FREE (iobref->iobrefs);
        GF_FREE (iobref);

out:
        return;
}

//撤销对iobref的引用，如果引用为0，释放内存
void
iobref_unref (struct iobref *iobref)
{
        int ref = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        LOCK (&iobref->lock);
        {
                ref = (--iobref->ref);
        }
        UNLOCK (&iobref->lock);

        if (!ref)
                iobref_destroy (iobref);

out:
        return;
}


//撤销对iobref中所记录的所有iobuf的引用，在撤销对iobref的引用
void
iobref_clear (struct iobref *iobref)
{
        int i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        for (; i < iobref->alloced; i++) {
                if (iobref->iobrefs[i] != NULL) {
                        //撤销对iobuf的引用
                        iobuf_unref (iobref->iobrefs[i]);
                } else {
                        /** iobuf's are attched serially */
                        break;
                }
        }

        iobref_unref (iobref);

 out:
        return;
}

//增大iobref中分配的大小
static void
__iobref_grow (struct iobref *iobref)
{
	void *newptr = NULL;
	int i = 0;

	newptr = GF_REALLOC (iobref->iobrefs,
			     iobref->alloced * 2 * (sizeof (*iobref->iobrefs)));
	if (newptr) {
		iobref->iobrefs = newptr;
		iobref->alloced *= 2;

		for (i = iobref->used; i < iobref->alloced; i++)
			iobref->iobrefs[i] = NULL;
	}
}

//iobref中新增一个iobuf记录
int
__iobref_add (struct iobref *iobref, struct iobuf *iobuf)
{
        int  i = 0;
        int  ret = -ENOMEM;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

	if (iobref->used == iobref->alloced) {
		__iobref_grow (iobref);

		if (iobref->used == iobref->alloced) {
			ret = -ENOMEM;
			goto out;
		}
	}

        for (i = 0; i < iobref->alloced; i++) {
                if (iobref->iobrefs[i] == NULL) {
                        iobref->iobrefs[i] = iobuf_ref (iobuf);
			iobref->used++;
                        ret = 0;
                        break;
                }
        }

out:
        return ret;
}

//iobref中新增一个iobuf记录
int
iobref_add (struct iobref *iobref, struct iobuf *iobuf)
{
        int  ret = -EINVAL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        LOCK (&iobref->lock);
        {
                ret = __iobref_add (iobref, iobuf);
        }
        UNLOCK (&iobref->lock);

out:
        return ret;
}

//合并两个iobref
int
iobref_merge (struct iobref *to, struct iobref *from)
{
        int           i = 0;
        int           ret = -1;
        struct iobuf *iobuf = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", to, out);
        GF_VALIDATE_OR_GOTO ("iobuf", from, out);

        LOCK (&from->lock);
        {
                for (i = 0; i < from->alloced; i++) {
                        iobuf = from->iobrefs[i];

                        if (!iobuf)
                                break;

                        ret = iobref_add (to, iobuf);

                        if (ret < 0)
                                break;
                }
        }
        UNLOCK (&from->lock);

out:
        return ret;
}

//返回iobuf的大小
size_t
iobuf_size (struct iobuf *iobuf)
{
        size_t size = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        if (!iobuf->iobuf_arena) {
                gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                goto out;
        }

        if (!iobuf->iobuf_arena->iobuf_pool) {
                gf_log (THIS->name, GF_LOG_WARNING, "pool not found");
                goto out;
        }

        size = iobuf->iobuf_arena->page_size;
out:
        return size;
}

//返回iobref中记录的iobuf大小之和
size_t
iobref_size (struct iobref *iobref)
{
        size_t size = 0;
        int    i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        LOCK (&iobref->lock);
        {
                for (i = 0; i < iobref->alloced; i++) {
                        if (iobref->iobrefs[i])
                                size += iobuf_size (iobref->iobrefs[i]);
                }
        }
        UNLOCK (&iobref->lock);

out:
        return size;
}

//iobuf信息输出
void
iobuf_info_dump (struct iobuf *iobuf, const char *key_prefix)
{
        char   key[GF_DUMP_MAX_BUF_LEN];
        struct iobuf my_iobuf;
        int    ret = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        memset(&my_iobuf, 0, sizeof(my_iobuf));

        ret = TRY_LOCK(&iobuf->lock);
        if (ret) {
                return;
        }
        memcpy(&my_iobuf, iobuf, sizeof(my_iobuf));
        UNLOCK(&iobuf->lock);

        gf_proc_dump_build_key(key, key_prefix,"ref");
        gf_proc_dump_write(key, "%d", my_iobuf.ref);
        gf_proc_dump_build_key(key, key_prefix,"ptr");
        gf_proc_dump_write(key, "%p", my_iobuf.ptr);

out:
        return;
}

//iobuf arena信息输出
void
iobuf_arena_info_dump (struct iobuf_arena *iobuf_arena, const char *key_prefix)
{
        char key[GF_DUMP_MAX_BUF_LEN];
        int  i = 1;
        struct iobuf *trav;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        gf_proc_dump_build_key(key, key_prefix,"mem_base");
        gf_proc_dump_write(key, "%p", iobuf_arena->mem_base);
        gf_proc_dump_build_key(key, key_prefix, "active_cnt");
        gf_proc_dump_write(key, "%d", iobuf_arena->active_cnt);
        gf_proc_dump_build_key(key, key_prefix, "passive_cnt");
        gf_proc_dump_write(key, "%d", iobuf_arena->passive_cnt);
        gf_proc_dump_build_key(key, key_prefix, "alloc_cnt");
        gf_proc_dump_write(key, "%"PRIu64, iobuf_arena->alloc_cnt);
        gf_proc_dump_build_key(key, key_prefix, "max_active");
        gf_proc_dump_write(key, "%"PRIu64, iobuf_arena->max_active);
        gf_proc_dump_build_key(key, key_prefix, "page_size");
        gf_proc_dump_write(key, "%"PRIu64, iobuf_arena->page_size);
        list_for_each_entry (trav, &iobuf_arena->active.list, list) {
                gf_proc_dump_build_key(key, key_prefix,"active_iobuf.%d", i++);
                gf_proc_dump_add_section(key);
                iobuf_info_dump(trav, key);
        }

out:
        return;
}

//iobuf pool 统计信息输出
void
iobuf_stats_dump (struct iobuf_pool *iobuf_pool)
{
        char               msg[1024];
        struct iobuf_arena *trav = NULL;
        int                i = 1;
        int                j = 0;
        int                ret = -1;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        memset(msg, 0, sizeof(msg));

        ret = pthread_mutex_trylock(&iobuf_pool->mutex);

        if (ret) {
                return;
        }
        gf_proc_dump_add_section("iobuf.global");
        gf_proc_dump_write("iobuf_pool","%p", iobuf_pool);
        gf_proc_dump_write("iobuf_pool.default_page_size", "%d",
                                                iobuf_pool->default_page_size);
        gf_proc_dump_write("iobuf_pool.arena_size", "%d",
                           iobuf_pool->arena_size);
        gf_proc_dump_write("iobuf_pool.arena_cnt", "%d",
                           iobuf_pool->arena_cnt);
        gf_proc_dump_write("iobuf_pool.request_misses", "%"PRId64,
                           iobuf_pool->request_misses);

        for (j = 0; j < IOBUF_ARENA_MAX_INDEX; j++) {
                list_for_each_entry (trav, &iobuf_pool->arenas[j], list) {
                        snprintf(msg, sizeof(msg),
                                 "arena.%d", i);
                        gf_proc_dump_add_section(msg);
                        iobuf_arena_info_dump(trav,msg);
                        i++;
                }
                list_for_each_entry (trav, &iobuf_pool->purge[j], list) {
                        snprintf(msg, sizeof(msg),
                                 "purge.%d", i);
                        gf_proc_dump_add_section(msg);
                        iobuf_arena_info_dump(trav,msg);
                        i++;
                }
                list_for_each_entry (trav, &iobuf_pool->filled[j], list) {
                        snprintf(msg, sizeof(msg),
                                 "filled.%d", i);
                        gf_proc_dump_add_section(msg);
                        iobuf_arena_info_dump(trav,msg);
                        i++;
                }

        }

        pthread_mutex_unlock(&iobuf_pool->mutex);

out:
        return;
}

//iobuf转io向量
void
iobuf_to_iovec(struct iobuf *iob, struct iovec *iov)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iob, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iov, out);

        iov->iov_base = iobuf_ptr (iob);
        iov->iov_len =  iobuf_pagesize (iob);

out:
        return;
}
