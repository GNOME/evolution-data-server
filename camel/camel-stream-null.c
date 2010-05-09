/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-stream.c : abstract class for a stream */

/*
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-stream-null.h"

G_DEFINE_TYPE (CamelStreamNull, camel_stream_null, CAMEL_TYPE_STREAM)

static gssize
stream_null_write (CamelStream *stream,
                   const gchar *buffer,
                   gsize n,
                   GError **error)
{
	CAMEL_STREAM_NULL (stream)->written += n;

	return n;
}

static gboolean
stream_null_eos (CamelStream *stream)
{
	return TRUE;
}

static gint
stream_null_reset (CamelStream *stream,
                   GError **error)
{
	CAMEL_STREAM_NULL (stream)->written = 0;

	return 0;
}

static void
camel_stream_null_class_init (CamelStreamNullClass *class)
{
	CamelStreamClass *stream_class;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->write = stream_null_write;
	stream_class->eos = stream_null_eos;
	stream_class->reset = stream_null_reset;
}

static void
camel_stream_null_init (CamelStreamNull *stream_null)
{
}

/**
 * camel_stream_null_new:
 *
 * Returns a null stream.  A null stream is always at eof, and
 * always returns success for all reads and writes.
 *
 * Returns: a new #CamelStreamNull
 **/
CamelStream *
camel_stream_null_new(void)
{
	return g_object_new (CAMEL_TYPE_STREAM_NULL, NULL);
}
