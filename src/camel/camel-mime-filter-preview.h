/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_PREVIEW_H
#define CAMEL_MIME_FILTER_PREVIEW_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_PREVIEW \
	(camel_mime_filter_preview_get_type ())
#define CAMEL_MIME_FILTER_PREVIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_PREVIEW, CamelMimeFilterPreview))
#define CAMEL_MIME_FILTER_PREVIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_PREVIEW, CamelMimeFilterPreviewClass))
#define CAMEL_IS_MIME_FILTER_PREVIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_PREVIEW))
#define CAMEL_IS_MIME_FILTER_PREVIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_PREVIEW))
#define CAMEL_MIME_FILTER_PREVIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_PREVIEW, CamelMimeFilterPreviewClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterPreview CamelMimeFilterPreview;
typedef struct _CamelMimeFilterPreviewClass CamelMimeFilterPreviewClass;
typedef struct _CamelMimeFilterPreviewPrivate CamelMimeFilterPreviewPrivate;

struct _CamelMimeFilterPreview {
	CamelMimeFilter parent;
	CamelMimeFilterPreviewPrivate *priv;
};

struct _CamelMimeFilterPreviewClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[4];
};

GType		camel_mime_filter_preview_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_preview_new		(guint limit);
guint		camel_mime_filter_preview_get_limit	(CamelMimeFilterPreview *self);
void		camel_mime_filter_preview_set_limit	(CamelMimeFilterPreview *self,
							 guint limit);
const gchar *	camel_mime_filter_preview_get_text	(CamelMimeFilterPreview *self);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_PREVIEW_H */
