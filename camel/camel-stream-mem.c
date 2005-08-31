/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-stream-mem.c: memory buffer based stream */

/*
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 1999, 2000 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "camel-stream-mem.h"

static CamelSeekableStreamClass *parent_class = NULL;

/* Returns the class for a CamelStreamMem */
#define CSM_CLASS(so) CAMEL_STREAM_MEM_CLASS(CAMEL_OBJECT_GET_CLASS(so))

static ssize_t stream_read (CamelStream *stream, char *buffer, size_t n);
static ssize_t stream_write (CamelStream *stream, const char *buffer, size_t n);
static gboolean stream_eos (CamelStream *stream);
static off_t stream_seek (CamelSeekableStream *stream, off_t offset,
			  CamelStreamSeekPolicy policy);

static void camel_stream_mem_finalize (CamelObject *object);

static void
camel_stream_mem_class_init (CamelStreamMemClass *camel_stream_mem_class)
{
	CamelSeekableStreamClass *camel_seekable_stream_class =
		CAMEL_SEEKABLE_STREAM_CLASS (camel_stream_mem_class);
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_stream_mem_class);

	parent_class = CAMEL_SEEKABLE_STREAM_CLASS( camel_type_get_global_classfuncs( CAMEL_SEEKABLE_STREAM_TYPE ) );

	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->eos = stream_eos;

	camel_seekable_stream_class->seek = stream_seek;
}

static void
camel_stream_mem_init (CamelObject *object)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (object);

	stream_mem->owner = FALSE;
	stream_mem->buffer = 0;
}

