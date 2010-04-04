/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_INDEX_H
#define CAMEL_MIME_FILTER_INDEX_H

#include <camel/camel-index.h>
#include <camel/camel-mime-filter.h>

#define CAMEL_MIME_FILTER_INDEX(obj)         CAMEL_CHECK_CAST (obj, camel_mime_filter_index_get_type (), CamelMimeFilterIndex)
#define CAMEL_MIME_FILTER_INDEX_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mime_filter_index_get_type (), CamelMimeFilterIndexClass)
#define CAMEL_IS_MIME_FILTER_INDEX(obj)      CAMEL_CHECK_TYPE (obj, camel_mime_filter_index_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMimeFilterIndex CamelMimeFilterIndex;
typedef struct _CamelMimeFilterIndexClass CamelMimeFilterIndexClass;
typedef struct _CamelMimeFilterIndexPrivate CamelMimeFilterIndexPrivate;

struct _CamelMimeFilterIndex {
	CamelMimeFilter parent;
	CamelMimeFilterIndexPrivate *priv;
};

struct _CamelMimeFilterIndexClass {
	CamelMimeFilterClass parent_class;
};

CamelType	camel_mime_filter_index_get_type(void);
CamelMimeFilter *
		camel_mime_filter_index_new	(CamelIndex *index);

/* Set the match name for any indexed words */
void		camel_mime_filter_index_set_name(CamelMimeFilterIndex *filter,
						 CamelIndexName *name);
void		camel_mime_filter_index_set_index
						(CamelMimeFilterIndex *filter,
						 CamelIndex *index);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_INDEX_H */
