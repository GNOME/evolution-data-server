/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Matt Brown <matt@mattb.net.nz>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_PGP_H
#define CAMEL_MIME_FILTER_PGP_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_PGP \
	(camel_mime_filter_pgp_get_type ())
#define CAMEL_MIME_FILTER_PGP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_PGP, CamelMimeFilterPgp))
#define CAMEL_MIME_FILTER_PGP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_PGP, CamelMimeFilterPgpClass))
#define CAMEL_IS_MIME_FILTER_PGP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_PGP))
#define CAMEL_IS_MIME_FILTER_PGP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_PGP))
#define CAMEL_MIME_FILTER_PGP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_PGP, CamelMimeFilterPgpClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterPgp CamelMimeFilterPgp;
typedef struct _CamelMimeFilterPgpClass CamelMimeFilterPgpClass;
typedef struct _CamelMimeFilterPgpPrivate CamelMimeFilterPgpPrivate;

struct _CamelMimeFilterPgp {
	CamelMimeFilter parent;
	CamelMimeFilterPgpPrivate *priv;
};

struct _CamelMimeFilterPgpClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_pgp_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_pgp_new	(void);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_PGP_H */
