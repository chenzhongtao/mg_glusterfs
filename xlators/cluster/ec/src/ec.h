/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_H__
#define __EC_H__

#include "xlator.h"
#include "timer.h"

#define EC_XATTR_CONFIG  "trusted.ec.config"
#define EC_XATTR_SIZE    "trusted.ec.size"
#define EC_XATTR_VERSION "trusted.ec.version"

struct _ec;
typedef struct _ec ec_t;

struct _ec
{
    xlator_t *        xl;  //对应的xlator_t
    int32_t           nodes; //节点总数(子卷数)
    int32_t           bits_for_nodes; //需要多少位来表示所有节点,1或2个节点，1位，3或4个节点2位
    int32_t           fragments; //分段数
    int32_t           redundancy; //冗余数
    uint32_t          fragment_size; //分段大小，64*8=512
    uint32_t          stripe_size; //条带大小，分段大小*分段数
    int32_t           up;  //唤醒节点标志
    uint32_t          idx; //子卷id,初始为0，每次ec_child_select加1，达到子卷数又从0开始
    uint32_t          xl_up_count; //唤醒的节点数
    uintptr_t         xl_up;       //唤醒节点标志位
    uintptr_t         node_mask; //节点掩码，初始化为节点数个1  如4个节点为 ...00001111
    xlator_t **       xl_list; //子卷xlator列表，如xl_list[0]为第一子卷的xlator
    gf_lock_t         lock;   //锁
    gf_timer_t *      timer;  //计时器
    struct mem_pool * fop_pool; //1024个
    struct mem_pool * cbk_pool; //4096个
    struct mem_pool * lock_pool; //1024个
};

#endif /* __EC_H__ */
