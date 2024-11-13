// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#if !defined(_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcl_stat


TRACE_EVENT(bcl_stat,

	TP_PROTO(long time_s, int id, int level, int vol, int curr),

	TP_ARGS(time_s, id, level, vol, curr),

	TP_STRUCT__entry(
		__field(	long,	time_s)
		__field(	int,	id)
		__field(	int,	level)
		__field(	int,	vol)
		__field(	int,	curr)
	),

	TP_fast_assign(
		__entry->time_s	= time_s;
		__entry->id	= id;
		__entry->level	= level;
		__entry->vol	= vol;
		__entry->curr	= curr;
	),

	TP_printk("time_s:%ld id:%d level:%d vol:%d curr:%d", __entry->time_s, __entry->id, __entry->level, __entry->vol, __entry->curr)
);


#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>


