/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-cache.h"
#include "ioc-mem-types.h"
#include <assert.h>
#include <sys/time.h>

//空返回1
char
ioc_empty (struct ioc_cache *cache)
{
        char is_empty = -1;

        GF_VALIDATE_OR_GOTO ("io-cache", cache, out);

        is_empty = list_empty (&cache->page_lru);

out:
        return is_empty;
}


// 根据offset获取一页
ioc_page_t *
__ioc_page_get (ioc_inode_t *ioc_inode, off_t offset)
{
        ioc_page_t   *page           = NULL;
        ioc_table_t  *table          = NULL;
        off_t         rounded_offset = 0;

        GF_VALIDATE_OR_GOTO ("io-cache", ioc_inode, out);

        table = ioc_inode->table;
        GF_VALIDATE_OR_GOTO ("io-cache", ioc_inode, out);

        rounded_offset = floor (offset, table->page_size);

        // 这里使用rbthash是为了加速获取，不然要遍历整个ioc_inode->cache.page_lru
        page = rbthash_get (ioc_inode->cache.page_table, &rounded_offset,
                            sizeof (rounded_offset));

        if (page != NULL) {
                /* push the page to the end of the lru list */
                list_move_tail (&page->page_lru, &ioc_inode->cache.page_lru);
        }

out:
        return page;
}


ioc_page_t *
ioc_page_get (ioc_inode_t *ioc_inode, off_t offset)
{
        ioc_page_t *page = NULL;

        if (ioc_inode == NULL) {
                goto out;
        }

        ioc_inode_lock (ioc_inode);
        {
                page = __ioc_page_get (ioc_inode, offset);
        }
        ioc_inode_unlock (ioc_inode);

out:
        return page;
}


/*
 * __ioc_page_destroy -
 *
 * @page:
 *
 */
 //释放ioc page
int64_t
__ioc_page_destroy (ioc_page_t *page)
{
        int64_t  page_size = 0;

        GF_VALIDATE_OR_GOTO ("io-cache", page, out);

        if (page->iobref)
                //返回iobref中记录的iobuf大小之和
                page_size = iobref_size (page->iobref);

        // 还有frame在等待这一页，不能释放
        if (page->waitq) {
                /* frames waiting on this page, do not destroy this page */
                page_size = -1;
                page->stale = 1; // 不新鲜的
        } else {
                rbthash_remove (page->inode->cache.page_table, &page->offset,
                                sizeof (page->offset));
                list_del (&page->page_lru);

                gf_log (page->inode->table->xl->name, GF_LOG_TRACE,
                        "destroying page = %p, offset = %"PRId64" "
                        "&& inode = %p",
                        page, page->offset, page->inode);

                if (page->vector){
                        iobref_unref (page->iobref);
                        GF_FREE (page->vector);
                        page->vector = NULL;
                }

                page->inode = NULL;
        }

        if (page_size != -1) {
                pthread_mutex_destroy (&page->page_lock);
                GF_FREE (page);
        }

out:
        return page_size;
}


int64_t
ioc_page_destroy (ioc_page_t *page)
{
        int64_t ret = 0;
        struct ioc_inode *inode = NULL;

        if (page == NULL) {
                goto out;
        }

        ioc_inode_lock (page->inode);
        {
                inode = page->inode;
                ret = __ioc_page_destroy (page);
        }
        ioc_inode_unlock (inode);

out:
        return ret;
}

//释放ioc_inode的page， size_pruned 已释放缓存大小，size_to_prune需要释放缓存的大小,index 优先级
int32_t
__ioc_inode_prune (ioc_inode_t *curr, uint64_t *size_pruned,
                   uint64_t size_to_prune, uint32_t index)
{
        ioc_page_t  *page  = NULL, *next = NULL;
        int32_t      ret   = 0;
        ioc_table_t *table = NULL;

        if (curr == NULL) {
                goto out;
        }

        table = curr->table;

        list_for_each_entry_safe (page, next, &curr->cache.page_lru, page_lru) {
                *size_pruned += page->size;
                ret = __ioc_page_destroy (page);

                if (ret != -1)
                        table->cache_used -= ret;

                gf_log (table->xl->name, GF_LOG_TRACE,
                        "index = %d && table->cache_used = %"PRIu64" && table->"
                        "cache_size = %"PRIu64, index, table->cache_used,
                        table->cache_size);

                if ((*size_pruned) >= size_to_prune)
                        break;
        }

        if (ioc_empty (&curr->cache)) {
                list_del_init (&curr->inode_lru);
        }

out:
        return 0;
}
/*
 * ioc_prune - prune the cache. we have a limit to the number of pages we
 *             can have in-memory.
 *
 * @table: ioc_table_t of this translator
 *
 */
 // 清理一些缓存，重低优先级开始清理
