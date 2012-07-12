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
#include "camel-sasl-anonymous.h"
#include "camel-sasl-cram-md5.h"
#include "camel-sasl-digest-md5.h"
#include "camel-sasl-gssapi.h"
#include "camel-sasl-login.h"
#include "camel-sasl-ntlm.h"
#include "camel-sasl-plain.h"
#include "camel-sasl-popb4smtp.h"
#include "camel-sasl.h"
#include "camel-service.h"

#define CAMEL_SASL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SASL, CamelSaslPrivate))

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
sasl_build_class_table_rec (GType type,
                            GHashTable *class_table)
{
	GType *children;
	guint n_children, ii;

	children = g_type_children (type, &n_children);

	for (ii = 0; ii < n_children; ii++) {
		GType type = children[ii];
		CamelSaslClass *sasl_class;
		gpointer key;

		/* Recurse over the child's children. */
		sasl_build_class_table_rec (type, class_table);

		/* Skip abstract types. */
		if (G_TYPE_IS_ABSTRACT (type))
			continue;

		sasl_class = g_type_class_ref (type);

		if (sasl_class->auth_type == NULL) {
			g_critical (
				"%s has an empty CamelServiceAuthType",
				G_OBJECT_CLASS_NAME (sasl_class));
			g_type_class_unref (sasl_class);
			continue;
		}

		key = (gpointer) sasl_class->auth_type->authproto;
		g_hash_table_insert (class_table, key, sasl_class);
	}

	g_free (children);
}

static GHashTable *
sasl_build_class_table (void)
{
	GHashTable *class_table;

	/* Register known types. */
	CAMEL_TYPE_SASL_ANONYMOUS;
	CAMEL_TYPE_SASL_CRAM_MD5;
	CAMEL_TYPE_SASL_DIGEST_MD5;
#ifdef HAVE_KRB5
	CAMEL_TYPE_SASL_GSSAPI;
#endif
	CAMEL_TYPE_SASL_LOGIN;
	CAMEL_TYPE_SASL_NTLM;
	CAMEL_TYPE_SASL_PLAIN;
	CAMEL_TYPE_SASL_POPB4SMTP;

	class_table = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) g_type_class_unref);

	sasl_build_class_table_rec (CAMEL_TYPE_SASL, class_table);

	return class_table;
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

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
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

	g_simple_async_result_set_check_cancellable (simple, cancellable);

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
sasl_try_empty_password_thread (GSimpleAsyncResult *simple,
                                GObject *object,
                                GCancellable *cancellable)
{
	gboolean res;
	GError *error = NULL;

	res = camel_sasl_try_empty_password_sync (
		CAMEL_SASL (object), cancellable, &error);
	g_simple_async_result_set_op_res_gboolean (simple, res);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
sasl_try_empty_password (CamelSasl *sasl,
                         gint io_priority,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (sasl), callback, user_data, sasl_try_empty_password);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, sasl_try_empty_password_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
sasl_try_empty_password_finish (CamelSasl *sasl,
                                GAsyncResult *result,
                                GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (sasl), sasl_try_empty_password), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
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
	class->try_empty_password = sasl_try_empty_password;
	class->try_empty_password_finish = sasl_try_empty_password_finish;

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
	GHashTable *class_table;
	CamelSaslClass *sasl_class;
	CamelSasl *sasl = NULL;

	g_return_val_if_fail (service_name != NULL, NULL);
	g_return_val_if_fail (mechanism != NULL, NULL);
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class_table = sasl_build_class_table ();
	sasl_class = g_hash_table_lookup (class_table, mechanism);

	if (sasl_class != NULL)
		sasl = g_object_new (
			G_OBJECT_CLASS_TYPE (sasl_class),
			"mechanism", mechanism,
			"service", service,
			"service-name", service_name,
			NULL);

	g_hash_table_destroy (class_table);

	return sasl;
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
 * camel_sasl_try_empty_password_sync:
 * @sasl: a #CamelSasl object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Returns: whether or not @sasl can attempt to authenticate without a
 * password being provided by the caller. This will be %TRUE for an
 * authentication method which can attempt to use single-sign-on
 * credentials, but which can fall back to using a provided password
 * so it still has the @need_password flag set in its description.
 *
 * Since: 3.2
 **/
gboolean
camel_sasl_try_empty_password_sync (CamelSasl *sasl,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelSaslClass *class;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), FALSE);

	class = CAMEL_SASL_GET_CLASS (sasl);

	if (class->try_empty_password_sync == NULL)
		return FALSE;

	return class->try_empty_password_sync (sasl, cancellable, error);
}

