/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

/* This is *identical* to the camel-nntp-stream, so should probably
   work out a way to merge them */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "camel-pop3-stream.h"

extern gint camel_verbose_debug;
#define dd(x) (camel_verbose_debug?(x):0)

static CamelObjectClass *parent_class = NULL;

#define CAMEL_POP3_STREAM_SIZE (4096)
#define CAMEL_POP3_STREAM_LINE (1024) /* maximum line size */

static void
pop3_stream_finalize (CamelPOP3Stream *is)
{
	g_free (is->buf);
	g_free (is->linebuf);
	if (is->source)
		camel_object_unref (is->source);
}

static gint
stream_fill (CamelPOP3Stream *is)
{
	gint left = 0;

	if (is->source) {
		left = is->end - is->ptr;
		memmove (is->buf, is->ptr, left);
		is->end = is->buf + left;
		is->ptr = is->buf;
		left = camel_stream_read (
			is->source, (gchar *) is->end,
			CAMEL_POP3_STREAM_SIZE - (is->end - is->buf));
		if (left > 0) {
			is->end += left;
			is->end[0] = '\n';
			return is->end - is->ptr;
		} else {
			dd (printf ("POP3_STREAM_FILL (ERROR): '%s'\n", g_strerror (errno)));
			return -1;
		}
	}

	return 0;
}

static gssize
stream_read (CamelStream *stream, gchar *buffer, gsize n)
{
	CamelPOP3Stream *is = (CamelPOP3Stream *)stream;
	gchar *o, *oe;
	guchar *p, *e, c;
	gint state;

	if (is->mode != CAMEL_POP3_STREAM_DATA || n == 0)
		return 0;

	o = buffer;
	oe = buffer + n;
	state = is->state;

	/* Need to copy/strip '.'s and whatnot */
	p = is->ptr;
	e = is->end;

	switch (state) {
	state_0:
	case 0:		/* start of line, always read at least 3 chars */
		while (e - p < 3) {
			is->ptr = p;
			if (stream_fill (is) == -1)
				return -1;
			p = is->ptr;
			e = is->end;
		}
		if (p[0] == '.') {
			if (p[1] == '\r' && p[2] == '\n') {
				is->ptr = p+3;
				is->mode = CAMEL_POP3_STREAM_EOD;
				is->state = 0;
				dd (printf ("POP3_STREAM_READ (%d):\n%.*s\n", (gint)(o-buffer), (gint)(o-buffer), buffer));
				return o-buffer;
			}
			p++;
		}
		state = 1;
		/* FALLS THROUGH */
	case 1:		/* looking for next sol */
		while (o < oe) {
			c = *p++;
			if (c == '\n') {
				/* end of input sentinal check */
				if (p > e) {
					is->ptr = e;
					if (stream_fill (is) == -1)
						return -1;
					p = is->ptr;
					e = is->end;
				} else {
					*o++ = '\n';
					state = 0;
					goto state_0;
				}
			} else if (c != '\r') {
				*o++ = c;
			}
		}
		break;
	}

	is->ptr = p;
	is->state = state;

	dd (printf ("POP3_STREAM_READ (%d):\n%.*s\n", (gint)(o-buffer), (gint)(o-buffer), buffer));

	return o-buffer;
}

static gssize
stream_write (CamelStream *stream,
              const gchar *buffer,
              gsize n)
{
	CamelPOP3Stream *is = (CamelPOP3Stream *)stream;

	if (strncmp (buffer, "PASS ", 5) != 0)
		dd (printf ("POP3_STREAM_WRITE (%d):\n%.*s\n", (gint)n, (gint)n, buffer));
	else
		dd (printf ("POP3_STREAM_WRITE (%d):\nPASS xxxxxxxx\n", (gint)n));

	return camel_stream_write (is->source, buffer, n);
}

static gint
stream_close (CamelStream *stream)
{
	/* nop? */
	return 0;
}

