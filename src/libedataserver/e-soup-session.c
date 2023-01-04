/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

/**
 * SECTION: e-soup-session
 * @include: libedataserver/libedataserver.h
 * @short_description: A SoupSession descendant
 *
 * The #ESoupSession is a #SoupSession descendant, which hides common
 * tasks related to the way evolution-data-server works.
 **/

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <glib/gi18n-lib.h>

#include "e-data-server-util.h"
#include "e-oauth2-services.h"
#include "e-soup-auth-bearer.h"
#include "e-soup-ssl-trust.h"
#include "e-source-authentication.h"
#include "e-source-webdav.h"

#include "e-soup-session.h"

G_DEFINE_QUARK (e-soup-session-error-quark, e_soup_session_error)

#define E_SOUP_SESSION_MESSAGE_BYTES_KEY "e-soup-session-message-bytes"
#define BUFFER_SIZE 16384

struct _ESoupSessionPrivate {
	GMutex property_lock;
	GRecMutex session_lock; /* libsoup3 has no thread safety */
	GHashTable *using_auths; /* guarded by the session_lock, gchar *uri ~> gchar *authtype, as set in the SoupAuthManager */
	ESource *source;
	ENamedParameters *credentials;

	gboolean ssl_info_set;
	gchar *ssl_certificate_pem;
	GTlsCertificateFlags ssl_certificate_errors;

	SoupLoggerLogLevel log_level;

	GError *bearer_auth_error;
	ESoupAuthBearer *using_bearer_auth;

	gboolean auth_prefilled; /* When TRUE, the first 'retrying' is ignored in the "authenticate" handler */
};

enum {
	PROP_0,
	PROP_SOURCE,
	PROP_CREDENTIALS
};

G_DEFINE_TYPE_WITH_PRIVATE (ESoupSession, e_soup_session, SOUP_TYPE_SESSION)

/* Hold the session lock when calling this */
static gboolean
e_soup_session_auth_already_set_locked (ESoupSession *session,
					GUri *g_uri,
					SoupAuth *soup_auth,
					gboolean *out_auth_was_set)
{
	gchar *uri_str;
	const gchar *auth_type;
	const gchar *current_auth_type;
	gboolean same_types;

	uri_str = g_uri_to_string_partial (g_uri, G_URI_HIDE_PASSWORD);
	auth_type = G_OBJECT_TYPE_NAME (soup_auth);
	current_auth_type = g_hash_table_lookup (session->priv->using_auths, uri_str);
	*out_auth_was_set = current_auth_type != NULL;
	same_types = g_strcmp0 (auth_type, current_auth_type) == 0;

	if (!same_types) {
		/* Because the caller calls soup_auth_manager_clear_cached_credentials() in this case */
		if (*out_auth_was_set)
			g_hash_table_remove_all (session->priv->using_auths);

		g_hash_table_insert (session->priv->using_auths, uri_str, g_strdup (auth_type));
		uri_str = NULL;
	}

	g_free (uri_str);

	return same_types;
}

static void
e_soup_session_ensure_auth_usage (ESoupSession *session,
				  GUri *in_g_uri,
				  SoupMessage *message,
				  SoupAuth *soup_auth)
{
	SoupAuthManager *auth_manager;
	SoupSessionFeature *feature;
	GUri *g_uri;
	GType auth_type;
	gboolean auth_was_set = FALSE;

	g_return_if_fail (E_IS_SOUP_SESSION (session));
	g_return_if_fail (SOUP_IS_AUTH (soup_auth));

	g_rec_mutex_lock (&session->priv->session_lock);

	feature = soup_session_get_feature (SOUP_SESSION (session), SOUP_TYPE_AUTH_MANAGER);

	auth_type = G_OBJECT_TYPE (soup_auth);

	if (!soup_session_has_feature (SOUP_SESSION (session), auth_type)) {
		/* Add the SoupAuth type to support it. */
		soup_session_add_feature_by_type (SOUP_SESSION (session), auth_type);
	}

	if (in_g_uri) {
		g_uri = in_g_uri;
	} else {
		g_uri = message ? soup_message_get_uri (message) : NULL;
		if (g_uri && g_uri_get_host (g_uri) && *g_uri_get_host (g_uri)) {
			g_uri = g_uri_build (SOUP_HTTP_URI_FLAGS,
			                     g_uri_get_scheme (g_uri),
			                     NULL,
			                     g_uri_get_host (g_uri),
			                     g_uri_get_port (g_uri),
			                     "",
			                     NULL,
			                     NULL);
		} else {
			g_uri = NULL;
		}

		if (!g_uri) {
			ESource *source;

			source = e_soup_session_get_source (session);

			if (source) {
				ESourceWebdav *extension;

				extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
				g_uri = e_source_webdav_dup_uri (extension);
			}
		}
	}

	auth_manager = SOUP_AUTH_MANAGER (feature);

	/* This will make sure the 'soup_auth' is used regardless of the current 'auth_manager' state,
	   but do not set the same 'soup_auth' when it's already set (which can happen, when the session
	   is reused by multiple sources, which connect to the same server, with the same user.
	   See https://gitlab.gnome.org/GNOME/libsoup/-/issues/196 for more information. */
	if (g_uri && !e_soup_session_auth_already_set_locked (session, g_uri, soup_auth, &auth_was_set)) {
		if (auth_was_set)
			soup_auth_manager_clear_cached_credentials (auth_manager);

		soup_auth_manager_use_auth (auth_manager, g_uri, soup_auth);
	}

	g_rec_mutex_unlock (&session->priv->session_lock);

	if (!in_g_uri && g_uri)
		g_uri_unref (g_uri);
}

static gboolean
e_soup_session_setup_bearer_auth (ESoupSession *session,
				  SoupMessage *message,
				  gboolean is_in_authenticate_handler,
				  ESoupAuthBearer *bearer,
				  GCancellable *cancellable,
				  GError **error)
{
	ESource *source;
	gchar *access_token = NULL;
	gint expires_in_seconds = -1;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (bearer), FALSE);

	source = e_soup_session_get_source (session);

	if (!source) {
		/* Do not localize this error message, it should not get into the UI */
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No ESource specified");
		return FALSE;
	}

	success = e_source_get_oauth2_access_token_sync (source, cancellable,
		&access_token, &expires_in_seconds, error);

	if (success) {
		e_soup_auth_bearer_set_access_token (bearer, access_token, expires_in_seconds);

		/* Preload the SoupAuthManager with a valid "Bearer" token
		 * when using OAuth 2.0. This avoids an extra unauthorized
		 * HTTP round-trip, which apparently Google doesn't like. */
		if (!is_in_authenticate_handler)
			e_soup_session_ensure_auth_usage (session, NULL, message, SOUP_AUTH (bearer));
	}

	g_free (access_token);

	return success;
}

static gboolean
e_soup_session_maybe_prepare_bearer_auth (ESoupSession *session,
					  GUri *g_uri,
					  SoupMessage *message,
					  GCancellable *cancellable,
					  GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (g_uri != NULL, FALSE);

	g_mutex_lock (&session->priv->property_lock);
	if (session->priv->using_bearer_auth) {
		ESoupAuthBearer *using_bearer_auth = g_object_ref (session->priv->using_bearer_auth);

		g_mutex_unlock (&session->priv->property_lock);

		success = e_soup_session_setup_bearer_auth (session, message, FALSE, using_bearer_auth, cancellable, error);

		g_clear_object (&using_bearer_auth);
	} else {
		ESoupAuthBearer *soup_auth;

		g_mutex_unlock (&session->priv->property_lock);

		soup_auth = g_object_new (
			E_TYPE_SOUP_AUTH_BEARER,
			"authority", g_uri_get_host (g_uri), NULL);

		success = e_soup_session_setup_bearer_auth (session, message, FALSE, E_SOUP_AUTH_BEARER (soup_auth), cancellable, error);
		if (success) {
			g_mutex_lock (&session->priv->property_lock);
			g_clear_object (&session->priv->using_bearer_auth);
			session->priv->using_bearer_auth = g_object_ref (soup_auth);
			g_mutex_unlock (&session->priv->property_lock);
		}

		g_object_unref (soup_auth);
	}

	return success;
}

