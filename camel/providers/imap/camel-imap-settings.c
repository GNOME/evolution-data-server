/*
 * camel-imap-settings.c
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

#include "camel-imap-settings.h"

#define CAMEL_IMAP_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAP_SETTINGS, CamelImapSettingsPrivate))

struct _CamelImapSettingsPrivate {
	GMutex *property_lock;
	gchar *namespace;
	gchar *shell_command;
	gchar *real_junk_path;
	gchar *real_trash_path;
	gchar **fetch_headers_extra;

	gboolean check_all;
	gboolean check_subscribed;
	gboolean filter_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean use_namespace;
	gboolean use_real_junk_path;
	gboolean use_real_trash_path;
	gboolean use_shell_command;
	gboolean use_subscriptions;

	CamelFetchHeadersType fetch_headers;
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_CHECK_ALL,
	PROP_CHECK_SUBSCRIBED,
	PROP_FETCH_HEADERS,
	PROP_FETCH_HEADERS_EXTRA,
	PROP_FILTER_ALL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_HOST,
	PROP_NAMESPACE,
	PROP_PORT,
	PROP_REAL_JUNK_PATH,
	PROP_REAL_TRASH_PATH,
	PROP_SECURITY_METHOD,
	PROP_SHELL_COMMAND,
	PROP_USER,
	PROP_USE_NAMESPACE,
	PROP_USE_REAL_JUNK_PATH,
	PROP_USE_REAL_TRASH_PATH,
	PROP_USE_SHELL_COMMAND,
	PROP_USE_SUBSCRIPTIONS
};

G_DEFINE_TYPE_WITH_CODE (
	CamelImapSettings,
	camel_imap_settings,
	CAMEL_TYPE_OFFLINE_SETTINGS,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
imap_settings_set_property (GObject *object,
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

		case PROP_CHECK_ALL:
			camel_imap_settings_set_check_all (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_CHECK_SUBSCRIBED:
			camel_imap_settings_set_check_subscribed (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FETCH_HEADERS:
			camel_imap_settings_set_fetch_headers (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_FETCH_HEADERS_EXTRA:
			camel_imap_settings_set_fetch_headers_extra (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boxed (value));
			return;

		case PROP_FILTER_ALL:
			camel_imap_settings_set_filter_all (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK:
			camel_imap_settings_set_filter_junk (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK_INBOX:
			camel_imap_settings_set_filter_junk_inbox (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_NAMESPACE:
			camel_imap_settings_set_namespace (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_PORT:
			camel_network_settings_set_port (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_REAL_JUNK_PATH:
			camel_imap_settings_set_real_junk_path (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_REAL_TRASH_PATH:
			camel_imap_settings_set_real_trash_path (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_SHELL_COMMAND:
			camel_imap_settings_set_shell_command (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USE_NAMESPACE:
			camel_imap_settings_set_use_namespace (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_REAL_JUNK_PATH:
			camel_imap_settings_set_use_real_junk_path (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_REAL_TRASH_PATH:
			camel_imap_settings_set_use_real_trash_path (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_SHELL_COMMAND:
			camel_imap_settings_set_use_shell_command (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_SUBSCRIPTIONS:
			camel_imap_settings_set_use_subscriptions (
				CAMEL_IMAP_SETTINGS (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imap_settings_get_property (GObject *object,
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

		case PROP_CHECK_ALL:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_check_all (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_CHECK_SUBSCRIBED:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_check_subscribed (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_FETCH_HEADERS:
			g_value_set_enum (
				value,
				camel_imap_settings_get_fetch_headers (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_FETCH_HEADERS_EXTRA:
			g_value_take_boxed (
				value,
				camel_imap_settings_dup_fetch_headers_extra (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_FILTER_ALL:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_filter_all (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_filter_junk (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK_INBOX:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_filter_junk_inbox (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_HOST:
			g_value_take_string (
				value,
				camel_network_settings_dup_host (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_NAMESPACE:
			g_value_take_string (
				value,
				camel_imap_settings_dup_namespace (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_PORT:
			g_value_set_uint (
				value,
				camel_network_settings_get_port (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_REAL_JUNK_PATH:
			g_value_take_string (
				value,
				camel_imap_settings_dup_real_junk_path (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_REAL_TRASH_PATH:
			g_value_take_string (
				value,
				camel_imap_settings_dup_real_trash_path (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_SECURITY_METHOD:
			g_value_set_enum (
				value,
				camel_network_settings_get_security_method (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_SHELL_COMMAND:
			g_value_take_string (
				value,
				camel_imap_settings_dup_shell_command (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USE_NAMESPACE:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_use_namespace (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_USE_REAL_JUNK_PATH:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_use_real_junk_path (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_USE_REAL_TRASH_PATH:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_use_real_trash_path (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_USE_SHELL_COMMAND:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_use_shell_command (
				CAMEL_IMAP_SETTINGS (object)));
			return;

		case PROP_USE_SUBSCRIPTIONS:
			g_value_set_boolean (
				value,
				camel_imap_settings_get_use_subscriptions (
				CAMEL_IMAP_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imap_settings_finalize (GObject *object)
{
	CamelImapSettingsPrivate *priv;

	priv = CAMEL_IMAP_SETTINGS_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->namespace);
	g_free (priv->shell_command);
	g_free (priv->real_junk_path);
	g_free (priv->real_trash_path);
	g_strfreev (priv->fetch_headers_extra);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_settings_parent_class)->finalize (object);
}

static void
camel_imap_settings_class_init (CamelImapSettingsClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelImapSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imap_settings_set_property;
	object_class->get_property = imap_settings_get_property;
	object_class->finalize = imap_settings_finalize;

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_AUTH_MECHANISM,
		"auth-mechanism");

	g_object_class_install_property (
		object_class,
		PROP_CHECK_ALL,
		g_param_spec_boolean (
			"check-all",
			"Check All",
			"Check all folders for new messages",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CHECK_SUBSCRIBED,
		g_param_spec_boolean (
			"check-subscribed",
			"Check Subscribed",
			"Check only subscribed folders for new messages",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FETCH_HEADERS,
		g_param_spec_enum (
			"fetch-headers",
			"Fetch Headers",
			"Headers to fetch in message summaries",
			CAMEL_TYPE_FETCH_HEADERS_TYPE,
			CAMEL_FETCH_HEADERS_BASIC_AND_MAILING_LIST,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FETCH_HEADERS_EXTRA,
		g_param_spec_boxed (
			"fetch-headers-extra",
			"Fetch Headers Extra",
			"Additional headers to fetch in message summaries",
			G_TYPE_STRV,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_ALL,
		g_param_spec_boolean (
			"filter-all",
			"Filter All",
			"Whether to apply filters in all folders",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK,
		g_param_spec_boolean (
			"filter-junk",
			"Filter Junk",
			"Whether to filter junk from all folders",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK_INBOX,
		g_param_spec_boolean (
			"filter-junk-inbox",
			"Filter Junk Inbox",
			"Whether to filter junk from Inbox only",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_HOST,
		"host");

	g_object_class_install_property (
		object_class,
		PROP_NAMESPACE,
		g_param_spec_string (
			"namespace",
			"Namespace",
			"Custom IMAP namespace",
			"",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_PORT,
		"port");

	g_object_class_install_property (
		object_class,
		PROP_REAL_JUNK_PATH,
		g_param_spec_string (
			"real-junk-path",
			"Real Junk Path",
			"Path for a non-virtual Junk folder",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_REAL_TRASH_PATH,
		g_param_spec_string (
			"real-trash-path",
			"Real Trash Path",
			"Path for a non-virtual Trash folder",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");

	g_object_class_install_property (
		object_class,
		PROP_SHELL_COMMAND,
		g_param_spec_string (
			"shell-command",
			"Shell Command",
			"Shell command for connecting to the server",
			"ssh -C -l %u %h exec /usr/sbin/imapd",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_USER,
		"user");

	g_object_class_install_property (
		object_class,
		PROP_USE_NAMESPACE,
		g_param_spec_boolean (
			"use-namespace",
			"Use Namespace",
			"Whether to use a custom IMAP namespace",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_REAL_JUNK_PATH,
		g_param_spec_boolean (
			"use-real-junk-path",
			"Use Real Junk Path",
			"Whether to use a non-virtual Junk folder",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_REAL_TRASH_PATH,
		g_param_spec_boolean (
			"use-real-trash-path",
			"Use Real Trash Path",
			"Whether to use a non-virtual Trash folder",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_SHELL_COMMAND,
		g_param_spec_boolean (
			"use-shell-command",
			"Use Shell Command",
			"Whether to use a custom shell "
			"command to connect to the server",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_SUBSCRIPTIONS,
		g_param_spec_boolean (
			"use-subscriptions",
			"Use Subscriptions",
			"Whether to honor folder subscriptions",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imap_settings_init (CamelImapSettings *settings)
{
	settings->priv = CAMEL_IMAP_SETTINGS_GET_PRIVATE (settings);
	settings->priv->property_lock = g_mutex_new ();

	/* The default namespace is an empty string. */
	settings->priv->namespace = g_strdup ("");
}

