/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STREAM_NULL_H
#define CAMEL_STREAM_NULL_H

#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STREAM_NULL \
	(camel_stream_null_get_type ())
#define CAMEL_STREAM_NULL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STREAM_NULL, CamelStreamNull))
#define CAMEL_STREAM_NULL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STREAM_NULL, CamelStreamNullClass))
#define CAMEL_IS_STREAM_NULL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STREAM_NULL))
#define CAMEL_IS_STREAM_NULL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STREAM_NULL))
#define CAMEL_STREAM_NULL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STREAM_NULL, CamelStreamNullClass))

G_BEGIN_DECLS

typedef struct _CamelStreamNull CamelStreamNull;
typedef struct _CamelStreamNullClass CamelStreamNullClass;
typedef struct _CamelStreamNullPrivate CamelStreamNullPrivate;

struct _CamelStreamNull {
	CamelStream parent;
	CamelStreamNullPrivate *priv;
};

struct _CamelStreamNullClass {
	CamelStreamClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_stream_null_get_type (void);

CamelStream *	camel_stream_null_new		(void);
gsize		camel_stream_null_get_bytes_written
						(CamelStreamNull *stream_null);
gboolean	camel_stream_null_get_ends_with_crlf
						(CamelStreamNull *stream_null);

G_END_DECLS

#endif /* CAMEL_STREAM_NULL_H */