/**
 * camel_sasl_try_empty_password:
 * @sasl: a #CamelSasl
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously determine whether @sasl can be used for password-less
 * authentication, for example single-sign-on using system credentials.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_sasl_try_empty_password_finish() to get the result of the
 * operation.
 *
 * Since: 3.2
 **/
void
camel_sasl_try_empty_password (CamelSasl *sasl,
                               gint io_priority,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	CamelSaslClass *class;

	g_return_if_fail (CAMEL_IS_SASL (sasl));

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_if_fail (class->try_empty_password != NULL);

	class->try_empty_password (
		sasl, io_priority, cancellable, callback, user_data);
}

/**
 * camel_sasl_try_empty_password_finish:
 * @sasl: a #CamelSasl
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_sasl_try_empty_password().
 *
 * Returns: the SASL response.  If an error occurred, @error will also be set.
 *
 * Since: 3.2
 **/
gboolean
camel_sasl_try_empty_password_finish (CamelSasl *sasl,
                                      GAsyncResult *result,
                                      GError **error)
{
	CamelSaslClass *class;

	g_return_val_if_fail (CAMEL_IS_SASL (sasl), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_SASL_GET_CLASS (sasl);
	g_return_val_if_fail (class->try_empty_password_finish != NULL, FALSE);

	return class->try_empty_password_finish (sasl, result, error);
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

	if (sasl->priv->authenticated == authenticated)
		return;

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
 * Since: 3.0
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
 * Since: 3.0
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
 * Since: 3.0
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

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
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
 * Since: 3.0
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

	g_simple_async_result_set_check_cancellable (simple, cancellable);

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
 * Since: 3.0
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
	CamelSaslClass *sasl_class;
	GHashTable *class_table;
	GList *types = NULL;

	/* XXX I guess these are supposed to be common SASL auth types,
	 *     since this is called by the IMAP, POP and SMTP providers.
	 *     The returned list can be extended with other auth types
	 *     by way of camel_sasl_authtype(), so maybe we should just
	 *     drop the ad-hoc "include_plain" parameter? */

	class_table = sasl_build_class_table ();

	sasl_class = g_hash_table_lookup (class_table, "CRAM-MD5");
	g_return_val_if_fail (sasl_class != NULL, types);
	types = g_list_prepend (types, sasl_class->auth_type);

	sasl_class = g_hash_table_lookup (class_table, "DIGEST-MD5");
	g_return_val_if_fail (sasl_class != NULL, types);
	types = g_list_prepend (types, sasl_class->auth_type);

#ifdef HAVE_KRB5
	sasl_class = g_hash_table_lookup (class_table, "GSSAPI");
	g_return_val_if_fail (sasl_class != NULL, types);
	types = g_list_prepend (types, sasl_class->auth_type);
#endif

	sasl_class = g_hash_table_lookup (class_table, "NTLM");
	g_return_val_if_fail (sasl_class != NULL, types);
	types = g_list_prepend (types, sasl_class->auth_type);

	if (include_plain) {
		sasl_class = g_hash_table_lookup (class_table, "PLAIN");
		g_return_val_if_fail (sasl_class != NULL, types);
		types = g_list_prepend (types, sasl_class->auth_type);
	}

	g_hash_table_destroy (class_table);

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
	GHashTable *class_table;
	CamelSaslClass *sasl_class;
	CamelServiceAuthType *auth_type;

	g_return_val_if_fail (mechanism != NULL, NULL);

	class_table = sasl_build_class_table ();
	sasl_class = g_hash_table_lookup (class_table, mechanism);
	auth_type = (sasl_class != NULL) ? sasl_class->auth_type : NULL;
	g_hash_table_destroy (class_table);

	return auth_type;
}