/**
 * camel_imap_settings_get_check_all:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to check all folders for new messages.
 *
 * Returns: whether to check all folders for new messages
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_check_all (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->check_all;
}

/**
 * camel_imap_settings_set_check_all:
 * @settings: a #CamelImapSettings
 * @check_all: whether to check all folders for new messages
 *
 * Sets whether to check all folders for new messages.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_check_all (CamelImapSettings *settings,
                                   gboolean check_all)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->check_all == check_all)
		return;

	settings->priv->check_all = check_all;

	g_object_notify (G_OBJECT (settings), "check-all");
}

/**
 * camel_imap_settings_get_check_subscribed:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to check only subscribed folders for new messages.
 * Note that #CamelImapSettings:check-all, if %TRUE, overrides this setting.
 *
 * Returns: whether to check only subscribed folders for new messages
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_check_subscribed (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->check_subscribed;
}

/**
 * camel_imap_settings_set_check_subscribed:
 * @settings: a #CamelImapSettings
 * @check_subscribed: whether to check only subscribed folders for new messages
 *
 * Sets whether to check only subscribed folders for new messages.  Note
 * that #CamelImapSettings:check-all, if %TRUE, overrides this setting.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_check_subscribed (CamelImapSettings *settings,
                                          gboolean check_subscribed)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->check_subscribed == check_subscribed)
		return;

	settings->priv->check_subscribed = check_subscribed;

	g_object_notify (G_OBJECT (settings), "check-subscribed");
}

/**
 * camel_imap_settings_get_fetch_headers:
 * @settings: a #CamelImapSettings
 *
 * Returns the subset of headers to fetch when downloading message summaries.
 * Fewer headers means faster downloads, but filtering rules for incoming
 * messages may require additional headers such as mailing list headers.
 *
 * Returns: which subset of message headers to fetch
 *
 * Since: 3.2
 **/