/* could probably be a util method */
static void clear_mem(void *p, size_t len)
{
	char *s = p;

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

CamelType
camel_stream_mem_get_type (void)
{
	static CamelType camel_stream_mem_type = CAMEL_INVALID_TYPE;

	if (camel_stream_mem_type == CAMEL_INVALID_TYPE) {
		camel_stream_mem_type = camel_type_register( CAMEL_SEEKABLE_STREAM_TYPE,
							     "CamelStreamMem",
							     sizeof( CamelStreamMem ),
							     sizeof( CamelStreamMemClass ),
							     (CamelObjectClassInitFunc) camel_stream_mem_class_init,
							     NULL,
							     (CamelObjectInitFunc) camel_stream_mem_init,
							     (CamelObjectFinalizeFunc) camel_stream_mem_finalize );
	}

	return camel_stream_mem_type;
}


/**
 * camel_stream_mem_new:
 *
 * Create a new #CamelStreamMem object.
 *
 * Returns a new #CamelStreamMem
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
 * Create a new memory stream using @byte_array as the stream data.
 *
 * Note: @buffer will be copied into an internal #GByteArray structure
 * for use as the stream backing. This may have resource implications
 * you may wish to consider.
 *
 * Returns a new #CamelStreamMem
 **/
CamelStream *
camel_stream_mem_new_with_buffer (const char *buffer, size_t len)
{
	GByteArray *ba;

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
 * Returns a new #CamelStreamMem
 **/
CamelStream *
camel_stream_mem_new_with_byte_array (GByteArray *buffer)
{
	CamelStreamMem *stream_mem;

	stream_mem = CAMEL_STREAM_MEM (camel_object_new (CAMEL_STREAM_MEM_TYPE));
	stream_mem->buffer = buffer;
	stream_mem->owner = TRUE;

	return CAMEL_STREAM (stream_mem);
}


/**
 * camel_stream_mem_set_secure:
 * @mem: a #CamelStreamMem object
 * 
 * Mark the memory stream as secure.  At the very least this means the
 * data in the buffer will be cleared when the buffer is finalised.
 * This only applies to buffers owned by the stream.
 **/
void
camel_stream_mem_set_secure(CamelStreamMem *mem)
{
	mem->secure = 1;
	/* setup a mem-locked buffer etc?  blah blah, well not yet anyway */
}


/* note: with these functions the caller is the 'owner' of the buffer */

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
camel_stream_mem_set_byte_array (CamelStreamMem *mem, GByteArray *buffer)
{
	if (mem->buffer && mem->owner) {
		if (mem->secure && mem->buffer->len)
			clear_mem (mem->buffer->data, mem->buffer->len);
		g_byte_array_free (mem->buffer, TRUE);
	}
	mem->owner = FALSE;
	mem->buffer = buffer;
}


/**
 * camel_stream_mem_set_buffer:
 * @mem: a #CamelStreamMem object
 * @buffer: a memory buffer
 * @len: length of @buffer
 *
 * Set @buffer to be the backing data to the existing #CamelStreamMem, @mem.
 *
 * Note: @bufer will eb copied into an internal #GByteArray structure
 * and so may have resource implications to consider.
 **/
void
camel_stream_mem_set_buffer (CamelStreamMem *mem, const char *buffer, size_t len)
{
	GByteArray *ba;

	ba = g_byte_array_new ();
	g_byte_array_append(ba, (const guint8 *)buffer, len);
	camel_stream_mem_set_byte_array(mem, ba);
	mem->owner = TRUE;
}


static void
camel_stream_mem_finalize (CamelObject *object)
{
	CamelStreamMem *s = CAMEL_STREAM_MEM (object);

	if (s->buffer && s->owner) {
		/* TODO: we need our own bytearray type since we don't know
		   the real size of the underlying buffer :-/ */
		if (s->secure && s->buffer->len)
			clear_mem(s->buffer->data, s->buffer->len);
		g_byte_array_free(s->buffer, TRUE);
	}
}

static ssize_t
stream_read (CamelStream *stream, char *buffer, size_t n)
{
	CamelStreamMem *camel_stream_mem = CAMEL_STREAM_MEM (stream);
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	ssize_t nread;

	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		n = MIN(seekable->bound_end - seekable->position, n);

	nread = MIN (n, camel_stream_mem->buffer->len - seekable->position);
	if (nread > 0) {
		memcpy (buffer, camel_stream_mem->buffer->data + seekable->position, nread);
		seekable->position += nread;
	} else
		nread = 0;

	return nread;
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (stream);
	CamelSeekableStream *seekable = CAMEL_SEEKABLE_STREAM (stream);
	ssize_t nwrite = n;
	
	if (seekable->bound_end != CAMEL_STREAM_UNBOUND)
		nwrite = MIN(seekable->bound_end - seekable->position, n);

	/* FIXME: we shouldn't use g_byte_arrays or g_malloc perhaps? */
	if (seekable->position == stream_mem->buffer->len) {
		g_byte_array_append(stream_mem->buffer, (const guint8 *)buffer, nwrite);
	} else {
		g_byte_array_set_size(stream_mem->buffer, nwrite + stream_mem->buffer->len);
		memcpy(stream_mem->buffer->data + seekable->position, buffer, nwrite);
	}
	seekable->position += nwrite;

	return nwrite;
}

static gboolean
stream_eos (CamelStream *stream)
{
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (stream);
	CamelSeekableStream *seekable_stream = CAMEL_SEEKABLE_STREAM (stream);

	return stream_mem->buffer->len <= seekable_stream->position;
}

static off_t
stream_seek (CamelSeekableStream *stream, off_t offset,
	     CamelStreamSeekPolicy policy)
{
	off_t position;
	CamelStreamMem *stream_mem = CAMEL_STREAM_MEM (stream);

	switch  (policy) {
	case CAMEL_STREAM_SET:
		position = offset;
		break;
	case CAMEL_STREAM_CUR:
		position = stream->position + offset;
		break;
	case CAMEL_STREAM_END:
		position = (stream_mem->buffer)->len + offset;
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

	if (position > stream_mem->buffer->len) {
		int oldlen = stream_mem->buffer->len;
		g_byte_array_set_size (stream_mem->buffer, position);
		memset (stream_mem->buffer->data + oldlen, 0,
			position - oldlen);
	}

	stream->position = position;

	return position;
}
