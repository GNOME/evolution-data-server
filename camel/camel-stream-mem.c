/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-mem.c: memory buffer based stream */

/*
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "camel-stream-mem.h"

#define CAMEL_STREAM_MEM_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_STREAM_MEM, CamelStreamMemPrivate))

struct _CamelStreamMemPrivate {
	guint owner  : 1;	/* do we own the buffer? */
	guint secure : 1;	/* do we clear the buffer on finalize?
				   (only if we own it) */

	GByteArray *buffer;
};

G_DEFINE_TYPE (CamelStreamMem, camel_stream_mem, CAMEL_TYPE_SEEKABLE_STREAM)

/* could probably be a util method */
static void
clear_mem (gpointer p, gsize len)
{
	gchar *s = p;

	/* This also helps debug bad access memory errors */
	while (len > 4) {
		*s++ = 0xAB;
		*s++ = 0xAD;
		*s++ = 0xF0;
		*s++ = 0x0D;
		len -= 4;
	}

	memset(s, 0xbf, len);
}

static void
stream_mem_finalize (GObject *object)
{
	CamelStreamMemPrivate *priv;

	priv = CAMEL_STREAM_MEM_GET_PRIVATE (object);

	if (priv->buffer && priv->owner) {
		/* TODO: we need our own bytearray type since we don't know
		   the real size of the underlying buffer :-/ */
		if (priv->secure && priv->buffer->len)
			clear_mem (priv->buffer->data, priv->buffer->len);
		g_byte_array_free (priv->buffer, TRUE);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_stream_mem_parent_class)->finalize (object);
}

static gssize
stream_mem_read (CamelStream *stream,
                 gchar *buffer,
                 gsize n,
                 GError **error)
{
	CamelStreamMemPrivate *priv;
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	gssize nread;

	priv = CAMEL_STREAM_MEM_GET_PRIVATE (stream);

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN(seekable->bound_end - seekable->position, n);

	nread = MIN (n, priv->buffer->len - seekable->position);
	if (nread > 0) {
		memcpy (buffer, priv->buffer->data + seekable->position, nread);
		seekable->position += nread;
	} else
		nread = 0;

	return nread;
}

static gssize
stream_mem_write (CamelStream *stream,
                  const gchar *buffer,
                  gsize n,
                  GError **error)
{
	CamelStreamMemPrivate *priv;
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	gssize nwrite = n;

	priv = CAMEL_STREAM_MEM_GET_PRIVATE (stream);

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		nwrite = MIN(seekable->bound_end - seekable->position, n);

	/* FIXME: we shouldn't use g_byte_arrays or g_malloc perhaps? */
	if (seekable->position == priv->buffer->len) {
		g_byte_array_append (priv->buffer, (const guint8 *)buffer, nwrite);
	} else {
		g_byte_array_set_size (priv->buffer, nwrite + priv->buffer->len);
		memcpy (priv->buffer->data + seekable->position, buffer, nwrite);
	}
	seekable->position += nwrite;

	return nwrite;
}

static gboolean
stream_mem_eos (CamelStream *stream)
{
	CamelStreamMemPrivate *priv;
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (stream);

	priv = CAMEL_STREAM_MEM_GET_PRIVATE (stream);

	return priv->buffer->len <= seekable_stream->position;
}

static goffset
stream_mem_seek (CamelSeekableStream *stream,
                 goffset offset,
                 CamelStreamSeekPolicy policy,
                 GError **error)
{
	CamelStreamMemPrivate *priv;
	goffset position;

	priv = CAMEL_STREAM_MEM_GET_PRIVATE (stream);

	switch  (policy) {
	case CAMEL_STREAM_SET:
		position = offset;
		break;
	case CAMEL_STREAM_CUR:
		position = stream->position + offset;
		break;
	case CAMEL_STREAM_END:
		position = (priv->buffer)->len + offset;
		break;
	default:
		position = offset;
		break;
	}

	if (stream->bound_end != CAMEL_STREAM_UNBOUND)
		position = MIN (position, stream->bound_end);
	if (stream->bound_start != CAMEL_STREAM_UNBOUND)
		position = MAX (position, 0);
	else
		position = MAX (position, stream->bound_start);

	if (position > priv->buffer->len) {
		gint oldlen = priv->buffer->len;
		g_byte_array_set_size (priv->buffer, position);
		memset (priv->buffer->data + oldlen, 0,
			position - oldlen);
	}

	stream->position = position;

	return position;
}

