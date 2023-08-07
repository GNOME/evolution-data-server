/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
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
 * SECTION: e-oauth2-service
 * @include: libedataserver/libedataserver.h
 * @short_description: An interface for an OAuth2 service
 *
 * An interface for an OAuth2 service. Any descendant might be defined
 * as an extension of #EOAuth2Services and it should add itself into it
 * with e_oauth2_services_add(). To make it easier, an #EOAuth2ServiceBase
 * is provided for convenience.
 **/

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#ifndef BUILDING_VALUE_HELPER
#include <json-glib/json-glib.h>

#include "e-data-server-util.h"
#include "e-secret-store.h"
#include "e-soup-session.h"
#include "e-soup-ssl-trust.h"
#include "e-source-authentication.h"

#include "e-oauth2-service.h"

/* How many seconds earlier than reported by the server is the token considered expired
   and will be refreshed. */
#define TOKEN_VALIDITY_GAP_SECS 10

G_DEFINE_INTERFACE (EOAuth2Service, e_oauth2_service, G_TYPE_OBJECT)

static gboolean
eos_default_can_process (EOAuth2Service *service,
			 ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;
		gchar *method;

		auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		if (e_source_authentication_get_is_external (auth_extension))
			return FALSE;

		method = e_source_authentication_dup_method (auth_extension);

		if (g_strcmp0 (method, e_oauth2_service_get_name (service)) == 0) {
			g_free (method);
			return TRUE;
		}

		g_free (method);
	}

	return FALSE;
}

static gboolean
eos_default_guess_can_process (EOAuth2Service *service,
			       const gchar *protocol,
			       const gchar *hostname)
{
	gboolean can = FALSE;
	GSettings *settings;
	gchar **values;
	gint ii, name_len, hostname_len;
	const gchar *name;

	if (!hostname || !*hostname)
		return FALSE;

	name = e_oauth2_service_get_name (service);
	g_return_val_if_fail (name != NULL, FALSE);
	name_len = strlen (name);
	hostname_len = strlen (hostname);

	settings = g_settings_new ("org.gnome.evolution-data-server");
	values = g_settings_get_strv (settings, "oauth2-services-hint");
	g_object_unref (settings);

	for (ii = 0; !can && values && values[ii]; ii++) {
		const gchar *line = values[ii];
		gint len;

		if (!g_str_has_prefix (line, name) ||
		    (line[name_len] != ':' && line[name_len] != '-'))
			continue;

		if (line[name_len] == '-') {
			len = protocol ? strlen (protocol) : -1;

			if (len <= 0 || g_ascii_strncasecmp (line + name_len + 1, protocol, len) != 0 ||
			    line[name_len + len + 1] != ':')
				continue;

			line += name_len + len + 2;
		} else { /* line[name_len] == ':' */
			line += name_len + 1;
		}

		while (line && *line) {
			if (g_ascii_strncasecmp (line, hostname, hostname_len) == 0 &&
			    (line[hostname_len] == ',' || line[hostname_len] == '\0')) {
				can = TRUE;
				break;
			}

			line = strchr (line, ',');
			if (line)
				line++;
		}
	}

	g_strfreev (values);

	return can;
}

static guint32
eos_default_get_flags (EOAuth2Service *service)
{
	return E_OAUTH2_SERVICE_FLAG_NONE;
}

static const gchar *
eos_default_get_redirect_uri (EOAuth2Service *service,
			      ESource *source)
{
	return "urn:ietf:wg:oauth:2.0:oob";
}

static void
eos_default_prepare_authentication_uri_query (EOAuth2Service *service,
					      ESource *source,
					      GHashTable *uri_query)
{
	e_oauth2_service_util_set_to_form (uri_query, "response_type", "code");
	e_oauth2_service_util_set_to_form (uri_query, "client_id", e_oauth2_service_get_client_id (service, source));
	e_oauth2_service_util_set_to_form (uri_query, "redirect_uri", e_oauth2_service_get_redirect_uri (service, source));

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;
		gchar *user;

		auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		user = e_source_authentication_dup_user (auth_extension);

		if (user && *user)
			e_oauth2_service_util_take_to_form (uri_query, "login_hint", user);
		else
			g_free (user);
	}
}

static EOAuth2ServiceNavigationPolicy
eos_default_get_authentication_policy (EOAuth2Service *service,
				       ESource *source,
				       const gchar *uri)
{
	return E_OAUTH2_SERVICE_NAVIGATION_POLICY_ALLOW;
}

static void
eos_default_prepare_get_token_form (EOAuth2Service *service,
				    ESource *source,
				    const gchar *authorization_code,
				    GHashTable *form)
{
	e_oauth2_service_util_set_to_form (form, "code", authorization_code);
	e_oauth2_service_util_set_to_form (form, "client_id", e_oauth2_service_get_client_id (service, source));
	e_oauth2_service_util_set_to_form (form, "client_secret", e_oauth2_service_get_client_secret (service, source));
	e_oauth2_service_util_set_to_form (form, "redirect_uri", e_oauth2_service_get_redirect_uri (service, source));
	e_oauth2_service_util_set_to_form (form, "grant_type", "authorization_code");
}

static void
eos_default_prepare_get_token_message (EOAuth2Service *service,
				       ESource *source,
				       SoupMessage *message)
{
}

static void
eos_default_prepare_refresh_token_form (EOAuth2Service *service,
					ESource *source,
					const gchar *refresh_token,
					GHashTable *form)
{
	e_oauth2_service_util_set_to_form (form, "refresh_token", refresh_token);
	e_oauth2_service_util_set_to_form (form, "client_id", e_oauth2_service_get_client_id (service, source));
	e_oauth2_service_util_set_to_form (form, "client_secret", e_oauth2_service_get_client_secret (service, source));
	e_oauth2_service_util_set_to_form (form, "grant_type", "refresh_token");
}

static void
eos_default_prepare_refresh_token_message (EOAuth2Service *service,
					   ESource *source,
					   SoupMessage *message)
{
}

static gboolean
eos_default_extract_authorization_code (EOAuth2Service *service,
					ESource *source,
					const gchar *page_title,
					const gchar *page_uri,
					const gchar *page_content,
					gchar **out_authorization_code)
{
	gchar *error_code = NULL, *error_message = NULL;
	gboolean success;

	g_return_val_if_fail (out_authorization_code != NULL, FALSE);

	*out_authorization_code = NULL;

	/* It is an acceptable response when either the authorization code or the error information is set. */
	success = e_oauth2_service_util_extract_from_uri (page_uri, out_authorization_code, &error_code, &error_message);

	g_free (error_code);
	g_free (error_message);

	return success;
}

