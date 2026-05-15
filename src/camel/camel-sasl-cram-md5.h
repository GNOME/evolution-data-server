/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_CRAM_MD5_H
#define CAMEL_SASL_CRAM_MD5_H

#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_CRAM_MD5 \
	(camel_sasl_cram_md5_get_type ())
#define CAMEL_SASL_CRAM_MD5(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_CRAM_MD5, CamelSaslCramMd5))
#define CAMEL_SASL_CRAM_MD5_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_CRAM_MD5, CamelSaslCramMd5Class))
#define CAMEL_IS_SASL_CRAM_MD5(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_CRAM_MD5))
#define CAMEL_IS_SASL_CRAM_MD5_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_CRAM_MD5))
#define CAMEL_SASL_CRAM_MD5_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_CRAM_MD5, CamelSaslCramMd5Class))

G_BEGIN_DECLS

typedef struct _CamelSaslCramMd5 CamelSaslCramMd5;
typedef struct _CamelSaslCramMd5Class CamelSaslCramMd5Class;
typedef struct _CamelSaslCramMd5Private CamelSaslCramMd5Private;

struct _CamelSaslCramMd5 {
	CamelSasl parent;
	CamelSaslCramMd5Private *priv;
};

struct _CamelSaslCramMd5Class {
	CamelSaslClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_sasl_cram_md5_get_type (void);

G_END_DECLS

#endif /* CAMEL_SASL_CRAM_MD5_H */
