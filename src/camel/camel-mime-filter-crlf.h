/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_CRLF_H
#define CAMEL_MIME_FILTER_CRLF_H

#include <camel/camel-enums.h>
#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_CRLF \
	(camel_mime_filter_crlf_get_type ())
#define CAMEL_MIME_FILTER_CRLF(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_CRLF, CamelMimeFilterCRLF))
#define CAMEL_MIME_FILTER_CRLF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_CRLF, CamelMimeFilterCRLFClass))
#define CAMEL_IS_MIME_FILTER_CRLF(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_CRLF))
#define CAMEL_IS_MIME_FILTER_CRLF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_CRLF))
#define CAMEL_MIME_FILTER_CRLF_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_CRLF, CamelMimeFilterCRLFClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterCRLF CamelMimeFilterCRLF;
typedef struct _CamelMimeFilterCRLFClass CamelMimeFilterCRLFClass;
typedef struct _CamelMimeFilterCRLFPrivate CamelMimeFilterCRLFPrivate;

struct _CamelMimeFilterCRLF {
	CamelMimeFilter parent;
	CamelMimeFilterCRLFPrivate *priv;
};

struct _CamelMimeFilterCRLFClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_crlf_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_crlf_new	(CamelMimeFilterCRLFDirection direction,
						 CamelMimeFilterCRLFMode mode);
void		camel_mime_filter_crlf_set_ensure_crlf_end
						(CamelMimeFilterCRLF *filter,
						 gboolean ensure_crlf_end);
gboolean	camel_mime_filter_crlf_get_ensure_crlf_end
						(CamelMimeFilterCRLF *filter);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_CRLF_H */
