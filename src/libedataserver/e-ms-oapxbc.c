/*
 * SPDX-FileCopyrightText: (C) 2024 Siemens AG
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION: e-ms-oapxbc
 * @include: libedataserver/libedataserver.h
 * @short_description: Interact with a locally running Microsoft OAuth2 broker service
 *
 * An #EMsOapxbc object provides methods to interact with a locally running Microsoft
 * OAuth2 broker service to implement the OAuth2 ms-oapxbc extension. This extension
 * defines how broker clients can interact with the Microsoft OAuth2 endpoints.
 * The key concept hereby are the PRT SSO cookies, which are acquired from a locally running
 * broker service and are injected into the login UI and token refresh requests.
 *
 * To get PRT SSO cookies, first call e_ms_oapxbc_get_accounts_sync() to get the
 * users that are currently registered at the broker. Then, call
 * e_ms_oapxbc_acquire_prt_sso_cookie_sync() with the account object that matches
 * the user you want to get a PRT SSO cookie for. The PRT SSO cookies need to
 * be injected either as cookie or as header into the login UI, as well as the
 * token refresh requests. Note, that the PRT SSO cookies are short-lived with
 * a minimal lifetime of 60 minutes.
 *
 * Since: 3.54
 **/

#include "evolution-data-server-config.h"

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <uuid/uuid.h>

#include "e-dbus-identity-broker.h"

#include "e-ms-oapxbc.h"

#define DBUS_BROKER_NAME "com.microsoft.identity.broker1"
#define DBUS_BROKER_PATH "/com/microsoft/identity/broker1"
#define AUTH_TYPE_OAUTH2 8

struct _EMsOapxbc {
	GObject parent_instance;

	gchar client_id[UUID_STR_LEN];
	gchar session_id[UUID_STR_LEN];
	gchar *authority;
	EDBusIdentityBroker1 *broker;
};

G_DEFINE_TYPE (EMsOapxbc, e_ms_oapxbc, G_TYPE_OBJECT)

static void
e_ms_oapxbc_finalize (GObject *object)
{
	EMsOapxbc *self = E_MS_OAPXBC (object);

	g_clear_pointer (&self->authority, g_free);
	g_clear_object (&self->broker);

	G_OBJECT_CLASS (e_ms_oapxbc_parent_class)->finalize (object);
}

static void
e_ms_oapxbc_class_init (EMsOapxbcClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_ms_oapxbc_finalize;
}

static void
e_ms_oapxbc_init (EMsOapxbc *self)
{
	uuid_t session_id;

	uuid_generate_random (session_id);
	uuid_unparse_lower (session_id, self->session_id);
}

/**
 * e_ms_oapxbc_new_sync:
 * @client_id: a client ID of the broker client (UUID string).
 * @authority: an authority URL of the OAuth2 service
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Synchronously create a new #EMsOapxbc.
 * This initiates the communication with a locally running Microsoft Identity broker service
 * via D-Bus. In case the broker is not running, it is started. If no broker is registered,
 * this function will fail (return %NULL).
 *
 * Returns: (nullable) (transfer full): a new #EMsOapxbc
 *
 * Since: 3.54
 **/
EMsOapxbc *
e_ms_oapxbc_new_sync (const gchar *client_id,
		      const gchar *authority,
		      GCancellable *cancellable,
		      GError **error)
{
	EMsOapxbc *self;

	self = g_object_new (E_TYPE_MS_OAPXBC, NULL);

	strncpy (self->client_id, client_id, UUID_STR_LEN - 1);
	self->authority = g_strdup (authority);
	self->broker = e_dbus_identity_broker1_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		DBUS_BROKER_NAME,
		DBUS_BROKER_PATH,
		cancellable, error);
	if (!self->broker) {
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		g_prefix_error (error, _("Failed to create broker proxy: "));
		g_clear_object (&self);
	}
	return self;
}

