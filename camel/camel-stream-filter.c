/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "camel-stream-filter.h"

#define d(x)

/* use my malloc debugger? */
/*extern void g_check(gpointer mp);*/
#define g_check(x)

struct _filter {
	struct _filter *next;
	gint id;
	CamelMimeFilter *filter;
};

struct _CamelStreamFilterPrivate {
	struct _filter *filters;
	gint filterid;		/* next filter id */

	gchar *realbuffer;	/* buffer - READ_PAD */
	gchar *buffer;		/* READ_SIZE bytes */

	gchar *filtered;		/* the filtered data */
	gsize filteredlen;

	guint last_was_read:1;	/* was the last op read or write? */
	guint flushed:1;        /* were the filters flushed? */
};

#define READ_PAD (128)		/* bytes padded before buffer */
#define READ_SIZE (4096)

#define _PRIVATE(o) (((CamelStreamFilter *)(o))->priv)

static void camel_stream_filter_class_init (CamelStreamFilterClass *klass);
static void camel_stream_filter_init       (CamelStreamFilter *obj);

static	gssize   do_read       (CamelStream *stream, gchar *buffer, gsize n);
static	gssize   do_write      (CamelStream *stream, const gchar *buffer, gsize n);
static	gint       do_flush      (CamelStream *stream);
static	gint       do_close      (CamelStream *stream);
static	gboolean  do_eos        (CamelStream *stream);
static	gint       do_reset      (CamelStream *stream);

static CamelStreamClass *camel_stream_filter_parent;

static void
camel_stream_filter_class_init (CamelStreamFilterClass *klass)
{
	CamelStreamClass *camel_stream_class = (CamelStreamClass *) klass;

	camel_stream_filter_parent = CAMEL_STREAM_CLASS (camel_type_get_global_classfuncs (camel_stream_get_type ()));

	camel_stream_class->read = do_read;
	camel_stream_class->write = do_write;
	camel_stream_class->flush = do_flush;
	camel_stream_class->close = do_close;
	camel_stream_class->eos = do_eos;
	camel_stream_class->reset = do_reset;

}

static void
camel_stream_filter_init (CamelStreamFilter *obj)
{
	struct _CamelStreamFilterPrivate *p;

	_PRIVATE(obj) = p = g_malloc0(sizeof(*p));
	p->realbuffer = g_malloc(READ_SIZE + READ_PAD);
	p->buffer = p->realbuffer + READ_PAD;
	p->last_was_read = TRUE;
	p->flushed = FALSE;
}

static void
camel_stream_filter_finalize(CamelObject *o)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)o;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *fn, *f;

	f = p->filters;
	while (f) {
		fn = f->next;
		camel_object_unref((CamelObject *)f->filter);
		g_free(f);
		f = fn;
	}
	g_free(p->realbuffer);
	g_free(p);
	camel_object_unref((CamelObject *)filter->source);
}

CamelType
camel_stream_filter_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_STREAM_TYPE, "CamelStreamFilter",
					    sizeof (CamelStreamFilter),
					    sizeof (CamelStreamFilterClass),
					    (CamelObjectClassInitFunc) camel_stream_filter_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_stream_filter_init,
					    (CamelObjectFinalizeFunc) camel_stream_filter_finalize);
	}

	return type;
}

/**
 * camel_stream_filter_new:
 *
 * Create a new #CamelStreamFilter object.
 *
 * Returns: a new #CamelStreamFilter object.
 **/
CamelStream *
camel_stream_filter_new (CamelStream *stream)
{
	CamelStreamFilter *new = CAMEL_STREAM_FILTER ( camel_object_new (camel_stream_filter_get_type ()));

	new->source = stream;
	camel_object_ref ((CamelObject *)stream);

	return CAMEL_STREAM (new);
}

/**
 * camel_stream_filter_add:
 * @stream: a #CamelStreamFilter object
 * @filter: a #CamelMimeFilter object
 *
 * Add a new #CamelMimeFilter to execute during the processing of this
 * stream.  Each filter added is processed after the previous one.
 *
 * Note that a filter should only be added to a single stream
 * at a time, otherwise unpredictable results may occur.
 *
 * Returns: a filter id for the added @filter.
 **/
gint
camel_stream_filter_add (CamelStreamFilter *stream, CamelMimeFilter *filter)
{
	struct _CamelStreamFilterPrivate *p = _PRIVATE(stream);
	struct _filter *fn, *f;

	fn = g_malloc(sizeof(*fn));
	fn->id = p->filterid++;
	fn->filter = filter;
	camel_object_ref (filter);

	/* sure, we could use a GList, but we wouldn't save much */
	f = (struct _filter *)&p->filters;
	while (f->next)
		f = f->next;
	f->next = fn;
	fn->next = NULL;
	return fn->id;
}

/**
 * camel_stream_filter_remove:
 * @stream: a #CamelStreamFilter object
 * @id: Filter id, as returned from #camel_stream_filter_add
 *
 * Remove a processing filter from the stream by id.
 **/
void
camel_stream_filter_remove(CamelStreamFilter *stream, gint id)
{
	struct _CamelStreamFilterPrivate *p = _PRIVATE(stream);
	struct _filter *fn, *f;

	f = (struct _filter *)&p->filters;
	while (f && f->next) {
		fn = f->next;
		if (fn->id == id) {
			f->next = fn->next;
			camel_object_unref(fn->filter);
			g_free(fn);
		}
		f = f->next;
	}
}

