/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _INODE_H
#define _INODE_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <sys/types.h>

#define DEFAULT_INODE_MEMPOOL_ENTRIES   32 * 1024
#define INODE_PATH_FMT "<gfid:%s>"
struct _inode_table;
typedef struct _inode_table inode_table_t;

struct _inode;
typedef struct _inode inode_t;

struct _dentry;
typedef struct _dentry dentry_t;

#include "list.h"
#include "xlator.h"
#include "iatt.h"
#include "uuid.h"

// inode表
struct _inode_table {
        pthread_mutex_t    lock;
        // 哈希大小 14057
        size_t             hashsize;    /* bucket size of inode hash and dentry hash */
        // "%s/inode", xl->name
        char              *name;        /* name of the inode table, just for gf_log() */
        // inode链表的根
        inode_t           *root;        /* root directory inode, with number 1 */
        xlator_t          *xl;          /* xlator to be called to do purge */
        // lru最大个数
        uint32_t           lru_limit;   /* maximum LRU cache size */
        //inode哈希表 65536个
        struct list_head  *inode_hash;  /* buckets for inode hash table */
        //name哈希表 =hashsize 14057
        struct list_head  *name_hash;   /* buckets for dentry hash table */
        //active,lru,purge(清除)代表inode的三个状态
        //lru表示Least Recently Used最近最少使用算法
        struct list_head   active;      /* list of inodes currently active (in an fop) */
        uint32_t           active_size; /* count of inodes in active list */
        struct list_head   lru;         /* list of inodes recently used.
                                           lru.next most recent */
        uint32_t           lru_size;    /* count of inodes in lru list  */
        struct list_head   purge;       /* list of inodes to be purged soon */
        uint32_t           purge_size;  /* count of inodes in purge list */

        struct mem_pool   *inode_pool;  /* memory pool for inodes */
        struct mem_pool   *dentry_pool; /* memory pool for dentrys */
        struct mem_pool   *fd_mem_pool; /* memory pool for fd_t */
        // 等于xlator个数
        int                ctxcount;    /* number of slots in inode->ctx */
};

//目录项对象
struct _dentry {
        //该list_head用于放到 inode->dentry_list
        struct list_head   inode_list;   /* list of dentries of inode */
        //该list_head用于放到 table->name_hash ?
        struct list_head   hash;         /* hash table pointers */
        // 当前目录对应的inode
        inode_t           *inode;        /* inode of this directory entry */ 
        // 目录的名字
        char              *name;         /* name of the directory entry */
        // 前一目录对应的inode
        inode_t           *parent;       /* directory of the entry */
};

struct _inode_ctx {
        union {
                uint64_t    key;
                xlator_t   *xl_key;  //xlator_t的地址
        };
        /* if value1 is 0, then field is not set.. */
        union {
                uint64_t    value1;
                void       *ptr1;
        };
        /* if value2 is 0, then field is not set.. */
        union {
                uint64_t    value2;
                void       *ptr2;
        };
};

//索引节点对象
struct _inode {
        inode_table_t       *table;         /* the table this inode belongs to */
        uuid_t               gfid;          // inode 的 uuid
        gf_lock_t            lock;
        uint64_t             nlookup;       //被lookup的次数
        //打开的fd数
        uint32_t             fd_count;      /* Open fd count */
        uint32_t             ref;           /* reference count on this inode */
        // 文件类型
        ia_type_t            ia_type;       /* what kind of file */ 
        //该inode所有打开的文件
        struct list_head     fd_list;       /* list of open files on this inode */

        // 该inode的dentry_t ，很多dentry可以指向同一个inode(硬链接吗?)
        struct list_head     dentry_list;   /* list of directory entries for this inode */ 
        // 该list_head用于放到 table->inode_hash ?
        struct list_head     hash;          /* hash table pointers */
        //inode链表指针，偏移为0x68 ,方向根路径相反，如/test/trace.txt,  trace.txt ->next 为test 
        struct list_head     list;          /* active/lru/purge */
        //_inode_ctx数组
	struct _inode_ctx   *_ctx;    /* replacement for dict_t *(inode->ctx) */
};


#define UUID0_STR "00000000-0000-0000-0000-000000000000"
#define GFID_STR_PFX "<gfid:" UUID0_STR ">"
#define GFID_STR_PFX_LEN (sizeof (GFID_STR_PFX) - 1)