static gboolean
e_soup_session_maybe_prepare_basic_auth (ESoupSession *session,
					 GUri *g_uri,
					 SoupMessage *message,
					 const gchar *in_username,
					 const ENamedParameters *credentials,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupAuth *soup_auth;
	const gchar *username;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (g_uri != NULL, FALSE);

	if (!credentials || !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
		/* This error message won't get into the UI */
		g_set_error_literal (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED, soup_status_get_phrase (SOUP_STATUS_UNAUTHORIZED));
		return FALSE;
	}

	username = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME);
	if (!username || !*username)
		username = in_username;

	soup_auth = soup_auth_new (SOUP_TYPE_AUTH_BASIC, message, "Basic");

	soup_auth_authenticate (soup_auth, username, e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));

	g_mutex_lock (&session->priv->property_lock);
	session->priv->auth_prefilled = TRUE;
	g_mutex_unlock (&session->priv->property_lock);

	e_soup_session_ensure_auth_usage (session, g_uri, message, soup_auth);

	g_clear_object (&soup_auth);

	return TRUE;
}

static gboolean
e_soup_session_maybe_prepare_auth (ESoupSession *session,
				   SoupMessage *message,
				   GCancellable *cancellable,
				   GError **error)
{
	ESource *source;
	ENamedParameters *credentials;
	GUri *g_uri;
	gchar *auth_method = NULL, *user = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);

	source = e_soup_session_get_source (session);

	if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *extension;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_method = e_source_authentication_dup_method (extension);
		user = e_source_authentication_dup_user (extension);
	} else {
		return TRUE;
	}

	credentials = e_soup_session_dup_credentials (session);
	g_uri = message ? soup_message_get_uri (message) : NULL;
	if (g_uri && g_uri_get_host (g_uri) && *g_uri_get_host (g_uri)) {
		g_uri = g_uri_build (SOUP_HTTP_URI_FLAGS,
		                     g_uri_get_scheme (g_uri),
		                     NULL,
		                     g_uri_get_host (g_uri),
		                     g_uri_get_port (g_uri),
		                     "",
		                     NULL,
		                     NULL);
	} else {
		g_uri = NULL;
	}

	if (!g_uri) {
		ESourceWebdav *extension;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		g_uri = e_source_webdav_dup_uri (extension);
	}

	g_mutex_lock (&session->priv->property_lock);
	session->priv->auth_prefilled = FALSE;
	g_mutex_unlock (&session->priv->property_lock);

	/* Provide credentials beforehand only on secure connections */
	if (g_strcmp0 (g_uri_get_scheme (g_uri), "https") == 0) {
		if (g_strcmp0 (auth_method, "OAuth2") == 0 ||
		    e_oauth2_services_is_oauth2_alias_static (auth_method)) {
			success = e_soup_session_maybe_prepare_bearer_auth (session, g_uri, message, cancellable, error);
		} else if (g_strcmp0 (auth_method, "GSSAPI") == 0 && soup_auth_negotiate_supported ()) {
			SoupSession *soup_session = SOUP_SESSION (session);

			g_rec_mutex_lock (&session->priv->session_lock);

			if (!soup_session_get_feature (soup_session, SOUP_TYPE_AUTH_NEGOTIATE))
				soup_session_add_feature_by_type (soup_session, SOUP_TYPE_AUTH_NEGOTIATE);
			if (soup_session_get_feature (soup_session, SOUP_TYPE_AUTH_BASIC))
				soup_session_remove_feature_by_type (soup_session, SOUP_TYPE_AUTH_BASIC);

			g_rec_mutex_unlock (&session->priv->session_lock);
		} else if (g_strcmp0 (auth_method, "NTLM") == 0) {
			SoupSession *soup_session = SOUP_SESSION (session);

			g_rec_mutex_lock (&session->priv->session_lock);

			if (!soup_session_get_feature (soup_session, SOUP_TYPE_AUTH_NTLM))
				soup_session_add_feature_by_type (soup_session, SOUP_TYPE_AUTH_NTLM);
			/* Keep the basic auth, as a fallback */

			g_rec_mutex_unlock (&session->priv->session_lock);
		} else if (user && *user) {
			/* Default to Basic authentication when user is filled */
			success = e_soup_session_maybe_prepare_basic_auth (session, g_uri, message, user, credentials, cancellable, error);
		}
	}

	e_named_parameters_free (credentials);
	g_uri_unref (g_uri);
	g_free (auth_method);
	g_free (user);

	return success;
}

static gboolean
e_soup_session_authenticate_cb (SoupMessage *message,
				SoupAuth *auth,
				gboolean retrying,
				gpointer user_data)
{
	ESoupSession *session = user_data;
	const gchar *username;
	ENamedParameters *credentials;
	gchar *auth_user = NULL;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);

	g_mutex_lock (&session->priv->property_lock);

	if (E_IS_SOUP_AUTH_BEARER (auth)) {
		g_object_ref (auth);
		g_warn_if_fail ((gpointer) session->priv->using_bearer_auth == (gpointer) auth);
		g_clear_object (&session->priv->using_bearer_auth);
		session->priv->using_bearer_auth = E_SOUP_AUTH_BEARER (auth);
	} else if (session->priv->using_bearer_auth) {
		/* This can mean the bearer auth expired, then a Basic auth is used by the libsoup;
		   that's not meant to be done here, thus fail early. */
		g_mutex_unlock (&session->priv->property_lock);
		return FALSE;
	}

	if (retrying && !session->priv->auth_prefilled) {
		g_mutex_unlock (&session->priv->property_lock);
		return FALSE;
	}
	session->priv->auth_prefilled = FALSE;
	g_mutex_unlock (&session->priv->property_lock);

	if (session->priv->using_bearer_auth) {
		GError *local_error = NULL;

		e_soup_session_setup_bearer_auth (session, message, TRUE, E_SOUP_AUTH_BEARER (auth), NULL, &local_error);

		if (local_error) {
			g_mutex_lock (&session->priv->property_lock);

			/* Warn about an unclaimed error before we clear it.
			 * This is just to verify the errors we set here are
			 * actually making it back to the user. */
			g_warn_if_fail (session->priv->bearer_auth_error == NULL);
			g_clear_error (&session->priv->bearer_auth_error);

			g_propagate_error (&session->priv->bearer_auth_error, local_error);

			g_mutex_unlock (&session->priv->property_lock);
		}

		return FALSE;
	}

	credentials = e_soup_session_dup_credentials (session);

	username = credentials ? e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME) : NULL;
	if ((!username || !*username) && session->priv->source &&
	    e_source_has_extension (session->priv->source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;

		auth_extension = e_source_get_extension (session->priv->source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_user = e_source_authentication_dup_user (auth_extension);

		username = auth_user;
	}

	if (username && *username && credentials &&
	    e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
		soup_auth_authenticate (auth, username, e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));
	} else if (g_strcmp0 (soup_auth_get_scheme_name (auth), "NTLM") == 0) {
		soup_auth_cancel (auth);
	}

	e_named_parameters_free (credentials);
	g_free (auth_user);

	return FALSE;
}

static void
e_soup_session_set_source (ESoupSession *session,
			   ESource *source)
{
	g_return_if_fail (E_IS_SOUP_SESSION (session));
	if (source)
		g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (!session->priv->source);

	session->priv->source = source ? g_object_ref (source) : NULL;
}