CamelFetchHeadersType
camel_imap_settings_get_fetch_headers (CamelImapSettings *settings)
{
	g_return_val_if_fail (
		CAMEL_IS_IMAP_SETTINGS (settings),
		CAMEL_FETCH_HEADERS_BASIC);

	return settings->priv->fetch_headers;
}

/**
 * camel_imap_settings_set_fetch_headers:
 * @settings: a #CamelImapSettings
 * @fetch_headers: which subset of message headers to fetch
 *
 * Sets the subset of headers to fetch when downloading message summaries.
 * Fewer headers means faster downloads, but filtering rules for incoming
 * messages may require additional headers such as mailing list headers.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_fetch_headers (CamelImapSettings *settings,
                                       CamelFetchHeadersType fetch_headers)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->fetch_headers == fetch_headers)
		return;

	settings->priv->fetch_headers = fetch_headers;

	g_object_notify (G_OBJECT (settings), "fetch-headers");
}

/**
 * camel_imap_settings_get_fetch_headers_extra:
 * @settings: a #CamelImapSettings
 *
 * Returns a %NULL-terminated list of extra headers to fetch when downloading
 * message summaries, or %NULL if there are no extra headers to fetch.  This
 * is mainly used for filtering rules that check for a specific headeder which
 * is not included in the %CAMEL_FETCH_HEADERS_BASIC_AND_MAILING_LIST subset.
 *
 * Returns: a %NULL-terminated list of extra headers to fetch
 *
 * Since: 3.2
 **/
