/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-oauth2-service-dynamic
 * @include: libedataserver/libedataserver.h
 * @short_description: OAuth2 service for dynamically registered clients
 *
 * #EOAuth2ServiceDynamic is an OAuth2 service whose client data is not
 * compiled in but read from the source's #ESourceOAuth2Client extension,
 * as filled at setup time by a provider, typically through RFC 8414
 * discovery and RFC 7591 dynamic client registration. It claims sources
 * whose authentication method is "OAuth2Dynamic" and uses PKCE (RFC 7636).
 *
 * Since: 3.64
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "e-oauth2-service.h"
#include "e-oauth2-service-base.h"
#include "e-source-oauth2-client.h"

#include "e-oauth2-service-dynamic.h"

struct _EOAuth2ServiceDynamic {
	EOAuth2ServiceBase parent;
};

/* Forward Declarations */
static void e_oauth2_service_dynamic_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EOAuth2ServiceDynamic, e_oauth2_service_dynamic, E_TYPE_OAUTH2_SERVICE_BASE,
	G_IMPLEMENT_INTERFACE (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_dynamic_oauth2_service_init))

static ESourceOAuth2Client *
eos_dynamic_get_client (ESource *source)
{
	if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT))
		return NULL;

	return e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
}

/* Returns the current value as a borrowed string valid as long as @source,
   by interning each distinct value, so earlier answers never dangle */
static const gchar *
eos_dynamic_intern_value (ESource *source,
			  const gchar *key_name,
			  gchar * (* dup_value) (ESourceOAuth2Client *extension))
{
	G_LOCK_DEFINE_STATIC (interned_values);
	ESourceOAuth2Client *extension;
	GHashTable *seen;
	gchar *fresh;
	gpointer orig_key = NULL;

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return NULL;

	fresh = dup_value (extension);
	if (!fresh || !*fresh) {
		g_free (fresh);
		return NULL;
	}

	G_LOCK (interned_values);

	seen = g_object_get_data (G_OBJECT (source), key_name);
	if (!seen) {
		seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		g_object_set_data_full (G_OBJECT (source), key_name, seen, (GDestroyNotify) g_hash_table_destroy);
	}

	if (g_hash_table_lookup_extended (seen, fresh, &orig_key, NULL)) {
		g_free (fresh);
	} else {
		g_hash_table_insert (seen, fresh, fresh);
		orig_key = fresh;
	}

	G_UNLOCK (interned_values);

	return orig_key;
}

static guint32
eos_dynamic_get_flags (EOAuth2Service *service)
{
	return E_OAUTH2_SERVICE_FLAG_USE_PKCE;
}

static const gchar *
eos_dynamic_get_name (EOAuth2Service *service)
{
	return "OAuth2Dynamic";
}

static const gchar *
eos_dynamic_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "OAuth2 (automatic)");
}

static const gchar *
eos_dynamic_get_client_id (EOAuth2Service *service,
			   ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-client-id",
		e_source_oauth2_client_dup_client_id);
}

static const gchar *
eos_dynamic_get_client_secret (EOAuth2Service *service,
			       ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-client-secret",
		e_source_oauth2_client_dup_client_secret);
}

static const gchar *
eos_dynamic_get_authentication_uri (EOAuth2Service *service,
				    ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-authorization-endpoint",
		e_source_oauth2_client_dup_authorization_endpoint);
}

static const gchar *
eos_dynamic_get_refresh_uri (EOAuth2Service *service,
			     ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-token-endpoint",
		e_source_oauth2_client_dup_token_endpoint);
}

static const gchar *
eos_dynamic_get_redirect_uri (EOAuth2Service *service,
			      ESource *source)
{
	const gchar *value;

	value = eos_dynamic_intern_value (source, "eds-oauth2-dynamic-redirect-uri",
		e_source_oauth2_client_dup_redirect_uri);

	return value ? value : "urn:ietf:wg:oauth:2.0:oob";
}

static void
eos_dynamic_prepare_authentication_uri_query (EOAuth2Service *service,
					      ESource *source,
					      GHashTable *uri_query)
{
	ESourceOAuth2Client *extension;
	gchar *value;

	g_return_if_fail (uri_query != NULL);

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return;

	value = e_source_oauth2_client_dup_scope (extension);
	if (value)
		e_oauth2_service_util_take_to_form (uri_query, "scope", value);

	value = e_source_oauth2_client_dup_resource (extension);
	if (value)
		e_oauth2_service_util_take_to_form (uri_query, "resource", value);
}

static void
eos_dynamic_prepare_get_token_form (EOAuth2Service *service,
				    ESource *source,
				    const gchar *authorization_code,
				    GHashTable *form)
{
	ESourceOAuth2Client *extension;
	gchar *value;

	g_return_if_fail (form != NULL);

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return;

	value = e_source_oauth2_client_dup_resource (extension);
	if (value)
		e_oauth2_service_util_take_to_form (form, "resource", value);
}

static void
eos_dynamic_prepare_refresh_token_form (EOAuth2Service *service,
					ESource *source,
					const gchar *refresh_token,
					GHashTable *form)
{
	ESourceOAuth2Client *extension;
	gchar *value;

	g_return_if_fail (form != NULL);

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return;

	value = e_source_oauth2_client_dup_resource (extension);
	if (value)
		e_oauth2_service_util_take_to_form (form, "resource", value);
}

static void
e_oauth2_service_dynamic_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->get_flags = eos_dynamic_get_flags;
	iface->get_name = eos_dynamic_get_name;
	iface->get_display_name = eos_dynamic_get_display_name;
	iface->get_client_id = eos_dynamic_get_client_id;
	iface->get_client_secret = eos_dynamic_get_client_secret;
	iface->get_authentication_uri = eos_dynamic_get_authentication_uri;
	iface->get_refresh_uri = eos_dynamic_get_refresh_uri;
	iface->get_redirect_uri = eos_dynamic_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_dynamic_prepare_authentication_uri_query;
	iface->prepare_get_token_form = eos_dynamic_prepare_get_token_form;
	iface->prepare_refresh_token_form = eos_dynamic_prepare_refresh_token_form;
}

static void
e_oauth2_service_dynamic_class_init (EOAuth2ServiceDynamicClass *klass)
{
}

static void
e_oauth2_service_dynamic_init (EOAuth2ServiceDynamic *oauth2_dynamic)
{
}
