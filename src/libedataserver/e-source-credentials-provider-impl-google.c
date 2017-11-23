/*
 * Copyright (C) 2015 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#ifdef ENABLE_GOOGLE_AUTH
#include <json-glib/json-glib.h>
#endif

#include "e-source-credentials-provider-impl-google.h"

struct _ESourceCredentialsProviderImplGooglePrivate {
	gboolean dummy;
};

G_DEFINE_TYPE (ESourceCredentialsProviderImplGoogle, e_source_credentials_provider_impl_google, E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL)

static gboolean
e_source_credentials_provider_impl_google_can_process (ESourceCredentialsProviderImpl *provider_impl,
						       ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return e_source_credentials_google_util_generate_secret_uid (source, NULL);
}

static gboolean
e_source_credentials_provider_impl_google_can_store (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE (provider_impl), FALSE);

	return TRUE;
}

static gboolean
e_source_credentials_provider_impl_google_can_prompt (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE (provider_impl), FALSE);

	return TRUE;
}

static gboolean
e_source_credentials_google_util_store_secret_for_source_sync (ESource *source,
							       const gchar *secret,
							       gboolean permanently,
							       GCancellable *cancellable,
							       GError **error)
{
	gchar *uid = NULL, *label;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (secret != NULL, FALSE);

	if (!e_source_credentials_google_util_generate_secret_uid (source, &uid)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			/* Translators: The first %s is a display name of the source, the second is its UID. */
			_("Source “%s” (%s) is not a valid Google source"),
			e_source_get_display_name (source),
			e_source_get_uid (source));
		return FALSE;
	}

	label = g_strdup_printf ("Evolution Data Source - %s", strstr (uid, "::") + 2);

	success = e_secret_store_store_sync (uid, secret, label, permanently, cancellable, error);

	g_free (label);
	g_free (uid);

	return success;
}

static gboolean
e_source_credentials_google_util_get_access_token_from_secret (const gchar *secret,
							       gchar **out_access_token)
{
	gchar *expires_after = NULL, *access_token = NULL;
	gboolean success = FALSE;

	if (!secret)
		return FALSE;

	if (!e_source_credentials_google_util_decode_from_secret (secret,
		E_GOOGLE_SECRET_ACCESS_TOKEN, &access_token,
		E_GOOGLE_SECRET_EXPIRES_AFTER, &expires_after,
		NULL))
		return FALSE;

	if (access_token && expires_after) {
		success = TRUE;

		if (out_access_token &&
		    g_ascii_strtoll (expires_after, NULL, 10) > g_get_real_time () / G_USEC_PER_SEC) {
			*out_access_token = access_token;
			access_token = NULL;
		}
	}

	g_free (expires_after);
	g_free (access_token);

	return success;
}

static gboolean
e_source_credentials_google_util_lookup_secret_for_source_sync (ESource *source,
								gboolean can_return_expired,
								GCancellable *cancellable,
								ENamedParameters **out_credentials,
								GError **error)
{
	gchar *uid = NULL, *secret = NULL, *access_token = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (out_credentials != NULL, FALSE);

	*out_credentials = NULL;

	if (!e_source_credentials_google_util_generate_secret_uid (source, &uid)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			/* Translators: The first %s is a display name of the source, the second is its UID. */
			_("Source “%s” (%s) is not a valid Google source"),
			e_source_get_display_name (source),
			e_source_get_uid (source));
		return FALSE;
	}

	if (!e_secret_store_lookup_sync (uid, &secret, cancellable, error)) {
		g_free (uid);
		return FALSE;
	}

	g_free (uid);

	if (!secret) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("Google secret not found"));
		return FALSE;
	}

	if (e_source_credentials_google_util_get_access_token_from_secret (secret, &access_token)) {
		if (access_token || can_return_expired) {
			*out_credentials = e_named_parameters_new ();
			e_named_parameters_set (*out_credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET, secret);
			e_named_parameters_set (*out_credentials, E_SOURCE_CREDENTIAL_PASSWORD, access_token);
		}
	}

	e_util_safe_free_string (access_token);
	e_util_safe_free_string (secret);

	return *out_credentials != NULL;
}

