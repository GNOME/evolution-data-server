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

#include "camel-mime-utils.h"
#include "camel-sasl-cram-md5.h"
#include "camel-sasl-digest-md5.h"
#include "camel-sasl-gssapi.h"
#include "camel-sasl-login.h"
#include "camel-sasl-ntlm.h"
#include "camel-sasl-plain.h"
#include "camel-sasl-popb4smtp.h"
#include "camel-sasl.h"
#include "camel-service.h"

#define w(x)

struct _CamelSaslPrivate {
	CamelService *service;
	gboolean authenticated;
	gchar *service_name;
	gchar *mechanism;
};

static CamelObjectClass *parent_class = NULL;

static void
sasl_set_mechanism (CamelSasl *sasl,
                    const gchar *mechanism)
{
	g_return_if_fail (mechanism != NULL);
	g_return_if_fail (sasl->priv->mechanism == NULL);

	sasl->priv->mechanism = g_strdup (mechanism);
}

static void
sasl_set_service (CamelSasl *sasl,
                  CamelService *service)
{
	g_return_if_fail (CAMEL_IS_SERVICE (service));
	g_return_if_fail (sasl->priv->service == NULL);

	sasl->priv->service = service;
	camel_object_ref (service);
}

static void
sasl_set_service_name (CamelSasl *sasl,
                       const gchar *service_name)
{
	g_return_if_fail (service_name != NULL);
	g_return_if_fail (sasl->priv->service_name == NULL);

	sasl->priv->service_name = g_strdup (service_name);
}

static void
sasl_finalize (CamelSasl *sasl)
{
	g_free (sasl->priv->mechanism);
	g_free (sasl->priv->service_name);
	camel_object_unref (sasl->priv->service);
	g_free (sasl->priv);
}

static void
camel_sasl_class_init (CamelSaslClass *camel_sasl_class)
{
	parent_class = camel_type_get_global_classfuncs (CAMEL_TYPE_OBJECT);
}

static void
camel_sasl_init (CamelSasl *sasl)
{
	sasl->priv = g_new0 (CamelSaslPrivate, 1);
}

CamelType
camel_sasl_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_TYPE_OBJECT,
					    "CamelSasl",
					    sizeof (CamelSasl),
					    sizeof (CamelSaslClass),
					    (CamelObjectClassInitFunc) camel_sasl_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_sasl_init,
					    (CamelObjectFinalizeFunc) sasl_finalize);
	}

	return type;
}

/**
 * camel_sasl_challenge:
 * @sasl: a #CamelSasl object
 * @token: a token, or %NULL
 * @ex: a #CamelException
 *
 * If @token is %NULL, generate the initial SASL message to send to
 * the server. (This will be %NULL if the client doesn't initiate the
 * exchange.) Otherwise, @token is a challenge from the server, and
 * the return value is the response.
 *
 * Returns: the SASL response or %NULL. If an error occurred, @ex will
 * also be set.
 **/
GByteArray *
camel_sasl_challenge (CamelSasl *sasl,
                      GByteArray *token,
                      CamelException *ex)
{
	CamelSaslClass *class;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_val_if_fail (class->challenge != NULL, NULL);

	return class->challenge (sasl, token, ex);
}

/**
 * camel_sasl_challenge_base64:
 * @sasl: a #CamelSasl object
 * @token: a base64-encoded token
 * @ex: a #CamelException
 *
 * As with #camel_sasl_challenge, but the challenge @token and the
 * response are both base64-encoded.
 *
 * Returns: the base64 encoded challenge string
 **/
gchar *
camel_sasl_challenge_base64 (CamelSasl *sasl,
                             const gchar *token,
                             CamelException *ex)
{
	GByteArray *token_binary, *ret_binary;
	gchar *ret;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	if (token && *token) {
		guchar *data;
		gsize length = 0;

		data = g_base64_decode (token, &length);
		token_binary = g_byte_array_new ();
		g_byte_array_append (token_binary, data, length);
		g_free (data);
	} else
		token_binary = NULL;

	ret_binary = camel_sasl_challenge (sasl, token_binary, ex);
	if (token_binary)
		g_byte_array_free (token_binary, TRUE);
	if (!ret_binary)
		return NULL;

	if (ret_binary->len > 0)
		ret = g_base64_encode (ret_binary->data, ret_binary->len);
	else
		ret = g_strdup ("");
	g_byte_array_free (ret_binary, TRUE);

	return ret;
}

/**
 * camel_sasl_new:
 * @service_name: the SASL service name
 * @mechanism: the SASL mechanism
 * @service: the CamelService that will be using this SASL
 *
 * Returns: a new #CamelSasl object for the given @service_name,
 * @mechanism, and @service, or %NULL if the mechanism is not
 * supported.
 **/
