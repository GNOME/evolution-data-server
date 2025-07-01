/*
 * camel-local-settings.c
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

#include "camel-local-settings.h"

#include <string.h>

struct _CamelLocalSettingsPrivate {
	GMutex property_lock;
	gchar *path;
	gboolean filter_all;
	gboolean filter_junk;
	gboolean maildir_alt_flag_sep;
};

enum {
	PROP_0,
	PROP_FILTER_ALL,
	PROP_FILTER_JUNK,
	PROP_MAILDIR_ALT_FLAG_SEP,
	PROP_PATH,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (
	CamelLocalSettings,
	camel_local_settings,
	CAMEL_TYPE_STORE_SETTINGS)

static void
local_settings_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FILTER_ALL:
			camel_local_settings_set_filter_all (
				CAMEL_LOCAL_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK:
			camel_local_settings_set_filter_junk (
				CAMEL_LOCAL_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_MAILDIR_ALT_FLAG_SEP:
			camel_local_settings_set_maildir_alt_flag_sep (
				CAMEL_LOCAL_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_PATH:
			camel_local_settings_set_path (
				CAMEL_LOCAL_SETTINGS (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
local_settings_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FILTER_ALL:
			g_value_set_boolean (
				value,
				camel_local_settings_get_filter_all (
				CAMEL_LOCAL_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_local_settings_get_filter_junk (
				CAMEL_LOCAL_SETTINGS (object)));
			return;

		case PROP_MAILDIR_ALT_FLAG_SEP:
			g_value_set_boolean (
				value,
				camel_local_settings_get_maildir_alt_flag_sep (
				CAMEL_LOCAL_SETTINGS (object)));
			return;

		case PROP_PATH:
			g_value_take_string (
				value,
				camel_local_settings_dup_path (
				CAMEL_LOCAL_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
local_settings_finalize (GObject *object)
{
	CamelLocalSettingsPrivate *priv;

	priv = CAMEL_LOCAL_SETTINGS (object)->priv;

	g_mutex_clear (&priv->property_lock);

	g_free (priv->path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_local_settings_parent_class)->finalize (object);
}

static void
camel_local_settings_class_init (CamelLocalSettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = local_settings_set_property;
	object_class->get_property = local_settings_get_property;
	object_class->finalize = local_settings_finalize;

	/**
	 * CamelLocalSettings:path
	 *
	 * File path to the local store
	 **/
	properties[PROP_PATH] =
		g_param_spec_string (
			"path", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelLocalSettings:filter-all
	 *
	 * Whether to apply filters in all folders
	 **/
	properties[PROP_FILTER_ALL] =
		g_param_spec_boolean (
			"filter-all", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelLocalSettings:filter-junk
	 *
	 * Whether to check new messages for junk
	 **/
	properties[PROP_FILTER_JUNK] =
		g_param_spec_boolean (
			"filter-junk", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelLocalSettings:maildir-alt-flag-sep
	 *
	 * Whether to use alternative flag separator in Maildir file name
	 **/
	properties[PROP_MAILDIR_ALT_FLAG_SEP] =
		g_param_spec_boolean (
			"maildir-alt-flag-sep", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
camel_local_settings_init (CamelLocalSettings *settings)
{
	settings->priv = camel_local_settings_get_instance_private (settings);
	g_mutex_init (&settings->priv->property_lock);
}

/**
 * camel_local_settings_get_path:
 * @settings: a #CamelLocalSettings
 *
 * Returns the file path to the root of the local mail store.
 *
 * Returns: the file path to the local store
 *
 * Since: 3.4
 **/
const gchar *
camel_local_settings_get_path (CamelLocalSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings), NULL);

	return settings->priv->path;
}

/**
 * camel_local_settings_dup_path:
 * @settings: a #CamelLocalSettings
 *
 * Thread-safe variation of camel_local_settings_get_path().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelLocalSettings:path
 *
 * Since: 3.4
 **/
gchar *
camel_local_settings_dup_path (CamelLocalSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_local_settings_get_path (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_local_settings_set_path:
 * @settings: a #CamelLocalSettings
 * @path: the file path to the local store
 *
 * Sets the file path to the root of the local mail store.  Any
 * trailing directory separator characters will be stripped off
 * of the #CamelLocalSettings:path property.
 *
 * Since: 3.4
 **/
void
camel_local_settings_set_path (CamelLocalSettings *settings,
                               const gchar *path)
{
	gsize length = 0;
	gchar *new_path;

	g_return_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings));

	/* Exclude trailing directory separators. */
	if (path != NULL) {
		length = strlen (path);
		while (length > 0) {
			if (G_IS_DIR_SEPARATOR (path[length - 1]))
				length--;
			else
				break;
		}
	}

	g_mutex_lock (&settings->priv->property_lock);

	new_path = g_strndup (path, length);

	if (g_strcmp0 (settings->priv->path, new_path) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		g_free (new_path);
		return;
	}

	g_free (settings->priv->path);
	settings->priv->path = new_path;

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_PATH]);
}

