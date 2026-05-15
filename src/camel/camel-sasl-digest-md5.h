/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_DIGEST_MD5_H
#define CAMEL_SASL_DIGEST_MD5_H

#include <sys/types.h>
#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_DIGEST_MD5 \
	(camel_sasl_digest_md5_get_type ())
#define CAMEL_SASL_DIGEST_MD5(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_DIGEST_MD5, CamelSaslDigestMd5))
#define CAMEL_SASL_DIGEST_MD5_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_DIGEST_MD5, CamelSaslDigestMd5Class))
#define CAMEL_IS_SASL_DIGEST_MD5(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_DIGEST_MD5))
#define CAMEL_IS_SASL_DIGEST_MD5_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_DIGEST_MD5))
#define CAMEL_SASL_DIGEST_MD5_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_DIGEST_MD5, CamelSaslDigestMd5Class))

G_BEGIN_DECLS

typedef struct _CamelSaslDigestMd5 CamelSaslDigestMd5;
typedef struct _CamelSaslDigestMd5Class CamelSaslDigestMd5Class;
typedef struct _CamelSaslDigestMd5Private CamelSaslDigestMd5Private;

struct _CamelSaslDigestMd5 {
	CamelSasl parent;
	CamelSaslDigestMd5Private *priv;
};

struct _CamelSaslDigestMd5Class {
	CamelSaslClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_sasl_digest_md5_get_type (void);

G_END_DECLS

#endif /* CAMEL_SASL_DIGEST_MD5_H */
