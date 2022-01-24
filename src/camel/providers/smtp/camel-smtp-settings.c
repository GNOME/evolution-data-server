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
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_HOST,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_USER,
	PROP_REENCODE_DATA
};

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

	g_object_class_install_property (
		object_class,
		PROP_REENCODE_DATA,
		g_param_spec_boolean (
			"reencode-data",
			"Reencode Data",
			"Whether to re-encode data on send",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));
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

	g_object_notify (G_OBJECT (settings), "reencode-data");
}
