/*
 * camel-smtp-settings.c
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

#include "camel-smtp-settings.h"

struct _CamelSmtpSettingsPrivate {
	gboolean reencode_data;
	gboolean dsn_ret_full;
	gboolean dsn_notify_success;
	gboolean dsn_notify_failure;
	gboolean dsn_notify_delay;
};

enum {
	PROP_0,
	PROP_REENCODE_DATA,
	PROP_DSN_RET_FULL,
	PROP_DSN_NOTIFY_SUCCESS,
	PROP_DSN_NOTIFY_FAILURE,
	PROP_DSN_NOTIFY_DELAY,
	N_PROPS,

	PROP_AUTH_MECHANISM,
	PROP_HOST,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_USER
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_CODE (CamelSmtpSettings, camel_smtp_settings, CAMEL_TYPE_SETTINGS,
	G_ADD_PRIVATE (CamelSmtpSettings)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
smtp_settings_set_property (GObject *object,
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

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_REENCODE_DATA:
			camel_smtp_settings_set_reencode_data (
				CAMEL_SMTP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_DSN_RET_FULL:
			camel_smtp_settings_set_dsn_ret_full (
				CAMEL_SMTP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_DSN_NOTIFY_SUCCESS:
			camel_smtp_settings_set_dsn_notify_success (
				CAMEL_SMTP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_DSN_NOTIFY_FAILURE:
			camel_smtp_settings_set_dsn_notify_failure (
				CAMEL_SMTP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_DSN_NOTIFY_DELAY:
			camel_smtp_settings_set_dsn_notify_delay (
				CAMEL_SMTP_SETTINGS (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
smtp_settings_get_property (GObject *object,
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

		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_REENCODE_DATA:
			g_value_set_boolean (
				value,
				camel_smtp_settings_get_reencode_data (
				CAMEL_SMTP_SETTINGS (object)));
			return;

		case PROP_DSN_RET_FULL:
			g_value_set_boolean (
				value,
				camel_smtp_settings_get_dsn_ret_full (
				CAMEL_SMTP_SETTINGS (object)));
			return;

		case PROP_DSN_NOTIFY_SUCCESS:
			g_value_set_boolean (
				value,
				camel_smtp_settings_get_dsn_notify_success (
				CAMEL_SMTP_SETTINGS (object)));
			return;

		case PROP_DSN_NOTIFY_FAILURE:
			g_value_set_boolean (
				value,
				camel_smtp_settings_get_dsn_notify_failure (
				CAMEL_SMTP_SETTINGS (object)));
			return;

		case PROP_DSN_NOTIFY_DELAY:
			g_value_set_boolean (
				value,
				camel_smtp_settings_get_dsn_notify_delay (
				CAMEL_SMTP_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
camel_smtp_settings_class_init (CamelSmtpSettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = smtp_settings_set_property;
	object_class->get_property = smtp_settings_get_property;

	/**
	 * CamelSmtpSettings:reencode-data
	 *
	 * Whether to re-encode data on send
	 **/
	properties[PROP_REENCODE_DATA] =
		g_param_spec_boolean (
			"reencode-data", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelSmtpSettings:dsn-ret-full
	 *
	 * Whether to return full messages in DSN responses
	 **/
	properties[PROP_DSN_RET_FULL] =
		g_param_spec_boolean (
			"dsn-ret-full", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelSmtpSettings:dsn-notify-success
	 *
	 * Whether to DSN-notify on success
	 **/
	properties[PROP_DSN_NOTIFY_SUCCESS] =
		g_param_spec_boolean (
			"dsn-notify-success", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelSmtpSettings:dsn-notify-failure
	 *
	 * Whether to DSN-notify on failure
	 **/
	properties[PROP_DSN_NOTIFY_FAILURE] =
		g_param_spec_boolean (
			"dsn-notify-failure", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelSmtpSettings:dsn-notify-delay
	 *
	 * Whether to DSN-notify on delay
	 **/
	properties[PROP_DSN_NOTIFY_DELAY] =
		g_param_spec_boolean (
			"dsn-notify-delay", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_AUTH_MECHANISM,
		"auth-mechanism");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_HOST,
		"host");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_PORT,
		"port");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_USER,
		"user");
}

static void
camel_smtp_settings_init (CamelSmtpSettings *settings)
{
	settings->priv = camel_smtp_settings_get_instance_private (settings);
}

gboolean
camel_smtp_settings_get_reencode_data (CamelSmtpSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SMTP_SETTINGS (settings), FALSE);

	return settings->priv->reencode_data;
}

void
camel_smtp_settings_set_reencode_data (CamelSmtpSettings *settings,
				       gboolean reencode_data)
{
	g_return_if_fail (CAMEL_IS_SMTP_SETTINGS (settings));

	if ((settings->priv->reencode_data ? 1 : 0) == (reencode_data ? 1 : 0))
		return;

	settings->priv->reencode_data = reencode_data;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_REENCODE_DATA]);
}

gboolean
camel_smtp_settings_get_dsn_ret_full (CamelSmtpSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SMTP_SETTINGS (settings), FALSE);

	return settings->priv->dsn_ret_full;
}

void
camel_smtp_settings_set_dsn_ret_full (CamelSmtpSettings *settings,
				      gboolean dsn_ret_full)
{
	g_return_if_fail (CAMEL_IS_SMTP_SETTINGS (settings));

	if ((settings->priv->dsn_ret_full ? 1 : 0) == (dsn_ret_full ? 1 : 0))
		return;

	settings->priv->dsn_ret_full = dsn_ret_full;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_DSN_RET_FULL]);
}

gboolean
camel_smtp_settings_get_dsn_notify_success (CamelSmtpSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SMTP_SETTINGS (settings), FALSE);

	return settings->priv->dsn_notify_success;
}

void
camel_smtp_settings_set_dsn_notify_success (CamelSmtpSettings *settings,
					    gboolean dsn_notify_success)
{
	g_return_if_fail (CAMEL_IS_SMTP_SETTINGS (settings));

	if ((settings->priv->dsn_notify_success ? 1 : 0) == (dsn_notify_success ? 1 : 0))
		return;

	settings->priv->dsn_notify_success = dsn_notify_success;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_DSN_NOTIFY_SUCCESS]);
}

gboolean
camel_smtp_settings_get_dsn_notify_failure (CamelSmtpSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SMTP_SETTINGS (settings), FALSE);

	return settings->priv->dsn_notify_failure;
}

void
camel_smtp_settings_set_dsn_notify_failure (CamelSmtpSettings *settings,
					    gboolean dsn_notify_failure)
{
	g_return_if_fail (CAMEL_IS_SMTP_SETTINGS (settings));

	if ((settings->priv->dsn_notify_failure ? 1 : 0) == (dsn_notify_failure ? 1 : 0))
		return;

	settings->priv->dsn_notify_failure = dsn_notify_failure;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_DSN_NOTIFY_FAILURE]);
}

gboolean
camel_smtp_settings_get_dsn_notify_delay (CamelSmtpSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SMTP_SETTINGS (settings), FALSE);

	return settings->priv->dsn_notify_delay;
}

void
camel_smtp_settings_set_dsn_notify_delay (CamelSmtpSettings *settings,
					  gboolean dsn_notify_delay)
{
	g_return_if_fail (CAMEL_IS_SMTP_SETTINGS (settings));

	if ((settings->priv->dsn_notify_delay ? 1 : 0) == (dsn_notify_delay ? 1 : 0))
		return;

	settings->priv->dsn_notify_delay = dsn_notify_delay;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_DSN_NOTIFY_DELAY]);
}
