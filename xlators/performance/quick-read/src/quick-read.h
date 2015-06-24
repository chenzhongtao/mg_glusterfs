/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __QUICK_READ_H
#define __QUICK_READ_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "list.h"
#include "compat.h"
#include "compat-errno.h"
#include "common-utils.h"
#include "call-stub.h"
#include "defaults.h"
#include <libgen.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#include "quick-read-mem-types.h"


struct qr_inode {
	void             *data; //数据内容
	size_t            size; //数据大小
    int               priority; // 优先级
	uint32_t          ia_mtime; // 修改时间
	uint32_t          ia_mtime_nsec;
	struct iatt       buf; // 属性
    struct timeval    last_refresh; //更新时间
    struct list_head  lru;
};
typedef struct qr_inode qr_inode_t;


struct qr_priority {
        char             *pattern;  //模式 如*.jpg
        int32_t           priority; //优先级 1,2,...
        struct list_head  list;
};
typedef struct qr_priority qr_priority_t;

struct qr_conf {
        uint64_t         max_file_size;
        int32_t          cache_timeout;
        uint64_t         cache_size;
        int              max_pri;
        struct list_head priority_list;
};
typedef struct qr_conf qr_conf_t;

struct qr_inode_table {
        uint64_t          cache_used;//所有qr_inode size之和
        struct list_head *lru; //长度为 max_pri的数组
        gf_lock_t         lock;
};
typedef struct qr_inode_table qr_inode_table_t;

struct qr_private {
        qr_conf_t         conf;
        qr_inode_table_t  table;
};
typedef struct qr_private qr_private_t;


#endif /* #ifndef __QUICK_READ_H */
