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

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "e-data-server-util.h"
#include "e-oauth2-service.h"
#include "e-oauth2-service-base.h"

#include "e-oauth2-service-google.h"

/* https://developers.google.com/identity/protocols/OAuth2InstalledApp */
/* https://developers.google.com/identity/protocols/oauth2/native-app */

/* Forward Declarations */
static void e_oauth2_service_google_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EOAuth2ServiceGoogle, e_oauth2_service_google, E_TYPE_OAUTH2_SERVICE_BASE,
	G_IMPLEMENT_INTERFACE (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_google_oauth2_service_init))

static gboolean
eos_google_guess_can_process (EOAuth2Service *service,
			      const gchar *protocol,
			      const gchar *hostname)
{
	return hostname && (
		e_util_utf8_strstrcase (hostname, ".google.com") ||
		e_util_utf8_strstrcase (hostname, ".googlemail.com") ||
		e_util_utf8_strstrcase (hostname, ".googleusercontent.com") ||
		e_util_utf8_strstrcase (hostname, ".gmail.com"));
}

static const gchar *
eos_google_get_name (EOAuth2Service *service)
{
	return "Google";
}

static const gchar *
eos_google_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "Google");
}

static const gchar *
eos_google_read_settings (EOAuth2Service *service,
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
eos_google_get_client_id (EOAuth2Service *service,
			  ESource *source)
{
	static gchar glob_buff[128] = {0, };
	const gchar *client_id;

	client_id = eos_google_read_settings (service, "oauth2-google-client-id");

	if (client_id && *client_id)
		return client_id;

	return e_oauth2_service_util_compile_value (GOOGLE_CLIENT_ID, glob_buff, sizeof (glob_buff));
}

static const gchar *
eos_google_get_client_secret (EOAuth2Service *service,
			      ESource *source)
{
	static gchar glob_buff[128] = {0, };
	const gchar *client_secret;

	client_secret = eos_google_read_settings (service, "oauth2-google-client-secret");

	if (client_secret && *client_secret)
		return client_secret;

	return e_oauth2_service_util_compile_value (GOOGLE_CLIENT_SECRET, glob_buff, sizeof (glob_buff));
}

static const gchar *
eos_google_get_authentication_uri (EOAuth2Service *service,
				   ESource *source)
{
	return "https://accounts.google.com/o/oauth2/v2/auth";
}

static const gchar *
eos_google_get_refresh_uri (EOAuth2Service *service,
			    ESource *source)
{
	return "https://oauth2.googleapis.com/token";
}

static const gchar *
eos_google_get_redirect_uri (EOAuth2Service *service,
			     ESource *source)
{
	G_LOCK_DEFINE_STATIC (redirect_uri);
	const gchar *key_name = "oauth2-google-redirect-uri";
	gchar *value;

	G_LOCK (redirect_uri);

	value = g_object_get_data (G_OBJECT (service), key_name);
	if (!value) {
		const gchar *client_id = eos_google_get_client_id (service, source);

		if (client_id) {
			GPtrArray *array;
			gchar **strv;
			gchar *joinstr;
			guint ii;

			strv = g_strsplit (client_id, ".", -1);
			array = g_ptr_array_new ();

			for (ii = 0; strv[ii]; ii++) {
				g_ptr_array_insert (array, 0, strv[ii]);
			}

			g_ptr_array_add (array, NULL);

			joinstr = g_strjoinv (".", (gchar **) array->pdata);
			/* Use reverse-DNS of the client ID with the below path */
			value = g_strconcat (joinstr, ":/oauth2redirect", NULL);

			g_ptr_array_free (array, TRUE);
			g_strfreev (strv);
			g_free (joinstr);

			g_object_set_data_full (G_OBJECT (service), key_name, value, g_free);
		}
	}

	G_UNLOCK (redirect_uri);

	return value;
}

static void
eos_google_prepare_authentication_uri_query (EOAuth2Service *service,
					     ESource *source,
					     GHashTable *uri_query)
{
	const gchar *GOOGLE_SCOPE =
		/* GMail IMAP and SMTP access */
		"https://mail.google.com/ "
		/* Google Calendar API (CalDAV and GData) */
		"https://www.googleapis.com/auth/calendar "
		/* Google Contacts API (GData) */
		"https://www.google.com/m8/feeds/ "
		/* Google Contacts API (CardDAV) - undocumented */
		"https://www.googleapis.com/auth/carddav "
		/* Google Tasks - undocumented */
		"https://www.googleapis.com/auth/tasks";

	g_return_if_fail (uri_query != NULL);

	e_oauth2_service_util_set_to_form (uri_query, "scope", GOOGLE_SCOPE);
	e_oauth2_service_util_set_to_form (uri_query, "include_granted_scopes", "false");
}

static gboolean
eos_google_extract_authorization_code (EOAuth2Service *service,
				       ESource *source,
				       const gchar *page_title,
				       const gchar *page_uri,
				       const gchar *page_content,
				       gchar **out_authorization_code)
{
	g_return_val_if_fail (out_authorization_code != NULL, FALSE);

	*out_authorization_code = NULL;

	if (page_title && *page_title) {
		/* Known response, but no authorization code */
		if (g_ascii_strncasecmp (page_title, "Denied ", 7) == 0)
			return TRUE;

		if (g_ascii_strncasecmp (page_title, "Success code=", 13) == 0) {
			*out_authorization_code = g_strdup (page_title + 13);
			return TRUE;
		}
	}

	if (page_uri && *page_uri) {
		GUri *suri;

		suri = g_uri_parse (page_uri, SOUP_HTTP_URI_FLAGS, NULL);
		if (suri) {
			const gchar *query = g_uri_get_query (suri);
			gboolean known = FALSE;

			if (query && *query) {
				GHashTable *params;

				params = soup_form_decode (query);
				if (params) {
					const gchar *code;

					code = g_hash_table_lookup (params, "code");
					if (code && *code) {
						*out_authorization_code = g_strdup (code);
						known = TRUE;
					} else if (g_hash_table_lookup (params, "error")) {
						known = TRUE;
					}

					g_hash_table_destroy (params);
				}
			}

			g_uri_unref (suri);

			if (known)
				return TRUE;
		}
	}

	return FALSE;
}

static void
e_oauth2_service_google_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->guess_can_process = eos_google_guess_can_process;
	iface->get_name = eos_google_get_name;
	iface->get_display_name = eos_google_get_display_name;
	iface->get_client_id = eos_google_get_client_id;
	iface->get_client_secret = eos_google_get_client_secret;
	iface->get_authentication_uri = eos_google_get_authentication_uri;
	iface->get_refresh_uri = eos_google_get_refresh_uri;
	iface->get_redirect_uri = eos_google_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_google_prepare_authentication_uri_query;
	iface->extract_authorization_code = eos_google_extract_authorization_code;
}

static void
e_oauth2_service_google_class_init (EOAuth2ServiceGoogleClass *klass)
{
}

static void
e_oauth2_service_google_init (EOAuth2ServiceGoogle *oauth2_google)
{
}