static void
e_soup_session_set_property (GObject *object,
			     guint property_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			e_soup_session_set_source (
				E_SOUP_SESSION (object),
				g_value_get_object (value));
			return;

		case PROP_CREDENTIALS:
			e_soup_session_set_credentials (
				E_SOUP_SESSION (object),
				g_value_get_boxed (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_soup_session_get_property (GObject *object,
			     guint property_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			g_value_set_object (
				value,
				e_soup_session_get_source (
				E_SOUP_SESSION (object)));
			return;

		case PROP_CREDENTIALS:
			g_value_take_boxed (
				value,
				e_soup_session_dup_credentials (
				E_SOUP_SESSION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_soup_session_finalize (GObject *object)
{
	ESoupSession *session = E_SOUP_SESSION (object);

	g_clear_error (&session->priv->bearer_auth_error);
	g_clear_object (&session->priv->source);
	g_clear_object (&session->priv->using_bearer_auth);
	g_clear_pointer (&session->priv->credentials, e_named_parameters_free);
	g_clear_pointer (&session->priv->ssl_certificate_pem, g_free);
	g_clear_pointer (&session->priv->using_auths, g_hash_table_unref);

	g_mutex_clear (&session->priv->property_lock);
	g_rec_mutex_clear (&session->priv->session_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_soup_session_parent_class)->finalize (object);
}

static void
e_soup_session_class_init (ESoupSessionClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = e_soup_session_set_property;
	object_class->get_property = e_soup_session_get_property;
	object_class->finalize = e_soup_session_finalize;

	/**
	 * ESoupSession:source:
	 *
	 * The #ESource being used for this soup session.
	 *
	 * Since: 3.26
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			NULL,
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESoupSession:credentials:
	 *
	 * The #ENamedParameters containing login credentials.
	 *
	 * Since: 3.26
	 **/
	g_object_class_install_property (
		object_class,
		PROP_CREDENTIALS,
		g_param_spec_boxed (
			"credentials",
			"Credentials",
			NULL,
			E_TYPE_NAMED_PARAMETERS,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_soup_session_init (ESoupSession *session)
{
	session->priv = e_soup_session_get_instance_private (session);
	session->priv->using_auths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	session->priv->ssl_info_set = FALSE;
	session->priv->log_level = SOUP_LOGGER_LOG_NONE;
	session->priv->auth_prefilled = FALSE;

	g_mutex_init (&session->priv->property_lock);
	g_rec_mutex_init (&session->priv->session_lock);

	g_object_set (
		G_OBJECT (session),
		"timeout", 90,
		"accept-language-auto", TRUE,
		NULL);

	if (!soup_session_get_feature (SOUP_SESSION (session), SOUP_TYPE_CONTENT_DECODER))
		soup_session_add_feature_by_type (SOUP_SESSION (session), SOUP_TYPE_CONTENT_DECODER);
}

/**
 * e_soup_session_new:
 * @source: an #ESource
 *
 * Creates a new #ESoupSession associated with given @source.
 * The @source can be used to store and read SSL trust settings, but only if
 * it already contains an #ESourceWebdav extension. Otherwise the SSL trust
 * settings are ignored.
 *
 * Returns: (transfer full): a new #ESoupSession; free it with g_object_unref(),
 *    when no longer needed.
 *
 * Since: 3.26
 **/
ESoupSession *
e_soup_session_new (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return g_object_new (E_TYPE_SOUP_SESSION,
		"source", source,
		NULL);
}

/**
 * e_soup_session_setup_logging:
 * @session: an #ESoupSession
 * @logging_level: (nullable): logging level to setup, or %NULL
 *
 * Setups logging for the @session. The @logging_level can be one of:
 * "all" - log whole raw communication;
 * "body" - the same as "all";
 * "headers" - log the headers only;
 * "min" - minimal logging;
 * "1" - the same as "all".
 * Any other value, including %NULL, disables logging.
 *
 * Use e_soup_session_get_log_level() to get current log level.
 *
 * Since: 3.26
 **/
void
e_soup_session_setup_logging (ESoupSession *session,
			      const gchar *logging_level)
{
	SoupLogger *logger;

	g_return_if_fail (E_IS_SOUP_SESSION (session));

	g_rec_mutex_lock (&session->priv->session_lock);

	soup_session_remove_feature_by_type (SOUP_SESSION (session), SOUP_TYPE_LOGGER);
	session->priv->log_level = SOUP_LOGGER_LOG_NONE;

	if (!logging_level) {
		g_rec_mutex_unlock (&session->priv->session_lock);
		return;
	}

	if (g_ascii_strcasecmp (logging_level, "all") == 0 ||
	    g_ascii_strcasecmp (logging_level, "body") == 0 ||
	    g_ascii_strcasecmp (logging_level, "1") == 0)
		session->priv->log_level = SOUP_LOGGER_LOG_BODY;
	else if (g_ascii_strcasecmp (logging_level, "headers") == 0)
		session->priv->log_level = SOUP_LOGGER_LOG_HEADERS;
	else if (g_ascii_strcasecmp (logging_level, "min") == 0)
		session->priv->log_level = SOUP_LOGGER_LOG_MINIMAL;
	else {
		g_rec_mutex_unlock (&session->priv->session_lock);
		return;
	}

	logger = soup_logger_new (session->priv->log_level);
	soup_session_add_feature (SOUP_SESSION (session), SOUP_SESSION_FEATURE (logger));
	g_object_unref (logger);

	g_rec_mutex_unlock (&session->priv->session_lock);
}

/**
 * e_soup_session_get_log_level:
 * @session: an #ESoupSession
 *
 * Returns: Current log level, as #SoupLoggerLogLevel
 *
 * Since: 3.26
 **/
SoupLoggerLogLevel
e_soup_session_get_log_level (ESoupSession *session)
{
	g_return_val_if_fail (E_IS_SOUP_SESSION (session), SOUP_LOGGER_LOG_NONE);

	return session->priv->log_level;
}

/**
 * e_soup_session_get_source:
 * @session: an #ESoupSession
 *
 * Returns an #ESource associated with the @session, if such was set in the creation time.
 *
 * Returns: (transfer none) (nullable): Associated #ESource with the @session, or %NULL.
 *
 * Since: 3.26
 **/
ESource *
e_soup_session_get_source (ESoupSession *session)
{
	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);

	return session->priv->source;
}

/**
 * e_soup_session_set_credentials:
 * @session: an #ESoupSession
 * @credentials: (nullable): an #ENamedParameters with credentials to use, or %NULL
 *
 * Sets credentials to use for connection. Using %NULL for @credentials
 * unsets previous value.
 *
 * Since: 3.26
 **/
void
e_soup_session_set_credentials (ESoupSession *session,
				const ENamedParameters *credentials)
{
	SoupSessionFeature *feature;

	g_return_if_fail (E_IS_SOUP_SESSION (session));

	g_mutex_lock (&session->priv->property_lock);

	if (e_named_parameters_equal (credentials, session->priv->credentials)) {
		g_mutex_unlock (&session->priv->property_lock);
		return;
	}

	e_named_parameters_free (session->priv->credentials);
	if (credentials)
		session->priv->credentials = e_named_parameters_new_clone (credentials);
	else
		session->priv->credentials = NULL;

	g_mutex_unlock (&session->priv->property_lock);

	g_object_notify (G_OBJECT (session), "credentials");

	/* Update also internal SoupSession state */
	g_rec_mutex_lock (&session->priv->session_lock);
	feature = soup_session_get_feature (SOUP_SESSION (session), SOUP_TYPE_AUTH_MANAGER);
	soup_auth_manager_clear_cached_credentials (SOUP_AUTH_MANAGER (feature));
	g_hash_table_remove_all (session->priv->using_auths);
	g_rec_mutex_unlock (&session->priv->session_lock);
}

/**
 * e_soup_session_dup_credentials:
 * @session: an #ESoupSession
 *
 * Returns: (nullable) (transfer full): A copy of the credentials being
 *    previously set with e_soup_session_set_credentials(), or %NULL when
 *    none are set. Free the returned pointer with e_named_parameters_free(),
 *    when no longer needed.
 *
 * Since: 3.26
 **/
ENamedParameters *
e_soup_session_dup_credentials (ESoupSession *session)
{
	ENamedParameters *credentials;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);

	g_mutex_lock (&session->priv->property_lock);

	if (session->priv->credentials)
		credentials = e_named_parameters_new_clone (session->priv->credentials);
	else
		credentials = NULL;

	g_mutex_unlock (&session->priv->property_lock);

	return credentials;
}

/**
 * e_soup_session_get_authentication_requires_credentials:
 * @session: an #ESoupSession
 *
 * Returns: Whether the last connection attempt required any credentials.
 *    Authentications like OAuth2 do not want extra credentials to work.
 *
 * Since: 3.28
 **/
gboolean
e_soup_session_get_authentication_requires_credentials (ESoupSession *session)
{
	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);

	return !session->priv->using_bearer_auth;
}

/**
 * e_soup_session_get_ssl_error_details:
 * @session: an #ESoupSession
 * @out_certificate_pem: (out) (optional): return location for a server TLS/SSL certificate
 *   in PEM format, when the last operation failed with a TLS/SSL error, or %NULL
 * @out_certificate_errors: (out) (optional): return location for a #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with a TLS/SSL error, or %NULL
 *
 * Populates @out_certificate_pem and @out_certificate_errors with the last values
 * returned on #G_TLS_ERROR_BAD_CERTIFICATE error.
 *
 * Returns: Whether the information was available and set to the out parameters.
 *
 * Since: 3.26
 **/
gboolean
e_soup_session_get_ssl_error_details (ESoupSession *session,
				      gchar **out_certificate_pem,
				      GTlsCertificateFlags *out_certificate_errors)
{
	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);

	g_mutex_lock (&session->priv->property_lock);
	if (!session->priv->ssl_info_set) {
		g_mutex_unlock (&session->priv->property_lock);
		return FALSE;
	}

	if (out_certificate_pem)
		*out_certificate_pem = g_strdup (session->priv->ssl_certificate_pem);

	if (out_certificate_errors)
		*out_certificate_errors = session->priv->ssl_certificate_errors;

	g_mutex_unlock (&session->priv->property_lock);

	return TRUE;
}

/**
 * e_soup_session_handle_authentication_failure:
 * @session: an #ESoupSession
 * @credentials: (nullable): credentials used for the authentication
 * @op_error: a #GError of the authentication operation
 * @out_auth_result: (out): an #ESourceAuthenticationResult with an authentication result
 * @out_certificate_pem: (out) (optional): return location for a server TLS/SSL certificate
 *   in PEM format, when the last operation failed with a TLS/SSL error, or %NULL
 * @out_certificate_errors: (out) (optional): return location for a #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with a TLS/SSL error, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Handles authentication failure and sets appropriate value to the @out_auth_result
 * for the provided @op_error and used @credentials. Converts the @op_error
 * into an appropriate error returned in the @error.
 *
 * Also updates connection status on the associated #ESource with the @session.
 *
 * Since: 3.46
 **/
void
e_soup_session_handle_authentication_failure (ESoupSession *session,
					      const ENamedParameters *credentials,
					      const GError *op_error,
					      ESourceAuthenticationResult *out_auth_result,
					      gchar **out_certificate_pem,
					      GTlsCertificateFlags *out_certificate_errors,
					      GError **error)
{
	ESource *source;
	gboolean requires_credentials;
	gboolean credentials_empty;
	gboolean is_tls_error;

	g_return_if_fail (E_IS_SOUP_SESSION (session));
	g_return_if_fail (out_auth_result != NULL);

	source = e_soup_session_get_source (session);
	requires_credentials = e_soup_session_get_authentication_requires_credentials (session);
	credentials_empty = (!credentials || !e_named_parameters_count (credentials) ||
		(e_named_parameters_count (credentials) == 1 && e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_SSL_TRUST))) &&
		requires_credentials;
	is_tls_error = g_error_matches (op_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE);

	*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;

	if (g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN) && credentials_empty) {
		*out_auth_result = E_SOURCE_AUTHENTICATION_REQUIRED;
	} else if (g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		if (credentials_empty)
			*out_auth_result = E_SOURCE_AUTHENTICATION_REQUIRED;
		else
			*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
	} else if (g_error_matches (op_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
		   (!requires_credentials && g_error_matches (op_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))) {
		*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
	} else if (!op_error) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Unknown error"));
	}

	if (op_error)
		g_propagate_error (error, g_error_copy (op_error));

	if (is_tls_error) {
		*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;

		if (source)
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_SSL_FAILED);
		e_soup_session_get_ssl_error_details (session, out_certificate_pem, out_certificate_errors);
	} else if (source) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
	}
}

static void
e_soup_session_preset_message (SoupMessage *message)
{
	if (message) {
		GUri *normalized_uri;

		normalized_uri = e_soup_session_util_normalize_uri_path (soup_message_get_uri (message));

		if (normalized_uri) {
			soup_message_set_uri (message, normalized_uri);
			g_uri_unref (normalized_uri);
		}

		soup_message_headers_append (soup_message_get_request_headers (message), "User-Agent", "Evolution/" VERSION);
		soup_message_headers_append (soup_message_get_request_headers (message), "Connection", "close");

		/* Disable caching for proxies (RFC 4918, section 10.4.5) */
		soup_message_headers_append (soup_message_get_request_headers (message), "Cache-Control", "no-cache");
		soup_message_headers_append (soup_message_get_request_headers (message), "Pragma", "no-cache");
	}
}

/**
 * e_soup_session_new_message:
 * @session: an #ESoupSession
 * @method: an HTTP method
 * @uri_string: a URI string to use for the request
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #SoupMessage, similar to soup_message_new(),
 * but also presets request headers with "User-Agent" to be "Evolution/version"
 * and with "Connection" to be "close".
 *
 * See also e_soup_session_new_message_from_uri().
 *
 * Returns: (transfer full): a new #SoupMessage, or %NULL on error
 *
 * Since: 3.26
 **/
SoupMessage *
e_soup_session_new_message (ESoupSession *session,
			    const gchar *method,
			    const gchar *uri_string,
			    GError **error)
{
	SoupMessage *message;
	GUri *uri;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);

	uri = g_uri_parse (uri_string, SOUP_HTTP_URI_FLAGS, error);
	if (!uri)
		return NULL;

	message = e_soup_session_new_message_from_uri (session, method, uri, error);

	g_uri_unref (uri);

	return message;
}