static gboolean
eos_default_extract_error_message (EOAuth2Service *service,
				   ESource *source,
				   const gchar *page_title,
				   const gchar *page_uri,
				   const gchar *page_content,
				   gchar **out_error_message)
{
	gchar *error_code = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (out_error_message != NULL, FALSE);

	*out_error_message = NULL;

	/* possibly JSON data */
	if (page_content && *page_content == '{') {
		JsonParser *parser;

		parser = json_parser_new ();

		if (json_parser_load_from_data (parser, page_content, strlen (page_content), NULL)) {
			JsonReader *reader;

			reader = json_reader_new (json_parser_get_root (parser));

			if (json_reader_read_member (reader, "error")) {
				json_reader_end_member (reader);

				if (json_reader_read_member (reader, "error_description")) {
					const gchar *value = json_reader_get_string_value (reader);

					if (value && *value) {
						*out_error_message = g_strdup (value);
						success = TRUE;
					}
				}
			}

			json_reader_end_member (reader);

			g_object_unref (reader);
		}

		g_object_unref (parser);

		if (success)
			return success;
	}

	success = e_oauth2_service_util_extract_from_uri (page_uri, NULL, &error_code, out_error_message);

	if (success) {
		/* it means the user cancelled the wizard or did not allow access to the account data */
		if (g_strcmp0 (error_code, "access_denied") == 0)
			g_clear_pointer (out_error_message, g_free);
		else if (!*out_error_message)
			*out_error_message = g_steal_pointer (&error_code);

		success = *out_error_message != NULL;
	}

	g_free (error_code);

	return success;
}

static void
e_oauth2_service_default_init (EOAuth2ServiceInterface *iface)
{
	iface->can_process = eos_default_can_process;
	iface->guess_can_process = eos_default_guess_can_process;
	iface->get_flags = eos_default_get_flags;
	iface->get_redirect_uri = eos_default_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_default_prepare_authentication_uri_query;
	iface->get_authentication_policy = eos_default_get_authentication_policy;
	iface->prepare_get_token_form = eos_default_prepare_get_token_form;
	iface->prepare_get_token_message = eos_default_prepare_get_token_message;
	iface->prepare_refresh_token_form = eos_default_prepare_refresh_token_form;
	iface->prepare_refresh_token_message = eos_default_prepare_refresh_token_message;
	iface->extract_authorization_code = eos_default_extract_authorization_code;
	iface->extract_error_message = eos_default_extract_error_message;
}

/**
 * e_oauth2_service_can_process:
 * @service: an #EOAuth2Service
 * @source: an #ESource
 *
 * Checks whether the @service can be used with the given @source.
 *
 * The default implementation checks whether the @source has an #ESourceAuthentication
 * extension and when its method matches e_oauth2_service_get_name(), then it automatically
 * returns %TRUE. Contrary, when the @source contains GNOME Online Accounts or Ubuntu
 * Online Accounts extension, then it returns %FALSE.
 *
 * The default implementation is tried always as the first and when it fails, then
 * the descendant's implementation is called.
 *
 * Returns: Whether the @service can be used for the given @source
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_can_process (EOAuth2Service *service,
			      ESource *source)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (iface->can_process != NULL, FALSE);

	if (eos_default_can_process (service, source))
		return TRUE;

	return iface->can_process != eos_default_can_process &&
	       iface->can_process (service, source);
}

/**
 * e_oauth2_service_guess_can_process:
 * @service: an #EOAuth2Service
 * @protocol: (nullable): a protocol to search the service for, like "imap", or %NULL
 * @hostname: (nullable): a host name to search the service for, like "server.example.com", or %NULL
 *
 * Checks whether the @service can be used with the given @protocol and/or @hostname.
 * Any of @protocol and @hostname can be %NULL, but not both. It's up to each implementer
 * to decide, which of the arguments are important and whether all or only any of them
 * can be required.
 *
 * The function is meant to check whether the @service can be offered
 * for example when configuring a new account. The real usage is
 * determined by e_oauth2_service_can_process().
 *
 * The default implementation consults org.gnome.evolution-data-server.oauth2-services-hint
 * GSettings key against given hostname. See its description for more information.
 *
 * The default implementation is tried always as the first and when it fails, then
 * the descendant's implementation is called.
 *
 * Returns: Whether the @service can be used for the given arguments
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_guess_can_process (EOAuth2Service *service,
				    const gchar *protocol,
				    const gchar *hostname)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (protocol || hostname, FALSE);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (iface->guess_can_process != NULL, FALSE);

	if (eos_default_guess_can_process (service, protocol, hostname))
		return TRUE;

	return iface->guess_can_process != eos_default_guess_can_process &&
	       iface->guess_can_process (service, protocol, hostname);
}

/**
 * e_oauth2_service_get_flags:
 * @service: an #EOAuth2Service
 *
 * Returns: bit-or of #EOAuth2ServiceFlags for the @service. The default
 *    implementation returns %E_OAUTH2_SERVICE_FLAG_NONE.
 *
 * Since: 3.28
 **/
guint32
e_oauth2_service_get_flags (EOAuth2Service *service)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), E_OAUTH2_SERVICE_FLAG_NONE);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, E_OAUTH2_SERVICE_FLAG_NONE);
	g_return_val_if_fail (iface->get_flags != NULL, E_OAUTH2_SERVICE_FLAG_NONE);

	return iface->get_flags (service);
}

/**
 * e_oauth2_service_get_name:
 * @service: an #EOAuth2Service
 *
 * Returns a unique name of the service. It can be named for example
 * by the server or the company from which it receives the OAuth2
 * token and where it refreshes it, like "Company" for login.company.com.
 *
 * Returns: the name of the @service
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_name (EOAuth2Service *service)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_name != NULL, NULL);

	return iface->get_name (service);
}

/**
 * e_oauth2_service_get_display_name:
 * @service: an #EOAuth2Service
 *
 * Returns a human readable name of the service. This is similar to
 * e_oauth2_service_get_name(), except this string should be localized,
 * because it will be used in user-visible strings.
 *
 * Returns: the display name of the @service
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_display_name (EOAuth2Service *service)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_display_name != NULL, NULL);

	return iface->get_display_name (service);
}

/**
 * e_oauth2_service_get_client_id:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 *
 * Returns: application client ID, as provided by the server
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_client_id (EOAuth2Service *service,
				ESource *source)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_client_id != NULL, NULL);

	return iface->get_client_id (service, source);
}

/**
 * e_oauth2_service_get_client_secret:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 *
 * Returns: (nullable): application client secret, as provided by the server, or %NULL
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_client_secret (EOAuth2Service *service,
				    ESource *source)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_client_secret != NULL, NULL);

	return iface->get_client_secret (service, source);
}

/**
 * e_oauth2_service_get_authentication_uri:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 *
 * Returns: an authentication URI, to be used to obtain
 *    the authentication code
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_authentication_uri (EOAuth2Service *service,
					 ESource *source)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_authentication_uri != NULL, NULL);

	return iface->get_authentication_uri (service, source);
}

/**
 * e_oauth2_service_get_refresh_uri:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 *
 * Returns: a URI to be used to refresh the authentication token
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_refresh_uri (EOAuth2Service *service,
				  ESource *source)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_refresh_uri != NULL, NULL);

	return iface->get_refresh_uri (service, source);
}

/**
 * e_oauth2_service_get_redirect_uri:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 *
 * Returns a value for the "redirect_uri" keys in the authenticate and get_token
 * operations. The default implementation returns "urn:ietf:wg:oauth:2.0:oob".
 *
 * Returns: (nullable): The redirect_uri to use, or %NULL for none
 *
 * Since: 3.28
 **/
const gchar *
e_oauth2_service_get_redirect_uri (EOAuth2Service *service,
				   ESource *source)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (iface->get_redirect_uri != NULL, NULL);

	return iface->get_redirect_uri (service, source);
}