static gboolean
e_source_credentials_provider_impl_google_lookup_sync (ESourceCredentialsProviderImpl *provider_impl,
						       ESource *source,
						       GCancellable *cancellable,
						       ENamedParameters **out_credentials,
						       GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (out_credentials != NULL, FALSE);

	*out_credentials = NULL;

	if (!e_source_credentials_google_is_supported ()) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Google authentication is not supported"));
		return FALSE;
	}

	success = e_source_credentials_google_util_lookup_secret_for_source_sync (source, FALSE, cancellable, out_credentials, error);

	/* Try to refresh the access token, in case it's stored, but expired. */
	if (!success && !g_cancellable_is_cancelled (cancellable)) {
		ENamedParameters *empty_credentials;
		gchar *access_token = NULL;

		/* Use empty credentials, to read from store. */
		empty_credentials = e_named_parameters_new ();

		if (e_source_credentials_google_get_access_token_sync (source, empty_credentials, &access_token, NULL, cancellable, NULL)) {
			g_free (access_token);
			g_clear_error (error);

			success = e_source_credentials_google_util_lookup_secret_for_source_sync (source, FALSE, cancellable, out_credentials, error);
		}

		e_named_parameters_free (empty_credentials);
	}

	return success;
}

static gboolean
e_source_credentials_provider_impl_google_store_sync (ESourceCredentialsProviderImpl *provider_impl,
						      ESource *source,
						      const ENamedParameters *credentials,
						      gboolean permanently,
						      GCancellable *cancellable,
						      GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);
	g_return_val_if_fail (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET) != NULL, FALSE);

	if (!e_source_credentials_google_is_supported ()) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Google authentication is not supported"));
		return FALSE;
	}

	return e_source_credentials_google_util_store_secret_for_source_sync (source,
		e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET),
		permanently, cancellable, error);
}

static gboolean
e_source_credentials_provider_impl_google_delete_sync (ESourceCredentialsProviderImpl *provider_impl,
							 ESource *source,
							 GCancellable *cancellable,
							 GError **error)
{
	gchar *uid = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (!e_source_credentials_google_is_supported ()) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Google authentication is not supported"));
		return FALSE;
	}

	if (!e_source_credentials_google_util_generate_secret_uid (source, &uid)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			/* Translators: The first %s is a display name of the source, the second is its UID. */
			_("Source “%s” (%s) is not a valid Google source"),
			e_source_get_display_name (source),
			e_source_get_uid (source));
		return FALSE;
	}

	success = e_secret_store_delete_sync (uid, cancellable, error);

	g_free (uid);

	return success;
}

static void
e_source_credentials_provider_impl_google_class_init (ESourceCredentialsProviderImplGoogleClass *klass)
{
	ESourceCredentialsProviderImplClass *impl_class;

	g_type_class_add_private (klass, sizeof (ESourceCredentialsProviderImplGooglePrivate));

	impl_class = E_SOURCE_CREDENTIALS_PROVIDER_IMPL_CLASS (klass);
	impl_class->can_process = e_source_credentials_provider_impl_google_can_process;
	impl_class->can_store = e_source_credentials_provider_impl_google_can_store;
	impl_class->can_prompt = e_source_credentials_provider_impl_google_can_prompt;
	impl_class->lookup_sync = e_source_credentials_provider_impl_google_lookup_sync;
	impl_class->store_sync = e_source_credentials_provider_impl_google_store_sync;
	impl_class->delete_sync = e_source_credentials_provider_impl_google_delete_sync;
}

static void
e_source_credentials_provider_impl_google_init (ESourceCredentialsProviderImplGoogle *provider_impl)
{
	provider_impl->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider_impl,
		E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE, ESourceCredentialsProviderImplGooglePrivate);
}

