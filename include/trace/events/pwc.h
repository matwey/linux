/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_PWC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PWC_H

#include <linux/usb.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pwc

TRACE_EVENT(pwc_handler_enter,
	TP_PROTO(struct urb *urb),
	TP_ARGS(urb),
	TP_STRUCT__entry(
		__field(struct urb*, urb)
		__field(int, urb__status)
		__field(u32, urb__actual_length)
	),
	TP_fast_assign(
		__entry->urb = urb;
		__entry->urb__status = urb->status;
		__entry->urb__actual_length = urb->actual_length;
	),
	TP_printk("urb=%p (status=%d actual_length=%u)",
		__entry->urb,
		__entry->urb__status,
		__entry->urb__actual_length)
);

TRACE_EVENT(pwc_handler_exit,
	TP_PROTO(struct urb *urb),
	TP_ARGS(urb),
	TP_STRUCT__entry(
		__field(struct urb*, urb)
	),
	TP_fast_assign(
		__entry->urb = urb;
	),
	TP_printk("urb=%p", __entry->urb)
);

#endif /* _TRACE_PWC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