/**
 * e_soup_session_new_message_from_uri:
 * @session: an #ESoupSession
 * @method: an HTTP method
 * @uri: a #GUri to use for the request
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #SoupMessage, similar to soup_message_new_from_uri(),
 * but also presets request headers with "User-Agent" to be "Evolution/version"
 * and with "Connection" to be "close".
 *
 * See also e_soup_session_new_message().
 *
 * Returns: (transfer full): a new #SoupMessage, or %NULL on error
 *
 * Since: 3.46
 **/
SoupMessage *
e_soup_session_new_message_from_uri (ESoupSession *session,
				     const gchar *method,
				     GUri *uri,
				     GError **error)
{
	SoupMessage *message;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);

	if (g_uri_get_user (uri) && !g_uri_get_password (uri)) {
		/* Do not allow setting user without password in the URI, because libsoup3 tries
		   to authenticate even without password, which can break the code. */
		GUri *uri_copy;

		uri_copy = soup_uri_copy (uri,
			SOUP_URI_USER, NULL,
			SOUP_URI_PASSWORD, NULL,
			SOUP_URI_NONE);

		message = soup_message_new_from_uri (method, uri_copy);

		g_uri_unref (uri_copy);
	} else {
		message = soup_message_new_from_uri (method, uri);
	}

	if (!message)
		return NULL;

	e_soup_session_preset_message (message);

	return message;
}

static gboolean
e_soup_session_extract_ssl_data (ESoupSession *session,
				 SoupMessage *message,
				 gchar **out_certificate_pem,
				 GTlsCertificateFlags *out_certificate_errors)
{
	GTlsCertificate *certificate = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);

	if (!out_certificate_pem)
		return FALSE;

	g_object_get (G_OBJECT (message),
		"tls-peer-certificate", &certificate,
		out_certificate_errors ? "tls-peer-certificate-errors" : NULL, out_certificate_errors,
		NULL);

	if (certificate) {
		g_object_get (certificate, "certificate-pem", out_certificate_pem, NULL);
		res = TRUE;

		g_object_unref (certificate);
	}

	return res;
}

static void
e_soup_session_extract_ssl_data_internal (ESoupSession *session,
					  SoupMessage *message)
{
	g_return_if_fail (E_IS_SOUP_SESSION (session));
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	g_mutex_lock (&session->priv->property_lock);

	g_clear_pointer (&session->priv->ssl_certificate_pem, g_free);

	session->priv->ssl_info_set = e_soup_session_extract_ssl_data (session, message,
		&session->priv->ssl_certificate_pem, &session->priv->ssl_certificate_errors);

	g_mutex_unlock (&session->priv->property_lock);
}

static gboolean
e_soup_session_extract_google_daily_limit_error (gconstpointer read_bytes,
						 gsize bytes_length,
						 GError **error)
{
	gchar *body;
	gboolean contains_daily_limit = FALSE;

	if (!read_bytes || !bytes_length)
		return FALSE;

	body = g_strndup (read_bytes, bytes_length);

	/* Do not localize this string, it is returned by the server. */
	if (body && (e_util_strstrcase (body, "Daily Limit") ||
	    e_util_strstrcase (body, "https://console.developers.google.com/"))) {
		/* Special-case this condition and provide this error up to the UI. */
		g_set_error_literal (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN, body);
		contains_daily_limit = TRUE;
	}

	g_free (body);

	return contains_daily_limit;
}