gboolean
e_source_credentials_google_is_supported (void)
{
#ifdef ENABLE_GOOGLE_AUTH
	return TRUE;
#else
	return FALSE;
#endif
}

/* The "refresh token" code is mostly copied from e-credentials-prompter-impl-google.c,
   which cannot be linked here, because the build dependency is opposite. */

#define GOOGLE_TOKEN_URI "https://www.googleapis.com/oauth2/v3/token"

static gchar *
cpi_google_create_refresh_token_post_data (const gchar *refresh_token)
{
#ifdef ENABLE_GOOGLE_AUTH
	g_return_val_if_fail (refresh_token != NULL, NULL);

	return soup_form_encode (
		"refresh_token", refresh_token,
		"client_id", GOOGLE_CLIENT_ID,
		"client_secret", GOOGLE_CLIENT_SECRET,
		"grant_type", "refresh_token",
		NULL);
#else
        return NULL;
#endif /* ENABLE_GOOGLE_AUTH */
}

static void
cpi_google_abort_session_cb (GCancellable *cancellable,
			     SoupSession *session)
{
	soup_session_abort (session);
}

static gboolean
cpi_google_setup_proxy_resolver (SoupSession *session,
				 ESource *cred_source,
				 GCancellable *cancellable)
{
	ESourceAuthentication *auth_extension;
	ESource *source = NULL;
	gchar *uid;
	gboolean success = TRUE;

	g_return_val_if_fail (SOUP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (cred_source), FALSE);

	if (!e_source_has_extension (cred_source, E_SOURCE_EXTENSION_AUTHENTICATION))
		return TRUE;

	auth_extension = e_source_get_extension (cred_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	uid = e_source_authentication_dup_proxy_uid (auth_extension);

	if (uid != NULL) {
		ESourceRegistry *registry;

		registry = e_source_registry_new_sync (cancellable, NULL);
		if (!registry)
			success = FALSE;
		else
			source = e_source_registry_ref_source (registry, uid);

		g_clear_object (&registry);
		g_free (uid);
	}

	if (source != NULL) {
		GProxyResolver *proxy_resolver;

		proxy_resolver = G_PROXY_RESOLVER (source);
		if (g_proxy_resolver_is_supported (proxy_resolver))
			g_object_set (session, SOUP_SESSION_PROXY_RESOLVER, proxy_resolver, NULL);

		g_object_unref (source);
	}

	return success;
}

static guint
cpi_google_post_data_sync (const gchar *uri,
			   const gchar *post_data,
			   ESource *cred_source,
			   GCancellable *cancellable,
			   gchar **out_response_data)
{
	SoupSession *session;
	SoupMessage *message;
	guint status_code = SOUP_STATUS_CANCELLED;

	g_return_val_if_fail (uri != NULL, SOUP_STATUS_MALFORMED);
	g_return_val_if_fail (post_data != NULL, SOUP_STATUS_MALFORMED);
	/* g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), SOUP_STATUS_MALFORMED); */
	g_return_val_if_fail (E_IS_SOURCE (cred_source), SOUP_STATUS_MALFORMED);
	g_return_val_if_fail (out_response_data != NULL, SOUP_STATUS_MALFORMED);

	*out_response_data = NULL;

	session = soup_session_new ();
	g_object_set (
		session,
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_STRICT, TRUE,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	if (!cpi_google_setup_proxy_resolver (session, cred_source, cancellable)) {
		g_object_unref (session);

		return SOUP_STATUS_CANT_RESOLVE_PROXY;
	}

	message = soup_message_new (SOUP_METHOD_POST, uri);
	g_return_val_if_fail (message != NULL, SOUP_STATUS_MALFORMED);

	soup_message_set_request (message, "application/x-www-form-urlencoded",
		SOUP_MEMORY_TEMPORARY, post_data, strlen (post_data));

	e_soup_ssl_trust_connect (message, cred_source);

	soup_message_headers_append (message->request_headers, "Connection", "close");

	if (!g_cancellable_is_cancelled (cancellable)) {
		gulong cancel_handler_id = 0;

		if (cancellable)
			cancel_handler_id = g_cancellable_connect (cancellable, G_CALLBACK (cpi_google_abort_session_cb), session, NULL);

		status_code = soup_session_send_message (session, message);

		if (cancel_handler_id)
			g_cancellable_disconnect (cancellable, cancel_handler_id);
	}

	if (SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		if (message->response_body) {
			*out_response_data = g_strndup (message->response_body->data, message->response_body->length);
		} else {
			status_code = SOUP_STATUS_MALFORMED;
		}
	}

	g_object_unref (message);
	g_object_unref (session);

	return status_code;
}

static gboolean
e_source_credentials_google_refresh_token_sync (ESource *source,
						const ENamedParameters *credentials,
						gchar **out_access_token,
						gint *out_expires_in_seconds,
						GCancellable *cancellable,
						GError **error)
{
	const gchar *secret;
	gchar *refresh_token = NULL;
	gchar *post_data, *response_json = NULL;
	guint soup_status;
	gboolean success = FALSE;

	secret = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET);
	if (!secret) {
		if (error && !*error)
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to get Google secret from credentials"));

		return FALSE;
	}

	/* The caller can have set an error from the secret lookup,
	   which are propagated, but otherwise a success is reported. */
	if (error && *error)
		g_clear_error (error);

	if (!e_source_credentials_google_util_decode_from_secret (secret, E_GOOGLE_SECRET_REFRESH_TOKEN, &refresh_token, NULL) ||
	    !refresh_token || !*refresh_token) {
		g_free (refresh_token);
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Refresh token not found in Google secret"));
		return FALSE;
	}

	post_data = cpi_google_create_refresh_token_post_data (refresh_token);
	g_warn_if_fail (post_data != NULL);
	if (!post_data) {
		g_free (refresh_token);
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to construct refresh_token request"));
		return FALSE;
	}

	soup_status = cpi_google_post_data_sync (GOOGLE_TOKEN_URI, post_data, source, cancellable, &response_json);

	if (SOUP_STATUS_IS_SUCCESSFUL (soup_status) && response_json) {
		gchar *access_token = NULL, *expires_in = NULL;

		if (e_source_credentials_google_util_decode_from_secret (response_json,
			"access_token", &access_token,
			"expires_in", &expires_in,
			NULL) && access_token && expires_in) {
			gint64 expires_after_tm, expires_in_tm;
			gchar *expires_after;
			gchar *new_secret = NULL;

			expires_in_tm = g_ascii_strtoll (expires_in, NULL, 10) - 1;
			expires_after_tm = g_get_real_time () / G_USEC_PER_SEC;
			expires_after_tm += expires_in_tm;
			expires_after = g_strdup_printf ("%" G_GINT64_FORMAT, expires_after_tm);

			if (e_source_credentials_google_util_encode_to_secret (&new_secret,
				E_GOOGLE_SECRET_REFRESH_TOKEN, refresh_token,
				E_GOOGLE_SECRET_ACCESS_TOKEN, access_token,
				E_GOOGLE_SECRET_EXPIRES_AFTER, expires_after, NULL)) {
				/* Oops, always storing permanently, though it makes more sense here.
				   Otherwise can read RememberPassword from [Authentication] and use
				   it here. */
				success = e_source_credentials_google_util_store_secret_for_source_sync (source,
					new_secret, TRUE, cancellable, error);
				g_free (new_secret);
			} else {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to encode new access token to Google secret"));
			}

			if (success) {
				if (out_access_token)
					*out_access_token = g_strdup (access_token);
				if (out_expires_in_seconds)
					*out_expires_in_seconds = expires_in_tm;
			}
		} else {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to get access token from refresh_token server response"));
		}

		g_free (access_token);
		g_free (expires_in);
	} else {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to refresh token"));
	}

	g_free (refresh_token);
	g_free (response_json);
	g_free (post_data);

	return success;
}

static gboolean
e_source_credentials_google_extract_from_credentials_valid_only (const ENamedParameters *credentials,
								 gchar **out_access_token,
								 gint *out_expires_in_seconds)
{
	gchar *access_token = NULL;
	gint expires_in_seconds = -1;

	if (!credentials)
		return FALSE;

	if (e_source_credentials_google_util_extract_from_credentials (credentials, &access_token, &expires_in_seconds)) {
		if (expires_in_seconds <= 0) {
			g_free (access_token);
			access_token = NULL;
		}
	} else {
		access_token = NULL;
	}

	/* The token exists and is valid, just use it. */
	if (access_token) {
		if (out_access_token)
			*out_access_token = access_token;
		else
			g_free (access_token);

		if (out_expires_in_seconds)
			*out_expires_in_seconds = expires_in_seconds;

		return TRUE;
	}

	return FALSE;
}

gboolean
e_source_credentials_google_get_access_token_sync (ESource *source,
						   const ENamedParameters *credentials,
						   gchar **out_access_token,
						   gint *out_expires_in_seconds,
						   GCancellable *cancellable,
						   GError **error)
{
	ENamedParameters *tmp_credentials = NULL;
	gboolean success;

	g_return_val_if_fail (credentials != NULL, FALSE);

	if (!e_source_credentials_google_is_supported ()) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Google authentication is not supported"));
		return FALSE;
	}

	/* The token exists and is valid, just use it. */
	if (e_source_credentials_google_extract_from_credentials_valid_only (credentials, out_access_token, out_expires_in_seconds)) {
		return TRUE;
	}

	/* Maybe some other part refreshed the token already and stored it into the secret store,
	   thus try to read it and use it, if it contains proper data.
	*/
	if (e_source_credentials_google_util_lookup_secret_for_source_sync (source, TRUE, cancellable, &tmp_credentials, error) &&
	    e_source_credentials_google_extract_from_credentials_valid_only (tmp_credentials, out_access_token, out_expires_in_seconds)) {
		e_named_parameters_free (tmp_credentials);

		return TRUE;
	}

	/* Try to refresh the token */
	success = e_source_credentials_google_refresh_token_sync (source, tmp_credentials ? tmp_credentials : credentials,
		out_access_token, out_expires_in_seconds, cancellable, error);

	e_named_parameters_free (tmp_credentials);

	return success;
}