const gchar * const *
camel_imap_settings_get_fetch_headers_extra (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	return (const gchar * const *) settings->priv->fetch_headers_extra;
}

/**
 * camel_imap_settings_dup_fetch_headers_extra:
 * @settings: a #CamelImapSettings
 *
 * Thread-safe variation of camel_imap_settings_get_fetch_headers_extra().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string array should be freed with g_strfreev() when no
 * longer needed.
 *
 * Returns: a newly-allocated copy of #CamelImapSettings:fetch-headers-extra
 *
 * Since: 3.4
 **/
gchar **
camel_imap_settings_dup_fetch_headers_extra (CamelImapSettings *settings)
{
	const gchar * const *protected;
	gchar **duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_imap_settings_get_fetch_headers_extra (settings);
	duplicate = g_strdupv ((gchar **) protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

static gboolean
fetch_headers_equal (const gchar * const *h1,
                     const gchar * const *h2)
{
	gint ii;

	if (!h1 || !h2)
		return h1 == h2;

	for (ii = 0; h1[ii] && h2[ii]; ii++) {
		if (g_strcmp0 (h1[ii], h2[ii]) != 0)
			return FALSE;
	}

	return !h1[ii] && h1[ii] == h2[ii];
}

/**
 * camel_imap_settings_set_fetch_headers_extra:
 * @settings: a #CamelImapSettings
 * @fetch_headers_extra: a %NULL-terminated list of extra headers to fetch,
 *                       or %NULL
 *
 * Sets a %NULL-terminated list of extra headers to fetch when downloading
 * message summaries.  This is mainly used for filtering rules that check
 * for a specific header which is not included in the
 * %CAMEL_FETCH_HEADERS_BASIC_AND_MAILING_LIST subset.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_fetch_headers_extra (CamelImapSettings *settings,
                                             const gchar * const *fetch_headers_extra)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	if (fetch_headers_equal (
		(const gchar * const *) settings->priv->fetch_headers_extra,
		fetch_headers_extra)) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_strfreev (settings->priv->fetch_headers_extra);
	settings->priv->fetch_headers_extra =
		g_strdupv ((gchar **) fetch_headers_extra);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "fetch-headers-extra");
}

/**
 * camel_imap_settings_get_filter_junk:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Returns: whether to filter junk in all folders
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_filter_junk (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

/**
 * camel_imap_settings_set_filter_junk:
 * @settings: a #CamelImapSettings
 * @filter_junk: whether to filter junk in all filers
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_filter_junk (CamelImapSettings *settings,
                                     gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->filter_junk == filter_junk)
		return;

	settings->priv->filter_junk = filter_junk;

	g_object_notify (G_OBJECT (settings), "filter-junk");
}

/**
 * camel_imap_settings_get_filter_all:
 * @settings: a #CamelImapSettings
 *
 * Returns whether apply filters in all folders.
 *
 * Returns: whether to apply filters in all folders
 *
 * Since: 3.4
 **/
gboolean
camel_imap_settings_get_filter_all (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->filter_all;
}

/**
 * camel_imap_settings_set_filter_all:
 * @settings: a #CamelImapSettings
 * @filter_all: whether to apply filters in all folders
 *
 * Sets whether to apply filters in all folders.
 *
 * Since: 3.4
 **/
void
camel_imap_settings_set_filter_all (CamelImapSettings *settings,
                                    gboolean filter_all)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->filter_all == filter_all)
		return;

	settings->priv->filter_all = filter_all;

	g_object_notify (G_OBJECT (settings), "filter-all");
}

