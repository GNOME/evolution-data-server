/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_TOHTML_H
#define CAMEL_MIME_FILTER_TOHTML_H

#include <camel/camel-enums.h>
#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_TOHTML \
	(camel_mime_filter_tohtml_get_type ())
#define CAMEL_MIME_FILTER_TOHTML(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_TOHTML, CamelMimeFilterToHTML))
#define CAMEL_MIME_FILTER_TOHTML_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_TOHTML, CamelMimeFilterToHTMLClass))
#define CAMEL_IS_MIME_FILTER_TOHTML(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_TOHTML))
#define CAMEL_IS_MIME_FILTER_TOHTML_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_TOHTML))
#define CAMEL_MIME_FILTER_TOHTML_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_TOHTML, CamelMimeFilterToHTMLClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterToHTML CamelMimeFilterToHTML;
typedef struct _CamelMimeFilterToHTMLClass CamelMimeFilterToHTMLClass;
typedef struct _CamelMimeFilterToHTMLPrivate CamelMimeFilterToHTMLPrivate;

struct _CamelMimeFilterToHTML {
	CamelMimeFilter parent;
	CamelMimeFilterToHTMLPrivate *priv;
};

struct _CamelMimeFilterToHTMLClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_tohtml_get_type (void);
CamelMimeFilter *
		camel_mime_filter_tohtml_new	(CamelMimeFilterToHTMLFlags flags,
						 guint32 color);

/* utility functions to replace e_text_to_html */

gchar *		camel_text_to_html		(const gchar *in,
						 CamelMimeFilterToHTMLFlags flags,
						 guint32 color);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_TOHTML_H */