gboolean
e_source_credentials_google_util_generate_secret_uid (ESource *source,
						      gchar **out_uid)
{
	ESourceAuthentication *authentication_extension;
	gchar *auth_method, *user;
	gboolean is_google_auth;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (out_uid)
		*out_uid = NULL;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		return FALSE;

	authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	auth_method = e_source_authentication_dup_method (authentication_extension);
	is_google_auth = g_strcmp0 (auth_method, "Google") == 0;

	g_free (auth_method);

	if (!is_google_auth)
		return FALSE;

	user = e_source_authentication_dup_user (authentication_extension);
	if (!user || !*user) {
		g_free (user);
		return FALSE;
	}

	if (out_uid)
		*out_uid = g_strdup_printf ("OAuth2::Google[%s]", user);

	g_free (user);

	return TRUE;
}

gboolean
e_source_credentials_google_util_encode_to_secret (gchar **out_secret,
						   const gchar *key1_name,
						   const gchar *value1,
						   ...)
{
#ifdef ENABLE_GOOGLE_AUTH
	JsonBuilder *builder;
	JsonNode *node;
	const gchar *key, *value;
	va_list va;

	g_return_val_if_fail (out_secret != NULL, FALSE);
	g_return_val_if_fail (key1_name != NULL, FALSE);
	g_return_val_if_fail (value1 != NULL, FALSE);

	*out_secret = NULL;

	builder = json_builder_new ();

	va_start (va, value1);
	key = key1_name;
	value = value1;

	json_builder_begin_object (builder);

	while (key && value) {
		json_builder_set_member_name (builder, key);
		json_builder_add_string_value (builder, value);

		key = va_arg (va, const gchar *);
		if (!key)
			break;

		value = va_arg (va, const gchar *);
		g_warn_if_fail (value != NULL);
	}

	va_end (va);

	json_builder_end_object (builder);
	node = json_builder_get_root (builder);

	g_object_unref (builder);

	if (node) {
		JsonGenerator *generator;

		generator = json_generator_new ();
		json_generator_set_root (generator, node);

		*out_secret = json_generator_to_data (generator, NULL);

		g_object_unref (generator);
		json_node_free (node);
	}

	return *out_secret != NULL;
#else
	return FALSE;
#endif
}