/**
 * e_oauth2_service_prepare_authentication_uri_query:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @uri_query: (element-type utf8 utf8): query for the URI to use
 *
 * The @service can change what arguments are passed in the authentication URI
 * in this method. The default implementation sets some values too, namely
 * "response_type", "client_id", "redirect_uri" and "login_hint", if available
 * in the @source. These parameters are always provided, even when the interface
 * implementer overrides this method.
 *
 * The @uri_query hash table expects both key and value to be newly allocated
 * strings, which will be freed together with the hash table or when the key
 * is replaced.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_prepare_authentication_uri_query (EOAuth2Service *service,
						   ESource *source,
						   GHashTable *uri_query)
{
	EOAuth2ServiceInterface *iface;

	g_return_if_fail (E_IS_OAUTH2_SERVICE (service));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (uri_query != NULL);

	eos_default_prepare_authentication_uri_query (service, source, uri_query);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_if_fail (iface != NULL);
	g_return_if_fail (iface->prepare_authentication_uri_query != NULL);

	if (iface->prepare_authentication_uri_query != eos_default_prepare_authentication_uri_query)
		iface->prepare_authentication_uri_query (service, source, uri_query);
}

/**
 * e_oauth2_service_get_authentication_policy:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @uri: a URI of the navigation resource
 *
 * Used to decide what to do when the server redirects to the next page.
 * The default implementation always returns %E_OAUTH2_SERVICE_NAVIGATION_POLICY_ALLOW.
 *
 * This method is called before e_oauth2_service_extract_authorization_code() and
 * can be used to block certain resources or to abort the authentication when
 * the server redirects to an unexpected page (like when user denies authorization
 * in the page).
 *
 * Returns: one of #EOAuth2ServiceNavigationPolicy
 *
 * Since: 3.28
 **/
EOAuth2ServiceNavigationPolicy
e_oauth2_service_get_authentication_policy (EOAuth2Service *service,
					    ESource *source,
					    const gchar *uri)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), E_OAUTH2_SERVICE_NAVIGATION_POLICY_ABORT);
	g_return_val_if_fail (E_IS_SOURCE (source), E_OAUTH2_SERVICE_NAVIGATION_POLICY_ABORT);
	g_return_val_if_fail (uri != NULL, E_OAUTH2_SERVICE_NAVIGATION_POLICY_ABORT);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, E_OAUTH2_SERVICE_NAVIGATION_POLICY_ABORT);
	g_return_val_if_fail (iface->get_authentication_policy != NULL, E_OAUTH2_SERVICE_NAVIGATION_POLICY_ABORT);

	return iface->get_authentication_policy (service, source, uri);
}

/**
 * e_oauth2_service_extract_authorization_code:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @page_title: a web page title
 * @page_uri: a web page URI
 * @page_content: (nullable): a web page content
 * @out_authorization_code: (out) (transfer full): the extracted authorization code
 *
 * Tries to extract an authorization code from a web page provided by the server.
 * The function can be called multiple times, whenever the page load is finished.
 * The default implementation uses e_oauth2_service_util_extract_from_uri() to get
 * the code from the given @page_uri.
 *
 * There can happen three states: 1) either the @service cannot determine
 * the authentication code from the page information, then the %FALSE is
 * returned and the @out_authorization_code is left untouched; or 2) the server
 * reported a failure, in which case the function returns %TRUE and lefts
 * the @out_authorization_code untouched; or 3) the @service could extract
 * the authentication code from the given arguments, then the function
 * returns %TRUE and sets the received authorization code to @out_authorization_code.
 *
 * The @page_content is %NULL, unless flags returned by e_oauth2_service_get_flags()
 * contain also %E_OAUTH2_SERVICE_FLAG_EXTRACT_REQUIRES_PAGE_CONTENT.
 *
 * This method is always called after e_oauth2_service_get_authentication_policy().
 *
 * Returns: whether could recognized successful or failed server response.
 *    The @out_authorization_code is populated on success too.
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_extract_authorization_code (EOAuth2Service *service,
					     ESource *source,
					     const gchar *page_title,
					     const gchar *page_uri,
					     const gchar *page_content,
					     gchar **out_authorization_code)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (iface->extract_authorization_code != NULL, FALSE);

	return iface->extract_authorization_code (service, source, page_title, page_uri, page_content, out_authorization_code);
}

/**
 * e_oauth2_service_extract_error_message:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @page_title: a web page title
 * @page_uri: a web page URI
 * @page_content: (nullable): a web page content
 * @out_error_message: (out) (transfer full): the extracted error message
 *
 * Tries to extract error message from the server response, return %TRUE,
 * when an error message could be found, in which case also sets
 * the @out_error_message with it. The default implementation uses
 * e_oauth2_service_util_extract_from_uri(), returning either the error
 * description or the error code, when the description is not found.
 *
 * The @out_error_message is expected to be plain text.
 *
 * Returns: whether could recognized failed server response.
 *    The @out_error_message is populated on success.
 *
 * Since: 3.48
 **/
gboolean
e_oauth2_service_extract_error_message (EOAuth2Service *service,
					ESource *source,
					const gchar *page_title,
					const gchar *page_uri,
					const gchar *page_content,
					gchar **out_error_message)
{
	EOAuth2ServiceInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_val_if_fail (iface != NULL, FALSE);

	if (!iface->extract_error_message)
		return FALSE;

	return iface->extract_error_message (service, source, page_title, page_uri, page_content, out_error_message);
}

