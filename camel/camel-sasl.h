/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_H
#define CAMEL_SASL_H

#include <camel/camel-object.h>
#include <camel/camel-service.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL \
	(camel_sasl_get_type ())
#define CAMEL_SASL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL, CamelSasl))
#define CAMEL_SASL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL, CamelSaslClass))
#define CAMEL_IS_SASL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL))
#define CAMEL_IS_SASL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL))
#define CAMEL_SASL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL, CamelSaslClass))

G_BEGIN_DECLS

typedef struct _CamelSasl CamelSasl;
typedef struct _CamelSaslClass CamelSaslClass;
typedef struct _CamelSaslPrivate CamelSaslPrivate;

struct _CamelSasl {
	CamelObject parent;
	CamelSaslPrivate *priv;
};

struct _CamelSaslClass {
	CamelObjectClass parent_class;

	GByteArray *	(*challenge)		(CamelSasl *sasl,
						 GByteArray *token,
						 GError **error);
};

GType		camel_sasl_get_type		(void);
GByteArray *	camel_sasl_challenge		(CamelSasl *sasl,
						 GByteArray *token,
						 GError **error);
gchar *		camel_sasl_challenge_base64	(CamelSasl *sasl,
						 const gchar *token,
						 GError **error);
CamelSasl *	camel_sasl_new			(const gchar *service_name,
						 const gchar *mechanism,
						 CamelService *service);
gboolean	camel_sasl_get_authenticated	(CamelSasl *sasl);
void		camel_sasl_set_authenticated	(CamelSasl *sasl,
						 gboolean authenticated);
const gchar *	camel_sasl_get_mechanism	(CamelSasl *sasl);
CamelService *	camel_sasl_get_service		(CamelSasl *sasl);
const gchar *	camel_sasl_get_service_name	(CamelSasl *sasl);

GList *		camel_sasl_authtype_list	(gboolean include_plain);
CamelServiceAuthType *
		camel_sasl_authtype		(const gchar *mechanism);

G_END_DECLS

#endif /* CAMEL_SASL_H */
