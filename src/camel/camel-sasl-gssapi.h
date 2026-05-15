/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_GSSAPI_H
#define CAMEL_SASL_GSSAPI_H

#include <sys/types.h>
#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_GSSAPI \
	(camel_sasl_gssapi_get_type ())
#define CAMEL_SASL_GSSAPI(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_GSSAPI, CamelSaslGssapi))
#define CAMEL_SASL_GSSAPI_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_GSSAPI, CamelSaslGssapiClass))
#define CAMEL_IS_SASL_GSSAPI(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_GSSAPI))
#define CAMEL_IS_SASL_GSSAPI_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_GSSAPI))
#define CAMEL_SASL_GSSAPI_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_GSSAPI, CamelSaslGssapiClass))

G_BEGIN_DECLS

typedef struct _CamelSaslGssapi CamelSaslGssapi;
typedef struct _CamelSaslGssapiClass CamelSaslGssapiClass;
typedef struct _CamelSaslGssapiPrivate CamelSaslGssapiPrivate;

struct _CamelSaslGssapi {
	CamelSasl parent;
	CamelSaslGssapiPrivate *priv;
};

struct _CamelSaslGssapiClass {
	CamelSaslClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_sasl_gssapi_get_type	(void);
gboolean	camel_sasl_gssapi_is_available	(void);
void		camel_sasl_gssapi_override_host_and_user
						(CamelSaslGssapi *sasl,
						 const gchar *override_host,
						 const gchar *override_user);

G_END_DECLS

#endif /* CAMEL_SASL_GSSAPI_H */