int32_t
ioc_prune (ioc_table_t *table)
{
        ioc_inode_t *curr          = NULL, *next_ioc_inode = NULL;
        int32_t      index         = 0;
        uint64_t     size_to_prune = 0;
        uint64_t     size_pruned   = 0;

        GF_VALIDATE_OR_GOTO ("io-cache", table, out);

        ioc_table_lock (table);
        {
                //需要释放缓存的大小
                size_to_prune = table->cache_used - table->cache_size;
                /* take out the least recently used inode */
                for (index=0; index < table->max_pri; index++) {
                        list_for_each_entry_safe (curr, next_ioc_inode,
                                                  &table->inode_lru[index],
                                                  inode_lru) {
                                /* prune page-by-page for this inode, till
                                 * we reach the equilibrium */
                                ioc_inode_lock (curr);
                                {
                                        //释放ioc_inode的page
                                        __ioc_inode_prune (curr, &size_pruned,
                                                           size_to_prune,
                                                           index);
                                }
                                ioc_inode_unlock (curr);

                                if (size_pruned >= size_to_prune)
                                        break;
                        } /* list_for_each_entry_safe (curr...) */

                        if (size_pruned >= size_to_prune)
                                break;
                } /* for(index=0;...) */

        } /* ioc_inode_table locked region end */
        ioc_table_unlock (table);

out:
        return 0;
}

/*
 * __ioc_page_create - create a new page.
 *
 * @ioc_inode:
 * @offset:
 *
 */
ioc_page_t *
__ioc_page_create (ioc_inode_t *ioc_inode, off_t offset)
{
        ioc_table_t *table          = NULL;
        ioc_page_t  *page           = NULL;
        off_t        rounded_offset = 0;
        ioc_page_t  *newpage        = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", ioc_inode, out);

        table = ioc_inode->table;
        GF_VALIDATE_OR_GOTO ("io-cache", table, out);

        rounded_offset = floor (offset, table->page_size);

        // 创建新的一页
        newpage = GF_CALLOC (1, sizeof (*newpage), gf_ioc_mt_ioc_newpage_t);
        if (newpage == NULL) {
                goto out;
        }

        if (!ioc_inode) {
                GF_FREE (newpage);
                newpage = NULL;
                goto out;
        }
        //页对应的offset
        newpage->offset = rounded_offset;
        //页对应的inode
        newpage->inode = ioc_inode;
        pthread_mutex_init (&newpage->page_lock, NULL);

        rbthash_insert (ioc_inode->cache.page_table, newpage, &rounded_offset,
                        sizeof (rounded_offset));

        list_add_tail (&newpage->page_lru, &ioc_inode->cache.page_lru);

        page = newpage;

        gf_log ("io-cache", GF_LOG_TRACE,
                "returning new page %p", page);

out:
        return page;
}

/*
 * ioc_wait_on_page - pause a frame to wait till the arrival of a page.
 * here we need to handle the case when the frame who calls wait_on_page
 * himself has caused page_fault
 *
 * @page: page to wait on
 * @frame: call frame who is waiting on page
 *
 */
 // 添加到page等待的等待队列中
void
__ioc_wait_on_page (ioc_page_t *page, call_frame_t *frame, off_t offset,
                    size_t size)
{
        ioc_waitq_t *waitq = NULL;
        ioc_local_t *local = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", frame, out);
        local = frame->local;

        GF_VALIDATE_OR_GOTO (frame->this->name, local, out);
        if (page == NULL) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "asked to wait on a NULL page");
                goto out;
        }

        //创建一个等待队列项，加入到page->waitq队列中
        waitq = GF_CALLOC (1, sizeof (*waitq), gf_ioc_mt_ioc_waitq_t);
        if (waitq == NULL) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto out;
        }

        gf_log (frame->this->name, GF_LOG_TRACE,
                "frame(%p) waiting on page = %p, offset=%"PRId64", "
                "size=%"GF_PRI_SIZET"",
                frame, page, offset, size);

        waitq->data = frame;
        //头插到page->waitq中
        waitq->next = page->waitq;
        waitq->pending_offset = offset;
        waitq->pending_size = size;
        page->waitq = waitq;
        /* one frame can wait only once on a given page,
         * local->wait_count is number of pages a frame is waiting on */
        ioc_local_lock (local);
        {
                local->wait_count++;
        }
        ioc_local_unlock (local);

