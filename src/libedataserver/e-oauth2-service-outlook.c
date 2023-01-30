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

#include "e-oauth2-service-outlook.h"

/* https://apps.dev.microsoft.com/
   https://msdn.microsoft.com/en-us/library/office/dn631845.aspx
   https://www.limilabs.com/blog/oauth2-outlook-com-imap-installed-applications
*/

#define OUTLOOK_SCOPE "wl.offline_access wl.emails wl.imap"

/* Forward Declarations */
static void e_oauth2_service_outlook_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EOAuth2ServiceOutlook, e_oauth2_service_outlook, E_TYPE_OAUTH2_SERVICE_BASE,
	G_IMPLEMENT_INTERFACE (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_outlook_oauth2_service_init))

static gboolean
eos_outlook_guess_can_process (EOAuth2Service *service,
			       const gchar *protocol,
			       const gchar *hostname)
{
	return hostname && e_util_utf8_strstrcase (hostname, ".outlook.com");
}

static const gchar *
eos_outlook_get_name (EOAuth2Service *service)
{
	return "Outlook";
}

static const gchar *
eos_outlook_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "Outlook");
}

static const gchar *
eos_outlook_read_settings (EOAuth2Service *service,
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
eos_outlook_get_client_id (EOAuth2Service *service,
			   ESource *source)
{
	const gchar *client_id;

	client_id = eos_outlook_read_settings (service, "oauth2-outlook-client-id");

	if (client_id && *client_id)
		return client_id;

	return OUTLOOK_CLIENT_ID;
}

static const gchar *
eos_outlook_get_client_secret (EOAuth2Service *service,
			       ESource *source)
{
	const gchar *client_secret;

	client_secret = eos_outlook_read_settings (service, "oauth2-outlook-client-secret");

	if (client_secret && *client_secret)
		return client_secret;

	client_secret = OUTLOOK_CLIENT_SECRET;

	if (client_secret && !*client_secret)
		return NULL;

	return client_secret;
}

static const gchar *
eos_outlook_get_authentication_uri (EOAuth2Service *service,
				    ESource *source)
{
	return "https://login.live.com/oauth20_authorize.srf";
}

static const gchar *
eos_outlook_get_refresh_uri (EOAuth2Service *service,
			     ESource *source)
{
	return "https://login.live.com/oauth20_token.srf";
}

static const gchar *
eos_outlook_get_redirect_uri (EOAuth2Service *service,
			      ESource *source)
{
	return "https://login.live.com/oauth20_desktop.srf";
}

static void
eos_outlook_prepare_authentication_uri_query (EOAuth2Service *service,
					      ESource *source,
					      GHashTable *uri_query)
{
	g_return_if_fail (uri_query != NULL);

	e_oauth2_service_util_set_to_form (uri_query, "response_mode", "query");
	e_oauth2_service_util_set_to_form (uri_query, "scope", OUTLOOK_SCOPE);
}

static void
eos_outlook_prepare_refresh_token_form (EOAuth2Service *service,
					ESource *source,
					const gchar *refresh_token,
					GHashTable *form)
{
	g_return_if_fail (form != NULL);

	e_oauth2_service_util_set_to_form (form, "scope", OUTLOOK_SCOPE);
	e_oauth2_service_util_set_to_form (form, "redirect_uri", e_oauth2_service_get_redirect_uri (service, source));
}

static void
e_oauth2_service_outlook_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->guess_can_process = eos_outlook_guess_can_process;
	iface->get_name = eos_outlook_get_name;
	iface->get_display_name = eos_outlook_get_display_name;
	iface->get_client_id = eos_outlook_get_client_id;
	iface->get_client_secret = eos_outlook_get_client_secret;
	iface->get_authentication_uri = eos_outlook_get_authentication_uri;
	iface->get_refresh_uri = eos_outlook_get_refresh_uri;
	iface->get_redirect_uri = eos_outlook_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_outlook_prepare_authentication_uri_query;
	iface->prepare_refresh_token_form = eos_outlook_prepare_refresh_token_form;
}

static void
e_oauth2_service_outlook_class_init (EOAuth2ServiceOutlookClass *klass)
{
}

static void
e_oauth2_service_outlook_init (EOAuth2ServiceOutlook *oauth2_outlook)
{
}
