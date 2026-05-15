/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_CHARSET_H
#define CAMEL_MIME_FILTER_CHARSET_H

#include <camel/camel-mime-filter.h>
#include <iconv.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_CHARSET \
	(camel_mime_filter_charset_get_type ())
#define CAMEL_MIME_FILTER_CHARSET(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_CHARSET, CamelMimeFilterCharset))
#define CAMEL_MIME_FILTER_CHARSET_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_CHARSET, CamelMimeFilterCharsetClass))
#define CAMEL_IS_MIME_FILTER_CHARSET(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_CHARSET))
#define CAMEL_IS_MIME_FILTER_CHARSET_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_CHARSET))
#define CAMEL_MIME_FILTER_CHARSET_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_CHARSET, CamelMimeFilterCharsetClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterCharset CamelMimeFilterCharset;
typedef struct _CamelMimeFilterCharsetClass CamelMimeFilterCharsetClass;
typedef struct _CamelMimeFilterCharsetPrivate CamelMimeFilterCharsetPrivate;

struct _CamelMimeFilterCharset {
	CamelMimeFilter parent;
	CamelMimeFilterCharsetPrivate *priv;
};

struct _CamelMimeFilterCharsetClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_charset_get_type (void);
CamelMimeFilter *
		camel_mime_filter_charset_new	(const gchar *from_charset,
						 const gchar *to_charset);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_CHARSET_H */
