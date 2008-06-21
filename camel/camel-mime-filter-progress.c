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

static void camel_mime_filter_progress_class_init (CamelMimeFilterProgressClass *klass);
static void camel_mime_filter_progress_init       (CamelObject *o);
static void camel_mime_filter_progress_finalize   (CamelObject *o);

static CamelMimeFilterClass *parent_class = NULL;

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
					    (CamelObjectFinalizeFunc) camel_mime_filter_progress_finalize);
	}
	
	return type;
}

static void
camel_mime_filter_progress_finalize (CamelObject *o)
{
	;
}

static void
camel_mime_filter_progress_init (CamelObject *o)
{
	CamelMimeFilterProgress *progress = (CamelMimeFilterProgress *) o;
	
	progress->count = 0;
}

static void
filter_filter (CamelMimeFilter *filter, char *in, size_t len, size_t prespace,
	       char **out, size_t *outlen, size_t *outprespace)
{
	CamelMimeFilterProgress *progress = (CamelMimeFilterProgress *) filter;
	double percent;
	
	progress->count += len;
	
	if (progress->count < progress->total)
		percent = ((double) progress->count * 100.0) / ((double) progress->total);
	else
		percent = 100.0;
	
	camel_operation_progress (progress->operation, (int) percent);
	
	*outprespace = prespace;
	*outlen = len;
	*out = in;
}

static void 
filter_complete (CamelMimeFilter *filter, char *in, size_t len, size_t prespace,
		 char **out, size_t *outlen, size_t *outprespace)
{
	filter_filter (filter, in, len, prespace, out, outlen, outprespace);
}

static void
filter_reset (CamelMimeFilter *filter)
{
	CamelMimeFilterProgress *progress = (CamelMimeFilterProgress *) filter;
	
	progress->count = 0;
}

static void
camel_mime_filter_progress_class_init (CamelMimeFilterProgressClass *klass)
{
	CamelMimeFilterClass *filter_class = (CamelMimeFilterClass *) klass;
	
	parent_class = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));
	
	filter_class->reset = filter_reset;
	filter_class->filter = filter_filter;
	filter_class->complete = filter_complete;
}


/**
 * camel_mime_filter_progress_new:
 * @operation: a #CamelOperation
 * @total: total number of bytes to report progress on
 *
 * Create a new #CamelMimeFilterProgress object that will report
 * streaming progress.
 *
 * Returns a new #CamelMimeFilter object
 **/
CamelMimeFilter *
camel_mime_filter_progress_new (CamelOperation *operation, size_t total)
{
	CamelMimeFilter *filter;
	
	filter = (CamelMimeFilter *) camel_object_new (camel_mime_filter_progress_get_type ());
	((CamelMimeFilterProgress *) filter)->operation = operation;
	((CamelMimeFilterProgress *) filter)->total = total;
	
	return filter;
}