/**
 * e_soup_session_check_result:
 * @session: an #ESoupSession
 * @message: a #SoupMessage
 * @read_bytes: (nullable): optional bytes which had been read from the stream, or %NULL
 * @bytes_length: how many bytes had been read; ignored when @read_bytes is %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Checks result of the @message and sets the @error if it failed.
 * When it failed and the @read_bytes is provided, then these are
 * set to @message's response body, thus it can be used later.
 *
 * Returns: Whether succeeded, aka %TRUE, when no error recognized
 *    and %FALSE otherwise.
 *
 * Since: 3.26
 **/
gboolean
e_soup_session_check_result (ESoupSession *session,
			     SoupMessage *message,
			     gconstpointer read_bytes,
			     gsize bytes_length,
			     GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);

	success = SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message));
	if (!success) {
		if (soup_message_get_status (message) == SOUP_STATUS_FORBIDDEN &&
		    e_soup_session_extract_google_daily_limit_error (read_bytes, bytes_length, error)) {
			/* Nothing to do */
		} else {
			g_set_error (error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
				_("Failed with HTTP error %d: %s"), soup_message_get_status (message),
				e_soup_session_util_status_to_string (soup_message_get_status (message),
				soup_message_get_reason_phrase (message)));
		}

		e_soup_session_extract_ssl_data_internal (session, message);
	}

	return success;
}

static void
e_soup_session_restore_method_on_restarted_cb (SoupMessage *message,
					       gpointer user_data)
{
	const gchar *orig_method = user_data;

	g_return_if_fail (orig_method != NULL);

	/* Redirect can change the method, this makes sure it'll be preserved. */
	if (g_strcmp0 (orig_method, soup_message_get_method (message)) != 0)
		soup_message_set_method (message, orig_method);
}

static gboolean
e_soup_session_prepare_message_send_phase1_sync (ESoupSession *session,
						 SoupMessage *message,
						 gulong *out_authenticate_id,
						 gulong *out_restarted_id,
						 GCancellable *cancellable,
						 GError **error)
{
	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);

	if (!e_soup_session_maybe_prepare_auth (session, message, cancellable, error))
		return FALSE;

	*out_authenticate_id = g_signal_connect (message, "authenticate",
		G_CALLBACK (e_soup_session_authenticate_cb), session);

	*out_restarted_id = g_signal_connect_data (message, "restarted",
		G_CALLBACK (e_soup_session_restore_method_on_restarted_cb), g_strdup (soup_message_get_method (message)), (GClosureNotify) g_free, 0);

	/* Always connect the SSL trust, even when the WebDAV extension is not present on the source,
	   otherwise any SSL trust is not properly handled. */
	if (session->priv->source)
		e_soup_ssl_trust_connect (message, session->priv->source);

	return TRUE;
}

static gboolean
e_soup_session_prepare_message_send_phase2_sync (ESoupSession *session,
						 SoupMessage *message,
						 GCancellable *cancellable,
						 GError **error)
{
	ESoupAuthBearer *using_bearer_auth = NULL;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);

	g_mutex_lock (&session->priv->property_lock);
	if (session->priv->using_bearer_auth)
		using_bearer_auth = g_object_ref (session->priv->using_bearer_auth);
	g_mutex_unlock (&session->priv->property_lock);

	if (using_bearer_auth &&
	    e_soup_auth_bearer_is_expired (using_bearer_auth)) {
		GError *local_error = NULL;

		if (!e_soup_session_setup_bearer_auth (session, message, FALSE, using_bearer_auth, cancellable, &local_error)) {
			if (local_error) {
				g_propagate_error (error, local_error);
			} else {
				g_set_error_literal (&local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_BAD_REQUEST, _("Failed to setup authentication"));
			}

			g_object_unref (using_bearer_auth);

			return FALSE;
		}
	}

	g_clear_object (&using_bearer_auth);

	return TRUE;
}

typedef struct _AsyncSendData {
	guint size;
	ESoupSession *session;
	GTask *task;
	gulong authenticate_id;
	gulong restarted_id;
	gchar *certificate_pem;
	GTlsCertificateFlags certificate_errors;
	gint io_priority;
	gboolean caught_bearer_expired;
} AsyncSendData;

static void
async_send_data_free (gpointer ptr)
{
	AsyncSendData *asd = ptr;

	if (asd) {
		/* The ads->task is unreffed in e_soup_session_send_message_ready_cb() */
		g_clear_object (&asd->session);
		g_free (asd->certificate_pem);
		g_slice_free (AsyncSendData, asd);
	}
}

/**
 * e_soup_session_prepare_message_send_sync:
 * @session: an #ESoupSession
 * @message: a #SoupMessage to prepare for asynchronous send
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Prepares the @message to be a sent asynchronously with
 * e_soup_session_send_message(). The returned pointer is passed
 * to the e_soup_session_send_message() as the prepare_data
 * parameter.
 *
 * Returns: (nullable) (transfer full): prepare data for e_soup_session_send_message(),
 *   or %NULL on error.
 *
 * Since: 3.46
 **/
gpointer
e_soup_session_prepare_message_send_sync (ESoupSession *session,
					  SoupMessage *message,
					  GCancellable *cancellable,
					  GError **error)
{
	gulong authenticate_id = 0;
	gulong restarted_id = 0;
	gboolean success;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), NULL);

	success = e_soup_session_prepare_message_send_phase1_sync (session, message, &authenticate_id, &restarted_id, cancellable, error);
	if (success) {
		success = e_soup_session_prepare_message_send_phase2_sync (session, message, cancellable, error);
		if (!success) {
			if (authenticate_id)
				g_signal_handler_disconnect (message, authenticate_id);
			if (restarted_id)
				g_signal_handler_disconnect (message, restarted_id);
		}
	}

	if (success) {
		AsyncSendData *asd;

		asd = g_slice_new0 (AsyncSendData);
		asd->size = sizeof (AsyncSendData);
		asd->authenticate_id = authenticate_id;
		asd->restarted_id = restarted_id;

		return asd;
	}

	return NULL;
}

static GByteArray *
e_soup_session_read_bytes (SoupMessage *message,
			   GInputStream *input_stream,
			   GCancellable *cancellable,
			   GError **error)
{
	GByteArray *bytes;
	goffset expected_length;
	gpointer buffer;
	gsize nread = 0;
	gboolean success = FALSE;

	expected_length = soup_message_headers_get_content_length (soup_message_get_response_headers (message));
	if (expected_length > 0)
		bytes = g_byte_array_sized_new (expected_length > 1024 * 1024 * 10 ? 1024 * 1024 * 10 : expected_length);
	else
		bytes = g_byte_array_new ();

	buffer = g_malloc (BUFFER_SIZE);

	while (success = g_input_stream_read_all (input_stream, buffer, BUFFER_SIZE, &nread, cancellable, error),
	       success && nread > 0) {
		g_byte_array_append (bytes, buffer, nread);
	}

	g_free (buffer);

	if (!success)
		g_clear_pointer (&bytes, g_byte_array_unref);

	return bytes;
}

static void
e_soup_session_store_data_on_message (SoupMessage *message,
				      GInputStream *input_stream,
				      GCancellable *cancellable)
{
	if (input_stream) {
		GByteArray *bytes;

		bytes = e_soup_session_read_bytes (message, input_stream, cancellable, NULL);

		if (bytes) {
			g_object_set_data_full (G_OBJECT (message), E_SOUP_SESSION_MESSAGE_BYTES_KEY,
				bytes, (GDestroyNotify) g_byte_array_unref);
		}
	}
}