/**
 * e_oauth2_service_prepare_get_token_form:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @authorization_code: authorization code, as returned from e_oauth2_service_extract_authorization_code()
 * @form: (element-type utf8 utf8): form parameters to be used in the POST request
 *
 * Sets additional form parameters to be used in the POST request when requesting
 * access token after successfully obtained authorization code.
 * The default implementation sets some values too, namely
 * "code", "client_id", "client_secret", "redirect_uri" and "grant_type".
 * These parameters are always provided, even when the interface implementer overrides this method.
 *
 * The @form hash table expects both key and value to be newly allocated
 * strings, which will be freed together with the hash table or when the key
 * is replaced.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_prepare_get_token_form (EOAuth2Service *service,
					 ESource *source,
					 const gchar *authorization_code,
					 GHashTable *form)
{
	EOAuth2ServiceInterface *iface;

	g_return_if_fail (E_IS_OAUTH2_SERVICE (service));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (authorization_code != NULL);
	g_return_if_fail (form != NULL);

	eos_default_prepare_get_token_form (service, source, authorization_code, form);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_if_fail (iface != NULL);
	g_return_if_fail (iface->prepare_get_token_form != NULL);

	if (iface->prepare_get_token_form != eos_default_prepare_get_token_form)
		iface->prepare_get_token_form (service, source, authorization_code, form);
}

/**
 * e_oauth2_service_prepare_get_token_message:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @message: a #SoupMessage
 *
 * The @service can change the @message before it's sent to
 * the e_oauth2_service_get_authentication_uri(), with POST data
 * being provided by e_oauth2_service_prepare_get_token_form().
 * The default implementation does nothing with the @message.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_prepare_get_token_message (EOAuth2Service *service,
					    ESource *source,
					    SoupMessage *message)
{
	EOAuth2ServiceInterface *iface;

	g_return_if_fail (E_IS_OAUTH2_SERVICE (service));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_if_fail (iface != NULL);
	g_return_if_fail (iface->prepare_get_token_message != NULL);

	iface->prepare_get_token_message (service, source, message);
}

/**
 * e_oauth2_service_prepare_refresh_token_form:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @refresh_token: a refresh token to be used
 * @form: (element-type utf8 utf8): form parameters to be used in the POST request
 *
 * Sets additional form parameters to be used in the POST request when requesting
 * to refresh an access token.
 * The default implementation sets some values too, namely
 * "refresh_token", "client_id", "client_secret" and "grant_type".
 * These parameters are always provided, even when the interface implementer overrides this method.
 *
 * The @form hash table expects both key and value to be newly allocated
 * strings, which will be freed together with the hash table or when the key
 * is replaced.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_prepare_refresh_token_form (EOAuth2Service *service,
					     ESource *source,
					     const gchar *refresh_token,
					     GHashTable *form)
{
	EOAuth2ServiceInterface *iface;

	g_return_if_fail (E_IS_OAUTH2_SERVICE (service));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (refresh_token != NULL);
	g_return_if_fail (form != NULL);

	eos_default_prepare_refresh_token_form (service, source, refresh_token, form);

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_if_fail (iface != NULL);
	g_return_if_fail (iface->prepare_refresh_token_form != NULL);

	if (iface->prepare_refresh_token_form != eos_default_prepare_refresh_token_form)
		iface->prepare_refresh_token_form (service, source, refresh_token, form);
}

/**
 * e_oauth2_service_prepare_refresh_token_message:
 * @service: an #EOAuth2Service
 * @source: an associated #ESource
 * @message: a #SoupMessage
 *
 * The @service can change the @message before it's sent to
 * the e_oauth2_service_get_refresh_uri(), with POST data
 * being provided by e_oauth2_service_prepare_refresh_token_form().
 * The default implementation does nothing with the @message.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_prepare_refresh_token_message (EOAuth2Service *service,
						ESource *source,
						SoupMessage *message)
{
	EOAuth2ServiceInterface *iface;

	g_return_if_fail (E_IS_OAUTH2_SERVICE (service));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	iface = E_OAUTH2_SERVICE_GET_INTERFACE (service);
	g_return_if_fail (iface != NULL);
	g_return_if_fail (iface->prepare_refresh_token_message != NULL);

	iface->prepare_refresh_token_message (service, source, message);
}

static SoupSession *
eos_create_soup_session (EOAuth2ServiceRefSourceFunc ref_source,
			 gpointer ref_source_user_data,
			 ESource *source)
{
	static gint oauth2_debug = -1;
	ESourceAuthentication *auth_extension;
	ESource *proxy_source = NULL;
	SoupSession *session;
	gchar *uid;

	if (oauth2_debug == -1)
		oauth2_debug = g_strcmp0 (g_getenv ("OAUTH2_DEBUG"), "1") == 0 ? 1 : 0;

	session = soup_session_new ();
	g_object_set (
		session,
		"timeout", 90,
		"accept-language-auto", TRUE,
		NULL);

	if (oauth2_debug) {
		SoupLogger *logger;

		logger = soup_logger_new (SOUP_LOGGER_LOG_BODY);
		soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
		g_object_unref (logger);
	}

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		return session;

	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	uid = e_source_authentication_dup_proxy_uid (auth_extension);
	if (uid) {
		proxy_source = ref_source (ref_source_user_data, uid);

		g_free (uid);
	}

	if (proxy_source) {
		GProxyResolver *proxy_resolver;

		proxy_resolver = G_PROXY_RESOLVER (proxy_source);
		if (g_proxy_resolver_is_supported (proxy_resolver))
			g_object_set (session, "proxy-resolver", proxy_resolver, NULL);

		g_object_unref (proxy_source);
	}

	return session;
}

static SoupMessage *
eos_create_soup_message (ESource *source,
			 const gchar *uri,
			 GHashTable *post_form)
{
	SoupMessage *message;
	gchar *post_data;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (post_form != NULL, NULL);

	message = soup_message_new (SOUP_METHOD_POST, uri);
	g_return_val_if_fail (message != NULL, NULL);

	post_data = soup_form_encode_hash (post_form);
	if (!post_data) {
		g_warn_if_fail (post_data != NULL);
		g_object_unref (message);

		return NULL;
	}

	e_soup_session_util_set_message_request_body_from_data (message, FALSE, "application/x-www-form-urlencoded", post_data, strlen (post_data), g_free);
	e_soup_ssl_trust_connect (message, source);

	soup_message_headers_append (soup_message_get_request_headers (message), "Connection", "close");

	return message;
}

static void
eos_abort_session_cb (GCancellable *cancellable,
		      SoupSession *session)
{
	soup_session_abort (session);
}

static gboolean
eos_send_message (SoupSession *session,
		  SoupMessage *message,
		  GBytes **out_response_body,
		  GCancellable *cancellable,
		  GError **error)
{
	guint status_code = 0;
	GBytes *response_body = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (SOUP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_response_body != NULL, FALSE);

	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		gulong cancel_handler_id = 0;

		if (cancellable)
			cancel_handler_id = g_cancellable_connect (cancellable, G_CALLBACK (eos_abort_session_cb), session, NULL);

		response_body = soup_session_send_and_read (session, message, cancellable, error);

		if (cancel_handler_id)
			g_cancellable_disconnect (cancellable, cancel_handler_id);

		status_code = soup_message_get_status (message);
	}

	if (SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		if (response_body) {
			*out_response_body = g_steal_pointer (&response_body);
			success = TRUE;
		} else {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Malformed, no message body set"));
		}
	} else if (status_code != 0) {
		GString *error_msg;

		error_msg = g_string_new (soup_message_get_reason_phrase (message));
		if (response_body && g_bytes_get_size (response_body)) {
			g_string_append (error_msg, " (");
			g_string_append_len (error_msg, (const gchar *) g_bytes_get_data (response_body, NULL), g_bytes_get_size (response_body));
			g_string_append_c (error_msg, ')');

			*out_response_body = g_steal_pointer (&response_body);
		}

		g_set_error_literal (error, E_SOUP_SESSION_ERROR, soup_message_get_status (message), error_msg->str);

		g_string_free (error_msg, TRUE);
	}

	g_clear_pointer (&response_body, g_bytes_unref);

	return success;
}

static gboolean
eos_generate_secret_uid (EOAuth2Service *service,
			 ESource *source,
			 gchar **out_uid)
{
	ESourceAuthentication *authentication_extension;
	gchar *user;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (out_uid)
		*out_uid = NULL;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		return FALSE;

	authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	user = e_source_authentication_dup_user (authentication_extension);
	if (!user || !*user) {
		g_free (user);
		return FALSE;
	}

	if (out_uid)
		*out_uid = g_strdup_printf ("OAuth2::%s[%s]", e_oauth2_service_get_name (service), user);

	g_free (user);

	return TRUE;
}

static gboolean
eos_encode_to_secret (gchar **out_secret,
		      const gchar *key1_name,
		      const gchar *value1,
		      ...) G_GNUC_NULL_TERMINATED;

static gboolean
eos_encode_to_secret (gchar **out_secret,
		      const gchar *key1_name,
		      const gchar *value1,
		      ...)
{
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
}

static gboolean
eos_decode_from_secret (const gchar *secret,
			gssize length,
			const gchar *key1_name,
			gchar **out_value1,
			...) G_GNUC_NULL_TERMINATED;

static gboolean
eos_decode_from_secret (const gchar *secret,
			gssize length,
			const gchar *key1_name,
			gchar **out_value1,
			...)
{
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
	if (!json_parser_load_from_data (parser, secret, length, &error)) {
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
}

static gboolean
eos_store_token_sync (EOAuth2Service *service,
		      ESource *source,
		      const gchar *refresh_token,
		      const gchar *access_token,
		      const gchar *expires_in,
		      GCancellable *cancellable,
		      GError **error)
{
	gint64 expires_after_tm;
	gchar *expires_after, *secret = NULL, *uid = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);

	if (!refresh_token || !access_token || !expires_in)
		return FALSE;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	expires_after_tm = g_get_real_time () / G_USEC_PER_SEC;
	expires_after_tm += g_ascii_strtoll (expires_in, NULL, 10);
	expires_after = g_strdup_printf ("%" G_GINT64_FORMAT, expires_after_tm);

	if (eos_encode_to_secret (&secret,
		E_OAUTH2_SECRET_REFRESH_TOKEN, refresh_token,
		E_OAUTH2_SECRET_ACCESS_TOKEN, access_token,
		E_OAUTH2_SECRET_EXPIRES_AFTER, expires_after, NULL) &&
	    eos_generate_secret_uid (service, source, &uid)) {
		gchar *label;

		label = g_strdup_printf ("Evolution Data Source - %s", strstr (uid, "::") + 2);

		success = e_secret_store_store_sync (uid, secret, label, TRUE, cancellable, error);

		g_free (label);
	}

	g_free (uid);
	g_free (secret);
	g_free (expires_after);

	return success;
}

/* Can return success when the access token is already expired and refresh token is available */
static gboolean
eos_lookup_token_sync (EOAuth2Service *service,
		       ESource *source,
		       gchar **out_refresh_token,
		       gchar **out_access_token,
		       gint *out_expires_in,
		       GCancellable *cancellable,
		       GError **error)
{
	gchar *secret = NULL, *uid = NULL, *expires_after = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (out_refresh_token != NULL, FALSE);
	g_return_val_if_fail (out_access_token != NULL, FALSE);
	g_return_val_if_fail (out_expires_in != NULL, FALSE);

	*out_refresh_token = NULL;
	*out_access_token = NULL;
	*out_expires_in = -1;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (!eos_generate_secret_uid (service, source, &uid)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			/* Translators: The first %s is a display name of the source, the second is its UID and
			   the third is the name of the OAuth service. */
			_("Source “%s” (%s) is not valid for “%s” OAuth2 service"),
			e_source_get_display_name (source),
			e_source_get_uid (source),
			e_oauth2_service_get_name (service));
		return FALSE;
	}

	if (!e_secret_store_lookup_sync (uid, &secret, cancellable, error)) {
		g_free (uid);
		return FALSE;
	}

	g_free (uid);

	if (!secret) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("OAuth2 secret not found"));
		return FALSE;
	}

	success = eos_decode_from_secret (secret, -1,
		E_OAUTH2_SECRET_REFRESH_TOKEN, out_refresh_token,
		E_OAUTH2_SECRET_ACCESS_TOKEN, out_access_token,
		E_OAUTH2_SECRET_EXPIRES_AFTER, &expires_after,
		NULL);

	if (success && expires_after) {
		gint64 num_expires_after, num_now;

		num_expires_after = g_ascii_strtoll (expires_after, NULL, 10);
		num_now = g_get_real_time () / G_USEC_PER_SEC;

		if (num_now < num_expires_after)
			*out_expires_in = num_expires_after - num_now - 1;
	}

	success = success && *out_refresh_token != NULL;

	if (!success) {
		g_clear_pointer (out_refresh_token, e_util_safe_free_string);
		g_clear_pointer (out_access_token, e_util_safe_free_string);
	}

	e_util_safe_free_string (secret);
	g_free (expires_after);

	return success;
}

