/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STREAM_FILTER_H
#define CAMEL_STREAM_FILTER_H

#include <camel/camel-stream.h>
#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STREAM_FILTER \
	(camel_stream_filter_get_type ())
#define CAMEL_STREAM_FILTER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STREAM_FILTER, CamelStreamFilter))
#define CAMEL_STREAM_FILTER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STREAM_FILTER, CamelStreamFilterClass))
#define CAMEL_IS_STREAM_FILTER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STREAM_FILTER))
#define CAMEL_IS_STREAM_FILTER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STREAM_FILTER))
#define CAMEL_STREAM_FILTER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STREAM_FILTER, CamelStreamFilterClass))

G_BEGIN_DECLS

typedef struct _CamelStreamFilter CamelStreamFilter;
typedef struct _CamelStreamFilterClass CamelStreamFilterClass;
typedef struct _CamelStreamFilterPrivate CamelStreamFilterPrivate;

struct _CamelStreamFilter {
	CamelStream parent;
	CamelStreamFilterPrivate *priv;
};

struct _CamelStreamFilterClass {
	CamelStreamClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_stream_filter_get_type	(void);
CamelStream *	camel_stream_filter_new		(CamelStream *source);
CamelStream *	camel_stream_filter_get_source	(CamelStreamFilter *stream);
gint		camel_stream_filter_add		(CamelStreamFilter *stream,
						 CamelMimeFilter *filter);
void		camel_stream_filter_remove	(CamelStreamFilter *stream,
						 gint id);

G_END_DECLS

#endif /* CAMEL_STREAM_FILTER_H */
