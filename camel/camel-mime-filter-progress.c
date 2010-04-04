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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "camel-mime-filter-progress.h"

#define d(x)
#define w(x)

struct _CamelMimeFilterProgressPrivate {
	CamelOperation *operation;
	gsize total;
	gsize count;
};

static CamelMimeFilterClass *parent_class = NULL;

static void
mime_filter_progress_finalize (CamelMimeFilterProgress *mime_filter)
{
	g_free (mime_filter->priv);
}

static void
mime_filter_progress_filter (CamelMimeFilter *mime_filter,
                             const gchar *in,
                             gsize len,
                             gsize prespace,
                             gchar **out,
                             gsize *outlen,
                             gsize *outprespace)
{
	CamelMimeFilterProgressPrivate *priv;
	gdouble percent;

	priv = CAMEL_MIME_FILTER_PROGRESS (mime_filter)->priv;

	priv->count += len;

	if (priv->count < priv->total)
		percent = ((gdouble) priv->count * 100.0) / ((gdouble) priv->total);
	else
		percent = 100.0;

	camel_operation_progress (priv->operation, (gint) percent);

	*outprespace = prespace;
	*outlen = len;
	*out = (gchar *) in;
}

static void
mime_filter_progress_complete (CamelMimeFilter *mime_filter,
                               const gchar *in,
                               gsize len,
                               gsize prespace,
                               gchar **out,
                               gsize *outlen,
                               gsize *outprespace)
{
	mime_filter_progress_filter (
		mime_filter, in, len, prespace,
		out, outlen, outprespace);
}

static void
mime_filter_progress_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterProgressPrivate *priv;

	priv = CAMEL_MIME_FILTER_PROGRESS (mime_filter)->priv;

	priv->count = 0;
}

static void
camel_mime_filter_progress_class_init (CamelMimeFilterProgressClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	parent_class = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_progress_filter;
	mime_filter_class->complete = mime_filter_progress_complete;
	mime_filter_class->reset = mime_filter_progress_reset;
}

static void
camel_mime_filter_progress_init (CamelMimeFilterProgress *mime_filter)
{
	mime_filter->priv = g_new0 (CamelMimeFilterProgressPrivate, 1);
}

CamelType
camel_mime_filter_progress_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (),
					    "CamelMimeFilterProgress",
					    sizeof (CamelMimeFilterProgress),
					    sizeof (CamelMimeFilterProgressClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_progress_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_progress_init,
					    (CamelObjectFinalizeFunc) mime_filter_progress_finalize);
	}

	return type;
}

/**
 * camel_mime_filter_progress_new:
 * @operation: a #CamelOperation
 * @total: total number of bytes to report progress on
 *
 * Create a new #CamelMimeFilterProgress object that will report
 * streaming progress.
 *
 * Returns: a new #CamelMimeFilter object
 *
 * Since: 2.24
 **/
CamelMimeFilter *
camel_mime_filter_progress_new (CamelOperation *operation,
                                gsize total)
{
	CamelMimeFilter *filter;
	CamelMimeFilterProgressPrivate *priv;

	filter = (CamelMimeFilter *) camel_object_new (camel_mime_filter_progress_get_type ());
	priv = CAMEL_MIME_FILTER_PROGRESS (filter)->priv;

	priv->operation = operation;
	priv->total = total;

	return filter;
}