/**
 * e_oauth2_service_receive_and_store_token_sync:
 * @service: an #EOAuth2Service
 * @source: an #ESource
 * @authorization_code: authorization code provided by the server
 * @ref_source: (scope call): an #EOAuth2ServiceRefSourceFunc function to obtain an #ESource
 * @ref_source_user_data: user data for @ref_source
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Queries @service at e_oauth2_service_get_refresh_uri() with a request to obtain
 * a new access token, associated with the given @authorization_code and stores
 * it into the secret store on success.
 *
 * Returns: whether succeeded
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_receive_and_store_token_sync (EOAuth2Service *service,
					       ESource *source,
					       const gchar *authorization_code,
					       EOAuth2ServiceRefSourceFunc ref_source,
					       gpointer ref_source_user_data,
					       GCancellable *cancellable,
					       GError **error)
{
	SoupSession *session;
	SoupMessage *message;
	GHashTable *post_form;
	GBytes *response_json = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (authorization_code != NULL, FALSE);
	g_return_val_if_fail (ref_source != NULL, FALSE);

	session = eos_create_soup_session (ref_source, ref_source_user_data, source);
	if (!session)
		return FALSE;

	post_form = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	e_oauth2_service_prepare_get_token_form (service, source, authorization_code, post_form);

	message = eos_create_soup_message (source, e_oauth2_service_get_refresh_uri (service, source), post_form);

	g_hash_table_destroy (post_form);

	if (!message) {
		g_object_unref (session);
		return FALSE;
	}

	e_oauth2_service_prepare_get_token_message (service, source, message);

	success = eos_send_message (session, message, &response_json, cancellable, error);
	if (success) {
		gchar *access_token = NULL, *refresh_token = NULL, *expires_in = NULL, *token_type = NULL;

		if (eos_decode_from_secret (g_bytes_get_data (response_json, NULL), g_bytes_get_size (response_json),
			"access_token", &access_token,
			"refresh_token", &refresh_token,
			"expires_in", &expires_in,
			"token_type", &token_type,
			NULL) && access_token && refresh_token && expires_in && token_type) {

			g_warn_if_fail (g_ascii_strcasecmp (token_type, "Bearer") == 0);

			success = eos_store_token_sync (service, source,
				refresh_token, access_token, expires_in, cancellable, error);
		} else {
			success = FALSE;
		}

		e_util_safe_free_string (access_token);
		e_util_safe_free_string (refresh_token);
		g_free (expires_in);
		g_free (token_type);
	}

	g_object_unref (message);
	g_object_unref (session);
	if (response_json)
		g_bytes_unref (response_json);

	return success;
}

/**
 * e_oauth2_service_refresh_and_store_token_sync:
 * @service: an #EOAuth2Service
 * @source: an #ESource
 * @refresh_token: refresh token as provided by the server
 * @ref_source: (scope call): an #EOAuth2ServiceRefSourceFunc function to obtain an #ESource
 * @ref_source_user_data: user data for @ref_source
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Queries @service at e_oauth2_service_get_refresh_uri() with a request to refresh
 * existing access token with provided @refresh_token and stores it into the secret
 * store on success.
 *
 * Returns: whether succeeded
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_refresh_and_store_token_sync (EOAuth2Service *service,
					       ESource *source,
					       const gchar *refresh_token,
					       EOAuth2ServiceRefSourceFunc ref_source,
					       gpointer ref_source_user_data,
					       GCancellable *cancellable,
					       GError **error)
{
	SoupSession *session;
	SoupMessage *message;
	GHashTable *post_form;
	GBytes *response_json = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (refresh_token != NULL, FALSE);
	g_return_val_if_fail (ref_source != NULL, FALSE);

	session = eos_create_soup_session (ref_source, ref_source_user_data, source);
	if (!session)
		return FALSE;

	post_form = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	e_oauth2_service_prepare_refresh_token_form (service, source, refresh_token, post_form);

	message = eos_create_soup_message (source, e_oauth2_service_get_refresh_uri (service, source), post_form);

	g_hash_table_destroy (post_form);

	if (!message) {
		g_object_unref (session);
		return FALSE;
	}

	e_oauth2_service_prepare_refresh_token_message (service, source, message);

	success = eos_send_message (session, message, &response_json, cancellable, &local_error);

	if (success) {
		gchar *access_token = NULL, *expires_in = NULL, *new_refresh_token = NULL;

		if (eos_decode_from_secret (g_bytes_get_data (response_json, NULL), g_bytes_get_size (response_json),
			"access_token", &access_token,
			"expires_in", &expires_in,
			"refresh_token", &new_refresh_token,
			NULL) && access_token && expires_in) {
			success = eos_store_token_sync (service, source,
				(new_refresh_token && *new_refresh_token) ? new_refresh_token : refresh_token,
				access_token, expires_in, cancellable, error);
		} else {
			success = FALSE;

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Received incorrect response from server “%s”."),
				e_oauth2_service_get_refresh_uri (service, source));
		}

		e_util_safe_free_string (access_token);
		g_free (new_refresh_token);
		g_free (expires_in);
	} else if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_BAD_REQUEST)) {
		gchar *detailed_error = NULL;

		if (response_json && g_bytes_get_size (response_json) > 0) {
			gchar *str;

			str = g_strndup ((const gchar *) g_bytes_get_data (response_json, NULL), g_bytes_get_size (response_json));

			if (!e_oauth2_service_extract_error_message (service, source, NULL, NULL, str, &detailed_error))
				detailed_error = NULL;

			g_free (str);
		}

		if (detailed_error && *detailed_error) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED,
				_("Failed to refresh access token. Sign to the server again, please.\n\nDetailed error: %s"), detailed_error);
		} else {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED,
				_("Failed to refresh access token. Sign to the server again, please."));
		}

		g_clear_error (&local_error);
		g_free (detailed_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	g_object_unref (message);
	g_object_unref (session);
	if (response_json)
		g_bytes_unref (response_json);

	return success;
}

/**
 * e_oauth2_service_delete_token_sync:
 * @service: an #EOAuth2Service
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes token information for the @service and @source from the secret store.
 *
 * Returns: whether succeeded
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_delete_token_sync (EOAuth2Service *service,
				    ESource *source,
				    GCancellable *cancellable,
				    GError **error)
{
	gchar *uid = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (!eos_generate_secret_uid (service, source, &uid)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			/* Translators: The first %s is a display name of the source, the second is its UID. */
			_("Source “%s” (%s) is not a valid OAuth2 source"),
			e_source_get_display_name (source),
			e_source_get_uid (source));
		return FALSE;
	}

	success = e_secret_store_delete_sync (uid, cancellable, error);

	g_free (uid);

	return success;
}

