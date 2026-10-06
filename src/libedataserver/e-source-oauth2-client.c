/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-source-oauth2-client
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for a dynamically registered OAuth 2.0 client
 *
 * The #ESourceOAuth2Client extension carries the per-account OAuth 2.0
 * client data a provider obtained at setup time, typically through
 * RFC 8414 discovery and RFC 7591 dynamic client registration.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceOAuth2Client *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
 * ]|
 *
 * Since: 3.64
 **/

#include "evolution-data-server-config.h"

#include "e-data-server-util.h"

#include "e-source-oauth2-client.h"

struct _ESourceOAuth2Client {
	ESourceExtension parent;

	gchar *client_id;
	gchar *client_secret;
	gchar *authorization_endpoint;
	gchar *token_endpoint;
	gchar *redirect_uri;
	gchar *scope;
	gchar *resource;
	gchar *issuer;
};

enum {
	PROP_0,
	PROP_CLIENT_ID,
	PROP_CLIENT_SECRET,
	PROP_AUTHORIZATION_ENDPOINT,
	PROP_TOKEN_ENDPOINT,
	PROP_REDIRECT_URI,
	PROP_SCOPE,
	PROP_RESOURCE,
	PROP_ISSUER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE (ESourceOAuth2Client, e_source_oauth2_client, E_TYPE_SOURCE_EXTENSION)

static void
source_oauth2_client_set_property (GObject *object,
				   guint property_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CLIENT_ID:
			e_source_oauth2_client_set_client_id (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_CLIENT_SECRET:
			e_source_oauth2_client_set_client_secret (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_AUTHORIZATION_ENDPOINT:
			e_source_oauth2_client_set_authorization_endpoint (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_TOKEN_ENDPOINT:
			e_source_oauth2_client_set_token_endpoint (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_REDIRECT_URI:
			e_source_oauth2_client_set_redirect_uri (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_SCOPE:
			e_source_oauth2_client_set_scope (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_RESOURCE:
			e_source_oauth2_client_set_resource (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

		case PROP_ISSUER:
			e_source_oauth2_client_set_issuer (
				E_SOURCE_OAUTH2_CLIENT (object),
				g_value_get_string (value));
			return;

	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_oauth2_client_get_property (GObject *object,
				   guint property_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CLIENT_ID:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_client_id (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_CLIENT_SECRET:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_client_secret (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_AUTHORIZATION_ENDPOINT:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_authorization_endpoint (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_TOKEN_ENDPOINT:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_token_endpoint (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_REDIRECT_URI:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_redirect_uri (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_SCOPE:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_scope (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_RESOURCE:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_resource (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

		case PROP_ISSUER:
			g_value_take_string (
				value,
				e_source_oauth2_client_dup_issuer (
				E_SOURCE_OAUTH2_CLIENT (object)));
			return;

	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_oauth2_client_finalize (GObject *object)
{
	ESourceOAuth2Client *extension = E_SOURCE_OAUTH2_CLIENT (object);

	g_free (extension->client_id);
	g_free (extension->client_secret);
	g_free (extension->authorization_endpoint);
	g_free (extension->token_endpoint);
	g_free (extension->redirect_uri);
	g_free (extension->scope);
	g_free (extension->resource);
	g_free (extension->issuer);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_oauth2_client_parent_class)->finalize (object);
}

static void
e_source_oauth2_client_class_init (ESourceOAuth2ClientClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_oauth2_client_set_property;
	object_class->get_property = source_oauth2_client_get_property;
	object_class->finalize = source_oauth2_client_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_OAUTH2_CLIENT;

	/**
	 * ESourceOAuth2Client:client-id
	 *
	 * The client identifier issued for this account
	 *
	 * Since: 3.64
	 **/
	properties[PROP_CLIENT_ID] =
		g_param_spec_string (
			"client-id",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:client-secret
	 *
	 * The client secret, when the authorization server issued one
	 *
	 * Since: 3.64
	 **/
	properties[PROP_CLIENT_SECRET] =
		g_param_spec_string (
			"client-secret",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:authorization-endpoint
	 *
	 * The authorization endpoint URI
	 *
	 * Since: 3.64
	 **/
	properties[PROP_AUTHORIZATION_ENDPOINT] =
		g_param_spec_string (
			"authorization-endpoint",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:token-endpoint
	 *
	 * The token endpoint URI
	 *
	 * Since: 3.64
	 **/
	properties[PROP_TOKEN_ENDPOINT] =
		g_param_spec_string (
			"token-endpoint",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:redirect-uri
	 *
	 * The redirect URI the client was registered with
	 *
	 * Since: 3.64
	 **/
	properties[PROP_REDIRECT_URI] =
		g_param_spec_string (
			"redirect-uri",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:scope
	 *
	 * Space-separated scopes to request
	 *
	 * Since: 3.64
	 **/
	properties[PROP_SCOPE] =
		g_param_spec_string (
			"scope",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:resource
	 *
	 * The RFC 8707 resource indicator, when the server requires one
	 *
	 * Since: 3.64
	 **/
	properties[PROP_RESOURCE] =
		g_param_spec_string (
			"resource",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	/**
	 * ESourceOAuth2Client:issuer
	 *
	 * The RFC 8414 issuer the endpoints were discovered from
	 *
	 * Since: 3.64
	 **/
	properties[PROP_ISSUER] =
		g_param_spec_string (
			"issuer",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
e_source_oauth2_client_init (ESourceOAuth2Client *extension)
{
}

/**
 * e_source_oauth2_client_get_client_id:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the client identifier issued for this account
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_client_id (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->client_id;
}

/**
 * e_source_oauth2_client_dup_client_id:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_client_id().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:client-id
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_client_id (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->client_id);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_client_id:
 * @extension: an #ESourceOAuth2Client
 * @client_id: (nullable): a client identifier, or %NULL
 *
 * Sets the client identifier issued for this account. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_client_id (ESourceOAuth2Client *extension,
                                      const gchar *client_id)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->client_id, client_id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->client_id);
	extension->client_id = e_util_strdup_strip (client_id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_CLIENT_ID]);
}

/**
 * e_source_oauth2_client_get_client_secret:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the client secret, when the authorization server issued one
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_client_secret (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->client_secret;
}

/**
 * e_source_oauth2_client_dup_client_secret:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_client_secret().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:client-secret
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_client_secret (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->client_secret);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_client_secret:
 * @extension: an #ESourceOAuth2Client
 * @client_secret: (nullable): a client secret, or %NULL
 *
 * Sets the client secret, when the authorization server issued one. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_client_secret (ESourceOAuth2Client *extension,
                                          const gchar *client_secret)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->client_secret, client_secret) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->client_secret);
	extension->client_secret = e_util_strdup_strip (client_secret);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_CLIENT_SECRET]);
}

/**
 * e_source_oauth2_client_get_authorization_endpoint:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the authorization endpoint URI
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_authorization_endpoint (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->authorization_endpoint;
}

/**
 * e_source_oauth2_client_dup_authorization_endpoint:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_authorization_endpoint().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:authorization-endpoint
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_authorization_endpoint (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->authorization_endpoint);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_authorization_endpoint:
 * @extension: an #ESourceOAuth2Client
 * @authorization_endpoint: (nullable): an authorization endpoint URI, or %NULL
 *
 * Sets the authorization endpoint URI. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_authorization_endpoint (ESourceOAuth2Client *extension,
                                                   const gchar *authorization_endpoint)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->authorization_endpoint, authorization_endpoint) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->authorization_endpoint);
	extension->authorization_endpoint = e_util_strdup_strip (authorization_endpoint);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_AUTHORIZATION_ENDPOINT]);
}

/**
 * e_source_oauth2_client_get_token_endpoint:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the token endpoint URI
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_token_endpoint (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->token_endpoint;
}

/**
 * e_source_oauth2_client_dup_token_endpoint:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_token_endpoint().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:token-endpoint
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_token_endpoint (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->token_endpoint);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_token_endpoint:
 * @extension: an #ESourceOAuth2Client
 * @token_endpoint: (nullable): a token endpoint URI, or %NULL
 *
 * Sets the token endpoint URI. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_token_endpoint (ESourceOAuth2Client *extension,
                                           const gchar *token_endpoint)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->token_endpoint, token_endpoint) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->token_endpoint);
	extension->token_endpoint = e_util_strdup_strip (token_endpoint);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_TOKEN_ENDPOINT]);
}

/**
 * e_source_oauth2_client_get_redirect_uri:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the redirect URI the client was registered with
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_redirect_uri (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->redirect_uri;
}

/**
 * e_source_oauth2_client_dup_redirect_uri:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_redirect_uri().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:redirect-uri
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_redirect_uri (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->redirect_uri);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_redirect_uri:
 * @extension: an #ESourceOAuth2Client
 * @redirect_uri: (nullable): a redirect URI, or %NULL
 *
 * Sets the redirect URI the client was registered with. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_redirect_uri (ESourceOAuth2Client *extension,
                                         const gchar *redirect_uri)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->redirect_uri, redirect_uri) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->redirect_uri);
	extension->redirect_uri = e_util_strdup_strip (redirect_uri);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_REDIRECT_URI]);
}

/**
 * e_source_oauth2_client_get_scope:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): space-separated scopes to request
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_scope (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->scope;
}

/**
 * e_source_oauth2_client_dup_scope:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_scope().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:scope
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_scope (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->scope);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_scope:
 * @extension: an #ESourceOAuth2Client
 * @scope: (nullable): space-separated scopes, or %NULL
 *
 * Sets the space-separated scopes to request. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_scope (ESourceOAuth2Client *extension,
                                  const gchar *scope)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->scope, scope) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->scope);
	extension->scope = e_util_strdup_strip (scope);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_SCOPE]);
}

/**
 * e_source_oauth2_client_get_resource:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the RFC 8707 resource indicator, when the server requires one
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_resource (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->resource;
}

/**
 * e_source_oauth2_client_dup_resource:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_resource().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:resource
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_resource (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->resource);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_resource:
 * @extension: an #ESourceOAuth2Client
 * @resource: (nullable): a resource indicator, or %NULL
 *
 * Sets the RFC 8707 resource indicator, when the server requires one. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_resource (ESourceOAuth2Client *extension,
                                     const gchar *resource)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->resource, resource) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->resource);
	extension->resource = e_util_strdup_strip (resource);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_RESOURCE]);
}

/**
 * e_source_oauth2_client_get_issuer:
 * @extension: an #ESourceOAuth2Client
 *
 * Returns: (nullable): the RFC 8414 issuer the endpoints were discovered from
 *
 * Since: 3.64
 **/
const gchar *
e_source_oauth2_client_get_issuer (ESourceOAuth2Client *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	return extension->issuer;
}

/**
 * e_source_oauth2_client_dup_issuer:
 * @extension: an #ESourceOAuth2Client
 *
 * Thread-safe variation of e_source_oauth2_client_get_issuer().
 *
 * Returns: (nullable) (transfer full): a copy of #ESourceOAuth2Client:issuer
 *
 * Since: 3.64
 **/
gchar *
e_source_oauth2_client_dup_issuer (ESourceOAuth2Client *extension)
{
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));
	duplicate = g_strdup (extension->issuer);
	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_oauth2_client_set_issuer:
 * @extension: an #ESourceOAuth2Client
 * @issuer: (nullable): an issuer identifier, or %NULL
 *
 * Sets the RFC 8414 issuer the endpoints were discovered from. An empty string is treated as %NULL.
 *
 * Since: 3.64
 **/
void
e_source_oauth2_client_set_issuer (ESourceOAuth2Client *extension,
                                   const gchar *issuer)
{
	g_return_if_fail (E_IS_SOURCE_OAUTH2_CLIENT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->issuer, issuer) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->issuer);
	extension->issuer = e_util_strdup_strip (issuer);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_ISSUER]);
}