/**
 * camel_imap_settings_get_filter_junk_inbox:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Returns: whether to filter junk in Inbox only
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_filter_junk_inbox (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk_inbox;
}

/**
 * camel_imap_settings_set_filter_junk_inbox:
 * @settings: a #CamelImapSettings
 * @filter_junk_inbox: whether to filter junk in Inbox only
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_filter_junk_inbox (CamelImapSettings *settings,
                                           gboolean filter_junk_inbox)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->filter_junk_inbox == filter_junk_inbox)
		return;

	settings->priv->filter_junk_inbox = filter_junk_inbox;

	g_object_notify (G_OBJECT (settings), "filter-junk-inbox");
}

/**
 * camel_imap_settings_get_namespace:
 * @settings: a #CamelImapSettings
 *
 * Returns the custom IMAP namespace in which to find folders.
 *
 * Returns: the custom IMAP namespace, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_imap_settings_get_namespace (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	return settings->priv->namespace;
}

/**
 * camel_imap_settings_dup_namespace:
 * @settings: a #CamelImapSettings
 *
 * Thread-safe variation of camel_imap_settings_get_namespace().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelImapSettings:namespace
 *
 * Since: 3.4
 **/
gchar *
camel_imap_settings_dup_namespace (CamelImapSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_imap_settings_get_namespace (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imap_settings_set_namespace:
 * @settings: a #CamelImapSettings
 * @namespace: an IMAP namespace, or %NULL
 *
 * Sets the custom IMAP namespace in which to find folders.  If @namespace
 * is %NULL, the default namespace is used.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_namespace (CamelImapSettings *settings,
                                   const gchar *namespace)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	/* The default namespace is an empty string. */
	if (namespace == NULL)
		namespace = "";

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->namespace, namespace) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->namespace);
	settings->priv->namespace = g_strdup (namespace);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "namespace");
}

/**
 * camel_imap_settings_get_real_junk_path:
 * @settings: a #CamelImapSettings
 *
 * Returns the path to a real, non-virtual Junk folder to be used instead
 * of Camel's standard virtual Junk folder.
 *
 * Returns: path to a real Junk folder
 *
 * Since: 3.2
 **/
const gchar *
camel_imap_settings_get_real_junk_path (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	return settings->priv->real_junk_path;
}

/**
 * camel_imap_settings_dup_real_junk_path:
 * @settings: a #CamelImapSettings
 *
 * Thread-safe variation of camel_imap_settings_get_real_junk_path().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelImapSettings:real-junk-path
 *
 * Since: 3.4
 **/
gchar *
camel_imap_settings_dup_real_junk_path (CamelImapSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_imap_settings_get_real_junk_path (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imap_settings_set_real_junk_path:
 * @settings: a #CamelImapSettings
 * @real_junk_path: path to a real Junk folder, or %NULL
 *
 * Sets the path to a real, non-virtual Junk folder to be used instead of
 * Camel's standard virtual Junk folder.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_real_junk_path (CamelImapSettings *settings,
                                        const gchar *real_junk_path)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (real_junk_path != NULL && *real_junk_path == '\0')
		real_junk_path = NULL;

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->real_junk_path, real_junk_path) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->real_junk_path);
	settings->priv->real_junk_path = g_strdup (real_junk_path);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "real-junk-path");
}

/**
 * camel_imap_settings_get_real_trash_path:
 * @settings: a #CamelImapSettings
 *
 * Returns the path to a real, non-virtual Trash folder to be used instead
 * of Camel's standard virtual Trash folder.
 *
 * Returns: path to a real Trash folder
 *
 * Since: 3.2
 **/
const gchar *
camel_imap_settings_get_real_trash_path (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	return settings->priv->real_trash_path;
}

