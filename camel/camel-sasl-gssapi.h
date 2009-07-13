/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __CAMEL_SASL_GSSAPI_H__
#define __CAMEL_SASL_GSSAPI_H__

#include <sys/types.h>
#include <camel/camel-sasl.h>

#define CAMEL_SASL_GSSAPI_TYPE     (camel_sasl_gssapi_get_type ())
#define CAMEL_SASL_GSSAPI(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SASL_GSSAPI_TYPE, CamelSaslGssapi))
#define CAMEL_SASL_GSSAPI_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SASL_GSSAPI_TYPE, CamelSaslGssapiClass))
#define CAMEL_IS_SASL_GSSAPI(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SASL_GSSAPI_TYPE))

G_BEGIN_DECLS

typedef struct _CamelSaslGssapi CamelSaslGssapi;
typedef struct _CamelSaslGssapiClass CamelSaslGssapiClass;

struct _CamelSaslGssapi {
	CamelSasl parent_object;

	struct _CamelSaslGssapiPrivate *priv;

};

struct _CamelSaslGssapiClass {
	CamelSaslClass parent_class;

};

/* Standard Camel function */
CamelType camel_sasl_gssapi_get_type (void);

extern CamelServiceAuthType camel_sasl_gssapi_authtype;

G_END_DECLS

#endif /* __CAMEL_SASL_GSSAPI_H__ */