static gint
stream_flush (CamelStream *stream)
{
	/* nop? */
	return 0;
}

static gboolean
stream_eos (CamelStream *stream)
{
	CamelPOP3Stream *is = (CamelPOP3Stream *)stream;

	return is->mode != CAMEL_POP3_STREAM_DATA;
}

static gint
stream_reset (CamelStream *stream)
{
	/* nop?  reset literal mode? */
	return 0;
}

static void
camel_pop3_stream_class_init (CamelStreamClass *class)
{
	CamelStreamClass *stream_class;

	parent_class = camel_type_get_global_classfuncs( CAMEL_TYPE_OBJECT );

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = stream_read;
	stream_class->write = stream_write;
	stream_class->close = stream_close;
	stream_class->flush = stream_flush;
	stream_class->eos = stream_eos;
	stream_class->reset = stream_reset;
}

static void
camel_pop3_stream_init (CamelPOP3Stream *is)
{
	/* +1 is room for appending a 0 if we need to for a line */
	is->ptr = is->end = is->buf = g_malloc (CAMEL_POP3_STREAM_SIZE+1);
	is->lineptr = is->linebuf = g_malloc (CAMEL_POP3_STREAM_LINE+1);
	is->lineend = is->linebuf + CAMEL_POP3_STREAM_LINE;

	/* init sentinal */
	is->ptr[0] = '\n';

	is->state = 0;
	is->mode = CAMEL_POP3_STREAM_LINE;
}

CamelType
camel_pop3_stream_get_type (void)
{
	static CamelType camel_pop3_stream_type = CAMEL_INVALID_TYPE;

	if (camel_pop3_stream_type == CAMEL_INVALID_TYPE) {
		camel_pop3_stream_type = camel_type_register( camel_stream_get_type(),
							    "CamelPOP3Stream",
							    sizeof( CamelPOP3Stream ),
							    sizeof( CamelPOP3StreamClass ),
							    (CamelObjectClassInitFunc) camel_pop3_stream_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_pop3_stream_init,
							    (CamelObjectFinalizeFunc) pop3_stream_finalize );
	}

	return camel_pop3_stream_type;
}

/**
 * camel_pop3_stream_new:
 *
 * Returns a NULL stream.  A null stream is always at eof, and
 * always returns success for all reads and writes.
 *
 * Returns: the stream
 **/
CamelStream *
camel_pop3_stream_new (CamelStream *source)
{
	CamelPOP3Stream *is;

	is = (CamelPOP3Stream *)camel_object_new(camel_pop3_stream_get_type ());
	is->source = camel_object_ref (source);

	return (CamelStream *)is;
}

/* Get one line from the pop3 stream */
gint
camel_pop3_stream_line (CamelPOP3Stream *is, guchar **data, guint *len)
{
	register guchar c, *p, *o, *oe;
	gint newlen, oldlen;
	guchar *e;

	if (is->mode == CAMEL_POP3_STREAM_EOD) {
		*data = is->linebuf;
		*len = 0;
		return 0;
	}

	o = is->linebuf;
	oe = is->lineend - 1;
	p = is->ptr;
	e = is->end;

	/* Data mode, convert leading '..' to '.', and stop when we reach a solitary '.' */
	if (is->mode == CAMEL_POP3_STREAM_DATA) {
		/* need at least 3 chars in buffer */
		while (e-p < 3) {
			is->ptr = p;
			if (stream_fill (is) == -1)
				return -1;
			p = is->ptr;
			e = is->end;
		}

		/* check for isolated '.\r\n' or begging of line '.' */
		if (p[0] == '.') {
			if (p[1] == '\r' && p[2] == '\n') {
				is->ptr = p+3;
				is->mode = CAMEL_POP3_STREAM_EOD;
				*data = is->linebuf;
				*len = 0;
				is->linebuf[0] = 0;

				dd (printf ("POP3_STREAM_LINE (END)\n"));

				return 0;
			}
			p++;
		}
	}

	while (1) {
		while (o < oe) {
			c = *p++;
			if (c == '\n') {
				/* sentinal? */
				if (p> e) {
					is->ptr = e;
					if (stream_fill (is) == -1)
						return -1;
					p = is->ptr;
					e = is->end;
				} else {
					is->ptr = p;
					*data = is->linebuf;
					*len = o - is->linebuf;
					*o = 0;

					dd (printf ("POP3_STREAM_LINE (%d): '%s'\n", *len, *data));

					return 1;
				}
			} else if (c != '\r') {
				*o++ = c;
			}
		}

		/* limit this for bad server data? */
		oldlen = o - is->linebuf;
		newlen = (is->lineend - is->linebuf) * 3 / 2;
		is->lineptr = is->linebuf = g_realloc (is->linebuf, newlen);
		is->lineend = is->linebuf + newlen;
		oe = is->lineend - 1;
		o = is->linebuf + oldlen;
	}

	return -1;
}