/**
 * e_ms_oapxbc_get_accounts_sync:
 * @self: an #EMsOapxbc
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Synchronously calls getAccounts() D-Bus method on the Microsoft
 * OAuth2 broker service and returns the result as a #JsonObject.

 * The #JsonObject contains the accounts that are currently registered at the broker,
 * whereby the "accounts" node provides a #JsonArray of account entries. Note, that
 * the availability of the types and entries needs to be checked by the caller before
 * accessing them. The accounts entries can be inspected e.g. for the "username" and 
 * "homeAccountId" fields. Then, one entry needs to be selected and passed as-is to
 * e_ms_oapxbc_acquire_prt_sso_cookie_sync().
 *
 * Returns: (nullable) (transfer full): the accounts, or %NULL on error
 *
 * Since: 3.54
 **/
JsonObject *
e_ms_oapxbc_get_accounts_sync (EMsOapxbc *self,
			       GCancellable *cancellable,
			       GError **error)
{
	gchar *response = NULL;
	JsonBuilder *builder;
	JsonGenerator *generator;
	JsonParser *parser;
	JsonNode *root;
	JsonObject *accounts;
	gchar *data;
	gboolean success;

	g_return_val_if_fail (E_IS_MS_OAPXBC (self), NULL);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "clientId");
	json_builder_add_string_value (builder, self->client_id);
	json_builder_set_member_name (builder, "redirectUri");
	json_builder_add_string_value (builder, self->client_id);
	json_builder_end_object (builder);
	root = json_builder_get_root (builder);
	g_object_unref (builder);

	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	data = json_generator_to_data (generator, NULL);
	json_node_unref (root);
	g_object_unref (generator);

	success = e_dbus_identity_broker1_call_get_accounts_sync (
		self->broker, "0.0", self->session_id, data, G_DBUS_CALL_FLAGS_NONE, -1,
		&response, cancellable, error);
	g_free (data);
	if (!success) {
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		g_prefix_error (error, _("Failed to call getAccounts: "));
		return NULL;
	}

	parser = json_parser_new ();
	success = json_parser_load_from_data (parser, response, -1, error);
	g_free (response);
	if (!success) {
		g_prefix_error (error, _("Failed to parse getAccounts response: "));
		g_clear_object (&parser);
		return NULL;
	}

	root = json_parser_get_root (parser);
	if (json_node_get_value_type (root) != JSON_TYPE_OBJECT) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Failed to parse getAccounts response: root is not an object"));
		g_clear_object (&parser);
		return NULL;
	}

	accounts = json_node_get_object (root);
	json_object_ref (accounts);
	g_clear_object (&parser);

	return accounts;
}

static gchar *
prepare_prt_sso_request_data (JsonObject *account,
			      JsonObject *auth_params,
			      const gchar *sso_url)
{
	JsonNode *params, *account_node, *auth_params_node, *sso_url_node;
	JsonObject *params_obj;
	JsonGenerator *gen;
	gchar *data;

	params = json_node_new (JSON_NODE_OBJECT);
	params_obj = json_object_new ();
	json_node_set_object (params, params_obj);
	json_object_unref (params_obj);
	account_node = json_node_new (JSON_NODE_OBJECT);
	json_node_set_object (account_node, account);
	auth_params_node = json_node_new (JSON_NODE_OBJECT);
	json_node_set_object (auth_params_node, auth_params);
	sso_url_node = json_node_new (JSON_NODE_VALUE);
	json_node_set_string (sso_url_node, sso_url);

	json_object_set_member (params_obj, "account", account_node);
	json_object_set_member (params_obj, "authParameters", auth_params_node);
	json_object_set_member (params_obj, "ssoUrl", sso_url_node);

	gen = json_generator_new ();
	json_generator_set_root (gen, params);
	data = json_generator_to_data (gen, NULL);
	g_object_unref (gen);
	json_node_unref (params);

	return data;
}

