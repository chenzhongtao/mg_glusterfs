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

#include "timer.h"
#include "logging.h"
#include "common-utils.h"
#include "globals.h"
#include "timespec.h"

// 插入一个事件到定时器链表中
gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,
                     struct timespec delta,
                     gf_timer_cbk_t callbk,
                     void *data)
{
        gf_timer_registry_t *reg = NULL;
        gf_timer_t *event = NULL;
        gf_timer_t *trav = NULL;
        uint64_t at = 0;

        if (ctx == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }

        reg = gf_timer_registry_init (ctx);

        if (!reg) {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "!reg");
                return NULL;
        }

        event = GF_CALLOC (1, sizeof (*event), gf_common_mt_gf_timer_t);
        if (!event) {
                return NULL;
        }
        timespec_now (&event->at);
        timespec_adjust_delta (&event->at, delta);
        at = TS (event->at);
        event->callbk = callbk;
        event->data = data;
        event->xl = THIS;
        pthread_mutex_lock (&reg->lock);
        {
                //列表最后一个
                trav = reg->active.prev;
                //找最后一个时间比我早的(链表是按时间排序的)
                while (trav != &reg->active) {
                        if (TS (trav->at) < at)
                                break;
                        trav = trav->prev;
                }
                event->prev = trav;
                event->next = event->prev->next;
                event->prev->next = event;
                event->next->prev = event;
        }
        pthread_mutex_unlock (&reg->lock);
        return event;
}

//把event从active放到stale列表
int32_t
gf_timer_call_stale (gf_timer_registry_t *reg,
                     gf_timer_t *event)
{
        if (reg == NULL || event == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return 0;
        }

        event->next->prev = event->prev;
        event->prev->next = event->next;
        event->next = &reg->stale;
        event->prev = event->next->prev;
        event->next->prev = event;
        event->prev->next = event;

        return 0;
}

/*计时器取消*/
int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
                      gf_timer_t *event)
{
        gf_timer_registry_t *reg = NULL;

        if (ctx == NULL || event == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return 0;
        }

        reg = gf_timer_registry_init (ctx);
        if (!reg) {
                gf_log ("timer", GF_LOG_ERROR, "!reg");
                GF_FREE (event);
                return 0;
        }

        pthread_mutex_lock (&reg->lock);
        {
                event->next->prev = event->prev;
                event->prev->next = event->next;
        }
        pthread_mutex_unlock (&reg->lock);

        GF_FREE (event);
        return 0;
}

//计时器线程
void *
gf_timer_proc (void *ctx)
{
        gf_timer_registry_t *reg = NULL;
        const struct timespec sleepts = {.tv_sec = 1, .tv_nsec = 0, };

        if (ctx == NULL)
        {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }

        reg = gf_timer_registry_init (ctx);
        if (!reg) {
                gf_log ("timer", GF_LOG_ERROR, "!reg");
                return NULL;
        }

        while (!reg->fin) {
                uint64_t now;
                struct timespec now_ts;
                gf_timer_t *event = NULL;

                timespec_now (&now_ts);
                //现在的时间
                now = TS (now_ts);
                while (1) {
                        uint64_t at;
                        char need_cbk = 0;

                        pthread_mutex_lock (&reg->lock);
                        {
                                event = reg->active.next;
                                at = TS (event->at);
                                if (event != &reg->active && now >= at) {
                                        need_cbk = 1;
                                        gf_timer_call_stale (reg, event);
                                }
                        }
                        pthread_mutex_unlock (&reg->lock);
                        if (event->xl)
                                THIS = event->xl;
                        if (need_cbk)
                                event->callbk  (event->data);

                        else
                            //跳出while(1)
                                break;
                }
                // 睡1秒
                nanosleep (&sleepts, NULL);
        }

        pthread_mutex_lock (&reg->lock);
        {
                while (reg->active.next != &reg->active) {
                        gf_timer_call_cancel (ctx, reg->active.next);
                }

                while (reg->stale.next != &reg->stale) {
                        gf_timer_call_cancel (ctx, reg->stale.next);
                }
        }
        pthread_mutex_unlock (&reg->lock);
        pthread_mutex_destroy (&reg->lock);
        GF_FREE (((glusterfs_ctx_t *)ctx)->timer);

        return NULL;
}

// 计时器注册初始化
gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *ctx)
{
        if (ctx == NULL) {
                gf_log_callingfn ("timer", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }
        // 还没初始化过的
        if (!ctx->timer) {
                gf_timer_registry_t *reg = NULL;

                reg = GF_CALLOC (1, sizeof (*reg),
                                 gf_common_mt_gf_timer_registry_t);
                if (!reg)
                        goto out;

                pthread_mutex_init (&reg->lock, NULL);
                reg->active.next = &reg->active;
                reg->active.prev = &reg->active;
                reg->stale.next = &reg->stale;
                reg->stale.prev = &reg->stale;

                ctx->timer = reg;
                gf_thread_create (&reg->th, NULL, gf_timer_proc, ctx);
        }
out:
        return ctx->timer;
}
