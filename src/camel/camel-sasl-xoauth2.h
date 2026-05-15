/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_XOAUTH2_H
#define CAMEL_SASL_XOAUTH2_H

#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_XOAUTH2 \
	(camel_sasl_xoauth2_get_type ())
#define CAMEL_SASL_XOAUTH2(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_XOAUTH2, CamelSaslXOAuth2))
#define CAMEL_SASL_XOAUTH2_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_XOAUTH2, CamelSaslXOAuth2Class))
#define CAMEL_IS_SASL_XOAUTH2(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_XOAUTH2))
#define CAMEL_IS_SASL_XOAUTH2_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_XOAUTH2))
#define CAMEL_SASL_XOAUTH2_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_XOAUTH2, CamelSaslXOAuth2Class))

G_BEGIN_DECLS

typedef struct _CamelSaslXOAuth2 CamelSaslXOAuth2;
typedef struct _CamelSaslXOAuth2Class CamelSaslXOAuth2Class;
typedef struct _CamelSaslXOAuth2Private CamelSaslXOAuth2Private;

struct _CamelSaslXOAuth2 {
	CamelSasl parent;
	CamelSaslXOAuth2Private *priv;
};

struct _CamelSaslXOAuth2Class {
	CamelSaslClass parent_class;
};

GType		camel_sasl_xoauth2_get_type	(void) G_GNUC_CONST;

G_END_DECLS

#endif /* CAMEL_SASL_XOAUTH2_H */