static void
e_soup_session_send_message_ready_cb (GObject *source_object,
				      GAsyncResult *result,
				      gpointer user_data)
{
	AsyncSendData *asd = user_data;
	ESoupSession *esession;
	SoupSession *session;
	SoupMessage *message;
	GInputStream *input_stream;
	GError *local_error = NULL;
	gboolean caught_bearer_expired = FALSE;

	g_return_if_fail (asd != NULL);

	session = SOUP_SESSION (source_object);
	esession = E_SOUP_SESSION (session);

	g_rec_mutex_lock (&esession->priv->session_lock);

	input_stream = soup_session_send_finish (session, result, &local_error);
	message = soup_session_get_async_result_message (session, result);

	if (message) {
		if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
			e_soup_session_store_data_on_message (message, input_stream, NULL);
			g_clear_object (&input_stream);
		}

		if (g_error_matches (local_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE)) {
			e_soup_session_extract_ssl_data (E_SOUP_SESSION (session), message, &asd->certificate_pem, &asd->certificate_errors);
		} else if (!local_error && !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
			GByteArray *bytes = e_soup_session_util_get_message_bytes (message);

			if (soup_message_get_status (message) != SOUP_STATUS_FORBIDDEN ||
			    !e_soup_session_extract_google_daily_limit_error (bytes ? bytes->data : NULL, bytes ? bytes->len : 0, &local_error))
				g_set_error_literal (&local_error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
					soup_message_get_reason_phrase (message));
		}
	}

	g_rec_mutex_unlock (&esession->priv->session_lock);

	if (message && soup_message_get_status (message) == SOUP_STATUS_UNAUTHORIZED && !asd->caught_bearer_expired) {
		g_mutex_lock (&esession->priv->property_lock);
		if (esession->priv->using_bearer_auth && e_soup_auth_bearer_is_expired (esession->priv->using_bearer_auth)) {
			g_signal_emit_by_name (message, "restarted");
			asd->caught_bearer_expired = TRUE;
			caught_bearer_expired = TRUE;
		}
		g_mutex_unlock (&esession->priv->property_lock);
	}

	if (caught_bearer_expired) {
		g_clear_error (&local_error);
		g_clear_object (&input_stream);

		g_rec_mutex_lock (&esession->priv->session_lock);

		soup_session_send_async (session, message, asd->io_priority, g_task_get_cancellable (asd->task),
			e_soup_session_send_message_ready_cb, asd);

		g_rec_mutex_unlock (&esession->priv->session_lock);
	} else {
		if (local_error) {
			g_task_return_error (asd->task, local_error);
			g_clear_object (&input_stream);
		} else {
			g_task_return_pointer (asd->task, input_stream, g_object_unref);
		}

		g_object_unref (asd->task);
	}
}

/**
 * e_soup_session_send_message:
 * @session: an #ESoupSession
 * @message: a #SoupMessage to send
 * @io_priority: the I/O priority of the request, like %G_PRIORITY_DEFAULT
 * @prepare_data: (transfer full): data returned from e_soup_session_prepare_message_send_sync()
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: (scope async): the callback to invoke once the request is finished
 * @user_data: user data for @callback
 *
 * Asynchronously sends the @message. Finish the call with
 * e_soup_session_send_message_finish().
 *
 * The @prepare_data is a result of the e_soup_session_prepare_message_send_sync()
 * and this function assumes ownership of it. The @prepare_data cannot be used
 * again after this call.
 *
 * Since: 3.46
 **/
void
e_soup_session_send_message (ESoupSession *session,
			     SoupMessage *message,
			     gint io_priority,
			     gpointer prepare_data,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer user_data)
{
	AsyncSendData *asd = prepare_data;

	g_return_if_fail (E_IS_SOUP_SESSION (session));
	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (prepare_data != NULL);
	g_return_if_fail (asd->size == sizeof (AsyncSendData));
	g_return_if_fail (asd->session == NULL);
	g_return_if_fail (asd->task == NULL);

	asd->session = g_object_ref (session);
	asd->task = g_task_new (session, cancellable, callback, user_data);
	asd->caught_bearer_expired = FALSE;
	asd->io_priority = io_priority;
	g_task_set_source_tag (asd->task, e_soup_session_send_message);
	g_task_set_task_data (asd->task, asd, async_send_data_free);

	g_rec_mutex_lock (&session->priv->session_lock);

	soup_session_send_async (SOUP_SESSION (session), message, io_priority, cancellable,
		e_soup_session_send_message_ready_cb, asd);

	g_rec_mutex_unlock (&session->priv->session_lock);
}

/**
 * e_soup_session_send_message_finish:
 * @session: an #ESoupSession
 * @result: a #GAsyncResult object
 * @out_certificate_pem: (out) (optional) (nullable): return location for a server TLS/SSL certificate
 *   in PEM format, when the last operation failed with a TLS/SSL error
 * @out_certificate_errors: (out) (optional): return location for a #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with a TLS/SSL error
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the call of e_soup_session_send_message(). This is supposed to
 * be called from the callback passed to the e_soup_session_send_message().
 *
 * The optional @out_certificate_pem and @out_certificate_errors are set,
 * when provided, only if the operation failed with a TLS/SSL error.
 *
 * Make sure the #GInputStream is read and freed from the same thread,
 * and with the same thread default main context, which this function
 * was called from, otherwise it can break libsoup3.
 *
 * Returns: (transfer full) (nullable): a #GInputStream for reading the response body, or %NULL on error
 *
 * Since: 3.46
 **/
GInputStream *
e_soup_session_send_message_finish (ESoupSession *session,
				    GAsyncResult *result,
				    gchar **out_certificate_pem,
				    GTlsCertificateFlags *out_certificate_errors,
				    GError **error)
{
	GInputStream *input_stream;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);

	input_stream = g_task_propagate_pointer (G_TASK (result), error);

	if (!input_stream) {
		AsyncSendData *asd = g_task_get_task_data (G_TASK (result));

		if (out_certificate_pem)
			*out_certificate_pem = asd ? g_steal_pointer (&asd->certificate_pem) : NULL;

		if (out_certificate_errors)
			*out_certificate_errors = asd ? asd->certificate_errors : 0;
	}

	return input_stream;
}

/**
 * e_soup_session_send_message_sync:
 * @session: an #ESoupSession
 * @message: a #SoupMessage to send
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Synchronously sends prepared message and returns #GInputStream
 * that can be used to read its contents.
 *
 * This calls soup_session_send() internally, but it also setups
 * the @message according to #ESoupSession:source authentication
 * settings. It also extracts information about used certificate,
 * in case of G_TLS_ERROR_BAD_CERTIFICATE error and keeps it
 * for later use by e_soup_session_get_ssl_error_details().
 *
 * Use e_soup_session_send_message_simple_sync() to read whole
 * content into a #GByteArray.
 *
 * Note that SoupSession doesn't log content read from GInputStream,
 * thus the caller may print the read content on its own when needed.
 *
 * Note the @message is fully filled only after there is anything
 * read from the resulting #GInputStream, thus use
 * e_soup_session_check_result() to verify that the receive had
 * been finished properly.
 *
 * Make sure the #GInputStream is read and freed from the same thread,
 * and with the same thread default main context, which this function
 * was called from, otherwise it can break libsoup3.
 *
 * Returns: (transfer full): A newly allocated #GInputStream,
 *    that can be used to read from the URI pointed to by @message.
 *    Free it with g_object_unref(), when no longer needed.
 *
 * Since: 3.26
 **/
