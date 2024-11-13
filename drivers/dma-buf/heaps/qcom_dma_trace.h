/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_dma

#if !defined(_TRACE_QCOM_DMA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_DMA_TRACE_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

TRACE_EVENT(qcom_dma_alloc,
        TP_PROTO(size_t len, unsigned long ino, const char *name
                ),
        TP_ARGS(len, ino, name
                ),
        TP_STRUCT__entry(
                __field(size_t, len)
                __field(unsigned long, ino)
                __string(name, name)
        ),
        TP_fast_assign(
                __entry->len = len;
                __entry->ino = ino;
                __assign_str(name, name);
        ),
        TP_printk(
                "len=%zu, inode=%lu, name=%s",
                __entry->len, __entry->ino, __get_str(name)
        )
);

TRACE_EVENT(qcom_dma_free,
        TP_PROTO(size_t len, unsigned long ino, const char *name
                ),
        TP_ARGS(len, ino, name
                ),
        TP_STRUCT__entry(
                __field(size_t, len)
                __field(unsigned long, ino)
                __string(name, name)
        ),
        TP_fast_assign(
                __entry->len = len;
                __entry->ino = ino;
                __assign_str(name, name);
        ),
        TP_printk(
                "len=%zu, inode=%lu, name=%s",
                __entry->len, __entry->ino, __get_str(name)
        )
);

#endif /* _TRACE_QCOM_DMA_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/dma-buf/heaps
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE qcom_dma_trace
#include <trace/define_trace.h>