G_LOCK_DEFINE_STATIC (access_token_requests);
static GHashTable *access_token_requests = NULL;
static guint access_token_requests_free_id = 0;

static gboolean
eos_free_access_token_requests_timeout_cb (gpointer user_data)
{
	G_LOCK (access_token_requests);

	if (access_token_requests && !g_hash_table_size (access_token_requests)) {
		g_hash_table_destroy (access_token_requests);
		access_token_requests = NULL;
		access_token_requests_free_id = 0;
	} else if (g_main_current_source () && access_token_requests_free_id == g_source_get_id (g_main_current_source ())) {
		access_token_requests_free_id = 0;
	}

	G_UNLOCK (access_token_requests);

	return FALSE;
}

typedef enum {
	RESPONSE_UNKNOWN,
	RESPONSE_SUCCESS,
	RESPONSE_FAILURE
} ResponseCode;

typedef struct _AccessTokenRequest {
	gint ref_count;
	gboolean finished;
	GCond cond;
	GMutex mutex;
	gchar *access_token;
	gint expires_in;
	GError *error;
} AccessTokenRequest;

static AccessTokenRequest *
access_token_request_new (void)
{
	AccessTokenRequest *atr;

	atr = g_slice_new0 (AccessTokenRequest);
	atr->ref_count = 1;
	atr->finished = FALSE;
	g_cond_init (&atr->cond);
	g_mutex_init (&atr->mutex);

	return atr;
}

static void
access_token_request_ref (AccessTokenRequest *atr)
{
	g_return_if_fail (atr != NULL);
	g_atomic_int_inc (&atr->ref_count);
}

static void
access_token_request_unref (AccessTokenRequest *atr)
{
	g_return_if_fail (atr != NULL);

	if (g_atomic_int_dec_and_test (&atr->ref_count)) {
		g_cond_clear (&atr->cond);
		g_mutex_clear (&atr->mutex);
		g_clear_pointer (&atr->access_token, e_util_safe_free_string);
		g_clear_error (&atr->error);
		g_slice_free (AccessTokenRequest, atr);
	}
}

/* Hold access_token_requests when calling this */
static ResponseCode
eos_wait_for_access_token_request_locked (ESource *source,
					  gchar **out_access_token,
					  gint *out_expires_in,
					  GCancellable *cancellable,
					  GError **out_error)
{
	AccessTokenRequest *atr;
	ResponseCode resp = RESPONSE_UNKNOWN;

	if (!access_token_requests)
		return RESPONSE_UNKNOWN;

	atr = g_hash_table_lookup (access_token_requests, e_source_get_uid (source));
	if (!atr)
		return RESPONSE_UNKNOWN;

	access_token_request_ref (atr);

	G_UNLOCK (access_token_requests);

	g_mutex_lock (&atr->mutex);

	while (!atr->finished) {
		/* Check once per second whether this request was not cancelled meanwhile */
		g_cond_wait_until (&atr->cond, &atr->mutex, g_get_monotonic_time () + G_TIME_SPAN_SECOND);

		if (g_cancellable_set_error_if_cancelled (cancellable, out_error)) {
			resp = RESPONSE_FAILURE;
			break;
		}
	}

	g_mutex_unlock (&atr->mutex);

	G_LOCK (access_token_requests);

	if (resp == RESPONSE_UNKNOWN && !g_error_matches (atr->error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_warn_if_fail (atr->finished);

		if (atr->error) {
			resp = RESPONSE_FAILURE;
			if (out_error)
				g_propagate_error (out_error, g_error_copy (atr->error));
		} else {
			resp = RESPONSE_SUCCESS;
			*out_access_token = g_strdup (atr->access_token);
			*out_expires_in = atr->expires_in;
		}
	}

	access_token_request_unref (atr);

	return resp;
}

/* Hold access_token_requests when calling this */
static void
eos_reserve_access_token_request_locked (ESource *source)
{
	AccessTokenRequest *atr;

	if (!access_token_requests) {
		access_token_requests = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) access_token_request_unref);
	} else if (access_token_requests_free_id > 0) {
		g_source_remove (access_token_requests_free_id);
		access_token_requests_free_id = 0;
	}

	g_warn_if_fail (!g_hash_table_contains (access_token_requests, e_source_get_uid (source)));

	atr = access_token_request_new ();

	g_hash_table_insert (access_token_requests, e_source_dup_uid (source), atr);
}

