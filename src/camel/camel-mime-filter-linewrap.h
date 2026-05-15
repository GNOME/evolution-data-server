/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_LINEWRAP_H
#define CAMEL_MIME_FILTER_LINEWRAP_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_LINEWRAP \
	(camel_mime_filter_linewrap_get_type ())
#define CAMEL_MIME_FILTER_LINEWRAP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_LINEWRAP, CamelMimeFilterLinewrap))
#define CAMEL_MIME_FILTER_LINEWRAP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_LINEWRAP, CamelMimeFilterLinewrapClass))
#define CAMEL_IS_MIME_FILTER_LINEWRAP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_LINEWRAP))
#define CAMEL_IS_MIME_FILTER_LINEWRAP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_LINEWRAP))
#define CAMEL_MIME_FILTER_LINEWRAP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_LINEWRAP, CamelMimeFilterLinewrapClass))

G_BEGIN_DECLS

enum {
	CAMEL_MIME_FILTER_LINEWRAP_NOINDENT = (1 << 0), /* does not indent; it's forced for indent_char = 0 */
	CAMEL_MIME_FILTER_LINEWRAP_WORD     = (1 << 1), /* indents on word boundary */
};

typedef struct _CamelMimeFilterLinewrap CamelMimeFilterLinewrap;
typedef struct _CamelMimeFilterLinewrapClass CamelMimeFilterLinewrapClass;
typedef struct _CamelMimeFilterLinewrapPrivate CamelMimeFilterLinewrapPrivate;

struct _CamelMimeFilterLinewrap {
	CamelMimeFilter parent;
	CamelMimeFilterLinewrapPrivate *priv;
};

struct _CamelMimeFilterLinewrapClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_linewrap_get_type (void);
CamelMimeFilter *
		camel_mime_filter_linewrap_new	(guint preferred_len,
						 guint max_len,
						 gchar indent_char,
                                                 guint32 flags);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_LINEWRAP_H */
