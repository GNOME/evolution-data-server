/*
 * camel-store-settings.c
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

#include "camel-store-settings.h"

struct _CamelStoreSettingsPrivate {
	gboolean filter_inbox;
	gint store_changes_interval;
};

enum {
	PROP_0,
	PROP_FILTER_INBOX,
	PROP_STORE_CHANGES_INTERVAL,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (
	CamelStoreSettings,
	camel_store_settings,
	CAMEL_TYPE_SETTINGS)

static void
store_settings_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FILTER_INBOX:
			camel_store_settings_set_filter_inbox (
				CAMEL_STORE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_STORE_CHANGES_INTERVAL:
			camel_store_settings_set_store_changes_interval (
				CAMEL_STORE_SETTINGS (object),
				g_value_get_int (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
store_settings_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FILTER_INBOX:
			g_value_set_boolean (
				value,
				camel_store_settings_get_filter_inbox (
				CAMEL_STORE_SETTINGS (object)));
			return;

		case PROP_STORE_CHANGES_INTERVAL:
			g_value_set_int (
				value,
				camel_store_settings_get_store_changes_interval (
				CAMEL_STORE_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
camel_store_settings_class_init (CamelStoreSettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = store_settings_set_property;
	object_class->get_property = store_settings_get_property;

	/**
	 * CamelStoreSettings:filter-inbox
	 *
	 * Whether to filter new messages in Inbox
	 **/
	properties[PROP_FILTER_INBOX] =
		g_param_spec_boolean (
			"filter-inbox", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelStoreSettings:store-changes-interval
	 *
	 * Interval, in seconds, to store folder changes
	 **/
	properties[PROP_STORE_CHANGES_INTERVAL] =
		g_param_spec_int (
			"store-changes-interval", NULL, NULL,
			-1,
			999,
			3,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
camel_store_settings_init (CamelStoreSettings *settings)
{
	settings->priv = camel_store_settings_get_instance_private (settings);
}

/**
 * camel_store_settings_get_filter_inbox:
 * @settings: a #CamelStoreSettings
 *
 * Returns whether to automatically apply filters to newly arrived messages
 * in the store's Inbox folder (assuming it has an Inbox folder).
 *
 * Returns: whether to filter new messages in Inbox
 *
 * Since: 3.2
 **/
gboolean
camel_store_settings_get_filter_inbox (CamelStoreSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_STORE_SETTINGS (settings), FALSE);

	return settings->priv->filter_inbox;
}

/**
 * camel_store_settings_set_filter_inbox:
 * @settings: a #CamelStoreSettings
 * @filter_inbox: whether to filter new messages in Inbox
 *
 * Sets whether to automatically apply filters to newly arrived messages
 * in the store's Inbox folder (assuming it has an Inbox folder).
 *
 * Since: 3.2
 **/
void
camel_store_settings_set_filter_inbox (CamelStoreSettings *settings,
                                       gboolean filter_inbox)
{
	g_return_if_fail (CAMEL_IS_STORE_SETTINGS (settings));

	if (settings->priv->filter_inbox == filter_inbox)
		return;

	settings->priv->filter_inbox = filter_inbox;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FILTER_INBOX]);
}

/**
 * camel_store_settings_get_store_changes_interval:
 * @settings: a #CamelStoreSettings
 *
 * Returns the interval, in seconds, for the changes in the folder being
 * saved automatically. 0 means immediately, while -1 means turning off
 * automatic folder change saving.
 *
 * Returns: the interval for automatic store of folder changes
 *
 * Since: 3.40
 **/
gint
camel_store_settings_get_store_changes_interval (CamelStoreSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_STORE_SETTINGS (settings), -1);

	return settings->priv->store_changes_interval;
}

/**
 * camel_store_settings_set_store_changes_interval:
 * @settings: a #CamelStoreSettings
 * @interval: the interval, in seconds
 *
 * Sets the interval, in seconds, for the changes in the folder being
 * saved automatically. 0 means immediately, while -1 means turning off
 * automatic folder change saving.
 *
 * Since: 3.40
 **/
void
camel_store_settings_set_store_changes_interval (CamelStoreSettings *settings,
						 gint interval)
{
	g_return_if_fail (CAMEL_IS_STORE_SETTINGS (settings));

	if (settings->priv->store_changes_interval == interval)
		return;

	settings->priv->store_changes_interval = interval;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_STORE_CHANGES_INTERVAL]);
}