gboolean
e_source_credentials_google_util_decode_from_secret (const gchar *secret,
						     const gchar *key1_name,
						     gchar **out_value1,
						     ...)
{
#ifdef ENABLE_GOOGLE_AUTH
	JsonParser *parser;
	JsonReader *reader;
	const gchar *key;
	gchar **out_value;
	va_list va;
	GError *error = NULL;

	g_return_val_if_fail (key1_name != NULL, FALSE);
	g_return_val_if_fail (out_value1 != NULL, FALSE);

	if (!secret || !*secret)
		return FALSE;

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, secret, -1, &error)) {
		g_object_unref (parser);

		g_debug ("%s: Failed to parse secret '%s': %s", G_STRFUNC, secret, error ? error->message : "Unknown error");
		g_clear_error (&error);

		return FALSE;
	}

	reader = json_reader_new (json_parser_get_root (parser));
	key = key1_name;
	out_value = out_value1;

	va_start (va, out_value1);

	while (key && out_value) {
		*out_value = NULL;

		if (json_reader_read_member (reader, key)) {
			*out_value = g_strdup (json_reader_get_string_value (reader));
			if (!*out_value) {
				const GError *reader_error = json_reader_get_error (reader);

				if (g_error_matches (reader_error, JSON_READER_ERROR, JSON_READER_ERROR_INVALID_TYPE)) {
					gint64 iv64;

					json_reader_end_member (reader);

					iv64 = json_reader_get_int_value (reader);

					if (!json_reader_get_error (reader))
						*out_value = g_strdup_printf ("%" G_GINT64_FORMAT, iv64);
				}
			}

			if (*out_value && !**out_value) {
				g_free (*out_value);
				*out_value = NULL;
			}
		}

		json_reader_end_member (reader);

		key = va_arg (va, const gchar *);
		if (!key)
			break;

		out_value = va_arg (va, gchar **);
		g_warn_if_fail (out_value != NULL);
	}

	g_object_unref (reader);
	g_object_unref (parser);
	va_end (va);

	return TRUE;
#else
	return FALSE;
#endif
}

gboolean
e_source_credentials_google_util_extract_from_credentials (const ENamedParameters *credentials,
							   gchar **out_access_token,
							   gint *out_expires_in_seconds)
{
	const gchar *secret;
	gchar *expires_after = NULL;
	gint64 expires_after_tm, now;

	if (!credentials ||
	    !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET) ||
	    !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD))
		return FALSE;

	secret = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET);

	if (!e_source_credentials_google_util_decode_from_secret (secret, E_GOOGLE_SECRET_EXPIRES_AFTER, &expires_after, NULL) ||
	    !expires_after)
		return FALSE;

	expires_after_tm = g_ascii_strtoll (expires_after, NULL, 10);
	g_free (expires_after);

	if (out_access_token)
		*out_access_token = g_strdup (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));

	if (out_expires_in_seconds) {
		now = g_get_real_time () / G_USEC_PER_SEC;
		if (expires_after_tm <= now)
			expires_after_tm = now;

		*out_expires_in_seconds = (gint) (expires_after_tm - now);
		if (*out_expires_in_seconds > 0)
			*out_expires_in_seconds = (*out_expires_in_seconds) - 1;
	}

	return TRUE;
}
