/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_PLAIN_H
#define CAMEL_SASL_PLAIN_H

#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_PLAIN \
	(camel_sasl_plain_get_type ())
#define CAMEL_SASL_PLAIN(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_PLAIN, CamelSaslPlain))
#define CAMEL_SASL_PLAIN_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_PLAIN, CamelSaslPlainClass))
#define CAMEL_IS_SASL_PLAIN(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_PLAIN))
#define CAMEL_IS_SASL_PLAIN_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_PLAIN))
#define CAMEL_SASL_PLAIN_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_PLAIN, CamelSaslPlainClass))

G_BEGIN_DECLS

typedef struct _CamelSaslPlain CamelSaslPlain;
typedef struct _CamelSaslPlainClass CamelSaslPlainClass;
typedef struct _CamelSaslPlainPrivate CamelSaslPlainPrivate;

struct _CamelSaslPlain {
	CamelSasl parent;
	CamelSaslPlainPrivate *priv;
};

struct _CamelSaslPlainClass {
	CamelSaslClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_sasl_plain_get_type (void);

G_END_DECLS

#endif /* CAMEL_SASL_PLAIN_H */