/**
 * camel_imap_settings_dup_real_trash_path:
 * @settings: a #CamelImapSettings
 *
 * Thread-safe variation of camel_imap_settings_get_real_trash_path().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelImapSettings:real-trash-path
 *
 * Since: 3.4
 **/
gchar *
camel_imap_settings_dup_real_trash_path (CamelImapSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_imap_settings_get_real_trash_path (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imap_settings_set_real_trash_path:
 * @settings: a #CamelImapSettings
 * @real_trash_path: path to a real Trash folder, or %NULL
 *
 * Sets the path to a real, non-virtual Trash folder to be used instead of
 * Camel's standard virtual Trash folder.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_real_trash_path (CamelImapSettings *settings,
                                         const gchar *real_trash_path)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (real_trash_path != NULL && *real_trash_path == '\0')
		real_trash_path = NULL;

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->real_trash_path, real_trash_path) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->real_trash_path);
	settings->priv->real_trash_path = g_strdup (real_trash_path);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "real-trash-path");
}

/**
 * camel_imap_settings_get_shell_command:
 * @settings: a #CamelImapSettings
 *
 * Returns an optional shell command used to establish an input/output
 * stream with an IMAP server.  Normally the input/output stream is
 * established through a network socket.
 *
 * This option is useful only to a select few advanced users who likely
 * administer their own IMAP server.  Most users will not understand what
 * this option means or how to use it.  Probably not worth exposing in a
 * graphical interface.
 *
 * Returns: shell command for connecting to the server, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_imap_settings_get_shell_command (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	return settings->priv->shell_command;
}

/**
 * camel_imap_settings_dup_shell_command:
 * @settings: a #CamelImapSettings
 *
 * Thread-safe variation of camel_imap_settings_get_shell_command().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelImapSettings:shell-command
 *
 * Since: 3.4
 **/
gchar *
camel_imap_settings_dup_shell_command (CamelImapSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_imap_settings_get_shell_command (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imap_settings_set_shell_command:
 * @settings: a #CamelImapSettings
 * @shell_command: shell command for connecting to the server, or %NULL
 *
 * Sets an optional shell command used to establish an input/output stream
 * with an IMAP server.  Normally the input/output stream is established
 * through a network socket.
 *
 * This option is useful only to a select few advanced users who likely
 * administer their own IMAP server.  Most users will not understand what
 * this option means or how to use it.  Probably not worth exposing in a
 * graphical interface.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_shell_command (CamelImapSettings *settings,
                                       const gchar *shell_command)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (shell_command != NULL && *shell_command == '\0')
		shell_command = NULL;

	g_mutex_lock (settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->shell_command, shell_command) == 0) {
		g_mutex_unlock (settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->shell_command);
	settings->priv->shell_command = g_strdup (shell_command);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "shell-command");
}

