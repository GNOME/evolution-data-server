/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_INDEX_H
#define CAMEL_MIME_FILTER_INDEX_H

#include <camel/camel-index.h>
#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_INDEX \
	(camel_mime_filter_index_get_type ())
#define CAMEL_MIME_FILTER_INDEX(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_INDEX, CamelMimeFilterIndex))
#define CAMEL_MIME_FILTER_INDEX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_INDEX, CamelMimeFilterIndexClass))
#define CAMEL_IS_MIME_FILTER_INDEX(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_INDEX))
#define CAMEL_IS_MIME_FILTER_INDEX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_INDEX))
#define CAMEL_MIME_FILTER_INDEX_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_INDEX, CamelMimeFilterIndexClass))

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

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_index_get_type (void);
CamelMimeFilter *
		camel_mime_filter_index_new	(CamelIndex *index);

/* Set the match name for any indexed words */
void		camel_mime_filter_index_set_name (CamelMimeFilterIndex *filter,
						 CamelIndexName *name);
void		camel_mime_filter_index_set_index
						(CamelMimeFilterIndex *filter,
						 CamelIndex *index);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_INDEX_H */
