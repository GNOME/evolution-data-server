/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-stream.c : abstract class for a stream */

/*
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include "camel-stream-null.h"

static void camel_stream_null_seekable_init (GSeekableIface *iface);

G_DEFINE_TYPE_WITH_CODE (CamelStreamNull, camel_stream_null, CAMEL_TYPE_STREAM,
	G_IMPLEMENT_INTERFACE (G_TYPE_SEEKABLE, camel_stream_null_seekable_init))

static gssize
stream_null_write (CamelStream *stream,
                   const gchar *buffer,
                   gsize n,
                   GCancellable *cancellable,
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

static goffset
stream_null_tell (GSeekable *seekable)
{
	return 0;
}

static gboolean
stream_null_can_seek (GSeekable *seekable)
{
	return TRUE;
}

static gboolean
stream_null_seek (GSeekable *seekable,
                  goffset offset,
                  GSeekType type,
                  GCancellable *cancellable,
                  GError **error)
{
	if (type != G_SEEK_SET || offset != 0) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Only reset to beginning is supported with CamelHttpStream"));
		return FALSE;
	}

	CAMEL_STREAM_NULL (seekable)->written = 0;

	return TRUE;
}

static gboolean
stream_null_can_truncate (GSeekable *seekable)
{
	return FALSE;
}

static gboolean
stream_null_truncate_fn (GSeekable *seekable,
                         goffset offset,
                         GCancellable *cancellable,
                         GError **error)
{
	/* XXX Don't bother translating this.  Camel never calls it. */
	g_set_error_literal (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		"Truncation is not supported");

	return FALSE;
}

static void
camel_stream_null_class_init (CamelStreamNullClass *class)
{
	CamelStreamClass *stream_class;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->write = stream_null_write;
	stream_class->eos = stream_null_eos;
}

static void
camel_stream_null_seekable_init (GSeekableIface *iface)
{
	iface->tell = stream_null_tell;
	iface->can_seek = stream_null_can_seek;
	iface->seek = stream_null_seek;
	iface->can_truncate = stream_null_can_truncate;
	iface->truncate_fn = stream_null_truncate_fn;
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
camel_stream_null_new (void)
{
	return g_object_new (CAMEL_TYPE_STREAM_NULL, NULL);
}