out:
        return;
}


/*
 * ioc_cache_still_valid - see if cached pages ioc_inode are still valid
 * against given stbuf
 *
 * @ioc_inode:
 * @stbuf:
 *
 * assumes ioc_inode is locked
 */
// 判断缓存是否还有效
int8_t
ioc_cache_still_valid (ioc_inode_t *ioc_inode, struct iatt *stbuf)
{
        int8_t cache_still_valid = 1;

        GF_VALIDATE_OR_GOTO ("io-cache", ioc_inode, out);

#if 0
        if (!stbuf || (stbuf->ia_mtime != ioc_inode->cache.mtime) ||
            (stbuf->st_mtim.tv_nsec != ioc_inode->stbuf.st_mtim.tv_nsec))
                cache_still_valid = 0;

#else
        if (!stbuf || (stbuf->ia_mtime != ioc_inode->cache.mtime)
            || (stbuf->ia_mtime_nsec != ioc_inode->cache.mtime_nsec))
                cache_still_valid = 0;

#endif

#if 0
        /* talk with avati@gluster.com to enable this section */
        if (!ioc_inode->mtime && stbuf) {
                cache_still_valid = 1;
                ioc_inode->mtime = stbuf->ia_mtime;
        }
#endif

out:
        return cache_still_valid;
}

//page等待队列返回
void
ioc_waitq_return (ioc_waitq_t *waitq)
{
        ioc_waitq_t  *trav   = NULL;
        ioc_waitq_t  *next   = NULL;
        call_frame_t *frame  = NULL;

        for (trav = waitq; trav; trav = next) {
                next = trav->next;

                frame = trav->data;
                ioc_frame_return (frame);
                GF_FREE (trav);
        }
}

