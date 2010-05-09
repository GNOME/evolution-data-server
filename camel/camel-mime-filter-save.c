/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-mime-filter-save.h"
#include "camel-stream-mem.h"

#define CAMEL_MIME_FILTER_SAVE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_MIME_FILTER_SAVE, CamelMimeFilterSavePrivate))

struct _CamelMimeFilterSavePrivate {
	CamelStream *stream;
};

G_DEFINE_TYPE (CamelMimeFilterSave, camel_mime_filter_save, CAMEL_TYPE_MIME_FILTER)

static void
mime_filter_save_filter (CamelMimeFilter *mime_filter,
                         const gchar *in,
                         gsize len,
                         gsize prespace,
                         gchar **out,
                         gsize *outlen,
                         gsize *outprespace)
{
	CamelMimeFilterSavePrivate *priv;

	priv = CAMEL_MIME_FILTER_SAVE_GET_PRIVATE (mime_filter);

	if (priv->stream != NULL)
		camel_stream_write (priv->stream, in, len, NULL);

	*out = (gchar *) in;
	*outlen = len;
	*outprespace = mime_filter->outpre;
}

static void
mime_filter_save_complete (CamelMimeFilter *mime_filter,
                           const gchar *in,
                           gsize len,
                           gsize prespace,
                           gchar **out,
                           gsize *outlen,
                           gsize *outprespace)
{
	if (len)
		mime_filter_save_filter (
			mime_filter, in, len, prespace,
			out, outlen, outprespace);
}

static void
mime_filter_save_reset (CamelMimeFilter *mime_filter)
{
	/* no-op */
}

static void
camel_mime_filter_save_class_init (CamelMimeFilterSaveClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	g_type_class_add_private (class, sizeof (CamelMimeFilterSavePrivate));

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_save_filter;
	mime_filter_class->complete = mime_filter_save_complete;
	mime_filter_class->reset = mime_filter_save_reset;
}

static void
camel_mime_filter_save_init (CamelMimeFilterSave *filter)
{
	filter->priv = CAMEL_MIME_FILTER_SAVE_GET_PRIVATE (filter);
}

/**
 * camel_mime_filter_save_new:
 * @stream: a #CamelStream object
 *
 * Create a new #CamelMimeFilterSave filter object that will save a
 * copy of all filtered data to @stream.
 *
 * Returns: a new #CamelMimeFilterSave object
 **/
CamelMimeFilter *
camel_mime_filter_save_new (CamelStream *stream)
{
	CamelMimeFilter *filter;
	CamelMimeFilterSavePrivate *priv;

	if (stream != NULL)
		g_return_val_if_fail (CAMEL_IS_STREAM (stream), NULL);

	filter = g_object_new (CAMEL_TYPE_MIME_FILTER_SAVE, NULL);
	priv = CAMEL_MIME_FILTER_SAVE_GET_PRIVATE (filter);

	if (stream != NULL)
		priv->stream = g_object_ref (stream);
	else
		priv->stream = camel_stream_mem_new ();

	return filter;
}