static gssize
do_read (CamelStream *stream, gchar *buffer, gsize n)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	gssize size;
	struct _filter *f;

	p->last_was_read = TRUE;

	g_check(p->realbuffer);

	if (p->filteredlen<=0) {
		gsize presize = READ_PAD;

		size = camel_stream_read(filter->source, p->buffer, READ_SIZE);
		if (size <= 0) {
			/* this is somewhat untested */
			if (camel_stream_eos(filter->source)) {
				f = p->filters;
				p->filtered = p->buffer;
				p->filteredlen = 0;
				while (f) {
					camel_mime_filter_complete(f->filter, p->filtered, p->filteredlen,
								   presize, &p->filtered, &p->filteredlen, &presize);
					g_check(p->realbuffer);
					f = f->next;
				}
				size = p->filteredlen;
				p->flushed = TRUE;
			}
			if (size <= 0)
				return size;
		} else {
			f = p->filters;
			p->filtered = p->buffer;
			p->filteredlen = size;

			d(printf ("\n\nOriginal content (%s): '", ((CamelObject *)filter->source)->klass->name));
			d(fwrite(p->filtered, sizeof(gchar), p->filteredlen, stdout));
			d(printf("'\n"));

			while (f) {
				camel_mime_filter_filter(f->filter, p->filtered, p->filteredlen, presize,
							 &p->filtered, &p->filteredlen, &presize);
				g_check(p->realbuffer);

				d(printf ("Filtered content (%s): '", ((CamelObject *)f->filter)->klass->name));
				d(fwrite(p->filtered, sizeof(gchar), p->filteredlen, stdout));
				d(printf("'\n"));

				f = f->next;
			}
		}
	}

	size = MIN(n, p->filteredlen);
	memcpy(buffer, p->filtered, size);
	p->filteredlen -= size;
	p->filtered += size;

	g_check(p->realbuffer);

	return size;
}

/* Note: Since the caller expects to write out as much as they asked us to
   write (for 'success'), we return what they asked us to write (for 'success')
   rather than the true number of written bytes */
static gssize
do_write (CamelStream *stream, const gchar *buf, gsize n)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *f;
	gsize presize, len, left = n;
	gchar *buffer, realbuffer[READ_SIZE+READ_PAD];

	p->last_was_read = FALSE;

	d(printf ("\n\nWriting: Original content (%s): '", ((CamelObject *)filter->source)->klass->name));
	d(fwrite(buf, sizeof(gchar), n, stdout));
	d(printf("'\n"));

	g_check(p->realbuffer);

	while (left) {
		/* Sigh, since filters expect non const args, copy the input first, we do this in handy sized chunks */
		len = MIN(READ_SIZE, left);
		buffer = realbuffer + READ_PAD;
		memcpy(buffer, buf, len);
		buf += len;
		left -= len;

		f = p->filters;
		presize = READ_PAD;
		while (f) {
			camel_mime_filter_filter(f->filter, buffer, len, presize, &buffer, &len, &presize);

			g_check(p->realbuffer);

			d(printf ("Filtered content (%s): '", ((CamelObject *)f->filter)->klass->name));
			d(fwrite(buffer, sizeof(gchar), len, stdout));
			d(printf("'\n"));

			f = f->next;
		}

		if (camel_stream_write(filter->source, buffer, len) != len)
			return -1;
	}

	g_check(p->realbuffer);

	return n;
}

static gint
do_flush (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *f;
	gchar *buffer;
	gsize presize;
	gsize len;

	if (p->last_was_read)
		return 0;

	buffer = (gchar *) "";
	len = 0;
	presize = 0;
	f = p->filters;

	d(printf ("\n\nFlushing: Original content (%s): '", ((CamelObject *)filter->source)->klass->name));
	d(fwrite(buffer, sizeof(gchar), len, stdout));
	d(printf("'\n"));

	while (f) {
		camel_mime_filter_complete(f->filter, buffer, len, presize, &buffer, &len, &presize);

		d(printf ("Filtered content (%s): '", ((CamelObject *)f->filter)->klass->name));
		d(fwrite(buffer, sizeof(gchar), len, stdout));
		d(printf("'\n"));

		f = f->next;
	}
	if (len > 0 && camel_stream_write(filter->source, buffer, len) == -1)
		return -1;
	return camel_stream_flush(filter->source);
}

static gint
do_close (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);

	if (!p->last_was_read) {
		do_flush(stream);
	}
	return camel_stream_close(filter->source);
}

static gboolean
do_eos (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);

	if (p->filteredlen > 0)
		return FALSE;

	if (!p->flushed)
		return FALSE;

	return camel_stream_eos(filter->source);
}

static gint
do_reset (CamelStream *stream)
{
	CamelStreamFilter *filter = (CamelStreamFilter *)stream;
	struct _CamelStreamFilterPrivate *p = _PRIVATE(filter);
	struct _filter *f;

	p->filteredlen = 0;
	p->flushed = FALSE;

	/* and reset filters */
	f = p->filters;
	while (f) {
		camel_mime_filter_reset(f->filter);
		f = f->next;
	}

	return camel_stream_reset(filter->source);
}

