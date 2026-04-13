/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "camel-jmap-settings.h"

struct _CamelJmapSettings {
	CamelStoreSettings parent;

	gchar *bearer_token;
	gboolean use_bearer_token;
};

enum {
	PROP_0,
	PROP_BEARER_TOKEN,
	PROP_USE_BEARER_TOKEN,
	N_PROPS,

	/* CamelNetworkSettings interface properties */
	PROP_AUTH_MECHANISM,
	PROP_HOST,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_USER
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_CODE (
	CamelJmapSettings,
	camel_jmap_settings,
	CAMEL_TYPE_STORE_SETTINGS,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
jmap_settings_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTH_MECHANISM:
			camel_network_settings_set_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_BEARER_TOKEN:
			camel_jmap_settings_set_bearer_token (
				CAMEL_JMAP_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_PORT:
			camel_network_settings_set_port (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_USE_BEARER_TOKEN:
			camel_jmap_settings_set_use_bearer_token (
				CAMEL_JMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
jmap_settings_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTH_MECHANISM:
			g_value_take_string (
				value,
				camel_network_settings_dup_auth_mechanism (
					CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_BEARER_TOKEN:
			g_value_take_string (
				value,
				camel_jmap_settings_dup_bearer_token (
					CAMEL_JMAP_SETTINGS (object)));
			return;

		case PROP_HOST:
			g_value_take_string (
				value,
				camel_network_settings_dup_host (
					CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_PORT:
			g_value_set_uint (
				value,
				camel_network_settings_get_port (
					CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_SECURITY_METHOD:
			g_value_set_enum (
				value,
				camel_network_settings_get_security_method (
					CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USE_BEARER_TOKEN:
			g_value_set_boolean (
				value,
				camel_jmap_settings_get_use_bearer_token (
					CAMEL_JMAP_SETTINGS (object)));
			return;

		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
					CAMEL_NETWORK_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
jmap_settings_finalize (GObject *object)
{
	CamelJmapSettings *self = CAMEL_JMAP_SETTINGS (object);

	g_clear_pointer (&self->bearer_token, g_free);

	G_OBJECT_CLASS (camel_jmap_settings_parent_class)->finalize (object);
}

static void
camel_jmap_settings_class_init (CamelJmapSettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = jmap_settings_set_property;
	object_class->get_property = jmap_settings_get_property;
	object_class->finalize = jmap_settings_finalize;

	properties[PROP_BEARER_TOKEN] = g_param_spec_string (
		"bearer-token",
		"Bearer Token",
		"Bearer token for OAuth2 authentication",
		NULL,
		G_PARAM_READWRITE |
		G_PARAM_CONSTRUCT |
		G_PARAM_EXPLICIT_NOTIFY |
		G_PARAM_STATIC_STRINGS);

	properties[PROP_USE_BEARER_TOKEN] = g_param_spec_boolean (
		"use-bearer-token",
		"Use Bearer Token",
		"Whether to authenticate using a Bearer token instead of username/password",
		FALSE,
		G_PARAM_READWRITE |
		G_PARAM_CONSTRUCT |
		G_PARAM_EXPLICIT_NOTIFY |
		G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/* Implement CamelNetworkSettings properties. */
	g_object_class_override_property (
		object_class, PROP_AUTH_MECHANISM, "auth-mechanism");
	g_object_class_override_property (
		object_class, PROP_HOST, "host");
	g_object_class_override_property (
		object_class, PROP_PORT, "port");
	g_object_class_override_property (
		object_class, PROP_SECURITY_METHOD, "security-method");
	g_object_class_override_property (
		object_class, PROP_USER, "user");
}

static void
camel_jmap_settings_init (CamelJmapSettings *settings)
{
}

/**
 * camel_jmap_settings_dup_bearer_token:
 * @settings: a #CamelJmapSettings
 *
 * Returns a copy of the bearer token used for OAuth2 authentication.
 * Free the returned string with g_free() when finished with it.
 *
 * Returns: (nullable): a copy of the bearer token, or %NULL
 */
gchar *
camel_jmap_settings_dup_bearer_token (CamelJmapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_JMAP_SETTINGS (settings), NULL);

	return g_strdup (settings->bearer_token);
}

/**
 * camel_jmap_settings_set_bearer_token:
 * @settings: a #CamelJmapSettings
 * @bearer_token: (nullable): a bearer token, or %NULL
 *
 * Sets the bearer token for OAuth2 authentication.
 */
void
camel_jmap_settings_set_bearer_token (CamelJmapSettings *settings,
                                      const gchar *bearer_token)
{
	g_return_if_fail (CAMEL_IS_JMAP_SETTINGS (settings));

	if (g_strcmp0 (settings->bearer_token, bearer_token) == 0) {
		return;
	}

	g_free (settings->bearer_token);
	settings->bearer_token = g_strdup (bearer_token);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_BEARER_TOKEN]);
}

/**
 * camel_jmap_settings_get_use_bearer_token:
 * @settings: a #CamelJmapSettings
 *
 * Returns whether the provider should use bearer token authentication
 * rather than username/password.
 *
 * Returns: %TRUE if using bearer token authentication
 */
gboolean
camel_jmap_settings_get_use_bearer_token (CamelJmapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_JMAP_SETTINGS (settings), FALSE);

	return settings->use_bearer_token;
}

/**
 * camel_jmap_settings_set_use_bearer_token:
 * @settings: a #CamelJmapSettings
 * @use_bearer_token: %TRUE to use bearer token authentication
 *
 * Sets whether to use bearer token authentication.
 */
void
camel_jmap_settings_set_use_bearer_token (CamelJmapSettings *settings,
                                          gboolean use_bearer_token)
{
	g_return_if_fail (CAMEL_IS_JMAP_SETTINGS (settings));

	if (settings->use_bearer_token == use_bearer_token)
		return;

	settings->use_bearer_token = use_bearer_token;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_BEARER_TOKEN]);
}
