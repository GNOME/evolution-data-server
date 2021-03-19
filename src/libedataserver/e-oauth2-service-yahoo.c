/*
 * Copyright (C) 2021 Red Hat (www.redhat.com)
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

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "e-oauth2-service.h"
#include "e-oauth2-service-base.h"

#include "e-oauth2-service-yahoo.h"

/* https://developer.yahoo.com/oauth2/guide/openid_connect/getting_started.html */

/* Forward Declarations */
static void e_oauth2_service_yahoo_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EOAuth2ServiceYahoo, e_oauth2_service_yahoo, E_TYPE_OAUTH2_SERVICE_BASE,
	G_IMPLEMENT_INTERFACE (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_yahoo_oauth2_service_init))

static gboolean
eos_yahoo_guess_can_process (EOAuth2Service *service,
			      const gchar *protocol,
			      const gchar *hostname)
{
	return hostname &&
		e_util_utf8_strstrcase (hostname, ".yahoo.com");
}

static const gchar *
eos_yahoo_get_name (EOAuth2Service *service)
{
	return "Yahoo";
}

static const gchar *
eos_yahoo_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "Yahoo!");
}

static const gchar *
eos_yahoo_read_settings (EOAuth2Service *service,
			 const gchar *key_name)
{
	G_LOCK_DEFINE_STATIC (user_settings);
	gchar *value;

	G_LOCK (user_settings);

	value = g_object_get_data (G_OBJECT (service), key_name);
	if (!value) {
		GSettings *settings;

		settings = g_settings_new ("org.gnome.evolution-data-server");
		value = g_settings_get_string (settings, key_name);
		g_object_unref (settings);

		if (value && *value) {
			g_object_set_data_full (G_OBJECT (service), key_name, value, g_free);
		} else {
			g_free (value);
			value = (gchar *) "";

			g_object_set_data (G_OBJECT (service), key_name, value);
		}
	}

	G_UNLOCK (user_settings);

	return value;
}

static const gchar *
eos_yahoo_get_client_id (EOAuth2Service *service,
			 ESource *source)
{
	const gchar *client_id;

	client_id = eos_yahoo_read_settings (service, "oauth2-yahoo-client-id");

	if (client_id && *client_id)
		return client_id;

	return YAHOO_CLIENT_ID;
}

static const gchar *
eos_yahoo_get_client_secret (EOAuth2Service *service,
			     ESource *source)
{
	const gchar *client_secret;

	client_secret = eos_yahoo_read_settings (service, "oauth2-yahoo-client-secret");

	if (client_secret && *client_secret)
		return client_secret;

	return YAHOO_CLIENT_SECRET;
}

static const gchar *
eos_yahoo_get_authentication_uri (EOAuth2Service *service,
				  ESource *source)
{
	return "https://api.login.yahoo.com/oauth2/request_auth";
}

static const gchar *
eos_yahoo_get_refresh_uri (EOAuth2Service *service,
			   ESource *source)
{
	return "https://api.login.yahoo.com/oauth2/get_token";
}

static const gchar *
eos_yahoo_get_redirect_uri (EOAuth2Service *service,
			    ESource *source)
{
	return "https://wiki.gnome.org/Apps/Evolution/YahooOAuth2/";
}

static void
eos_yahoo_prepare_authentication_uri_query (EOAuth2Service *service,
					    ESource *source,
					    GHashTable *uri_query)
{
	const gchar *YAHOO_SCOPE =
		/* Mail */
		"mail-w "
		/* Calendar */
		"ycal-w "
		/* Contacts */
		"sdct-w";
	gchar *nonce_str;
	guint64 nonce_val;

	g_return_if_fail (uri_query != NULL);

	nonce_val = getpid () + g_get_real_time () + g_get_monotonic_time() + g_random_int () + g_random_int ();
	nonce_str = g_strdup_printf ("%" G_GUINT64_FORMAT "d", nonce_val);

	e_oauth2_service_util_set_to_form (uri_query, "scope", YAHOO_SCOPE);
	e_oauth2_service_util_set_to_form (uri_query, "nonce", nonce_str);

	g_free (nonce_str);
}

static gboolean
eos_yahoo_extract_authorization_code (EOAuth2Service *service,
				      ESource *source,
				      const gchar *page_title,
				      const gchar *page_uri,
				      const gchar *page_content,
				      gchar **out_authorization_code)
{
	g_return_val_if_fail (out_authorization_code != NULL, FALSE);

	*out_authorization_code = NULL;

	if (page_uri && *page_uri) {
		SoupURI *suri;

		suri = soup_uri_new (page_uri);
		if (suri) {
			const gchar *query = soup_uri_get_query (suri);
			gboolean known = FALSE;

			if (query && *query) {
				GHashTable *params;

				params = soup_form_decode (query);
				if (params) {
					const gchar *response;

					response = g_hash_table_lookup (params, "code");
					if (response) {
						*out_authorization_code = g_strdup (response);
						known = TRUE;
					}

					g_hash_table_destroy (params);
				}
			}

			soup_uri_free (suri);

			if (known)
				return TRUE;
		}
	}

	return FALSE;
}

static void
e_oauth2_service_yahoo_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->guess_can_process = eos_yahoo_guess_can_process;
	iface->get_name = eos_yahoo_get_name;
	iface->get_display_name = eos_yahoo_get_display_name;
	iface->get_client_id = eos_yahoo_get_client_id;
	iface->get_client_secret = eos_yahoo_get_client_secret;
	iface->get_authentication_uri = eos_yahoo_get_authentication_uri;
	iface->get_refresh_uri = eos_yahoo_get_refresh_uri;
	iface->get_redirect_uri = eos_yahoo_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_yahoo_prepare_authentication_uri_query;
	iface->extract_authorization_code = eos_yahoo_extract_authorization_code;
}

static void
e_oauth2_service_yahoo_class_init (EOAuth2ServiceYahooClass *klass)
{
}

static void
e_oauth2_service_yahoo_init (EOAuth2ServiceYahoo *oauth2_yahoo)
{
}