// 页错误回调函数
int
ioc_fault_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iovec *vector,
               int32_t count, struct iatt *stbuf, struct iobref *iobref,
               dict_t *xdata)
{
        ioc_local_t *local            = NULL;
        off_t        offset           = 0;
        ioc_inode_t *ioc_inode        = NULL;
        ioc_table_t *table            = NULL;
        ioc_page_t  *page             = NULL;
        int32_t      destroy_size     = 0;
        size_t       page_size        = 0;
        ioc_waitq_t *waitq            = NULL;
        size_t       iobref_page_size = 0;
        char         zero_filled      = 0;

        GF_ASSERT (frame);

        local = frame->local;
        GF_ASSERT (local);

        offset = local->pending_offset;
        ioc_inode = local->inode;
        GF_ASSERT (ioc_inode);

        table = ioc_inode->table;
        GF_ASSERT (table);

        zero_filled = ((op_ret >=0) && (stbuf->ia_mtime == 0));

        ioc_inode_lock (ioc_inode);
        {
                if (op_ret == -1 || !(zero_filled ||
                                      ioc_cache_still_valid(ioc_inode,
                                                            stbuf))) {
                        gf_log (ioc_inode->table->xl->name, GF_LOG_TRACE,
                                "cache for inode(%p) is invalid. flushing "
                                "all pages", ioc_inode);
                        destroy_size = __ioc_inode_flush (ioc_inode);
                }

                if ((op_ret >= 0) && !zero_filled) {
                        //更新缓存的修改时间(这里只读一页，更新的确是整个inode缓存的时间??)
                        ioc_inode->cache.mtime = stbuf->ia_mtime;
                        ioc_inode->cache.mtime_nsec = stbuf->ia_mtime_nsec;
                }
                // 更新inode缓存的有效时间
                gettimeofday (&ioc_inode->cache.tv, NULL);

                if (op_ret < 0) {
                        /* error, readv returned -1 */
                        page = __ioc_page_get (ioc_inode, offset);
                        if (page)
                                // 释放页，返回页的等待队列
                                waitq = __ioc_page_error (page, op_ret,
                                                          op_errno);
                } else {
                        gf_log (ioc_inode->table->xl->name, GF_LOG_TRACE,
                                "op_ret = %d", op_ret);
                        //获取一页，发生页错误时就创建了这一页
                        page = __ioc_page_get (ioc_inode, offset);
                        if (!page) {
                                /* page was flushed */
                                /* some serious bug ? */
                                gf_log (frame->this->name, GF_LOG_WARNING,
                                        "wasted copy: %"PRId64"[+%"PRId64"] "
                                        "ioc_inode=%p", offset,
                                        table->page_size, ioc_inode);
                        } else {
                                //释放页的旧数据
                                if (page->vector) {
                                        iobref_unref (page->iobref);
                                        GF_FREE (page->vector);
                                        page->vector = NULL;
                                        page->iobref = NULL;
                                }

                                /* keep a copy of the page for our cache */
                                //复制新的数据
                                page->vector = iov_dup (vector, count);
                                if (page->vector == NULL) {
                                        page = __ioc_page_get (ioc_inode,
                                                               offset);
                                        if (page != NULL)
                                                waitq = __ioc_page_error (page,
                                                                          -1,
                                                                          ENOMEM);
                                        goto unlock;
                                }
                                // vector 数
                                page->count = count;
                                if (iobref) {
                                        page->iobref = iobref_ref (iobref);
                                } else {
                                        /* TODO: we have got a response to
                                         * our request and no data */
                                        gf_log (frame->this->name,
                                                GF_LOG_CRITICAL,
                                                "frame>root>rsp_refs is null");
                                } /* if(frame->root->rsp_refs) */

                                /* page->size should indicate表明 exactly how
                                 * much the readv call to the child
                                 * translator returned. earlier op_ret
                                 * from child translator was used, which
                                 * gave rise to a bug where reads from
                                 * io-cached volume were resulting in 0
                                 * byte replies */
                                page_size = iov_length(vector, count);
                                //页大小是read 返回的大小，因为文件的最后可能不够完整一页
                                page->size = page_size;
                                page->op_errno = op_errno;

                                //返回iobref中记录的iobuf大小之和
                                iobref_page_size = iobref_size (page->iobref);

                                if (page->waitq) {
                                        /* wake up all the frames waiting on
                                         * this page, including
                                         * the frame which triggered触发 fault */
                                        waitq = __ioc_page_wakeup (page,
                                                                   op_errno);
                                } /* if(page->waitq) */
                        } /* if(!page)...else */
                } /* if(op_ret < 0)...else */
        } /* ioc_inode locked region end */
unlock:
        ioc_inode_unlock (ioc_inode);

        ioc_waitq_return (waitq);

        if (iobref_page_size) {
                ioc_table_lock (table);
                {   
                        // 已缓存大小
                        table->cache_used += iobref_page_size;
                }
                ioc_table_unlock (table);
        }
        //有释放
        if (destroy_size) {
                ioc_table_lock (table);
                {
                        table->cache_used -= destroy_size;
                }
                ioc_table_unlock (table);
        }

        // 是否需要清除缓存
        if (ioc_need_prune (ioc_inode->table)) {
                ioc_prune (ioc_inode->table);
        }

        gf_log (frame->this->name, GF_LOG_TRACE, "fault frame %p returned",
                frame);
        pthread_mutex_destroy (&local->local_lock);

        fd_unref (local->fd);

        STACK_DESTROY (frame->root);
        return 0;
}


/*
 * ioc_page_fault -
 *
 * @ioc_inode:
 * @frame:
 * @fd:
 * @offset:
 *
 */
 // 页错误
 /* 新页已经创建，这里是读取页内容*/
