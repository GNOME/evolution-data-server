/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_HTML_H
#define CAMEL_MIME_FILTER_HTML_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_HTML \
	(camel_mime_filter_html_get_type ())
#define CAMEL_MIME_FILTER_HTML(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_HTML, CamelMimeFilterHTML))
#define CAMEL_MIME_FILTER_HTML_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_HTML, CamelMimeFilterHTMLClass))
#define CAMEL_IS_MIME_FILTER_HTML(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_HTML))
#define CAMEL_IS_MIME_FILTER_HTML_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_HTML))
#define CAMEL_MIME_FILTER_HTML_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_HTML, CamelMimeFilterHTMLClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterHTML CamelMimeFilterHTML;
typedef struct _CamelMimeFilterHTMLClass CamelMimeFilterHTMLClass;
typedef struct _CamelMimeFilterHTMLPrivate CamelMimeFilterHTMLPrivate;

struct _CamelMimeFilterHTML {
	CamelMimeFilter parent;
	CamelMimeFilterHTMLPrivate *priv;
};

struct _CamelMimeFilterHTMLClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_html_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_html_new	(void);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_HTML_H */
