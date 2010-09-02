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

#include "camel-debug.h"
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

#define CAMEL_SASL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SASL, CamelSaslPrivate))

struct _CamelSaslPrivate {
	CamelService *service;
	gboolean authenticated;
	gchar *service_name;
	gchar *mechanism;
};

enum {
	PROP_0,
	PROP_AUTHENTICATED,
	PROP_MECHANISM,
	PROP_SERVICE,
	PROP_SERVICE_NAME
};

G_DEFINE_ABSTRACT_TYPE (CamelSasl, camel_sasl, CAMEL_TYPE_OBJECT)

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

	sasl->priv->service = g_object_ref (service);
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
sasl_set_property (GObject *object,
                   guint property_id,
                   const GValue *value,
                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTHENTICATED:
			camel_sasl_set_authenticated (
				CAMEL_SASL (object),
				g_value_get_boolean (value));
			return;

		case PROP_MECHANISM:
			sasl_set_mechanism (
				CAMEL_SASL (object),
				g_value_get_string (value));
			return;

		case PROP_SERVICE:
			sasl_set_service (
				CAMEL_SASL (object),
				g_value_get_object (value));
			return;

		case PROP_SERVICE_NAME:
			sasl_set_service_name (
				CAMEL_SASL (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
sasl_get_property (GObject *object,
                   guint property_id,
                   GValue *value,
                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTHENTICATED:
			g_value_set_boolean (
				value, camel_sasl_get_authenticated (
				CAMEL_SASL (object)));
			return;

		case PROP_MECHANISM:
			g_value_set_string (
				value, camel_sasl_get_mechanism (
				CAMEL_SASL (object)));
			return;

		case PROP_SERVICE:
			g_value_set_object (
				value, camel_sasl_get_service (
				CAMEL_SASL (object)));
			return;

		case PROP_SERVICE_NAME:
			g_value_set_string (
				value, camel_sasl_get_service_name (
				CAMEL_SASL (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
sasl_dispose (GObject *object)
{
	CamelSaslPrivate *priv;

	priv = CAMEL_SASL_GET_PRIVATE (object);

	if (priv->service != NULL) {
		g_object_unref (priv->service);
		priv->service = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_sasl_parent_class)->dispose (object);
}

static void
sasl_finalize (GObject *object)
{
	CamelSaslPrivate *priv;

	priv = CAMEL_SASL_GET_PRIVATE (object);

	g_free (priv->mechanism);
	g_free (priv->service_name);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_sasl_parent_class)->finalize (object);
}

static void
camel_sasl_class_init (CamelSaslClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelSaslPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = sasl_set_property;
	object_class->get_property = sasl_get_property;
	object_class->dispose = sasl_dispose;
	object_class->finalize = sasl_finalize;

	g_object_class_install_property (
		object_class,
		PROP_AUTHENTICATED,
		g_param_spec_boolean (
			"authenticated",
			"Authenticated",
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_MECHANISM,
		g_param_spec_string (
			"mechanism",
			"Mechanism",
			NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_SERVICE,
		g_param_spec_object (
			"service",
			"Service",
			NULL,
			CAMEL_TYPE_SERVICE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_SERVICE_NAME,
		g_param_spec_string (
			"service-name",
			"Service Name",
			NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
camel_sasl_init (CamelSasl *sasl)
{
	sasl->priv = CAMEL_SASL_GET_PRIVATE (sasl);
}

/**
 * camel_sasl_challenge:
 * @sasl: a #CamelSasl object
 * @token: a token, or %NULL
 * @error: return location for a #GError, or %NULL
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
                      GError **error)
{
	CamelSaslClass *class;
	GByteArray *response;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_val_if_fail (class->challenge != NULL, NULL);

	response = class->challenge (sasl, token, error);
	if (token)
		CAMEL_CHECK_GERROR (sasl, challenge, response != NULL, error);

	return response;
}

/**
 * camel_sasl_challenge_base64:
 * @sasl: a #CamelSasl object
 * @token: a base64-encoded token
 * @error: return location for a #GError, or %NULL
 *
 * As with #camel_sasl_challenge, but the challenge @token and the
 * response are both base64-encoded.
 *
 * Returns: the base64 encoded challenge string
 **/
gchar *
camel_sasl_challenge_base64 (CamelSasl *sasl,
                             const gchar *token,
                             GError **error)
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

	ret_binary = camel_sasl_challenge (sasl, token_binary, error);
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
	GType type;

	g_return_val_if_fail (service_name != NULL, NULL);
	g_return_val_if_fail (mechanism != NULL, NULL);
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	/* We don't do ANONYMOUS here, because it's a little bit weird. */

	if (!strcmp (mechanism, "CRAM-MD5"))
		type = CAMEL_TYPE_SASL_CRAM_MD5;
	else if (!strcmp (mechanism, "DIGEST-MD5"))
		type = CAMEL_TYPE_SASL_DIGEST_MD5;
#ifdef HAVE_KRB5
	else if (!strcmp (mechanism, "GSSAPI"))
		type = CAMEL_TYPE_SASL_GSSAPI;
#endif
	else if (!strcmp (mechanism, "PLAIN"))
		type = CAMEL_TYPE_SASL_PLAIN;
	else if (!strcmp (mechanism, "LOGIN"))
		type = CAMEL_TYPE_SASL_LOGIN;
	else if (!strcmp (mechanism, "POPB4SMTP"))
		type = CAMEL_TYPE_SASL_POPB4SMTP;
	else if (!strcmp (mechanism, "NTLM"))
		type = CAMEL_TYPE_SASL_NTLM;
	else
		return NULL;

	return g_object_new (
		type, "mechanism", mechanism, "service",
		service, "service-name", service_name, NULL);
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

/**
 * camel_sasl_set_authenticated:
 * @sasl: a #CamelSasl
 * @authenticated: whether we have successfully authenticated
 *
 * Since: 2.32
 **/
void
camel_sasl_set_authenticated (CamelSasl *sasl,
                              gboolean authenticated)
{
	g_return_if_fail (CAMEL_IS_SASL (sasl));

	sasl->priv->authenticated = authenticated;

	g_object_notify (G_OBJECT (sasl), "authenticated");
}

/**
 * camel_sasl_get_mechanism:
 * @sasl: a #CamelSasl
 *
 * Since: 2.32
 **/
const gchar *
camel_sasl_get_mechanism (CamelSasl *sasl)
{
	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	return sasl->priv->mechanism;
}

/**
 * camel_sasl_get_service:
 * @sasl: a #CamelSasl
 *
 * Since: 2.32
 **/
CamelService *
camel_sasl_get_service (CamelSasl *sasl)
{
	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	return sasl->priv->service;
}

/**
 * camel_sasl_get_service_name:
 * @sasl: a #CamelSasl
 *
 * Since: 2.32
 **/
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