void
ioc_page_fault (ioc_inode_t *ioc_inode, call_frame_t *frame, fd_t *fd,
                off_t offset)
{
        ioc_table_t  *table       = NULL;
        call_frame_t *fault_frame = NULL;
        ioc_local_t  *fault_local = NULL;
        int32_t       op_ret      = -1, op_errno = -1;
        ioc_waitq_t  *waitq       = NULL;
        ioc_page_t   *page        = NULL;

        GF_ASSERT (ioc_inode);
        if (frame == NULL) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log ("io-cache", GF_LOG_WARNING,
                        "page fault on a NULL frame");
                goto err;
        }

        table = ioc_inode->table;
        fault_frame = copy_frame (frame);
        if (fault_frame == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        fault_local = mem_get0 (THIS->local_pool);
        if (fault_local == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                STACK_DESTROY (fault_frame->root);
                goto err;
        }

        /* NOTE: copy_frame() means, the frame the fop whose fd_ref we
         * are using till now won't be valid till we get reply from server.
         * we unref this fd, in fault_cbk */
        fault_local->fd = fd_ref (fd);

        fault_frame->local = fault_local;
        pthread_mutex_init (&fault_local->local_lock, NULL);

        INIT_LIST_HEAD (&fault_local->fill_list);
        fault_local->pending_offset = offset;
        fault_local->pending_size = table->page_size;
        fault_local->inode = ioc_inode;

        gf_log (frame->this->name, GF_LOG_TRACE,
                "stack winding page fault for offset = %"PRId64" with "
                "frame %p", offset, fault_frame);

        STACK_WIND (fault_frame, ioc_fault_cbk, FIRST_CHILD(fault_frame->this),
                    FIRST_CHILD(fault_frame->this)->fops->readv, fd,
                    table->page_size, offset, 0, NULL);
        return;

err:
        ioc_inode_lock (ioc_inode);
        {
                page = __ioc_page_get (ioc_inode, offset);
                if (page != NULL) {
                        waitq = __ioc_page_error (page, op_ret, op_errno);
                }
        }
        ioc_inode_unlock (ioc_inode);

        if (waitq != NULL) {
                ioc_waitq_return (waitq);
        }
}

//给frame填充数据
int32_t
__ioc_frame_fill (ioc_page_t *page, call_frame_t *frame, off_t offset,
                  size_t size, int32_t op_errno)
{
        ioc_local_t *local      = NULL;
        ioc_fill_t  *fill       = NULL;
        off_t        src_offset = 0;
        off_t        dst_offset = 0;
        ssize_t      copy_size  = 0;
        ioc_inode_t *ioc_inode  = NULL;
        ioc_fill_t  *new        = NULL;
        int8_t       found      = 0;
        int32_t      ret        = -1;

        GF_VALIDATE_OR_GOTO ("io-cache", frame, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, out);

        if (page == NULL) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "NULL page has been provided to serve read request");
                local->op_ret = -1;
                local->op_errno = EINVAL;
                goto out;
        }

        ioc_inode = page->inode;

        gf_log (frame->this->name, GF_LOG_TRACE,
                "frame (%p) offset = %"PRId64" && size = %"GF_PRI_SIZET" "
                "&& page->size = %"GF_PRI_SIZET" && wait_count = %d",
                frame, offset, size, page->size, local->wait_count);

        /* immediately move this page to the end of the page_lru list */、
        // 删除后添加到最后
        list_move_tail (&page->page_lru, &ioc_inode->cache.page_lru);
        /* fill local->pending_size bytes from local->pending_offset */
        if (local->op_ret != -1) {
                local->op_errno = op_errno;

                if (page->size == 0) {
                        goto done;
                }

                if (offset > page->offset)
                        /* offset is offset in file, convert it to offset in
                         * page */
                        src_offset = offset - page->offset; //offset相对于page->offset的偏移量
                /*FIXME: since offset is the offset within page is the
                 * else case valid? */
                else
                        /* local->pending_offset is in previous page(有可能吗). do not
                         * fill until we have filled all previous pages */
                        dst_offset = page->offset - offset;//基本都是0吧，应该不可能跑到前面的页去了

                /* we have to copy from offset to either end of this page
                 * or till the requested size */
                // 计算需要负责数据的大小
                copy_size = min (page->size - src_offset,
                                 size - dst_offset);

                if (copy_size < 0) {
                        /* if page contains fewer bytes and the required offset
                           is beyond the page size in the page */
                        // 如果页只包含少量的数据，且offset大于该页的数据量
                        copy_size = src_offset = 0;
                }

                gf_log (page->inode->table->xl->name, GF_LOG_TRACE,
                        "copy_size = %"GF_PRI_SIZET" && src_offset = "
                        "%"PRId64" && dst_offset = %"PRId64"",
                        copy_size, src_offset, dst_offset);

                {
                        // 分配一个 ioc_fill 
                        new = GF_CALLOC (1, sizeof (*new),
                                         gf_ioc_mt_ioc_fill_t);
                        if (new == NULL) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                goto out;
                        }

                        new->offset = page->offset; //怎么不是offset
                        new->size = copy_size;  // 复制的大小
                        new->iobref = iobref_ref (page->iobref);
                        new->count = iov_subset (page->vector, page->count,
                                                 src_offset,
                                                 src_offset + copy_size,
                                                 NULL);

                        new->vector = GF_CALLOC (new->count,
                                                 sizeof (struct iovec),
                                                 gf_ioc_mt_iovec);
                        if (new->vector == NULL) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;

                                iobref_unref (new->iobref);
                                GF_FREE (new);
                                goto out;
                        }

                        // new->vector 填充数据
                        new->count = iov_subset (page->vector, page->count,
                                                 src_offset,
                                                 src_offset + copy_size,
                                                 new->vector);

                        /* add the ioc_fill to fill_list for this frame */
                        if (list_empty (&local->fill_list)) {
                                /* if list is empty, then this is the first
                                 * time we are filling frame, add the
                                 * ioc_fill_t to the end of list 
                                 local->fill_list为空，这是我们第一次filling frame
                                 */
                                list_add_tail (&new->list, &local->fill_list);
                        } else {
                                found = 0;
                                /* list is not empty, we need to look for
                                 * where this offset fits in list 
                                 如果队列不为空，需要查找是不是有offset大于 new->offset
                                 ，没有就插到最后，有就插入到相应位置，这是条有序的的链表，
                                 按offset递增排序
                                 */
                                list_for_each_entry (fill, &local->fill_list,
                                                     list) {
                                        if (fill->offset > new->offset) {
                                                found = 1;
                                                break;
                                        }
                                }

                                if (found) {
                                        list_add_tail (&new->list,
                                                       &fill->list);
                                } else {
                                        list_add_tail (&new->list,
                                                       &local->fill_list);
                                }
                        }
                }

                local->op_ret += copy_size;
        }

