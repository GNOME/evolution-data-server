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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-sasl-login.h"
#include "camel-service.h"

#define CAMEL_SASL_LOGIN_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SASL_LOGIN, CamelSaslLoginPrivate))

CamelServiceAuthType camel_sasl_login_authtype = {
	N_("Login"),

	N_("This option will connect to the server using a "
	   "simple password."),

	"LOGIN",
	TRUE
};

enum {
	LOGIN_USER,
	LOGIN_PASSWD
};

struct _CamelSaslLoginPrivate {
	gint state;
};

G_DEFINE_TYPE (CamelSaslLogin, camel_sasl_login, CAMEL_TYPE_SASL)

static GByteArray *
sasl_login_challenge (CamelSasl *sasl,
                      GByteArray *token,
                      GError **error)
{
	CamelSaslLoginPrivate *priv;
	GByteArray *buf = NULL;
	CamelService *service;
	CamelURL *url;

	priv = CAMEL_SASL_LOGIN_GET_PRIVATE (sasl);

	service = camel_sasl_get_service (sasl);
	url = service->url;
	g_return_val_if_fail (url->passwd != NULL, NULL);

	/* Need to wait for the server */
	if (!token)
		return NULL;

	switch (priv->state) {
	case LOGIN_USER:
		buf = g_byte_array_new ();
		g_byte_array_append (buf, (guint8 *) url->user, strlen (url->user));
		break;
	case LOGIN_PASSWD:
		buf = g_byte_array_new ();
		g_byte_array_append (buf, (guint8 *) url->passwd, strlen (url->passwd));

		camel_sasl_set_authenticated (sasl, TRUE);
		break;
	default:
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Unknown authentication state."));
	}

	priv->state++;

	return buf;
}

static void
camel_sasl_login_class_init (CamelSaslLoginClass *class)
{
	CamelSaslClass *sasl_class;

	g_type_class_add_private (class, sizeof (CamelSaslLoginPrivate));

	sasl_class = CAMEL_SASL_CLASS (class);
	sasl_class->challenge = sasl_login_challenge;
}

static void
camel_sasl_login_init (CamelSaslLogin *sasl)
{
	sasl->priv = CAMEL_SASL_LOGIN_GET_PRIVATE (sasl);
}
