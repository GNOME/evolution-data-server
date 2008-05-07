/*
 *  Copyright (C) 2000 Ximian Inc.
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
complete(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlenptr, size_t *outprespace)
{
	CamelMimeFilterIndex *f = (CamelMimeFilterIndex *)mf;

	if (f->index == NULL || f->name==NULL) {
		goto donothing;
	}

	camel_index_name_add_buffer(f->name, in, len);
	camel_index_name_add_buffer(f->name, NULL, 0);

donothing:
	*out = in;
	*outlenptr = len;
	*outprespace = prespace;
}

static void
filter(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlenptr, size_t *outprespace)
{
	CamelMimeFilterIndex *f = (CamelMimeFilterIndex *)mf;

	if (f->index == NULL || f->name==NULL) {
		goto donothing;
	}

	camel_index_name_add_buffer(f->name, in, len);

donothing:
	*out = in;
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
 *
 * Create a new #CamelMimeFilterIndex object
 * 
 * Returns a new #CamelMimeFilterIndex object
 **/
CamelMimeFilterIndex *
camel_mime_filter_index_new (void)
{
	CamelMimeFilterIndex *new = CAMEL_MIME_FILTER_INDEX ( camel_object_new (camel_mime_filter_index_get_type ()));
	return new;
}


/**
 * camel_mime_filter_index_new_index:
 * @index: a #CamelIndex object
 *
 * Create a new #CamelMimeFilterIndex based on @index.
 *
 * Returns a new #CamelMimeFilterIndex object
 **/
CamelMimeFilterIndex *
camel_mime_filter_index_new_index (CamelIndex *index)
{
	CamelMimeFilterIndex *new = camel_mime_filter_index_new();

	if (new) {
		new->index = index;
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
		char *out;
		size_t outlen, outspace;

		camel_mime_filter_complete((CamelMimeFilter *)filter, "", 0, 0, &out, &outlen, &outspace);
		camel_object_unref (index);
	}

	filter->index = index;
	if (index)
		camel_object_ref (index);
}
