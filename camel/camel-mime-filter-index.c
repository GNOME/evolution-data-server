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

static void camel_mime_filter_index_class_init (CamelMimeFilterIndexClass *klass);
static void camel_mime_filter_index_finalize   (CamelObject *o);

static CamelMimeFilterClass *camel_mime_filter_index_parent;

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
					    NULL,
					    (CamelObjectFinalizeFunc) camel_mime_filter_index_finalize);
	}

	return type;
}

static void
camel_mime_filter_index_finalize(CamelObject *o)
{
	CamelMimeFilterIndex *f = (CamelMimeFilterIndex *)o;

	if (f->name)
		camel_object_unref((CamelObject *)f->name);
	camel_object_unref((CamelObject *)f->index);
}

static void
complete(CamelMimeFilter *mf, const gchar *in, gsize len, gsize prespace, gchar **out, gsize *outlenptr, gsize *outprespace)
{
	CamelMimeFilterIndex *f = (CamelMimeFilterIndex *)mf;

	if (f->index == NULL || f->name==NULL) {
		goto donothing;
	}

	camel_index_name_add_buffer(f->name, in, len);
	camel_index_name_add_buffer(f->name, NULL, 0);

donothing:
	*out = (gchar *) in;
	*outlenptr = len;
	*outprespace = prespace;
}

static void
filter(CamelMimeFilter *mf, const gchar *in, gsize len, gsize prespace, gchar **out, gsize *outlenptr, gsize *outprespace)
{
	CamelMimeFilterIndex *f = (CamelMimeFilterIndex *)mf;

	if (f->index == NULL || f->name==NULL) {
		goto donothing;
	}

	camel_index_name_add_buffer(f->name, in, len);

donothing:
	*out = (gchar *) in;
	*outlenptr = len;
	*outprespace = prespace;
}

static void
camel_mime_filter_index_class_init (CamelMimeFilterIndexClass *klass)
{
	CamelMimeFilterClass *filter_class = (CamelMimeFilterClass *) klass;

	camel_mime_filter_index_parent = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	/*filter_class->reset = reset;*/
	filter_class->filter = filter;
	filter_class->complete = complete;
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
	CamelMimeFilterIndex *new;

	new = CAMEL_MIME_FILTER_INDEX (camel_object_new (camel_mime_filter_index_get_type ()));

	if (new) {
		new->index = index;
		if (index)
			camel_object_ref (index);
	}
	return CAMEL_MIME_FILTER (new);
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
camel_mime_filter_index_set_name (CamelMimeFilterIndex *filter, CamelIndexName *name)
{
	if (filter->name)
		camel_object_unref (filter->name);
	filter->name = name;
	if (name)
		camel_object_ref (name);
}

/**
 * camel_mime_filter_index_set_index:
 * @filter: a #CamelMimeFilterIndex object
 * @index: a #CamelIndex object
 *
 * Set @index on @filter.
 **/
void
camel_mime_filter_index_set_index (CamelMimeFilterIndex *filter, CamelIndex *index)
{
	if (filter->index) {
		gchar *out;
		gsize outlen, outspace;

		camel_mime_filter_complete((CamelMimeFilter *)filter, "", 0, 0, &out, &outlen, &outspace);
		camel_object_unref (index);
	}

	filter->index = index;
	if (index)
		camel_object_ref (index);
}
