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

typedef struct _AsyncContext AsyncContext;

struct _CamelSaslPrivate {
	CamelService *service;
	gboolean authenticated;
	gchar *service_name;
	gchar *mechanism;
};

struct _AsyncContext {
	/* arguments */
	GByteArray *token;
	gchar *base64_token;

	/* results */
	GByteArray *response;
	gchar *base64_response;
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
async_context_free (AsyncContext *async_context)
{
	if (async_context->token != NULL)
		g_byte_array_free (async_context->token, TRUE);

	if (async_context->response != NULL)
		g_byte_array_free (async_context->response, TRUE);

	g_free (async_context->base64_token);
	g_free (async_context->base64_response);

	g_slice_free (AsyncContext, async_context);
}

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

	priv = CAMEL_SASL (object)->priv;

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

	priv = CAMEL_SASL (object)->priv;

	g_free (priv->mechanism);
	g_free (priv->service_name);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_sasl_parent_class)->finalize (object);
}

static void
sasl_challenge_thread (GSimpleAsyncResult *simple,
                       GObject *object,
                       GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->response = camel_sasl_challenge_sync (
		CAMEL_SASL (object), async_context->token,
		cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

static void
sasl_challenge (CamelSasl *sasl,
                GByteArray *token,
                gint io_priority,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->token = g_byte_array_new ();

	g_byte_array_append (async_context->token, token->data, token->len);

	simple = g_simple_async_result_new (
		G_OBJECT (sasl), callback, user_data, sasl_challenge);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, sasl_challenge_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static GByteArray *
sasl_challenge_finish (CamelSasl *sasl,
                       GAsyncResult *result,
                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GByteArray *response;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (sasl), sasl_challenge), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	response = async_context->response;
	async_context->response = NULL;

	return response;
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

	class->challenge = sasl_challenge;
	class->challenge_finish = sasl_challenge_finish;

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
	sasl->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		sasl, CAMEL_TYPE_SASL, CamelSaslPrivate);
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
 * @sasl: a #CamelSasl
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
 * camel_sasl_challenge_sync:
 * @sasl: a #CamelSasl
 * @token: a token, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * If @token is %NULL, generate the initial SASL message to send to
 * the server.  (This will be %NULL if the client doesn't initiate the
 * exchange.)  Otherwise, @token is a challenge from the server, and
 * the return value is the response.
 *
 * Free the returned #GByteArray with g_byte_array_free().
 *
 * Returns: the SASL response or %NULL. If an error occurred, @error will
 * also be set.
 **/
GByteArray *
camel_sasl_challenge_sync (CamelSasl *sasl,
                           GByteArray *token,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelSaslClass *class;
	GByteArray *response;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_val_if_fail (class->challenge_sync != NULL, NULL);

	response = class->challenge_sync (sasl, token, cancellable, error);
	if (token != NULL)
		CAMEL_CHECK_GERROR (
			sasl, challenge_sync, response != NULL, error);

	return response;
}

/**
 * camel_sasl_challenge:
 * @sasl: a #CamelSasl
 * @token: a token, or %NULL
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * If @token is %NULL, asynchronously generate the initial SASL message
 * to send to the server.  (This will be %NULL if the client doesn't
 * initiate the exchange.)  Otherwise, @token is a challenge from the
 * server, and the asynchronous result is the response.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_sasl_challenge_finish() to get the result of the operation.
 *
 * Since: 2.92
 **/
void
camel_sasl_challenge (CamelSasl *sasl,
                      GByteArray *token,
                      gint io_priority,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	CamelSaslClass *class;

	g_return_if_fail (CAMEL_IS_SASL (sasl));

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_if_fail (class->challenge != NULL);

	class->challenge (
		sasl, token, io_priority, cancellable, callback, user_data);
}

/**
 * camel_sasl_challenge_finish:
 * @sasl: a #CamelSasl
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_sasl_challenge().  Free the
 * returned #GByteArray with g_byte_array_free().
 *
 * Returns: the SASL response or %NULL.  If an error occurred, @error will
 * also be set.
 *
 * Since: 2.92
 **/
GByteArray *
camel_sasl_challenge_finish (CamelSasl *sasl,
                             GAsyncResult *result,
                             GError **error)
{
	CamelSaslClass *class;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_val_if_fail (class->challenge_finish != NULL, NULL);

	return class->challenge_finish (sasl, result, error);
}

/**
 * camel_sasl_challenge_base64_sync:
 * @sasl: a #CamelSasl
 * @token: a base64-encoded token
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * As with camel_sasl_challenge_sync(), but the challenge @token and the
 * response are both base64-encoded.
 *
 * Returns: the base64-encoded response
 *
 * Since: 2.92
 **/
gchar *
camel_sasl_challenge_base64_sync (CamelSasl *sasl,
                                  const gchar *token,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GByteArray *token_binary;
	GByteArray *response_binary;
	gchar *response;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);

	if (token != NULL && *token != '\0') {
		guchar *data;
		gsize length = 0;

		data = g_base64_decode (token, &length);
		token_binary = g_byte_array_new ();
		g_byte_array_append (token_binary, data, length);
		g_free (data);
	} else
		token_binary = NULL;

	response_binary = camel_sasl_challenge_sync (
		sasl, token_binary, cancellable, error);
	if (token_binary)
		g_byte_array_free (token_binary, TRUE);
	if (response_binary == NULL)
		return NULL;

	if (response_binary->len > 0)
		response = g_base64_encode (
			response_binary->data, response_binary->len);
	else
		response = g_strdup ("");

	g_byte_array_free (response_binary, TRUE);

	return response;
}

/* Helper for camel_sasl_challenge_base64() */
static void
sasl_challenge_base64_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->base64_response = camel_sasl_challenge_base64_sync (
		CAMEL_SASL (object), async_context->base64_token,
		cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

/**
 * camel_sasl_challenge_base64:
 * @sasl: a #CamelSasl
 * @token: a base64-encoded token
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * As with camel_sasl_challenge(), but the challenge @token and the
 * response are both base64-encoded.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_store_challenge_base64_finish() to get the result of
 * the operation.
 *
 * Since: 2.92
 **/
void
camel_sasl_challenge_base64 (CamelSasl *sasl,
                             const gchar *token,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_SASL (sasl));

	async_context = g_slice_new0 (AsyncContext);
	async_context->base64_token = g_strdup (token);

	simple = g_simple_async_result_new (
		G_OBJECT (sasl), callback, user_data,
		camel_sasl_challenge_base64);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, sasl_challenge_base64_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

/**
 * camel_sasl_challenge_base64_finish:
 * @sasl: a #CamelSasl
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_sasl_challenge_base64().
 *
 * Returns: the base64-encoded response
 *
 * Since: 2.92
 **/
gchar *
camel_sasl_challenge_base64_finish (CamelSasl *sasl,
                                    GAsyncResult *result,
                                    GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	gchar *response;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (sasl), camel_sasl_challenge_base64), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	response = async_context->base64_response;
	async_context->base64_response = NULL;

	return response;
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