/**
 * camel_local_settings_get_filter_all:
 * @settings: a #CamelLocalSettings
 *
 * Returns whether apply filters in all folders.
 *
 * Returns: whether to apply filters in all folders
 *
 * Since: 3.24
 **/
gboolean
camel_local_settings_get_filter_all (CamelLocalSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings), FALSE);

	return settings->priv->filter_all;
}

/**
 * camel_local_settings_set_filter_all:
 * @settings: a #CamelLocalSettings
 * @filter_all: whether to apply filters in all folders
 *
 * Sets whether to apply filters in all folders.
 *
 * Since: 3.24
 **/
void
camel_local_settings_set_filter_all (CamelLocalSettings *settings,
				     gboolean filter_all)
{
	g_return_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings));

	if (settings->priv->filter_all == filter_all)
		return;

	settings->priv->filter_all = filter_all;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FILTER_ALL]);
}

/**
 * camel_local_settings_get_filter_junk:
 * @settings: a #CamelLocalSettings
 *
 * Returns whether to check new messages for junk.
 *
 * Returns: whether to check new messages for junk
 *
 * Since: 3.24
 **/
gboolean
camel_local_settings_get_filter_junk (CamelLocalSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

/**
 * camel_local_settings_set_filter_junk:
 * @settings: a #CamelLocalSettings
 * @filter_junk: whether to check new messages for junk
 *
 * Sets whether to check new messages for junk.
 *
 * Since: 3.24
 **/
void
camel_local_settings_set_filter_junk (CamelLocalSettings *settings,
				      gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings));

	if (settings->priv->filter_junk == filter_junk)
		return;

	settings->priv->filter_junk = filter_junk;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FILTER_JUNK]);
}

/**
 * camel_local_settings_get_maildir_alt_flag_sep:
 * @settings: a #CamelLocalSettings
 *
 * Returns, whether the Maildir provider should use alternative
 * flag separator in the file name. When %TRUE, uses an exclamation
 * mark (!), when %FALSE, uses the colon (:). The default
 * is %FALSE, to be consistent with the Maildir specification.
 * The flag separator is flipped on the Windows build.
 *
 * Returns: whether the Maildir provider should use an alternative flag separator
 *
 * Since: 3.40
 **/
gboolean
camel_local_settings_get_maildir_alt_flag_sep (CamelLocalSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings), FALSE);

	return settings->priv->maildir_alt_flag_sep;
}

/**
 * camel_local_settings_set_maildir_alt_flag_sep:
 * @settings: a #CamelLocalSettings
 * @maildir_alt_flag_sep: value to set
 *
 * Sets whether Maildir should use alternative flag separator.
 * See camel_local_settings_get_maildir_alt_flag_sep() for more
 * information on what it means.
 *
 * Note: Change to this setting takes effect only for newly created
 *     Maildir stores.
 *
 * Since: 3.40
 **/
void
camel_local_settings_set_maildir_alt_flag_sep (CamelLocalSettings *settings,
					       gboolean maildir_alt_flag_sep)
{
	g_return_if_fail (CAMEL_IS_LOCAL_SETTINGS (settings));

	if ((settings->priv->maildir_alt_flag_sep ? 1 : 0) == (maildir_alt_flag_sep ? 1 : 0))
		return;

	settings->priv->maildir_alt_flag_sep = maildir_alt_flag_sep;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_MAILDIR_ALT_FLAG_SEP]);
}