GInputStream *
e_soup_session_send_message_sync (ESoupSession *session,
				  SoupMessage *message,
				  GCancellable *cancellable,
				  GError **error)
{
	GInputStream *input_stream;
	gboolean redirected;
	gboolean caught_bearer_expired = FALSE;
	gint resend_count = 0;
	gulong authenticate_id = 0;
	gulong restarted_id = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), NULL);

	if (!e_soup_session_prepare_message_send_phase1_sync (session, message, &authenticate_id, &restarted_id, cancellable, error))
		return NULL;

	g_mutex_lock (&session->priv->property_lock);
	g_clear_pointer (&session->priv->ssl_certificate_pem, g_free);
	session->priv->ssl_certificate_errors = 0;
	session->priv->ssl_info_set = FALSE;
	g_mutex_unlock (&session->priv->property_lock);

	redirected = TRUE;
	while (redirected) {
		redirected = FALSE;

		if (!e_soup_session_prepare_message_send_phase2_sync (session, message, cancellable, error)) {
			if (authenticate_id)
				g_signal_handler_disconnect (message, authenticate_id);
			if (restarted_id)
				g_signal_handler_disconnect (message, restarted_id);

			return NULL;
		}

		g_rec_mutex_lock (&session->priv->session_lock);

		input_stream = soup_session_send (SOUP_SESSION (session), message, cancellable, &local_error);

		g_rec_mutex_unlock (&session->priv->session_lock);

		if (input_stream) {
			if (SOUP_STATUS_IS_REDIRECTION (soup_message_get_status (message))) {
				/* libsoup uses 20, but the constant is not in any public header */
				if (resend_count >= 30) {
					g_set_error_literal (&local_error, SOUP_SESSION_ERROR, SOUP_SESSION_ERROR_TOO_MANY_REDIRECTS, _("Too many redirects"));
					g_clear_object (&input_stream);
				} else {
					const gchar *new_location;

					new_location = soup_message_headers_get_list (soup_message_get_response_headers (message), "Location");
					if (new_location) {
						GUri *new_uri;

						new_uri = g_uri_parse_relative (soup_message_get_uri (message), new_location, SOUP_HTTP_URI_FLAGS, NULL);

						soup_message_set_uri (message, new_uri);

						g_clear_object (&input_stream);
						g_uri_unref (new_uri);

						g_signal_emit_by_name (message, "restarted");

						resend_count++;
						redirected = TRUE;
					}
				}
			}
		} else if (soup_message_get_status (message) == SOUP_STATUS_UNAUTHORIZED && !caught_bearer_expired) {
			g_mutex_lock (&session->priv->property_lock);
			if (session->priv->using_bearer_auth && e_soup_auth_bearer_is_expired (session->priv->using_bearer_auth)) {
				g_signal_emit_by_name (message, "restarted");
				resend_count++;
				redirected = TRUE;
				caught_bearer_expired = TRUE;
				g_clear_error (&local_error);
			}
			g_mutex_unlock (&session->priv->property_lock);
		}
	}

	if (authenticate_id)
		g_signal_handler_disconnect (message, authenticate_id);

	if (restarted_id)
		g_signal_handler_disconnect (message, restarted_id);

	if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
		e_soup_session_store_data_on_message (message, input_stream, cancellable);
		g_clear_object (&input_stream);
	}

	if (g_error_matches (local_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE)) {
		e_soup_session_extract_ssl_data_internal (session, message);
	} else if (!local_error && !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
		GByteArray *bytes = e_soup_session_util_get_message_bytes (message);

		if (soup_message_get_status (message) != SOUP_STATUS_FORBIDDEN ||
		    !e_soup_session_extract_google_daily_limit_error (bytes ? bytes->data : NULL, bytes ? bytes->len : 0, error))
			g_set_error_literal (&local_error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
				soup_message_get_reason_phrase (message));
	}

	if (local_error) {
		g_propagate_error (error, local_error);
		g_clear_object (&input_stream);
	}

	return input_stream;
}

/**
 * e_soup_session_send_message_simple_sync:
 * @session: an #ESoupSession
 * @message: a #SoupMessage to send
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Similar to e_soup_session_send_message_sync(), except it reads
 * whole response content into memory and returns it as a #GByteArray.
 * Use e_soup_session_send_message_sync() when you want to have
 * more control on the content read.
 *
 * Returns: (transfer full): A newly allocated #GByteArray,
 *    which contains whole content from the URI pointed to by @message.
 *
 * Since: 3.26
 **/
GByteArray *
e_soup_session_send_message_simple_sync (ESoupSession *session,
					 SoupMessage *message,
					 GCancellable *cancellable,
					 GError **error)
{
	GInputStream *input_stream;
	GByteArray *bytes;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_SOUP_SESSION (session), NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), NULL);

	input_stream = e_soup_session_send_message_sync (session, message, cancellable, error);

	if (!input_stream) {
		bytes = e_soup_session_util_get_message_bytes (message);

		if (bytes) {
			GError *local_error = NULL;

			if (!e_soup_session_check_result (session, message, bytes->data, bytes->len, &local_error) && local_error) {
				g_clear_error (error);
				g_propagate_error (error, local_error);
			}
		}

		return NULL;
	}

	bytes = e_soup_session_read_bytes (message, input_stream, cancellable, error);

	g_object_unref (input_stream);

	success = bytes != NULL;

	if (success)
		success = e_soup_session_check_result (session, message, bytes->data, bytes->len, error);

	if (!success)
		g_clear_pointer (&bytes, g_byte_array_unref);

	return bytes;
}

/**
 * e_soup_session_util_status_to_string:
 * @status_code: an HTTP status code
 * @reason_phrase: (nullable): preferred string to use for the message, or %NULL
 *
 * Returns the @reason_phrase, if it's non-%NULL and non-empty, a static string
 * corresponding to @status_code. In case neither that can be found a localized
 * "Unknown error" message is returned.
 *
 * Returns: (transfer none): Error text based on given arguments. The returned
 *    value is valid as long as @reason_phrase is not freed.
 *
 * Since: 3.26
 **/
const gchar *
e_soup_session_util_status_to_string (guint status_code,
				      const gchar *reason_phrase)
{
	if (!reason_phrase || !*reason_phrase)
		reason_phrase = soup_status_get_phrase (status_code);

	if (reason_phrase && *reason_phrase)
		return reason_phrase;

	return _("Unknown error");
}

static gboolean
part_needs_encoding (const gchar *part)
{
	const gchar *pp;

	if (!part || !*part)
		return FALSE;

	for (pp = part; *pp; pp++) {
		if (!strchr ("/!()+-*~';,.$&_", *pp) &&
		    !g_ascii_isalnum (*pp) &&
		    (*pp != '%' || pp[1] != '4' || pp[2] != '0') && /* cover '%40', aka '@', as a common case, to avoid unnecessary allocations */
		    (*pp != '%' || pp[1] != '2' || pp[2] != '0')) { /* '%20', aka ' ' */
			break;
		}
	}

	return *pp;
}

/**
 * e_soup_session_util_normalize_uri_path:
 * @uri: a #GUri to normalize the path for
 *
 * Normalizes the path of the @uri, aka encodes characters, which should
 * be encoded, if needed. Returns, modified URI when any change had been made to the path.
 * It doesn't touch other parts of the @uri.
 *
 * Returns: (transfer full) (nullable): a new #GUri with modified path, or %NULL, when
 *    no change was required.
 *
 * Since: 3.46
 **/
GUri *
e_soup_session_util_normalize_uri_path (GUri *uri)
{
	const gchar *path;
	gchar **parts, *tmp;
	GUri *nuri = NULL;
	GUriFlags flags;
	gint ii;

	if (!uri)
		return NULL;

	flags = g_uri_get_flags (uri);

	if ((flags & G_URI_FLAGS_ENCODED) != 0 ||
	    (flags & G_URI_FLAGS_ENCODED_PATH) != 0)
		return NULL;

	path = g_uri_get_path (uri);

	if (!*path || g_strcmp0 (path, "/") == 0)
		return NULL;

	if (!part_needs_encoding (path))
		return NULL;

	parts = g_strsplit (path, "/", -1);

	if (!parts)
		return NULL;

	for (ii = 0; parts[ii]; ii++) {
		gchar *part = parts[ii];

		if (part_needs_encoding (part)) {
			if (strchr (part, '%')) {
				tmp = g_uri_unescape_string (part, NULL);
				g_free (part);
				part = tmp;
			}

			tmp = g_uri_escape_string (part, NULL, FALSE);
			g_free (part);
			parts[ii] = tmp;
		}
	}

	tmp = g_strjoinv ("/", parts);
	if (g_strcmp0 (path, tmp) != 0)
		nuri = soup_uri_copy (uri, SOUP_URI_PATH, tmp, SOUP_URI_NONE);

	g_free (tmp);
	g_strfreev (parts);

	return nuri;
}

typedef struct _EInputStreamWrapper {
	GInputStream parent;

	GInputStream *input_stream;
	goffset read_from;
} EInputStreamWrapper;

typedef struct _EInputStreamWrapperClass {
	GInputStreamClass parent_class;
} EInputStreamWrapperClass;

GType e_input_stream_wrapper_get_type (void);

static void e_input_stream_wrapper_pollable_iface_init (GPollableInputStreamInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EInputStreamWrapper, e_input_stream_wrapper, G_TYPE_INPUT_STREAM,
	G_IMPLEMENT_INTERFACE (G_TYPE_POLLABLE_INPUT_STREAM, e_input_stream_wrapper_pollable_iface_init))

#define E_INPUT_STREAM_WRAPPER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), e_input_stream_wrapper_get_type (), EInputStreamWrapper))

static gssize
e_input_stream_wrapper_read_fn (GInputStream *stream,
                                void *buffer,
				gsize count,
				GCancellable *cancellable,
				GError **error)
{
	EInputStreamWrapper *wrapper = E_INPUT_STREAM_WRAPPER (stream);

	return g_input_stream_read (wrapper->input_stream, buffer, count, cancellable, error);
}

static gssize
e_input_stream_wrapper_skip (GInputStream *stream,
			     gsize count,
			     GCancellable *cancellable,
			     GError **error)
{
	EInputStreamWrapper *wrapper = E_INPUT_STREAM_WRAPPER (stream);

	return g_input_stream_skip (wrapper->input_stream, count, cancellable, error);
}