done:
        ret = 0;
out:
        return ret;
}

/*
 * ioc_frame_unwind - frame unwinds only from here
 *
 * @frame: call frame to unwind
 *
 * to be used only by ioc_frame_return(), when a frame has
 * finished waiting on all pages, required
 *
 */
//所有page的等待都返回时，frame才unwind
static void
ioc_frame_unwind (call_frame_t *frame)
{
        ioc_local_t   *local  = NULL;
        ioc_fill_t    *fill   = NULL, *next = NULL;
        int32_t        count  = 0;
        struct iovec  *vector = NULL;
        int32_t        copied = 0;
        struct iobref *iobref = NULL;
        struct iatt    stbuf  = {0,};
        int32_t        op_ret = 0, op_errno = 0;

        GF_ASSERT (frame);

        local = frame->local;
        if (local == NULL) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "local is NULL");
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        if (local->op_ret < 0) {
                op_ret = local->op_ret;
                op_errno = local->op_errno;
                goto unwind;
        }

        //  ioc_local_lock (local);
        iobref = iobref_new ();
        if (iobref == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
        }

        // 所有page的数据都添加到local->fill_list中
        if (list_empty (&local->fill_list)) {
                gf_log (frame->this->name, GF_LOG_TRACE,
                        "frame(%p) has 0 entries in local->fill_list "
                        "(offset = %"PRId64" && size = %"GF_PRI_SIZET")",
                        frame, local->offset, local->size);
        }

        // 计算需要多少个count
        list_for_each_entry (fill, &local->fill_list, list) {
                count += fill->count;
        }

        // 分配vector
        vector = GF_CALLOC (count, sizeof (*vector), gf_ioc_mt_iovec);
        if (vector == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
        }

        // 填充vector
        list_for_each_entry_safe (fill, next, &local->fill_list, list) {
                if ((vector != NULL) &&  (iobref != NULL)) {
                        memcpy (((char *)vector) + copied,
                                fill->vector,
                                fill->count * sizeof (*vector));

                        copied += (fill->count * sizeof (*vector));
                        //合并所有fill->iobref
                        if (iobref_merge (iobref, fill->iobref)) {
            				op_ret = -1;
            				op_errno = ENOMEM;
			            }
                }

                list_del (&fill->list);
                iobref_unref (fill->iobref);
                GF_FREE (fill->vector);
                GF_FREE (fill);
        }

        if (op_ret != -1) {
                // 数据的大小
                op_ret = iov_length (vector, count);
        }