inode_table_t *
inode_table_new (size_t lru_limit, xlator_t *xl);

inode_t *
inode_new (inode_table_t *table);

inode_t *
inode_link (inode_t *inode, inode_t *parent,
            const char *name, struct iatt *stbuf);

void
inode_unlink (inode_t *inode, inode_t *parent, const char *name);

inode_t *
inode_parent (inode_t *inode, uuid_t pargfid, const char *name);

inode_t *
inode_ref (inode_t *inode);

inode_t *
inode_unref (inode_t *inode);

int
inode_lookup (inode_t *inode);

int
inode_forget (inode_t *inode, uint64_t nlookup);

int
inode_invalidate(inode_t *inode);

int
inode_rename (inode_table_t *table, inode_t *olddir, const char *oldname,
	      inode_t *newdir, const char *newname,
	      inode_t *inode, struct iatt *stbuf);

dentry_t *
__dentry_grep (inode_table_t *table, inode_t *parent, const char *name);

inode_t *
inode_grep (inode_table_t *table, inode_t *parent, const char *name);

int
inode_grep_for_gfid (inode_table_t *table, inode_t *parent, const char *name,
                     uuid_t gfid, ia_type_t *type);

inode_t *
inode_find (inode_table_t *table, uuid_t gfid);

int
inode_path (inode_t *inode, const char *name, char **bufp);

int
__inode_path (inode_t *inode, const char *name, char **bufp);

inode_t *
inode_from_path (inode_table_t *table, const char *path);

inode_t *
inode_resolve (inode_table_t *table, char *path);

/* deal with inode ctx's both values */

int
inode_ctx_set2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                uint64_t *value2);
int
__inode_ctx_set2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                  uint64_t *value2);

int
inode_ctx_get2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                uint64_t *value2);
int
__inode_ctx_get2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                  uint64_t *value2);

int
inode_ctx_del2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                uint64_t *value2);

int
inode_ctx_reset2 (inode_t *inode, xlator_t *xlator, uint64_t *value1,
                  uint64_t *value2);

/* deal with inode ctx's 1st value */

int
inode_ctx_set0 (inode_t *inode, xlator_t *xlator, uint64_t *value1);

int
__inode_ctx_set0 (inode_t *inode, xlator_t *xlator, uint64_t *value1);

int
inode_ctx_get0 (inode_t *inode, xlator_t *xlator, uint64_t *value1);
int
__inode_ctx_get0 (inode_t *inode, xlator_t *xlator, uint64_t *value1);

int
inode_ctx_reset0 (inode_t *inode, xlator_t *xlator, uint64_t *value1);

/* deal with inode ctx's 2st value */

int
inode_ctx_set1 (inode_t *inode, xlator_t *xlator, uint64_t *value2);

int
__inode_ctx_set1 (inode_t *inode, xlator_t *xlator, uint64_t *value2);

int
inode_ctx_get1 (inode_t *inode, xlator_t *xlator, uint64_t *value2);
int
__inode_ctx_get1 (inode_t *inode, xlator_t *xlator, uint64_t *value2);

int
inode_ctx_reset1 (inode_t *inode, xlator_t *xlator, uint64_t *value2);


static inline int
__inode_ctx_put(inode_t *inode, xlator_t *this, uint64_t v)
{
        return __inode_ctx_set0 (inode, this, &v);
}

static inline int
inode_ctx_put(inode_t *inode, xlator_t *this, uint64_t v)
{
        return inode_ctx_set0 (inode, this, &v);
}

#define __inode_ctx_set(i,x,v_p) __inode_ctx_set0(i,x,v_p)

#define inode_ctx_set(i,x,v_p) inode_ctx_set0(i,x,v_p)

#define inode_ctx_reset(i,x,v) inode_ctx_reset0(i,x,v)

#define __inode_ctx_get(i,x,v) __inode_ctx_get0(i,x,v)

#define inode_ctx_get(i,x,v) inode_ctx_get0(i,x,v)

#define inode_ctx_del(i,x,v) inode_ctx_del2(i,x,v,0)

gf_boolean_t
__is_root_gfid (uuid_t gfid);

void
__inode_table_set_lru_limit (inode_table_t *table, uint32_t lru_limit);

void
inode_table_set_lru_limit (inode_table_t *table, uint32_t lru_limit);

#endif /* _INODE_H */