static gboolean
e_input_stream_wrapper_close_fn (GInputStream *stream,
				 GCancellable *cancellable,
				 GError **error)
{
	/* Always success, but without closing the self::input_stream */
	return TRUE;
}

static gboolean
e_input_stream_wrapper_is_readable (GPollableInputStream *stream)
{
	return TRUE;
}

static GSource *
e_input_stream_wrapper_create_source (GPollableInputStream *stream,
				      GCancellable *cancellable)
{
	GSource *base_source, *pollable_source;

	base_source = g_timeout_source_new (0);
	pollable_source = g_pollable_source_new_full (stream, base_source, cancellable);
	g_source_unref (base_source);

	return pollable_source;
}

static void
e_input_stream_wrapper_dispose (GObject *object)
{
	EInputStreamWrapper *wrapper = E_INPUT_STREAM_WRAPPER (object);

	g_clear_object (&wrapper->input_stream);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_input_stream_wrapper_parent_class)->dispose (object);
}

static void
e_input_stream_wrapper_class_init (EInputStreamWrapperClass *klass)
{
	GInputStreamClass *input_stream_class;
	GObjectClass *object_class;

	input_stream_class = G_INPUT_STREAM_CLASS (klass);
	input_stream_class->read_fn = e_input_stream_wrapper_read_fn;
	input_stream_class->skip = e_input_stream_wrapper_skip;
	input_stream_class->close_fn = e_input_stream_wrapper_close_fn;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_input_stream_wrapper_dispose;
}

static void
e_input_stream_wrapper_pollable_iface_init (GPollableInputStreamInterface *iface)
{
	iface->is_readable = e_input_stream_wrapper_is_readable;
	iface->create_source = e_input_stream_wrapper_create_source;
}

static void
e_input_stream_wrapper_init (EInputStreamWrapper *self)
{
}

static void
e_input_stream_wrapper_assign (EInputStreamWrapper *self,
			       GInputStream *input_stream)
{
	self->input_stream = g_object_ref (input_stream);

	if (G_IS_SEEKABLE (input_stream))
		self->read_from = g_seekable_tell (G_SEEKABLE (input_stream));
}

static void
e_input_stream_wrapper_rewind (EInputStreamWrapper *self)
{
	if (G_IS_SEEKABLE (self->input_stream) && self->read_from != g_seekable_tell (G_SEEKABLE (self->input_stream)))
		g_seekable_seek (G_SEEKABLE (self->input_stream), self->read_from, G_SEEK_SET, NULL, NULL);
}

static GInputStream *
e_input_stream_wrapper_dup (EInputStreamWrapper *self)
{
	EInputStreamWrapper *dup;

	e_input_stream_wrapper_rewind (self);

	dup = g_object_new (e_input_stream_wrapper_get_type (), NULL);
	dup->input_stream = g_object_ref (self->input_stream);
	dup->read_from = self->read_from;

	return G_INPUT_STREAM (dup);
}

typedef struct _MessageData {
	GInputStream *input_stream;
	gssize length;
} MessageData;

static MessageData *
message_data_new (GInputStream *input_stream,
		  gssize length)
{
	MessageData *md;
	EInputStreamWrapper *wrapper;

	wrapper = g_object_new (e_input_stream_wrapper_get_type (), NULL);
	e_input_stream_wrapper_assign (wrapper, input_stream);

	md = g_slice_new0 (MessageData);
	md->input_stream = G_INPUT_STREAM (wrapper);
	md->length = length;

	return md;
}

static void
message_data_free (gpointer ptr)
{
	MessageData *md = ptr;

	if (md) {
		g_object_unref (md->input_stream);
		g_slice_free (MessageData, md);
	}
}

static void
e_soup_session_message_restarted_cb (SoupMessage *message,
				     gpointer user_data)
{
	GInputStream *input_stream;
	gssize length = 0;

	input_stream = e_soup_session_util_ref_message_request_body (message, &length);

	g_return_if_fail (input_stream != NULL);

	soup_message_set_request_body (message, NULL, input_stream, length);

	g_clear_object (&input_stream);
}

#define MESSAGE_DATA_KEY "ESoupSession::message-data"

/**
 * e_soup_session_util_set_message_request_body:
 * @message: a #SoupMessage
 * @content_type: (nullable): optional Content-Type of the @data, or %NULL
 * @input_stream (transfer none): the request body data as a #GInputStream
 * @length: length of the @data
 *
 * Sets the request body of the @message from the @input_stream of the @length, with optional
 * @content_type. The function makes sure the @message request body is set again
 * when the message is restarted.
 *
 * The @input_stream should implement the #GSeekable interface.
 *
 * Since: 3.46
 **/
void
e_soup_session_util_set_message_request_body (SoupMessage *message,
					      const gchar *content_type,
					      GInputStream *input_stream,
					      gssize length)
{
	MessageData *md;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (G_IS_SEEKABLE (input_stream));

	md = message_data_new (input_stream, length);

	g_object_set_data_full (G_OBJECT (message), MESSAGE_DATA_KEY, md, message_data_free);

	g_signal_connect (message, "restarted",
		G_CALLBACK (e_soup_session_message_restarted_cb), NULL);

	soup_message_set_request_body (message, content_type, md->input_stream, length);
}

/**
 * e_soup_session_util_set_message_request_body_from_data:
 * @message: a #SoupMessage
 * @create_copy: whether to create copy of the @data
 * @content_type: (nullable): optional Content-Type of the @data, or %NULL
 * @data: the request body data
 * @length: length of the @data
 * @free_func: (nullable): a free function for the @data, or %NULL
 *
 * Sets the request body of the @message from the @data of the @length, with optional
 * @content_type. The function makes sure the @message request body is set again
 * when the message is restarted.
 *
 * When the @create_copy is %TRUE, the @free_func should be %NULL.
 *
 * Since: 3.46
 **/
void
e_soup_session_util_set_message_request_body_from_data (SoupMessage *message,
							gboolean create_copy,
							const gchar *content_type,
							gconstpointer data,
							gssize length,
							GDestroyNotify free_func)
{
	GInputStream *input_stream;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (data != NULL);
	if (create_copy)
		g_return_if_fail (free_func == NULL);

	if (create_copy)
		input_stream = g_memory_input_stream_new_from_data (g_memdup2 (data, length), length, g_free);
	else
		input_stream = g_memory_input_stream_new_from_data (data, length, free_func);

	e_soup_session_util_set_message_request_body (message, content_type, input_stream, length);

	g_object_unref (input_stream);
}

/**
 * e_soup_session_util_ref_message_request_body:
 * @message: a #SoupMessage
 * @out_length: (out) (optional): length of the input stream
 *
 * Returns referenced request data for the @message, as being previously
 * set by the e_soup_session_util_set_message_request_body() or
 * e_soup_session_util_set_message_request_body_from_data().
 *
 * Do not call this function while the @message is queued in
 * a #SoupSession, nor modify the input stream position until
 * the @message lefts the #SoupSession.
 *
 * Returns: (nullable) (transfer full): a new #GInputStream with the request
 *    body being previously set, or %NULL. The @out_length is set to the length
 *    of the returned input stream.
 *
 * Since: 3.46
 **/
GInputStream *
e_soup_session_util_ref_message_request_body (SoupMessage *message,
					      gssize *out_length)
{
	MessageData *md;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), NULL);

	md = g_object_get_data (G_OBJECT (message), MESSAGE_DATA_KEY);

	if (!md || !md->input_stream)
		return NULL;

	if (out_length)
		*out_length = md->length;

	return e_input_stream_wrapper_dup (E_INPUT_STREAM_WRAPPER (md->input_stream));
}

/**
 * e_soup_session_util_get_message_bytes:
 * @message: a #SoupMessage
 *
 * Returns bytes read from the message response, when the message send failed.
 * This can be used to examine detailed error returned by the server in
 * the response body.
 *
 * Returns: (transfer none) (nullable): read message data on failed request, or %NULL, when none had been read
 *
 * Since: 3.46
 **/
GByteArray *
e_soup_session_util_get_message_bytes (SoupMessage *message)
{
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), NULL);

	return g_object_get_data (G_OBJECT (message), E_SOUP_SESSION_MESSAGE_BYTES_KEY);
}