/* Hold access_token_requests when calling this */
static void
eos_finish_access_token_request_locked (ESource *source,
					const gchar *access_token,
					gint expires_in,
					const GError *error)
{
	AccessTokenRequest *atr;

	g_return_if_fail (access_token_requests != NULL);

	atr = g_hash_table_lookup (access_token_requests, e_source_get_uid (source));
	g_return_if_fail (atr != NULL);

	g_mutex_lock (&atr->mutex);
	g_warn_if_fail (!atr->finished);
	if (atr->finished) {
		g_mutex_unlock (&atr->mutex);
		return;
	}

	atr->access_token = g_strdup (access_token);
	atr->expires_in = expires_in;
	if (error)
		atr->error = g_error_copy (error);
	atr->finished = TRUE;

	g_cond_broadcast (&atr->cond);
	g_mutex_unlock (&atr->mutex);

	g_hash_table_remove (access_token_requests, e_source_get_uid (source));

	/* Free the hash table a minute after it had been used the last time */
	if (!g_hash_table_size (access_token_requests))
		access_token_requests_free_id = g_timeout_add_seconds (60, eos_free_access_token_requests_timeout_cb, NULL);
}

/**
 * e_oauth2_service_get_access_token_sync:
 * @service: an #EOAuth2Service
 * @source: an #ESource
 * @ref_source: (scope call): an #EOAuth2ServiceRefSourceFunc function to obtain an #ESource
 * @ref_source_user_data: user data for @ref_source
 * @out_access_token: (out) (transfer full): return location for the access token
 * @out_expires_in: (out): how many seconds the access token expires in
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads access token information from the secret store for the @source and
 * in case it's expired it refreshes the token, if possible.
 *
 * Free the returned @out_access_token with g_free(), when no longer needed.
 *
 * Returns: %TRUE, when the returned access token has been set and it's not expired,
 *    %FALSE otherwise.
 *
 * Since: 3.28
 **/
gboolean
e_oauth2_service_get_access_token_sync (EOAuth2Service *service,
					ESource *source,
					EOAuth2ServiceRefSourceFunc ref_source,
					gpointer ref_source_user_data,
					gchar **out_access_token,
					gint *out_expires_in,
					GCancellable *cancellable,
					GError **error)
{
	ResponseCode resp;
	gchar *refresh_token = NULL;
	gchar *local_access_token = NULL;
	gint local_expires_in = 0;
	GError *local_error = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (ref_source != NULL, FALSE);
	g_return_val_if_fail (out_access_token != NULL, FALSE);
	g_return_val_if_fail (out_expires_in != NULL, FALSE);

	G_LOCK (access_token_requests);
	resp = eos_wait_for_access_token_request_locked (source, out_access_token, out_expires_in, cancellable, error);
	if (resp != RESPONSE_UNKNOWN && resp != RESPONSE_FAILURE && *out_expires_in <= TOKEN_VALIDITY_GAP_SECS) {
		*out_expires_in = 0;
	} else if (resp != RESPONSE_UNKNOWN) {
		G_UNLOCK (access_token_requests);

		return resp != RESPONSE_FAILURE;
	}

	eos_reserve_access_token_request_locked (source);

	G_UNLOCK (access_token_requests);

	if (!eos_lookup_token_sync (service, source, &refresh_token, &local_access_token, &local_expires_in, cancellable, &local_error)) {
		G_LOCK (access_token_requests);
		eos_finish_access_token_request_locked (source, NULL, 0, local_error);
		g_propagate_error (error, local_error);
		G_UNLOCK (access_token_requests);
		return FALSE;
	}

	if (local_expires_in <= TOKEN_VALIDITY_GAP_SECS && refresh_token) {
		success = e_oauth2_service_refresh_and_store_token_sync (service, source, refresh_token,
			ref_source, ref_source_user_data, cancellable, &local_error);

		g_clear_pointer (&refresh_token, e_util_safe_free_string);
		g_clear_pointer (&local_access_token, e_util_safe_free_string);

		success = success && eos_lookup_token_sync (service, source, &refresh_token, &local_access_token, &local_expires_in, cancellable, &local_error);
	}

	e_util_safe_free_string (refresh_token);

	if (success && local_expires_in <= 0) {
		g_clear_pointer (&local_access_token, e_util_safe_free_string);

		success = FALSE;

		g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED,
			_("The access token is expired and it failed to refresh it. Sign to the server again, please."));
	}

	G_LOCK (access_token_requests);
	if (success) {
		g_warn_if_fail (local_error == NULL);
		eos_finish_access_token_request_locked (source, local_access_token, local_expires_in, NULL);
		*out_access_token = local_access_token;
		*out_expires_in = local_expires_in;
	} else {
		g_warn_if_fail (local_access_token == NULL);
		eos_finish_access_token_request_locked (source, NULL, 0, local_error);
		g_propagate_error (error, local_error);
	}
	G_UNLOCK (access_token_requests);

	return success;
}

/**
 * e_oauth2_service_util_set_to_form:
 * @form: (element-type utf8 utf8): a #GHashTable
 * @name: a property name
 * @value: (nullable): a property value
 *
 * Sets @value for @name to @form. The @form should be
 * the one used in e_oauth2_service_prepare_authentication_uri_query(),
 * e_oauth2_service_prepare_get_token_form() or
 * e_oauth2_service_prepare_refresh_token_form().
 *
 * If the @value is %NULL, then the property named @name is removed
 * from the @form instead.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_util_set_to_form (GHashTable *form,
				   const gchar *name,
				   const gchar *value)
{
	g_return_if_fail (form != NULL);
	g_return_if_fail (name != NULL);

	if (value)
		g_hash_table_insert (form, g_strdup (name), g_strdup (value));
	else
		g_hash_table_remove (form, name);
}

/**
 * e_oauth2_service_util_take_to_form:
 * @form: (element-type utf8 utf8): a #GHashTable
 * @name: a property name
 * @value: (transfer full) (nullable): a property value
 *
 * Takes ownership of @value and sets it for @name to @form. The @value
 * will be freed with g_free(), when no longer needed. The @form should be
 * the one used in e_oauth2_service_prepare_authentication_uri_query(),
 * e_oauth2_service_prepare_get_token_form() or
 * e_oauth2_service_prepare_refresh_token_form().
 *
 * If the @value is %NULL, then the property named @name is removed
 * from the @form instead.
 *
 * Since: 3.28
 **/
void
e_oauth2_service_util_take_to_form (GHashTable *form,
				    const gchar *name,
				    gchar *value)
{
	g_return_if_fail (form != NULL);
	g_return_if_fail (name != NULL);

	if (value)
		g_hash_table_insert (form, g_strdup (name), value);
	else
		g_hash_table_remove (form, name);
}