static void
camel_stream_mem_class_init (CamelStreamMemClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;
	CamelSeekableStreamClass *seekable_stream_class;

	g_type_class_add_private (class, sizeof (CamelStreamMemPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = stream_mem_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = stream_mem_read;
	stream_class->write = stream_mem_write;
	stream_class->eos = stream_mem_eos;

	seekable_stream_class = CAMEL_SEEKABLE_STREAM_CLASS (class);
	seekable_stream_class->seek = stream_mem_seek;
}

static void
camel_stream_mem_init (CamelStreamMem *stream)
{
	stream->priv = CAMEL_STREAM_MEM_GET_PRIVATE (stream);
}

/**
 * camel_stream_mem_new:
 *
 * Create a new #CamelStreamMem object.
 *
 * Returns: a new #CamelStreamMem
 **/
CamelStream *
camel_stream_mem_new (void)
{
	return camel_stream_mem_new_with_byte_array (g_byte_array_new ());
}

/**
 * camel_stream_mem_new_with_buffer:
 * @buffer: a memory buffer to use as the stream data
 * @len: length of @buffer
 *
 * Create a new memory stream using @buffer as the stream data.
 *
 * Note: @buffer will be copied into an internal #GByteArray structure
 * for use as the stream backing. This may have resource implications
 * you may wish to consider.
 *
 * Returns: a new #CamelStreamMem
 **/
CamelStream *
camel_stream_mem_new_with_buffer (const gchar *buffer,
                                  gsize len)
{
	GByteArray *ba;

	g_return_val_if_fail (buffer != NULL, NULL);

	ba = g_byte_array_new ();
	g_byte_array_append (ba, (const guint8 *)buffer, len);

	return camel_stream_mem_new_with_byte_array (ba);
}

/**
 * camel_stream_mem_new_with_byte_array:
 * @buffer: a #GByteArray to use as the stream data
 *
 * Create a new #CamelStreamMem using @buffer as the stream data.
 *
 * Note: The newly created #CamelStreamMem will destroy @buffer
 * when destroyed.
 *
 * Returns: a new #CamelStreamMem
 **/
CamelStream *
camel_stream_mem_new_with_byte_array (GByteArray *buffer)
{
	CamelStream *stream;
	CamelStreamMemPrivate *priv;

	g_return_val_if_fail (buffer != NULL, NULL);

	stream = g_object_new (CAMEL_TYPE_STREAM_MEM, NULL);
	priv = CAMEL_STREAM_MEM_GET_PRIVATE (stream);

	priv->buffer = buffer;
	priv->owner = TRUE;

	return stream;
}

/**
 * camel_stream_mem_set_secure:
 * @mem: a #CamelStreamMem object
 *
 * Mark the memory stream as secure.  At the very least this means the
 * data in the buffer will be cleared when the buffer is finalized.
 * This only applies to buffers owned by the stream.
 **/
void
camel_stream_mem_set_secure(CamelStreamMem *mem)
{
	g_return_if_fail (CAMEL_IS_STREAM_MEM (mem));

	mem->priv->secure = 1;
}

/* note: with these functions the caller is the 'owner' of the buffer */

/**
 * camel_stream_mem_get_byte_array:
 * @mem: a #CamelStreamMem
 *
 * Since: 2.32
 **/
GByteArray *
camel_stream_mem_get_byte_array (CamelStreamMem *mem)
{
	g_return_val_if_fail (CAMEL_IS_STREAM_MEM (mem), NULL);

	return mem->priv->buffer;
}

/**
 * camel_stream_mem_set_byte_array:
 * @mem: a #CamelStreamMem object
 * @buffer: a #GByteArray
 *
 * Set @buffer to be the backing data to the existing #CamelStreamMem, @mem.
 *
 * Note: @mem will not take ownership of @buffer and so will need to
 * be freed separately from @mem.
 **/
void
camel_stream_mem_set_byte_array (CamelStreamMem *mem,
                                 GByteArray *buffer)
{
	g_return_if_fail (CAMEL_IS_STREAM_MEM (mem));
	g_return_if_fail (buffer != NULL);

	if (mem->priv->buffer && mem->priv->owner) {
		if (mem->priv->secure && mem->priv->buffer->len)
			clear_mem (
				mem->priv->buffer->data,
				mem->priv->buffer->len);
		g_byte_array_free (mem->priv->buffer, TRUE);
	}
	mem->priv->owner = FALSE;
	mem->priv->buffer = buffer;
}

/**
 * camel_stream_mem_set_buffer:
 * @mem: a #CamelStreamMem object
 * @buffer: a memory buffer
 * @len: length of @buffer
 *
 * Set @buffer to be the backing data to the existing #CamelStreamMem, @mem.
 *
 * Note: @buffer will be copied into an internal #GByteArray structure
 * and so may have resource implications to consider.
 **/
void
camel_stream_mem_set_buffer (CamelStreamMem *mem,
                             const gchar *buffer,
                             gsize len)
{
	GByteArray *ba;

	g_return_if_fail (CAMEL_IS_STREAM_MEM (mem));
	g_return_if_fail (buffer != NULL);

	ba = g_byte_array_new ();
	g_byte_array_append(ba, (const guint8 *)buffer, len);
	camel_stream_mem_set_byte_array(mem, ba);
	mem->priv->owner = TRUE;
}
