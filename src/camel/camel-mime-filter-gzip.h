/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_GZIP_H
#define CAMEL_MIME_FILTER_GZIP_H

#include <camel/camel-enums.h>
#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_GZIP \
	(camel_mime_filter_gzip_get_type ())
#define CAMEL_MIME_FILTER_GZIP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_GZIP, CamelMimeFilterGZip))
#define CAMEL_MIME_FILTER_GZIP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_GZIP, CamelMimeFilterGZipClass))
#define CAMEL_IS_MIME_FILTER_GZIP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_GZIP))
#define CAMEL_IS_MIME_FILTER_GZIP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_GZIP))
#define CAMEL_MIME_FILTER_GZIP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_GZIP, CamelMimeFilterGZipClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterGZip CamelMimeFilterGZip;
typedef struct _CamelMimeFilterGZipClass CamelMimeFilterGZipClass;
typedef struct _CamelMimeFilterGZipPrivate CamelMimeFilterGZipPrivate;

struct _CamelMimeFilterGZip {
	CamelMimeFilter parent;
	CamelMimeFilterGZipPrivate *priv;
};

struct _CamelMimeFilterGZipClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_gzip_get_type (void);
CamelMimeFilter *
		camel_mime_filter_gzip_new	(CamelMimeFilterGZipMode mode,
						 gint level);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_GZIP_H */