/**
 * camel_imap_settings_get_use_namespace:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to use a custom IMAP namespace to find folders.  The
 * namespace itself is given by the #CamelImapSettings:namespace property.
 *
 * Returns: whether to use a custom IMAP namespace
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_use_namespace (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->use_namespace;
}

/**
 * camel_imap_settings_set_use_namespace:
 * @settings: a #CamelImapSettings
 * @use_namespace: whether to use a custom IMAP namespace
 *
 * Sets whether to use a custom IMAP namespace to find folders.  The
 * namespace itself is given by the #CamelImapSettings:namespace property.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_use_namespace (CamelImapSettings *settings,
                                       gboolean use_namespace)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->use_namespace == use_namespace)
		return;

	settings->priv->use_namespace = use_namespace;

	g_object_notify (G_OBJECT (settings), "use-namespace");
}

/**
 * camel_imap_settings_get_use_real_junk_path:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to use a real, non-virtual Junk folder instead of Camel's
 * standard virtual Junk folder.
 *
 * Returns: whether to use a real Junk folder
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_use_real_junk_path (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->use_real_junk_path;
}

/**
 * camel_imap_settings_set_use_real_junk_path:
 * @settings: a #CamelImapSettings
 * @use_real_junk_path: whether to use a real Junk folder
 *
 * Sets whether to use a real, non-virtual Junk folder instead of Camel's
 * standard virtual Junk folder.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_use_real_junk_path (CamelImapSettings *settings,
                                            gboolean use_real_junk_path)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->use_real_junk_path == use_real_junk_path)
		return;

	settings->priv->use_real_junk_path = use_real_junk_path;

	g_object_notify (G_OBJECT (settings), "use-real-junk-path");
}

/**
 * camel_imap_settings_get_use_real_trash_path:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to use a real, non-virtual Trash folder instead of Camel's
 * standard virtual Trash folder.
 *
 * Returns: whether to use a real Trash folder
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_use_real_trash_path (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->use_real_trash_path;
}

/**
 * camel_imap_settings_set_use_real_trash_path:
 * @settings: a #CamelImapSettings
 * @use_real_trash_path: whether to use a real Trash folder
 *
 * Sets whether to use a real, non-virtual Trash folder instead of Camel's
 * standard virtual Trash folder.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_use_real_trash_path (CamelImapSettings *settings,
                                             gboolean use_real_trash_path)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->use_real_trash_path == use_real_trash_path)
		return;

	settings->priv->use_real_trash_path = use_real_trash_path;

	g_object_notify (G_OBJECT (settings), "use-real-trash-path");
}

/**
 * camel_imap_settings_get_use_shell_command:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to use a custom shell command to establish an input/output
 * stream with an IMAP server, instead of the more common method of opening a
 * network socket.  The shell command itself is given by the
 * #CamelImapSettings:shell-command property.
 *
 * This option is useful only to a select few advanced users who likely
 * administer their own IMAP server.  Most users will not understand what
 * this option means or how to use it.  Probably not worth exposing in a
 * graphical interface.
 *
 * Returns: whether to use a custom shell command to connect to the server
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_use_shell_command (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->use_shell_command;
}

/**
 * camel_imap_settings_set_use_shell_command:
 * @settings: a #CamelImapSettings
 * @use_shell_command: whether to use a custom shell command to connect
 *                     to the server
 *
 * Sets whether to use a custom shell command to establish an input/output
 * stream with an IMAP server, instead of the more common method of opening
 * a network socket.  The shell command itself is given by the
 * #CamelImapSettings:shell-command property.
 *
 * This option is useful only to a select few advanced users who likely
 * administer their own IMAP server.  Most users will not understand what
 * this option means or how to use it.  Probably not worth exposing in a
 * graphical interface.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_use_shell_command (CamelImapSettings *settings,
                                           gboolean use_shell_command)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->use_shell_command == use_shell_command)
		return;

	settings->priv->use_shell_command = use_shell_command;

	g_object_notify (G_OBJECT (settings), "use-shell-command");
}

/**
 * camel_imap_settings_get_use_subscriptions:
 * @settings: a #CamelImapSettings
 *
 * Returns whether to list and operate only on subscribed folders, or to
 * list and operate on all available folders regardless of subscriptions.
 *
 * Returns: whether to honor folder subscriptions
 *
 * Since: 3.2
 **/
gboolean
camel_imap_settings_get_use_subscriptions (CamelImapSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_SETTINGS (settings), FALSE);

	return settings->priv->use_subscriptions;
}

/**
 * camel_imap_settings_set_use_subscriptions:
 * @settings: a #CamelImapSettings
 * @use_subscriptions: whether to honor folder subscriptions
 *
 * Sets whether to list and operate only on subscribed folders, or to
 * list and operate on all available folders regardless of subscriptions.
 *
 * Since: 3.2
 **/
void
camel_imap_settings_set_use_subscriptions (CamelImapSettings *settings,
                                           gboolean use_subscriptions)
{
	g_return_if_fail (CAMEL_IS_IMAP_SETTINGS (settings));

	if (settings->priv->use_subscriptions == use_subscriptions)
		return;

	settings->priv->use_subscriptions = use_subscriptions;

	g_object_notify (G_OBJECT (settings), "use-subscriptions");
}