/* returns -1 on error, 0 if last lot of data, >0 if more remaining */
gint camel_pop3_stream_gets (CamelPOP3Stream *is, guchar **start, guint *len)
{
	gint max;
	guchar *end;

	*len = 0;

	max = is->end - is->ptr;
	if (max == 0) {
		max = stream_fill (is);
		if (max <= 0)
			return max;
	}

	*start = is->ptr;
	end = memchr (is->ptr, '\n', max);
	if (end)
		max = (end - is->ptr) + 1;
	*start = is->ptr;
	*len = max;
	is->ptr += max;

	dd (printf ("POP3_STREAM_GETS (%s,%d): '%.*s'\n", end==NULL?"more":"last", *len, (gint)*len, *start));

	return end == NULL?1:0;
}

void camel_pop3_stream_set_mode (CamelPOP3Stream *is, camel_pop3_stream_mode_t mode)
{
	is->mode = mode;
}

/* returns -1 on erorr, 0 if last data, >0 if more data left */
gint camel_pop3_stream_getd (CamelPOP3Stream *is, guchar **start, guint *len)
{
	guchar *p, *e, *s;
	gint state;

	*len = 0;

	if (is->mode == CAMEL_POP3_STREAM_EOD)
		return 0;

	if (is->mode == CAMEL_POP3_STREAM_LINE) {
		g_warning ("pop3_stream reading data in line mode\n");
		return 0;
	}

	state = is->state;
	p = is->ptr;
	e = is->end;

	while (e - p < 3) {
		is->ptr = p;
		if (stream_fill (is) == -1)
			return -1;
		p = is->ptr;
		e = is->end;
	}

	s = p;

	do {
		switch (state) {
		case 0:
			/* check leading '.', ... */
			if (p[0] == '.') {
				if (p[1] == '\r' && p[2] == '\n') {
					is->ptr = p+3;
					*len = p-s;
					*start = s;
					is->mode = CAMEL_POP3_STREAM_EOD;
					is->state = 0;

					dd (printf ("POP3_STREAM_GETD (%s,%d): '%.*s'\n", "last", *len, (gint)*len, *start));

					return 0;
				}

				/* If at start, just skip '.', else return data upto '.' but skip it */
				if (p == s) {
					s++;
					p++;
				} else {
					is->ptr = p+1;
					*len = p-s;
					*start = s;
					is->state = 1;

					dd (printf ("POP3_STREAM_GETD (%s,%d): '%.*s'\n", "more", *len, (gint)*len, *start));

					return 1;
				}
			}
			state = 1;
		case 1:
			/* Scan for sentinal */
			while ((*p++)!='\n')
				;

			if (p > e) {
				p = e;
			} else {
				state = 0;
			}
			break;
		}
	} while ((e-p) >= 3);

	is->state = state;
	is->ptr = p;
	*len = p-s;
	*start = s;

	dd (printf ("POP3_STREAM_GETD (%s,%d): '%.*s'\n", "more", *len, (gint)*len, *start));

	return 1;
}