unwind:
        gf_log (frame->this->name, GF_LOG_TRACE,
                "frame(%p) unwinding with op_ret=%d", frame, op_ret);

        //  ioc_local_unlock (local);
        
        frame->local = NULL;
        // unwind readv
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector,
                             count, &stbuf, iobref, NULL);

        if (iobref != NULL) {
                iobref_unref (iobref);
        }

        if (vector != NULL) {
                GF_FREE (vector);
                vector = NULL;
        }

        pthread_mutex_destroy (&local->local_lock);
        if (local)
                mem_put (local);

        return;
}

/*
 * ioc_frame_return -
 * @frame:
 *
 * to be called only when a frame is waiting on an in-transit page
 */
 // unwind 
void
ioc_frame_return (call_frame_t *frame)
{
        ioc_local_t *local      = NULL;
        int32_t      wait_count = 0;

        GF_ASSERT (frame);

        local = frame->local;
        GF_ASSERT (local->wait_count > 0);

        ioc_local_lock (local);
        {
                wait_count = --local->wait_count;
        }
        ioc_local_unlock (local);

        if (!wait_count) {
                ioc_frame_unwind (frame);
        }

        return;
}

/*
 * ioc_page_wakeup -
 * @page:
 *
 * to be called only when a frame is waiting on an in-transit page
 */
 //唤醒页等待
ioc_waitq_t *
__ioc_page_wakeup (ioc_page_t *page, int32_t op_errno)
{
        ioc_waitq_t  *waitq = NULL, *trav = NULL;
        call_frame_t *frame = NULL;
        int32_t       ret   = -1;

        GF_VALIDATE_OR_GOTO ("io-cache", page, out);

        waitq = page->waitq;
        page->waitq = NULL;

        // ready置一
        page->ready = 1;

        gf_log (page->inode->table->xl->name, GF_LOG_TRACE,
                "page is %p && waitq = %p", page, waitq);

        for (trav = waitq; trav; trav = trav->next) {
                frame = trav->data;
                // 填充数据
                ret = __ioc_frame_fill (page, frame, trav->pending_offset,
                                        trav->pending_size, op_errno);
                if (ret == -1) {
                        break;
                }
        }
        //如果页数据是旧的
        // 如果在读某页时，刚好有数据写入，写入要清空所有缓存，由于该页被frame等待，
        //清除不了，清除不了，page->stale置1，这里已经把旧数据读到frame中，所已要继续
        //清除
        if (page->stale) {
                __ioc_page_destroy (page);
        }

out:
        return waitq;
}



/*
 * ioc_page_error -
 * @page:
 * @op_ret:
 * @op_errno:
 *
 */
 //释放页，返回页的等待队列
ioc_waitq_t *
__ioc_page_error (ioc_page_t *page, int32_t op_ret, int32_t op_errno)
{
        ioc_waitq_t  *waitq = NULL, *trav = NULL;
        call_frame_t *frame = NULL;
        int64_t       ret   = 0;
        ioc_table_t  *table = NULL;
        ioc_local_t  *local = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", page, out);

        waitq = page->waitq;
        page->waitq = NULL;

        gf_log (page->inode->table->xl->name, GF_LOG_DEBUG,
                "page error for page = %p & waitq = %p", page, waitq);

        for (trav = waitq; trav; trav = trav->next) {

                frame = trav->data;

                local = frame->local;
                ioc_local_lock (local);
                {
                        if (local->op_ret != -1) {
                                local->op_ret = op_ret;
                                local->op_errno = op_errno;
                        }
                }
                ioc_local_unlock (local);
        }

        table = page->inode->table;
        // 释放页
        ret = __ioc_page_destroy (page);

        if (ret != -1) {
                table->cache_used -= ret;
        }

out:
        return waitq;
}

/*
 * ioc_page_error -
 * @page:
 * @op_ret:
 * @op_errno:
 *
 */
ioc_waitq_t *
ioc_page_error (ioc_page_t *page, int32_t op_ret, int32_t op_errno)
{
        ioc_waitq_t  *waitq = NULL;
        struct ioc_inode *inode = NULL;

        if (page == NULL) {
                goto out;
        }

        ioc_inode_lock (page->inode);
        {
                inode = page->inode;
                waitq = __ioc_page_error (page, op_ret, op_errno);
        }
        ioc_inode_unlock (inode);

out:
        return waitq;
}
