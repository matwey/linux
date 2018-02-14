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
		__field(void*, transfer_buffer)
	),
	TP_fast_assign(
		__entry->urb = urb;
		__entry->transfer_buffer = virt_to_phys(urb->transfer_buffer);
	),
	TP_printk("urb=%p transfer_buffer=%p", __entry->urb, __entry->transfer_buffer)
);

TRACE_EVENT(pwc_handler_exit,
	TP_PROTO(struct urb *urb, int len),
	TP_ARGS(urb, len),
	TP_STRUCT__entry(
		__field(struct urb*, urb)
		__field(int, len)
	),
	TP_fast_assign(
		__entry->urb = urb;
		__entry->len = len;
	),
	TP_printk("urb=%p len=%d", __entry->urb, __entry->len)
);

#endif /* _TRACE_PWC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
