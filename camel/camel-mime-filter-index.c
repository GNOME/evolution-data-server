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

#include "camel-mime-filter-index.h"
#include "camel-text-index.h"

struct _CamelMimeFilterIndexPrivate {
	CamelIndex *index;
	CamelIndexName *name;
};

static CamelMimeFilterClass *camel_mime_filter_index_parent;

static void
mime_filter_index_finalize (CamelMimeFilterIndex *mime_filter)
{
	if (mime_filter->priv->name)
		camel_object_unref (mime_filter->priv->name);
	camel_object_unref (mime_filter->priv->index);
	g_free (mime_filter->priv);
}

static void
mime_filter_index_filter (CamelMimeFilter *mime_filter,
                          const gchar *in,
                          gsize len,
                          gsize prespace,
                          gchar **out,
                          gsize *outlenptr,
                          gsize *outprespace)
{
	CamelMimeFilterIndexPrivate *priv;

	priv = CAMEL_MIME_FILTER_INDEX (mime_filter)->priv;

	if (priv->index == NULL || priv->name==NULL) {
		goto donothing;
	}

	camel_index_name_add_buffer (priv->name, in, len);

donothing:
	*out = (gchar *) in;
	*outlenptr = len;
	*outprespace = prespace;
}

static void
mime_filter_index_complete (CamelMimeFilter *mime_filter,
                            const gchar *in,
                            gsize len,
                            gsize prespace,
                            gchar **out,
                            gsize *outlenptr,
                            gsize *outprespace)
{
	CamelMimeFilterIndexPrivate *priv;

	priv = CAMEL_MIME_FILTER_INDEX (mime_filter)->priv;

	if (priv->index == NULL || priv->name==NULL) {
		goto donothing;
	}

	camel_index_name_add_buffer (priv->name, in, len);
	camel_index_name_add_buffer (priv->name, NULL, 0);

donothing:
	*out = (gchar *) in;
	*outlenptr = len;
	*outprespace = prespace;
}

static void
camel_mime_filter_index_class_init (CamelMimeFilterIndexClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	camel_mime_filter_index_parent = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_index_filter;
	mime_filter_class->complete = mime_filter_index_complete;
}

static void
camel_mime_filter_index_init (CamelMimeFilterIndex *mime_filter)
{
	mime_filter->priv = g_new0 (CamelMimeFilterIndexPrivate, 1);
}

CamelType
camel_mime_filter_index_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (), "CamelMimeFilterIndex",
					    sizeof (CamelMimeFilterIndex),
					    sizeof (CamelMimeFilterIndexClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_index_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_index_init,
					    (CamelObjectFinalizeFunc) mime_filter_index_finalize);
	}

	return type;
}

/**
 * camel_mime_filter_index_new:
 * @index: a #CamelIndex object
 *
 * Create a new #CamelMimeFilterIndex based on @index.
 *
 * Returns: a new #CamelMimeFilterIndex object
 **/
CamelMimeFilter *
camel_mime_filter_index_new (CamelIndex *index)
{
	CamelMimeFilter *new;
	CamelMimeFilterIndexPrivate *priv;

	new = CAMEL_MIME_FILTER (camel_object_new (camel_mime_filter_index_get_type ()));

	if (new) {
		priv = CAMEL_MIME_FILTER_INDEX (new)->priv;
		priv->index = index;
		if (index)
			camel_object_ref (index);
	}
	return new;
}

/* Set the match name for any indexed words */

/**
 * camel_mime_filter_index_set_name:
 * @filter: a #CamelMimeFilterIndex object
 * @name: a #CamelIndexName object
 *
 * Set the match name for any indexed words.
 **/
void
camel_mime_filter_index_set_name (CamelMimeFilterIndex *filter,
                                  CamelIndexName *name)
{
	g_return_if_fail (CAMEL_IS_MIME_FILTER_INDEX (filter));

	if (name != NULL) {
		g_return_if_fail (CAMEL_IS_INDEX_NAME (name));
		camel_object_ref (name);
	}

	if (filter->priv->name != NULL)
		camel_object_unref (filter->priv->name);

	filter->priv->name = name;
}

/**
 * camel_mime_filter_index_set_index:
 * @filter: a #CamelMimeFilterIndex object
 * @index: a #CamelIndex object
 *
 * Set @index on @filter.
 **/
void
camel_mime_filter_index_set_index (CamelMimeFilterIndex *filter,
                                   CamelIndex *index)
{
	g_return_if_fail (CAMEL_IS_MIME_FILTER_INDEX (filter));

	if (index != NULL) {
		g_return_if_fail (CAMEL_IS_INDEX (index));
		camel_object_ref (index);
	}

	if (filter->priv->index) {
		gchar *out;
		gsize outlen, outspace;

		camel_mime_filter_complete (
			CAMEL_MIME_FILTER (filter),
			"", 0, 0, &out, &outlen, &outspace);
		camel_object_unref (index);
	}

	filter->priv->index = index;
}
