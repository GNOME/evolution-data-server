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

struct _CamelMimeFilterSavePrivate {
	CamelStream *stream;
};

static void
mime_filter_save_finalize (CamelMimeFilterSave *mime_filter)
{
	g_free (mime_filter->priv);
}

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

	priv = CAMEL_MIME_FILTER_SAVE (mime_filter)->priv;

	if (priv->stream != NULL)
		camel_stream_write (priv->stream, in, len);

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

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_save_filter;
	mime_filter_class->complete = mime_filter_save_complete;
	mime_filter_class->reset = mime_filter_save_reset;
}

static void
camel_mime_filter_save_init (CamelMimeFilterSave *mime_filter)
{
	mime_filter->priv = g_new0 (CamelMimeFilterSavePrivate, 1);
}

CamelType
camel_mime_filter_save_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type(), "CamelMimeFilterSave",
					    sizeof (CamelMimeFilterSave),
					    sizeof (CamelMimeFilterSaveClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_save_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_save_init,
					    (CamelObjectFinalizeFunc) mime_filter_save_finalize);
	}

	return type;
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

	filter = CAMEL_MIME_FILTER (camel_object_new (CAMEL_MIME_FILTER_SAVE_TYPE));
	priv = CAMEL_MIME_FILTER_SAVE (filter)->priv;

	priv->stream = stream;
	if (stream != NULL)
		camel_object_ref (stream);

	return filter;
}