static gboolean
eos_util_extract_from_form (GHashTable *form,
			    gchar **out_authorization_code,
			    gchar **out_error_code,
			    gchar **out_error_description)
{
	gboolean any_set = FALSE;
	const gchar *value;

	if (!form)
		return FALSE;

	value = g_hash_table_lookup (form, "code");

	if (value && *value && out_authorization_code) {
		any_set = TRUE;
		*out_authorization_code = g_strdup (value);
	} else {
		value = g_hash_table_lookup (form, "error");

		if (value && *value && out_error_code) {
			any_set = TRUE;
			*out_error_code = g_strdup (value);
		}

		value = g_hash_table_lookup (form, "error_description");

		if (value && *value && out_error_description) {
			any_set = TRUE;
			*out_error_description = g_strdup (value);
		}
	}

	return any_set;
}

/**
 * e_oauth2_service_util_extract_from_uri:
 * @in_uri: a URI returned from the server
 * @out_authorization_code: (optional) (out) (transfer full) (nullable): extracted authorization code, can be %NULL
 * @out_error_code: (optional) (out) (transfer full) (nullable): extracted error code, can be %NULL
 * @out_error_description: (optional) (out) (transfer full) (nullable): extracted error description, can be %NULL
 *
 * Extracts either an authorization code from a 'code' argument of the @in_uri,
 * or an error code from an 'error' argument of the @in_uri and an error description
 * from the 'error_description' argument of the @in_uri.
 *
 * Returns: %TRUE, when any of the non-NULL out arguments had been populated.
 *
 * Since: 3.48
 **/
gboolean
e_oauth2_service_util_extract_from_uri (const gchar *in_uri,
					gchar **out_authorization_code,
					gchar **out_error_code,
					gchar **out_error_description)
{
	GUri *uri;
	gboolean any_set = FALSE;

	if (!in_uri || !*in_uri)
		return FALSE;

	uri = g_uri_parse (in_uri, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
	if (!uri)
		return FALSE;

	if (g_uri_get_query (uri)) {
		GHashTable *form = soup_form_decode (g_uri_get_query (uri));

		if (form) {
			any_set = eos_util_extract_from_form (form, out_authorization_code, out_error_code, out_error_description);

			g_hash_table_unref (form);
		}
	}

	if (!any_set && g_uri_get_fragment (uri)) {
		GHashTable *form = soup_form_decode (g_uri_get_fragment (uri));

		if (form) {
			any_set = eos_util_extract_from_form (form, out_authorization_code, out_error_code, out_error_description);

			g_hash_table_unref (form);
		}
	}

	g_uri_unref (uri);

	return any_set;
}

/**
 * e_oauth2_service_util_compile_value:
 * @compile_value: a value provided in the compile time
 * @out_glob_buff: (out caller-allocates): a global buffer to store the processed value to
 * @out_glob_buff_size: size of the @out_glob_buff
 *
 * Processes the @compile_value and returns the result, which is stored
 * into the @out_glob_buff. The @out_glob_buff should be large enough to hold
 * the processed value and it should be a global memory buffer (usually
 * statically allocated) initialized to 0, which is used to short-circuit
 * the call, because the processing is done only if the first element
 * of the @out_glob_buff is 0, in all other cases the function
 * immediately returns the @out_glob_buff.
 *
 * Returns: processed @compile_value, saved into *out_glob_buff
 *
 * Since: 3.46
 **/
#else  /* !BUILDING_VALUE_HELPER */
static /* to not claim missing prototype */
#endif /* !BUILDING_VALUE_HELPER */
const gchar *
e_oauth2_service_util_compile_value (const gchar *compile_value,
				     gchar *out_glob_buff,
				     gsize out_glob_buff_size)
{
	G_LOCK_DEFINE_STATIC (lock);

	g_return_val_if_fail (out_glob_buff != NULL, NULL);
	g_return_val_if_fail (out_glob_buff_size > 0, NULL);

	if (!compile_value || !*compile_value) {
		out_glob_buff[0] = '\0';
		return out_glob_buff;
	}

	G_LOCK (lock);

	if (!*out_glob_buff) {
		if (g_str_has_prefix (compile_value, "|") &&
		    g_str_has_suffix (compile_value, "|") && compile_value[1]) {
			gchar *tmp = g_strndup (compile_value + 1, strlen (compile_value) - 2);
			guchar *data;
			gsize data_len = 0;

			data = g_base64_decode (tmp, &data_len);
			if (!data) {
				g_warning ("Failed to decode base64 data");
			} else if (!data_len) {
				/* Nothing to decode */
			} else if (out_glob_buff_size < data_len) {
				g_warning ("global buffer size (%" G_GSIZE_FORMAT ") is not large enough, requires at least %" G_GSIZE_FORMAT " bytes",
					out_glob_buff_size, (gsize) data_len);
			} else {
				guchar rval = data[data_len - 1];
				guint ii;

				for (ii = 0; ii < data_len; ii++) {
					out_glob_buff[ii] = data[ii] ^ rval;
				}
			}
			g_free (data);
			g_free (tmp);
		} else if (out_glob_buff_size < strlen (compile_value) + 1) {
			g_warning ("global buffer size (%" G_GSIZE_FORMAT ") is not large enough, requires at least %" G_GSIZE_FORMAT " bytes",
				out_glob_buff_size, (gsize) (strlen (compile_value) + 1));
		} else {
			strcpy (out_glob_buff, compile_value);
		}
	}

	G_UNLOCK (lock);

	return out_glob_buff;
}

#ifdef BUILDING_VALUE_HELPER
#include <stdio.h>

gint
main (void)
{
	gchar chr;
	GString *str;

	str = g_string_new ("");

	g_random_set_seed ((gint) (g_get_monotonic_time () + g_get_real_time ()));

	while (chr = fgetc (stdin), !feof (stdin) || str->len) {
		if (chr == '\n' || feof (stdin)) {
			if (str->len) {
				GByteArray *array;
				const gchar *processed;
				gchar *b64, *res, *test_buff;
				gsize test_buff_size;
				guchar rval;
				guint ii;

				while (rval = g_random_int_range (1, 255), !rval) {
					;
				}

				array = g_byte_array_new ();

				for (ii = 0; ii < str->len; ii++) {
					guchar val = (guchar) str->str[ii];
					val = val ^ rval;
					g_byte_array_append (array, &val, 1);
				}

				g_byte_array_append (array, &rval, 1);

				b64 = g_base64_encode (array->data, array->len);
				res = g_strconcat ("|", b64, "|", NULL);

				g_byte_array_unref (array);
				g_free (b64);

				test_buff_size = strlen (res) + 1;
				test_buff = g_malloc0 (sizeof (gchar) * test_buff_size);

				processed = e_oauth2_service_util_compile_value (res, test_buff, test_buff_size);
				if (g_strcmp0 (processed, str->str) != 0) {
					g_warning ("Failed to de-process '%s', stopping", str->str);
					g_free (test_buff);
					g_free (res);
					break;
				}

				processed = e_oauth2_service_util_compile_value (res, test_buff, test_buff_size);
				if (g_strcmp0 (processed, str->str) != 0) {
					g_warning ("Failed to de-process '%s' for the second call, stopping", str->str);
					g_free (test_buff);
					g_free (res);
					break;
				}

				printf ("%s ~> %s\n", str->str, res);

				g_free (test_buff);
				g_free (res);

				g_string_truncate (str, 0);
			}
		} else if (chr != '\t' && chr != '\r' && chr != ' ') {
			g_string_append_c (str, chr);
		}
	}

	g_string_free (str, TRUE);

	return 0;
}

#endif /* BUILDING_VALUE_HELPER */