CamelSasl *
camel_sasl_new (const gchar *service_name,
                const gchar *mechanism,
                CamelService *service)
{
	CamelSasl *sasl;

	g_return_val_if_fail (service_name != NULL, NULL);
	g_return_val_if_fail (mechanism != NULL, NULL);
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	/* We don't do ANONYMOUS here, because it's a little bit weird. */

	if (!strcmp (mechanism, "CRAM-MD5"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_CRAM_MD5_TYPE);
	else if (!strcmp (mechanism, "DIGEST-MD5"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_DIGEST_MD5_TYPE);
#ifdef HAVE_KRB5
	else if (!strcmp (mechanism, "GSSAPI"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_GSSAPI_TYPE);
#endif
	else if (!strcmp (mechanism, "PLAIN"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_PLAIN_TYPE);
	else if (!strcmp (mechanism, "LOGIN"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_LOGIN_TYPE);
	else if (!strcmp (mechanism, "POPB4SMTP"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_POPB4SMTP_TYPE);
	else if (!strcmp (mechanism, "NTLM"))
		sasl = (CamelSasl *) camel_object_new (CAMEL_SASL_NTLM_TYPE);
	else
		return NULL;

	sasl_set_mechanism (sasl, mechanism);
	sasl_set_service (sasl, service);
	sasl_set_service_name (sasl, service_name);

	return sasl;
}

/**
 * camel_sasl_get_authenticated:
 * @sasl: a #CamelSasl object
 *
 * Returns: whether or not @sasl has successfully authenticated the
 * user. This will be %TRUE after it returns the last needed response.
 * The caller must still pass that information on to the server and
 * verify that it has accepted it.
 **/
gboolean
camel_sasl_get_authenticated (CamelSasl *sasl)
{
	g_return_val_if_fail (CAMEL_IS_SASL (sasl), FALSE);

	return sasl->priv->authenticated;
}

void
camel_sasl_set_authenticated (CamelSasl *sasl,
                              gboolean authenticated)
{
	g_return_if_fail (CAMEL_IS_SASL (sasl));

	sasl->priv->authenticated = authenticated;
}

const gchar *
camel_sasl_get_mechanism (CamelSasl *sasl)
{
	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	return sasl->priv->mechanism;
}

CamelService *
camel_sasl_get_service (CamelSasl *sasl)
{
	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	return sasl->priv->service;
}

const gchar *
camel_sasl_get_service_name (CamelSasl *sasl)
{
	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	return sasl->priv->service_name;
}

/**
 * camel_sasl_authtype_list:
 * @include_plain: whether or not to include the PLAIN mechanism
 *
 * Returns: a #GList of SASL-supported authtypes. The caller must
 * free the list, but not the contents.
 **/
GList *
camel_sasl_authtype_list (gboolean include_plain)
{
	GList *types = NULL;

	types = g_list_prepend (types, &camel_sasl_cram_md5_authtype);
	types = g_list_prepend (types, &camel_sasl_digest_md5_authtype);
#ifdef HAVE_KRB5
	types = g_list_prepend (types, &camel_sasl_gssapi_authtype);
#endif
	types = g_list_prepend (types, &camel_sasl_ntlm_authtype);
	if (include_plain)
		types = g_list_prepend (types, &camel_sasl_plain_authtype);

	return types;
}

/**
 * camel_sasl_authtype:
 * @mechanism: the SASL mechanism to get an authtype for
 *
 * Returns: a #CamelServiceAuthType for the given mechanism, if
 * it is supported.
 **/
CamelServiceAuthType *
camel_sasl_authtype (const gchar *mechanism)
{
	if (!strcmp (mechanism, "CRAM-MD5"))
		return &camel_sasl_cram_md5_authtype;
	else if (!strcmp (mechanism, "DIGEST-MD5"))
		return &camel_sasl_digest_md5_authtype;
#ifdef HAVE_KRB5
	else if (!strcmp (mechanism, "GSSAPI"))
		return &camel_sasl_gssapi_authtype;
#endif
	else if (!strcmp (mechanism, "PLAIN"))
		return &camel_sasl_plain_authtype;
	else if (!strcmp (mechanism, "LOGIN"))
		return &camel_sasl_login_authtype;
	else if (!strcmp(mechanism, "POPB4SMTP"))
		return &camel_sasl_popb4smtp_authtype;
	else if (!strcmp (mechanism, "NTLM"))
		return &camel_sasl_ntlm_authtype;
	else
		return NULL;
}
