/*
 * camel-spool-settings.c
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

#include "camel-spool-settings.h"

struct _CamelSpoolSettingsPrivate {
	gboolean use_xstatus_headers;
	gboolean listen_notifications;
};

enum {
	PROP_0,
	PROP_USE_XSTATUS_HEADERS,
	PROP_LISTEN_NOTIFICATIONS
};

G_DEFINE_TYPE_WITH_PRIVATE (
	CamelSpoolSettings,
	camel_spool_settings,
	CAMEL_TYPE_LOCAL_SETTINGS)

static void
spool_settings_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_USE_XSTATUS_HEADERS:
			camel_spool_settings_set_use_xstatus_headers (
				CAMEL_SPOOL_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_LISTEN_NOTIFICATIONS:
			camel_spool_settings_set_listen_notifications (
				CAMEL_SPOOL_SETTINGS (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
spool_settings_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_USE_XSTATUS_HEADERS:
			g_value_set_boolean (
				value,
				camel_spool_settings_get_use_xstatus_headers (
				CAMEL_SPOOL_SETTINGS (object)));
			return;

		case PROP_LISTEN_NOTIFICATIONS:
			g_value_set_boolean (
				value,
				camel_spool_settings_get_listen_notifications (
				CAMEL_SPOOL_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
camel_spool_settings_class_init (CamelSpoolSettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = spool_settings_set_property;
	object_class->get_property = spool_settings_get_property;

	g_object_class_install_property (
		object_class,
		PROP_USE_XSTATUS_HEADERS,
		g_param_spec_boolean (
			"use-xstatus-headers",
			"Use X-Status Headers",
			"Whether to use X-Status headers",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_LISTEN_NOTIFICATIONS,
		g_param_spec_boolean (
			"listen-notifications",
			"Listen Notifications",
			"Whether to listen for file/directory change notifications",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_spool_settings_init (CamelSpoolSettings *settings)
{
	settings->priv = camel_spool_settings_get_instance_private (settings);
}

/**
 * camel_spool_settings_get_use_xstatus_headers:
 * @settings: a #CamelSpoolSettings
 *
 * Returns whether to utilize both "Status" and "X-Status" headers for
 * interoperability with mbox-based mail clients like Elm, Pine and Mutt.
 *
 * Returns: whether to use "X-Status" headers
 *
 * Since: 3.2
 **/
gboolean
camel_spool_settings_get_use_xstatus_headers (CamelSpoolSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SPOOL_SETTINGS (settings), FALSE);

	return settings->priv->use_xstatus_headers;
}

/**
 * camel_spool_settings_set_use_xstatus_headers:
 * @settings: a #CamelSpoolSettings
 * @use_xstatus_headers: whether to use "X-Status" headers
 *
 * Sets whether to utilize both "Status" and "X-Status" headers for
 * interoperability with mbox-based mail clients like Elm, Pine and Mutt.
 *
 * Since: 3.2
 **/
void
camel_spool_settings_set_use_xstatus_headers (CamelSpoolSettings *settings,
                                              gboolean use_xstatus_headers)
{
	g_return_if_fail (CAMEL_IS_SPOOL_SETTINGS (settings));

	if (settings->priv->use_xstatus_headers == use_xstatus_headers)
		return;

	settings->priv->use_xstatus_headers = use_xstatus_headers;

	g_object_notify (G_OBJECT (settings), "use-xstatus-headers");
}

gboolean
camel_spool_settings_get_listen_notifications (CamelSpoolSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_SPOOL_SETTINGS (settings), FALSE);

	return settings->priv->listen_notifications;
}

void
camel_spool_settings_set_listen_notifications (CamelSpoolSettings *settings,
					       gboolean listen_notifications)
{
	g_return_if_fail (CAMEL_IS_SPOOL_SETTINGS (settings));

	if (settings->priv->listen_notifications == listen_notifications)
		return;

	settings->priv->listen_notifications = listen_notifications;

	g_object_notify (G_OBJECT (settings), "listen-notifications");
}
