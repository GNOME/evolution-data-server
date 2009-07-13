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

#ifndef CAMEL_SASL_LOGIN_H
#define CAMEL_SASL_LOGIN_H

#include <camel/camel-sasl.h>

#define CAMEL_SASL_LOGIN_TYPE     (camel_sasl_login_get_type ())
#define CAMEL_SASL_LOGIN(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SASL_LOGIN_TYPE, CamelSaslLogin))
#define CAMEL_SASL_LOGIN_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SASL_LOGIN_TYPE, CamelSaslLoginClass))
#define CAMEL_IS_SASL_LOGIN(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SASL_LOGIN_TYPE))

G_BEGIN_DECLS

typedef struct _CamelSaslLogin {
	CamelSasl parent_object;

	struct _CamelSaslLoginPrivate *priv;

} CamelSaslLogin;

typedef struct _CamelSaslLoginClass {
	CamelSaslClass parent_class;

} CamelSaslLoginClass;

/* Standard Camel function */
CamelType camel_sasl_login_get_type (void);

extern CamelServiceAuthType camel_sasl_login_authtype;

G_END_DECLS

#endif /* CAMEL_SASL_LOGIN_H */
