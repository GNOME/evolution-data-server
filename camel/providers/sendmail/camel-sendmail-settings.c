/*
 * camel-sendmail-settings.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "camel-sendmail-settings.h"

#define CAMEL_SENDMAIL_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SENDMAIL_SETTINGS, CamelSendmailSettingsPrivate))

struct _CamelSendmailSettingsPrivate {
	GMutex *property_lock;
	gchar *custom_binary;

	gboolean use_custom_binary;
};

enum {
	PROP_0,
	PROP_USE_CUSTOM_BINARY,
	PROP_CUSTOM_BINARY
};

G_DEFINE_TYPE (CamelSendmailSettings, camel_sendmail_settings, CAMEL_TYPE_SETTINGS)

static void
sendmail_settings_set_property (GObject *object,
				guint property_id,
				const GValue *value,
				GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_USE_CUSTOM_BINARY:
			camel_sendmail_settings_set_use_custom_binary (
				CAMEL_SENDMAIL_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_CUSTOM_BINARY:
			camel_sendmail_settings_set_custom_binary (
				CAMEL_SENDMAIL_SETTINGS (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
sendmail_settings_get_property (GObject *object,
				guint property_id,
				GValue *value,
				GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_USE_CUSTOM_BINARY:
			g_value_set_boolean (
				value,
				camel_sendmail_settings_get_use_custom_binary (
				CAMEL_SENDMAIL_SETTINGS (object)));
			return;

		case PROP_CUSTOM_BINARY:
			g_value_take_string (
				value,
				camel_sendmail_settings_dup_custom_binary (
				CAMEL_SENDMAIL_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
sendmail_settings_finalize (GObject *object)
{
	CamelSendmailSettingsPrivate *priv;

	priv = CAMEL_SENDMAIL_SETTINGS_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->custom_binary);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_sendmail_settings_parent_class)->finalize (object);
}

static void
camel_sendmail_settings_class_init (CamelSendmailSettingsClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelSendmailSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = sendmail_settings_set_property;
	object_class->get_property = sendmail_settings_get_property;
	object_class->finalize = sendmail_settings_finalize;

	g_object_class_install_property (
		object_class,
		PROP_USE_CUSTOM_BINARY,
		g_param_spec_boolean (
			"use-custom-binary",
			"Use Custom Binary",
			"Whether the custom-binary property identifies binary to run",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CUSTOM_BINARY,
		g_param_spec_string (
			"custom-binary",
			"Custom Binary",
			"Custom binary to run, instead of sendmail",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_sendmail_settings_init (CamelSendmailSettings *settings)
{
	settings->priv = CAMEL_SENDMAIL_SETTINGS_GET_PRIVATE (settings);
	settings->priv->property_lock = g_mutex_new ();
}

/**
 * camel_sendmail_settings_get_use_custom_binary:
 * @settings: a #CamelSendmailSettings
 *
 * Returns whether the 'custom-binary' property should be used as binary to run, instead of sendmail.
 *
 * Returns: whether the 'custom-binary' property should be used as binary to run, instead of sendmail
 *
 * Since: 3.8
 **/
gboolean
camel_sendmail_settings_get_use_custom_binary (CamelSendmailSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SENDMAIL_SETTINGS (settings), FALSE);

	return settings->priv->use_custom_binary;
}

/**
 * camel_sendmail_settings_set_use_custom_binary:
 * @settings: a #CamelSendmailSettings
 * @use_custom_binary: whether to use custom binary
 *
 * Sets whether to use custom binary, instead of sendmail.
 *
 * Since: 3.8
 **/
void
camel_sendmail_settings_set_use_custom_binary (CamelSendmailSettings *settings,
					       gboolean use_custom_binary)
{
	g_return_if_fail (CAMEL_IS_SENDMAIL_SETTINGS (settings));

	if ((settings->priv->use_custom_binary ? 1 : 0) == (use_custom_binary ? 1 : 0))
		return;

	settings->priv->use_custom_binary = use_custom_binary;

	g_object_notify (G_OBJECT (settings), "use-custom-binary");
}

/**
 * camel_sendmail_settings_get_custom_binary:
 * @settings: a #CamelSendmailSettings
 *
 * Returns the custom binary to run, instead of sendmail.
 *
 * Returns: the custom binary to run, instead of sendmail, or %NULL
 *
 * Since: 3.8
 **/
const gchar *
camel_sendmail_settings_get_custom_binary (CamelSendmailSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SENDMAIL_SETTINGS (settings), NULL);

	return settings->priv->custom_binary;
}

/**
 * camel_sendmail_settings_dup_custom_binary:
 * @settings: a #CamelSendmailSettings
 *
 * Thread-safe variation of camel_sendmail_settings_get_custom_binary().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelSendmailSettings:custom-binary
 *
 * Since: 3.8
 **/
gchar *
camel_sendmail_settings_dup_custom_binary (CamelSendmailSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_SENDMAIL_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_sendmail_settings_get_custom_binary (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_sendmail_settings_set_custom_binary:
 * @settings: a #CamelSendmailSettings
 * @custom_binary: a custom binary name, or %NULL
 *
 * Sets the custom binary name to run, instead of sendmail.
 *
 * Since: 3.8
 **/
void
camel_sendmail_settings_set_custom_binary (CamelSendmailSettings *settings,
					   const gchar *custom_binary)
{
	g_return_if_fail (CAMEL_IS_SENDMAIL_SETTINGS (settings));

	/* The default namespace is an empty string. */
	if (custom_binary && !*custom_binary)
		custom_binary = NULL;

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->custom_binary, custom_binary) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->custom_binary);
	settings->priv->custom_binary = g_strdup (custom_binary);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "custom-binary");
}
