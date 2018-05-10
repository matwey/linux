/*
 * seq_buf.c
 *
 * Copyright (C) 2014 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * The seq_buf is a handy tool that allows you to pass a descriptor around
 * to a buffer that other functions can write to. It is similar to the
 * seq_file functionality but has some differences.
 *
 * To use it, the seq_buf must be initialized with seq_buf_init().
 * This will set up the counters within the descriptor. You can call
 * seq_buf_init() more than once to reset the seq_buf to start
 * from scratch.
 */
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/seq_buf.h>

/* How much buffer is written? */
#define SEQ_BUF_USED(s) min((s)->len, (s)->size - 1)

/**
 * seq_buf_print_seq - move the contents of seq_buf into a seq_file
 * @m: the seq_file descriptor that is the destination
 * @s: the seq_buf descriptor that is the source.
 *
 * Returns zero on success, non zero otherwise
 */
int seq_buf_print_seq(struct seq_file *m, struct seq_buf *s)
{
	unsigned int len = SEQ_BUF_USED(s);

	return seq_write(m, s->buffer, len);
}

/**
 * seq_buf_vprintf - sequence printing of information.
 * @s: seq_buf descriptor
 * @fmt: printf format string
 * @args: va_list of arguments from a printf() type function
 *
 * Writes a vnprintf() format into the sequencce buffer.
 *
 * Returns zero on success, -1 on overflow.
 */
int seq_buf_vprintf(struct seq_buf *s, const char *fmt, va_list args)
{
	int len;

	WARN_ON(s->size == 0);

	if (s->len < s->size) {
		len = vsnprintf(s->buffer + s->len, s->size - s->len, fmt, args);
		if (s->len + len < s->size) {
			s->len += len;
			return 0;
		}
	}
	seq_buf_set_overflow(s);
	return -1;
}

/**
 * seq_buf_printf - sequence printing of information
 * @s: seq_buf descriptor
 * @fmt: printf format string
 *
 * Writes a printf() format into the sequence buffer.
 *
 * Returns zero on success, -1 on overflow.
 */
int seq_buf_printf(struct seq_buf *s, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = seq_buf_vprintf(s, fmt, ap);
	va_end(ap);

	return ret;
}

