/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_FROM_H
#define CAMEL_MIME_FILTER_FROM_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_FROM \
	(camel_mime_filter_from_get_type ())
#define CAMEL_MIME_FILTER_FROM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_FROM, CamelMimeFilterFrom))
#define CAMEL_MIME_FILTER_FROM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_FROM, CamelMimeFilterFromClass))
#define CAMEL_IS_MIME_FILTER_FROM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_FROM))
#define CAMEL_IS_MIME_FILTER_FROM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_FROM))
#define CAMEL_MIME_FILTER_FROM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_FROM, CamelMimeFilterFromClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterFrom CamelMimeFilterFrom;
typedef struct _CamelMimeFilterFromClass CamelMimeFilterFromClass;
typedef struct _CamelMimeFilterFromPrivate CamelMimeFilterFromPrivate;

struct _CamelMimeFilterFrom {
	CamelMimeFilter parent;
	CamelMimeFilterFromPrivate *priv;
};

struct _CamelMimeFilterFromClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_from_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_from_new	(void);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_FROM_H */