static JsonObject *
prepare_prt_auth_params (EMsOapxbc *self,
			 JsonObject *account,
			 JsonArray *scopes,
			 const gchar *redirect_uri)
{
	JsonNode *account_node, *scopes_node, *root;
	JsonObject *auth_params;
	JsonBuilder *builder;
	gchar *use_redirect_uri = NULL;
	const gchar *username;

	account_node = json_node_new (JSON_NODE_OBJECT);
	json_node_set_object (account_node, account);
	scopes_node = json_node_new (JSON_NODE_ARRAY);
	json_node_set_array (scopes_node, scopes);
	username = json_object_get_string_member (account, "username");

	if (!redirect_uri)
		use_redirect_uri = g_strdup_printf ("%s/oauth2/nativeclient", self->authority);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "account");
	json_builder_add_value (builder, account_node);
	json_builder_set_member_name (builder, "authority");
	json_builder_add_string_value (builder, self->authority);
	json_builder_set_member_name (builder, "authorizationType");
	json_builder_add_int_value (builder, AUTH_TYPE_OAUTH2);
	json_builder_set_member_name (builder, "clientId");
	json_builder_add_string_value (builder, self->client_id);
	json_builder_set_member_name (builder, "redirectUri");
	json_builder_add_string_value (builder, use_redirect_uri ? use_redirect_uri : redirect_uri);
	json_builder_set_member_name (builder, "requestedScopes");
	json_builder_add_value (builder, scopes_node);
	json_builder_set_member_name (builder, "username");
	json_builder_add_string_value (builder, username);
	json_builder_end_object (builder);

	root = json_builder_get_root (builder);
	auth_params = json_node_get_object (root);
	json_object_ref (auth_params);

	g_object_unref (builder);
	json_node_unref (root);
	g_free (use_redirect_uri);

	return auth_params;
}

/**
 * e_ms_oapxbc_acquire_prt_sso_cookie_sync:
 * @self: an #EMsOapxbc
 * @account: an account returned from e_ms_oapxbc_get_accounts_sync()
 * @sso_url: an SSO URL to acquire the PRT SSO cookie for.
 * @scopes: array of scopes
 * @redirect_uri: redirect URI
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Synchronously calls acquirePrtSsoCookie() D-Bus method on the Microsoft
 * OAuth2 broker service and converts the result into a new #SoupCookie.
 * The account object needs to be taken from the accounts list that is returned by
 * e_ms_oapxbc_get_accounts_sync(). The SSO URL is the OAuth2 authentication endpoint.
 * The scopes are the requested scopes for the OAuth2 service (usually only
 * https://graph.microsoft.com/.default). The redirect URI is the OAuth2 service
 * redirect URI.
 *
 * Returns: (nullable) (transfer full): an acquired cookie, or %NULL on error
 *
 * Since: 3.54
 **/
SoupCookie *
e_ms_oapxbc_acquire_prt_sso_cookie_sync (EMsOapxbc *self,
					 JsonObject *account,
					 const gchar *sso_url,
					 JsonArray *scopes,
					 const gchar *redirect_uri,
					 GCancellable *cancellable,
					 GError **error)
{
	JsonNode *root;
	JsonObject *auth_params, *json_cookie;
	JsonParser *parser;
	SoupCookie *soup_cookie;
	gchar *data;
	gchar *response = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_MS_OAPXBC (self), NULL);
	auth_params = prepare_prt_auth_params (self, account, scopes, redirect_uri);
	data = prepare_prt_sso_request_data (account, auth_params, sso_url);
	json_object_unref (auth_params);

	success = e_dbus_identity_broker1_call_acquire_prt_sso_cookie_sync (
		self->broker, "0.0", self->session_id, data, G_DBUS_CALL_FLAGS_NONE, -1,
		&response, cancellable, error);
	g_free (data);
	if (!success) {
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		g_prefix_error (error, _("Failed to acquire PRT SSO cookie: "));
		return NULL;
	}

	parser = json_parser_new ();
	success = json_parser_load_from_data (parser, response, -1, error);
	g_free (response);
	if (!success) {
		g_prefix_error (error, _("Failed to parse acquirePrtSsoCookie response: "));
		g_clear_object (&parser);
		return NULL;
	}
	root = json_parser_get_root (parser);
	if (json_node_get_value_type (root) != JSON_TYPE_OBJECT) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Failed to parse acquirePrtSsoCookie response: root is not an object"));
		g_clear_object (&parser);
		return NULL;
	}

	json_cookie = json_node_get_object (root);

	soup_cookie = soup_cookie_new (
		json_object_get_string_member (json_cookie, "cookieName"),
		json_object_get_string_member (json_cookie, "cookieContent"),
		/* [ms-oapxbc] is only supported on Microsoft Entra ID */
		"login.microsoftonline.com",
		"/", -1);
	soup_cookie_set_secure (soup_cookie, TRUE);
	soup_cookie_set_http_only (soup_cookie, TRUE);

	g_clear_object (&parser);

	return soup_cookie;
}
