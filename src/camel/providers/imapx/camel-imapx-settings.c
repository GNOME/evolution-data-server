/*
 * camel-imapx-settings.c
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

#include "evolution-data-server-config.h"

#include "camel-imapx-settings.h"

#define MIN_CONCURRENT_CONNECTIONS 1
#define MAX_CONCURRENT_CONNECTIONS 7

struct _CamelIMAPXSettingsPrivate {
	GMutex property_lock;
	gchar *namespace;
	gchar *real_junk_path;
	gchar *real_not_junk_path;
	gchar *real_trash_path;
	gchar *shell_command;

	guint concurrent_connections;

	gboolean use_multi_fetch;
	gboolean check_all;
	gboolean check_subscribed;
	gboolean filter_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean use_idle;
	gboolean use_namespace;
	gboolean use_qresync;
	gboolean use_real_junk_path;
	gboolean use_real_not_junk_path;
	gboolean use_real_trash_path;
	gboolean use_shell_command;
	gboolean use_subscriptions;
	gboolean ignore_other_users_namespace;
	gboolean ignore_shared_folders_namespace;
	gboolean full_update_on_metered_network;
	gboolean send_client_id;
	gboolean single_client_mode;

	CamelSortType fetch_order;
};

enum {
	PROP_0,
	PROP_USE_MULTI_FETCH,
	PROP_CHECK_ALL,
	PROP_CHECK_SUBSCRIBED,
	PROP_CONCURRENT_CONNECTIONS,
	PROP_FETCH_ORDER,
	PROP_FILTER_ALL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_NAMESPACE,
	PROP_REAL_JUNK_PATH,
	PROP_REAL_NOT_JUNK_PATH,
	PROP_REAL_TRASH_PATH,
	PROP_SHELL_COMMAND,
	PROP_USE_IDLE,
	PROP_USE_NAMESPACE,
	PROP_USE_QRESYNC,
	PROP_USE_REAL_JUNK_PATH,
	PROP_USE_REAL_NOT_JUNK_PATH,
	PROP_USE_REAL_TRASH_PATH,
	PROP_USE_SHELL_COMMAND,
	PROP_USE_SUBSCRIPTIONS,
	PROP_IGNORE_OTHER_USERS_NAMESPACE,
	PROP_IGNORE_SHARED_FOLDERS_NAMESPACE,
	PROP_FULL_UPDATE_ON_METERED_NETWORK,
	PROP_SEND_CLIENT_ID,
	PROP_SINGLE_CLIENT_MODE,
	N_PROPS,

	PROP_AUTH_MECHANISM,
	PROP_HOST,
	PROP_PORT,
	PROP_USER,
	PROP_SECURITY_METHOD
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_CODE (
	CamelIMAPXSettings,
	camel_imapx_settings,
	CAMEL_TYPE_OFFLINE_SETTINGS,
	G_ADD_PRIVATE (CamelIMAPXSettings)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
imapx_settings_set_property (GObject *object,
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

		case PROP_USE_MULTI_FETCH:
			camel_imapx_settings_set_use_multi_fetch (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_CHECK_ALL:
			camel_imapx_settings_set_check_all (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_CHECK_SUBSCRIBED:
			camel_imapx_settings_set_check_subscribed (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			camel_imapx_settings_set_concurrent_connections (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_FETCH_ORDER:
			camel_imapx_settings_set_fetch_order (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_FILTER_ALL:
			camel_imapx_settings_set_filter_all (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK:
			camel_imapx_settings_set_filter_junk (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK_INBOX:
			camel_imapx_settings_set_filter_junk_inbox (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_NAMESPACE:
			camel_imapx_settings_set_namespace (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_PORT:
			camel_network_settings_set_port (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_REAL_JUNK_PATH:
			camel_imapx_settings_set_real_junk_path (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_REAL_NOT_JUNK_PATH:
			camel_imapx_settings_set_real_not_junk_path (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_REAL_TRASH_PATH:
			camel_imapx_settings_set_real_trash_path (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_SHELL_COMMAND:
			camel_imapx_settings_set_shell_command (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USE_IDLE:
			camel_imapx_settings_set_use_idle (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_NAMESPACE:
			camel_imapx_settings_set_use_namespace (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_QRESYNC:
			camel_imapx_settings_set_use_qresync (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_REAL_JUNK_PATH:
			camel_imapx_settings_set_use_real_junk_path (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_REAL_NOT_JUNK_PATH:
			camel_imapx_settings_set_use_real_not_junk_path (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_REAL_TRASH_PATH:
			camel_imapx_settings_set_use_real_trash_path (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_SHELL_COMMAND:
			camel_imapx_settings_set_use_shell_command (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_SUBSCRIPTIONS:
			camel_imapx_settings_set_use_subscriptions (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_IGNORE_OTHER_USERS_NAMESPACE:
			camel_imapx_settings_set_ignore_other_users_namespace (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_IGNORE_SHARED_FOLDERS_NAMESPACE:
			camel_imapx_settings_set_ignore_shared_folders_namespace (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FULL_UPDATE_ON_METERED_NETWORK:
			camel_imapx_settings_set_full_update_on_metered_network (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_SEND_CLIENT_ID:
			camel_imapx_settings_set_send_client_id (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_SINGLE_CLIENT_MODE:
			camel_imapx_settings_set_single_client_mode (
				CAMEL_IMAPX_SETTINGS (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_settings_get_property (GObject *object,
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

		case PROP_USE_MULTI_FETCH:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_multi_fetch (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_CHECK_ALL:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_check_all (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_CHECK_SUBSCRIBED:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_check_subscribed (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				camel_imapx_settings_get_concurrent_connections (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_FETCH_ORDER:
			g_value_set_enum (
				value,
				camel_imapx_settings_get_fetch_order (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_FILTER_ALL:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_filter_all (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_filter_junk (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK_INBOX:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_filter_junk_inbox (
				CAMEL_IMAPX_SETTINGS (object)));
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
				camel_imapx_settings_dup_namespace (
				CAMEL_IMAPX_SETTINGS (object)));
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
				camel_imapx_settings_dup_real_junk_path (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_REAL_NOT_JUNK_PATH:
			g_value_take_string (
				value,
				camel_imapx_settings_dup_real_not_junk_path (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_REAL_TRASH_PATH:
			g_value_take_string (
				value,
				camel_imapx_settings_dup_real_trash_path (
				CAMEL_IMAPX_SETTINGS (object)));
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
				camel_imapx_settings_dup_shell_command (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USE_IDLE:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_idle (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_NAMESPACE:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_namespace (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_QRESYNC:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_qresync (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_REAL_JUNK_PATH:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_real_junk_path (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_REAL_NOT_JUNK_PATH:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_real_not_junk_path (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_REAL_TRASH_PATH:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_real_trash_path (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_SHELL_COMMAND:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_shell_command (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_USE_SUBSCRIPTIONS:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_use_subscriptions (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_IGNORE_OTHER_USERS_NAMESPACE:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_ignore_other_users_namespace (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_IGNORE_SHARED_FOLDERS_NAMESPACE:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_ignore_shared_folders_namespace (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_FULL_UPDATE_ON_METERED_NETWORK:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_full_update_on_metered_network (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_SEND_CLIENT_ID:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_send_client_id (
				CAMEL_IMAPX_SETTINGS (object)));
			return;

		case PROP_SINGLE_CLIENT_MODE:
			g_value_set_boolean (
				value,
				camel_imapx_settings_get_single_client_mode (
				CAMEL_IMAPX_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_settings_finalize (GObject *object)
{
	CamelIMAPXSettingsPrivate *priv;

	priv = CAMEL_IMAPX_SETTINGS (object)->priv;

	g_mutex_clear (&priv->property_lock);

	g_free (priv->namespace);
	g_free (priv->shell_command);
	g_free (priv->real_trash_path);
	g_free (priv->real_junk_path);
	g_free (priv->real_not_junk_path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_settings_parent_class)->finalize (object);
}

static void
camel_imapx_settings_class_init (CamelIMAPXSettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_settings_set_property;
	object_class->get_property = imapx_settings_get_property;
	object_class->finalize = imapx_settings_finalize;

	/**
	 * CamelIMAPXSettings:use-multi-fetch
	 *
	 * Whether allow downloading of large messages in chunks
	 **/
	properties[PROP_USE_MULTI_FETCH] =
		g_param_spec_boolean (
			"use-multi-fetch", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:check-all
	 *
	 * Check all folders for new messages
	 **/
	properties[PROP_CHECK_ALL] =
		g_param_spec_boolean (
			"check-all", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:check-subscribed
	 *
	 * Check only subscribed folders for new messages
	 **/
	properties[PROP_CHECK_SUBSCRIBED] =
		g_param_spec_boolean (
			"check-subscribed", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:concurrent-connections
	 *
	 * Number of concurrent IMAP connections to use
	 **/
	properties[PROP_CONCURRENT_CONNECTIONS] =
		g_param_spec_uint (
			"concurrent-connections", NULL, NULL,
			MIN_CONCURRENT_CONNECTIONS,
			MAX_CONCURRENT_CONNECTIONS,
			3,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:fetch-order
	 *
	 * Order in which new messages should be fetched
	 **/
	properties[PROP_FETCH_ORDER] =
		g_param_spec_enum (
			"fetch-order", NULL, NULL,
			CAMEL_TYPE_SORT_TYPE,
			CAMEL_SORT_ASCENDING,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:filter-all
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
	 * CamelIMAPXSettings:filter-junk
	 *
	 * Whether to filter junk from all folders
	 **/
	properties[PROP_FILTER_JUNK] =
		g_param_spec_boolean (
			"filter-junk", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:filter-junk-inbox
	 *
	 * Whether to filter junk from Inbox only
	 **/
	properties[PROP_FILTER_JUNK_INBOX] =
		g_param_spec_boolean (
			"filter-junk-inbox", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:namespace
	 *
	 * Custom IMAP namespace
	 **/
	properties[PROP_NAMESPACE] =
		g_param_spec_string (
			"namespace", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:real-junk-path
	 *
	 * Path for a non-virtual Junk folder
	 **/
	properties[PROP_REAL_JUNK_PATH] =
		g_param_spec_string (
			"real-junk-path", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:real-not-junk-path
	 *
	 * Path to restore Not-Junk messages from a non-virtual Junk folder
	 **/
	properties[PROP_REAL_NOT_JUNK_PATH] =
		g_param_spec_string (
			"real-not-junk-path", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:real-trash-path
	 *
	 * Path for a non-virtual Trash folder
	 **/
	properties[PROP_REAL_TRASH_PATH] =
		g_param_spec_string (
			"real-trash-path", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:shell-command
	 *
	 * Shell command for connecting to the server
	 **/
	properties[PROP_SHELL_COMMAND] =
		g_param_spec_string (
			"shell-command", NULL, NULL,
			"ssh -C -l %u %h exec /usr/sbin/imapd",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-idle
	 *
	 * Whether to use the IDLE IMAP extension
	 **/
	properties[PROP_USE_IDLE] =
		g_param_spec_boolean (
			"use-idle", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-namespace
	 *
	 * Whether to use a custom IMAP namespace
	 **/
	properties[PROP_USE_NAMESPACE] =
		g_param_spec_boolean (
			"use-namespace", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-qresync
	 *
	 * Whether to use the QRESYNC IMAP extension
	 **/
	properties[PROP_USE_QRESYNC] =
		g_param_spec_boolean (
			"use-qresync", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-real-junk-path
	 *
	 * Whether to use a non-virtual Junk folder
	 **/
	properties[PROP_USE_REAL_JUNK_PATH] =
		g_param_spec_boolean (
			"use-real-junk-path", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-real-not-junk-path
	 *
	 * Whether to use a special folder to restore Not-Junk messages from non-virtual Junk folder to
	 **/
	properties[PROP_USE_REAL_NOT_JUNK_PATH] =
		g_param_spec_boolean (
			"use-real-not-junk-path", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-real-trash-path
	 *
	 * Whether to use a non-virtual Trash folder
	 **/
	properties[PROP_USE_REAL_TRASH_PATH] =
		g_param_spec_boolean (
			"use-real-trash-path", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-shell-command
	 *
	 * Whether to use a custom shell command to connect to the server
	 **/
	properties[PROP_USE_SHELL_COMMAND] =
		g_param_spec_boolean (
			"use-shell-command", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:use-subscriptions
	 *
	 * Whether to honor folder subscriptions
	 **/
	properties[PROP_USE_SUBSCRIPTIONS] =
		g_param_spec_boolean (
			"use-subscriptions", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:ignore-other-users-namespace
	 *
	 * Whether to ignore other users namespace
	 **/
	properties[PROP_IGNORE_OTHER_USERS_NAMESPACE] =
		g_param_spec_boolean (
			"ignore-other-users-namespace", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:ignore-shared-folders-namespace
	 *
	 * Whether to ignore shared folders namespace
	 **/
	properties[PROP_IGNORE_SHARED_FOLDERS_NAMESPACE] =
		g_param_spec_boolean (
			"ignore-shared-folders-namespace", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:full-update-on-metered-network
	 *
	 * Whether can do full folder update even on metered network
	 **/
	properties[PROP_FULL_UPDATE_ON_METERED_NETWORK] =
		g_param_spec_boolean (
			"full-update-on-metered-network", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:send-client-id
	 *
	 * Whether to send client ID to the server
	 **/
	properties[PROP_SEND_CLIENT_ID] =
		g_param_spec_boolean (
			"send-client-id", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelIMAPXSettings:single-client-mode
	 *
	 * When set to true, does full folder flags refresh only once per day
	 **/
	properties[PROP_SINGLE_CLIENT_MODE] =
		g_param_spec_boolean (
			"single-client-mode", NULL, NULL,
			FALSE,
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
		PROP_USER,
		"user");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");
}

static void
camel_imapx_settings_init (CamelIMAPXSettings *settings)
{
	settings->priv = camel_imapx_settings_get_instance_private (settings);
	g_mutex_init (&settings->priv->property_lock);
}

/**
 * camel_imapx_settings_get_use_multi_fetch:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether large messages can be downloaded in chunks.
 * The default is %TRUE, but some server can be slower when
 * the messages are downloaded in parts, rather than in one call.
 *
 * Returns: whether large messages can be downloaded in chunks
 *
 * Since: 3.20
 **/
gboolean
camel_imapx_settings_get_use_multi_fetch (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_multi_fetch;
}

/**
 * camel_imapx_settings_set_use_multi_fetch:
 * @settings: a #CamelIMAPXSettings
 * @use_multi_fetch: whether can download large messages in chunks
 *
 * Sets whether can download large messages in chunks.
 *
 * Since: 3.20
 **/
void
camel_imapx_settings_set_use_multi_fetch (CamelIMAPXSettings *settings,
					  gboolean use_multi_fetch)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_multi_fetch == use_multi_fetch)
		return;

	settings->priv->use_multi_fetch = use_multi_fetch;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_MULTI_FETCH]);
}

/**
 * camel_imapx_settings_get_check_all:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to check all folders for new messages.
 *
 * Returns: whether to check all folders for new messages
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_check_all (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->check_all;
}

/**
 * camel_imapx_settings_set_check_all:
 * @settings: a #CamelIMAPXSettings
 * @check_all: whether to check all folders for new messages
 *
 * Sets whether to check all folders for new messages.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_check_all (CamelIMAPXSettings *settings,
                                    gboolean check_all)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->check_all == check_all)
		return;

	settings->priv->check_all = check_all;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_CHECK_ALL]);
}

/**
 * camel_imapx_settings_get_check_subscribed:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to check only subscribed folders for new messages.
 * Note that #CamelIMAPXSettings:check-all, if %TRUE, overrides this setting.
 *
 * Returns: whether to check only subscribed folders for new messages
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_check_subscribed (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->check_subscribed;
}

/**
 * camel_imapx_settings_set_check_subscribed:
 * @settings: a #CamelIMAPXSettings
 * @check_subscribed: whether to check only subscribed folders for new messages
 *
 * Sets whether to check only subscribed folders for new messages.  Note
 * that #CamelIMAPXSettings:check-all, if %TRUE, overrides this setting.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_check_subscribed (CamelIMAPXSettings *settings,
                                           gboolean check_subscribed)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->check_subscribed == check_subscribed)
		return;

	settings->priv->check_subscribed = check_subscribed;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_CHECK_SUBSCRIBED]);
}

/**
 * camel_imapx_settings_get_concurrent_connections:
 * @settings: a #CamelIMAPXSettings
 * 
 * Returns the number of concurrent network connections to the IMAP server
 * to use for faster command/response processing.
 *
 * Returns: the number of concurrent connections to use
 *
 * Since: 3.16
 **/
guint
camel_imapx_settings_get_concurrent_connections (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), 1);

	return settings->priv->concurrent_connections;
}

/**
 * camel_imapx_settings_set_concurrent_connections:
 * @settings: a #CamelIMAPXSettings
 * @concurrent_connections: the number of concurrent connections to use
 *
 * Sets the number of concurrent network connections to the IMAP server to
 * use for faster command/response processing.
 *
 * The minimum number of connections is 1, the maximum is 7.  The
 * @concurrent_connections value will be clamped to these limits if
 * necessary.
 *
 * Since: 3.16
 **/
void
camel_imapx_settings_set_concurrent_connections (CamelIMAPXSettings *settings,
                                                 guint concurrent_connections)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	concurrent_connections = CLAMP (
		concurrent_connections,
		MIN_CONCURRENT_CONNECTIONS,
		MAX_CONCURRENT_CONNECTIONS);

	if (settings->priv->concurrent_connections == concurrent_connections)
		return;

	settings->priv->concurrent_connections = concurrent_connections;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_CONCURRENT_CONNECTIONS]);
}

/**
 * camel_imapx_settings_get_fetch_order:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns the order in which new messages should be fetched.
 *
 * Returns: the order in which new messages should be fetched
 *
 * Since: 3.2
 **/
CamelSortType
camel_imapx_settings_get_fetch_order (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (
		CAMEL_IS_IMAPX_SETTINGS (settings),
		CAMEL_SORT_ASCENDING);

	return settings->priv->fetch_order;
}

/**
 * camel_imapx_settings_set_fetch_order:
 * @settings: a #CamelIMAPXSettings
 * @fetch_order: the order in which new messages should be fetched
 *
 * Sets the order in which new messages should be fetched.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_fetch_order (CamelIMAPXSettings *settings,
                                      CamelSortType fetch_order)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->fetch_order == fetch_order)
		return;

	settings->priv->fetch_order = fetch_order;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FETCH_ORDER]);
}

/**
 * camel_imapx_settings_get_filter_all:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether apply filters in all folders.
 *
 * Returns: whether to apply filters in all folders
 *
 * Since: 3.4
 **/
gboolean
camel_imapx_settings_get_filter_all (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->filter_all;
}

/**
 * camel_imapx_settings_set_filter_all:
 * @settings: a #CamelIMAPXSettings
 * @filter_all: whether to apply filters in all folders
 *
 * Sets whether to apply filters in all folders.
 *
 * Since: 3.4
 **/
void
camel_imapx_settings_set_filter_all (CamelIMAPXSettings *settings,
                                     gboolean filter_all)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->filter_all == filter_all)
		return;

	settings->priv->filter_all = filter_all;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FILTER_ALL]);
}

/**
 * camel_imapx_settings_get_filter_junk:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Returns: whether to filter junk in all folders
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_filter_junk (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

/**
 * camel_imapx_settings_set_filter_junk:
 * @settings: a #CamelIMAPXSettings
 * @filter_junk: whether to filter junk in all folders
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_filter_junk (CamelIMAPXSettings *settings,
                                      gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->filter_junk == filter_junk)
		return;

	settings->priv->filter_junk = filter_junk;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FILTER_JUNK]);
}

/**
 * camel_imapx_settings_get_filter_junk_inbox:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Returns: whether to filter junk in Inbox only
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_filter_junk_inbox (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk_inbox;
}

/**
 * camel_imapx_settings_set_filter_junk_inbox:
 * @settings: a #CamelIMAPXSettings
 * @filter_junk_inbox: whether to filter junk in Inbox only
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_filter_junk_inbox (CamelIMAPXSettings *settings,
                                            gboolean filter_junk_inbox)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->filter_junk_inbox == filter_junk_inbox)
		return;

	settings->priv->filter_junk_inbox = filter_junk_inbox;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FILTER_JUNK_INBOX]);
}

/**
 * camel_imapx_settings_get_namespace:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns the custom IMAP namespace in which to find folders.
 *
 * Returns: (nullable): the custom IMAP namespace, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_imapx_settings_get_namespace (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	return settings->priv->namespace;
}

/**
 * camel_imapx_settings_dup_namespace:
 * @settings: a #CamelIMAPXSettings
 *
 * Thread-safe variation of camel_imapx_settings_get_namespace().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: (nullable): a newly-allocated copy of #CamelIMAPXSettings:namespace
 *
 * Since: 3.4
 **/
gchar *
camel_imapx_settings_dup_namespace (CamelIMAPXSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_imapx_settings_get_namespace (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imapx_settings_set_namespace:
 * @settings: a #CamelIMAPXSettings
 * @namespace_: (nullable): an IMAP namespace, or %NULL
 *
 * Sets the custom IMAP namespace in which to find folders.  If @namespace_
 * is %NULL, the default namespace is used.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_namespace (CamelIMAPXSettings *settings,
                                    const gchar *namespace_)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	/* The default namespace is an empty string. */
	if (namespace_ == NULL)
		namespace_ = "";

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->namespace, namespace_) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->namespace);
	settings->priv->namespace = g_strdup (namespace_);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_NAMESPACE]);
}

/**
 * camel_imapx_settings_get_real_junk_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns the path to a real, non-virtual Junk folder to be used instead
 * of Camel's standard virtual Junk folder.
 *
 * Returns: (nullable): path to a real junk folder
 *
 * Since: 3.8
 **/
const gchar *
camel_imapx_settings_get_real_junk_path (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	return settings->priv->real_junk_path;
}

/**
 * camel_imapx_settings_dup_real_junk_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Thread-safe variation of camel_imapx_settings_get_real_junk_path().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: (nullable): a newly-allocated copy of #CamelIMAPXSettings:real-junk-path
 *
 * Since: 3.8
 **/
gchar *
camel_imapx_settings_dup_real_junk_path (CamelIMAPXSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_imapx_settings_get_real_junk_path (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imapx_settings_set_real_junk_path:
 * @settings: a #CamelIMAPXSettings
 * @real_junk_path: (nullable): path to a real Junk folder, or %NULL
 *
 * Sets the path to a real, non-virtual Junk folder to be used instead of
 * Camel's standard virtual Junk folder.
 *
 * Since: 3.8
 **/
void
camel_imapx_settings_set_real_junk_path (CamelIMAPXSettings *settings,
                                         const gchar *real_junk_path)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (real_junk_path != NULL && *real_junk_path == '\0')
		real_junk_path = NULL;

	g_mutex_lock (&settings->priv->property_lock);

	g_free (settings->priv->real_junk_path);
	settings->priv->real_junk_path = g_strdup (real_junk_path);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_REAL_JUNK_PATH]);
}

/**
 * camel_imapx_settings_get_real_trash_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns the path to a real, non-virtual Trash folder to be used instead
 * of Camel's standard virtual Trash folder.
 *
 * Returns: (nullable): path to a real Trash folder
 *
 * Since: 3.8
 **/
const gchar *
camel_imapx_settings_get_real_trash_path (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	return settings->priv->real_trash_path;
}

/**
 * camel_imapx_settings_dup_real_trash_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Thread-safe variation of camel_imapx_settings_get_real_trash_path().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: (nullable): a newly-allocated copy of #CamelIMAPXsettings:real-trash-path
 *
 * Since: 3.8
 **/
gchar *
camel_imapx_settings_dup_real_trash_path (CamelIMAPXSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_imapx_settings_get_real_trash_path (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imapx_settings_set_real_trash_path:
 * @settings: a #CamelIMAPXSettings
 * @real_trash_path: (nullable): path to a real Trash folder, or %NULL
 *
 * Sets the path to a real, non-virtual Trash folder to be used instead of
 * Camel's standard virtual Trash folder.
 *
 * Since: 3.8
 **/
void
camel_imapx_settings_set_real_trash_path (CamelIMAPXSettings *settings,
                                          const gchar *real_trash_path)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (real_trash_path != NULL && *real_trash_path == '\0')
		real_trash_path = NULL;

	g_mutex_lock (&settings->priv->property_lock);

	g_free (settings->priv->real_trash_path);
	settings->priv->real_trash_path = g_strdup (real_trash_path);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_REAL_TRASH_PATH]);
}

/**
 * camel_imapx_settings_get_shell_command:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns an optional shell command used to establish an input/output
 * stream with an IMAP server.  Normally the input/output stream is
 * established through a network socket.
 *
 * This option is useful only to a select few advanced users who likely
 * administer their own IMAP server.  Most users will not understand what
 * this option menas or how to use it.  Probably not worth exposing in a
 * graphical interface.
 *
 * Returns: (nullable): shell command for connecting to the server, or %NULL
 *
 * Since: 3.2
 **/
const gchar *
camel_imapx_settings_get_shell_command (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	return settings->priv->shell_command;
}

/**
 * camel_imapx_settings_dup_shell_command:
 * @settings: a #CamelIMAPXSettings
 *
 * Thread-safe variation of camel_imapx_settings_get_shell_command().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: (nullable): a newly-allocated copy of #CamelIMAPXSettings:shell-command
 *
 * Since: 3.4
 **/
gchar *
camel_imapx_settings_dup_shell_command (CamelIMAPXSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_imapx_settings_get_shell_command (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imapx_settings_set_shell_command:
 * @settings: a #CamelIMAPXSettings
 * @shell_command: (nullable): shell command for connecting to the server, or %NULL
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
camel_imapx_settings_set_shell_command (CamelIMAPXSettings *settings,
                                        const gchar *shell_command)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (shell_command != NULL && *shell_command == '\0')
		shell_command = NULL;

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->shell_command, shell_command) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->shell_command);
	settings->priv->shell_command = g_strdup (shell_command);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_SHELL_COMMAND]);
}

/**
 * camel_imapx_settings_get_use_idle:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use the IMAP IDLE extension if the server supports
 * it.  See RFC 2177 for more details.
 *
 * Returns: whether to use the IDLE extension
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_use_idle (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_idle;
}

/**
 * camel_imapx_settings_set_use_idle:
 * @settings: a #CamelIMAPXSettings
 * @use_idle: whether to use the IDLE extension
 *
 * Sets whether to use the IMAP IDLE extension if the server supports it.
 * See RFC 2177 for more details.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_use_idle (CamelIMAPXSettings *settings,
                                   gboolean use_idle)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_idle == use_idle)
		return;

	settings->priv->use_idle = use_idle;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_IDLE]);
}

/**
 * camel_imapx_settings_get_use_namespace:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use a custom IMAP namespace to find folders.  The
 * namespace itself is given by the #CamelIMAPStore:namespace property.
 *
 * Returns: whether to use a custom IMAP namespace
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_use_namespace (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_namespace;
}

/**
 * camel_imapx_settings_set_use_namespace:
 * @settings: a #CamelIMAPXSettings
 * @use_namespace: whether to use a custom IMAP namespace
 *
 * Sets whether to use a custom IMAP namespace to find folders.  The
 * namespace itself is given by the #CamelIMAPXSettings:namespace property.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_use_namespace (CamelIMAPXSettings *settings,
                                        gboolean use_namespace)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_namespace == use_namespace)
		return;

	settings->priv->use_namespace = use_namespace;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_NAMESPACE]);
}

/**
 * camel_imapx_settings_get_ignore_other_users_namespace:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to ignore namespace for other users.
 *
 * Returns: whether to ignore namespace for other users
 *
 * Since: 3.16
 **/
gboolean
camel_imapx_settings_get_ignore_other_users_namespace (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->ignore_other_users_namespace;
}

/**
 * camel_imapx_settings_set_ignore_other_users_namespace:
 * @settings: a #CamelIMAPXSettings
 * @ignore: whether to ignore the namespace
 *
 * Sets whether to ignore other users namespace.
 *
 * Since: 3.16
 **/
void
camel_imapx_settings_set_ignore_other_users_namespace (CamelIMAPXSettings *settings,
						       gboolean ignore)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->ignore_other_users_namespace == ignore)
		return;

	settings->priv->ignore_other_users_namespace = ignore;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_IGNORE_OTHER_USERS_NAMESPACE]);
}

/**
 * camel_imapx_settings_get_ignore_shared_folders_namespace:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to ignore namespace for shared folders.
 *
 * Returns: whether to ignore namespace for shared folders
 *
 * Since: 3.16
 **/
gboolean
camel_imapx_settings_get_ignore_shared_folders_namespace (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->ignore_shared_folders_namespace;
}

/**
 * camel_imapx_settings_set_ignore_shared_folders_namespace:
 * @settings: a #CamelIMAPXSettings
 * @ignore: whether to ignore the namespace
 *
 * Sets whether to ignore shared folders namespace.
 *
 * Since: 3.16
 **/
void
camel_imapx_settings_set_ignore_shared_folders_namespace (CamelIMAPXSettings *settings,
							  gboolean ignore)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->ignore_shared_folders_namespace == ignore)
		return;

	settings->priv->ignore_shared_folders_namespace = ignore;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_IGNORE_SHARED_FOLDERS_NAMESPACE]);
}

/**
 * camel_imapx_settings_get_use_qresync:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use the Quick Mailbox Resynchronization (QRESYNC)
 * IMAP extension if the server supports it.  See RFC 5162 for more
 * details.
 *
 * Returns: whether to use the QRESYNC extension
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_use_qresync (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_qresync;
}

/**
 * camel_imapx_settings_set_use_qresync:
 * @settings: a #CamelIMAPXSettings
 * @use_qresync: whether to use the QRESYNC extension
 *
 * Sets whether to use the Quick Mailbox Resynchronization (QRESYNC)
 * IMAP extension if the server supports it.  See RFC 5162 for more
 * details.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_use_qresync (CamelIMAPXSettings *settings,
                                      gboolean use_qresync)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_qresync == use_qresync)
		return;

	settings->priv->use_qresync = use_qresync;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_QRESYNC]);
}

/**
 * camel_imapx_settings_get_use_real_junk_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use a real, non-virtual Junk folder instead of Camel's
 * standard virtual Junk folder.
 *
 * Returns: whether to use a real Junk folder
 *
 * Since: 3.8
 **/
gboolean
camel_imapx_settings_get_use_real_junk_path (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_real_junk_path;
}

/**
 * camel_imapx_settings_set_use_real_junk_path:
 * @settings: a #CamelIMAPXSettings
 * @use_real_junk_path: whether to use a real Junk folder
 *
 * Sets whether to use a real, non-virtual Junk folder instead of Camel's
 * standard virtual Junk folder.
 *
 * Since: 3.8
 **/
void
camel_imapx_settings_set_use_real_junk_path (CamelIMAPXSettings *settings,
                                             gboolean use_real_junk_path)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_real_junk_path == use_real_junk_path)
		return;

	settings->priv->use_real_junk_path = use_real_junk_path;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_REAL_JUNK_PATH]);
}

/**
 * camel_imapx_settings_get_use_real_trash_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use a real, non-virtual Trash folder instead of Camel's
 * standard virtual Trash folder.
 *
 * Returns: whether to use a real Trash folder
 *
 * Since: 3.8
 **/
gboolean
camel_imapx_settings_get_use_real_trash_path (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_real_trash_path;
}

/**
 * camel_imapx_settings_set_use_real_trash_path:
 * @settings: a #CamelIMAPXSettings
 * @use_real_trash_path: whether to use a real Trash folder
 *
 * Sets whether to use a real, non-virtual Trash folder instead of Camel's
 * standard virtual Trash folder.
 *
 * Since: 3.8
 **/
void
camel_imapx_settings_set_use_real_trash_path (CamelIMAPXSettings *settings,
                                              gboolean use_real_trash_path)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_real_trash_path == use_real_trash_path)
		return;

	settings->priv->use_real_trash_path = use_real_trash_path;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_REAL_TRASH_PATH]);
}

/**
 * camel_imapx_settings_get_use_shell_command:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use a custom shell command to establish an input/output
 * stream with an IMAP server, instead of the more common method of opening a
 * network socket.  The shell command itself is given by the
 * #CamelIMAPXSettings:shell-command property.
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
camel_imapx_settings_get_use_shell_command (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_shell_command;
}

/**
 * camel_imapx_settings_set_use_shell_command:
 * @settings: a #CamelIMAPXSettings
 * @use_shell_command: whether to use a custom shell command to connect
 *                     to the server
 *
 * Sets whether to use a custom shell command to establish an input/output
 * stream with an IMAP server, instead of the more common method of opening
 * a network socket.  The shell command itself is given by the
 * #CamelIMAPXSettings:shell-command property.
 *
 * This option is useful only to a select few advanced users who likely
 * administer their own IMAP server.  Most users will not understand what
 * this option means or how to use it.  Probably not worth exposing in a
 * graphical interface.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_use_shell_command (CamelIMAPXSettings *settings,
                                            gboolean use_shell_command)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_shell_command == use_shell_command)
		return;

	settings->priv->use_shell_command = use_shell_command;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_SHELL_COMMAND]);
}

/**
 * camel_imapx_settings_get_use_subscriptions:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to list and operate only on subscribed folders, or to
 * list and operate on all available folders regardless of subscriptions.
 *
 * Returns: whether to honor folder subscriptions
 *
 * Since: 3.2
 **/
gboolean
camel_imapx_settings_get_use_subscriptions (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_subscriptions;
}

/**
 * camel_imapx_settings_set_use_subscriptions:
 * @settings: a #CamelIMAPXSettings
 * @use_subscriptions: whether to honor folder subscriptions
 *
 * Sets whether to list and operate only on subscribed folders, or to
 * list and operate on all available folders regardless of subscriptions.
 *
 * Since: 3.2
 **/
void
camel_imapx_settings_set_use_subscriptions (CamelIMAPXSettings *settings,
                                            gboolean use_subscriptions)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->use_subscriptions == use_subscriptions)
		return;

	settings->priv->use_subscriptions = use_subscriptions;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_SUBSCRIPTIONS]);
}

/**
 * camel_imapx_settings_get_full_update_on_metered_network:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to allow full folder update on metered network. With
 * this off the folder update skips updates of the known messages, which
 * can lead to not removing removed messages or not updating flags of
 * the old messages. The option is meant to save bandwidth usage on
 * the metered network.
 *
 * Returns: whether to allow full folder update on metered network
 *
 * Since: 3.34
 **/
gboolean
camel_imapx_settings_get_full_update_on_metered_network (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->full_update_on_metered_network;
}

/**
 * camel_imapx_settings_set_full_update_on_metered_network:
 * @settings: a #CamelIMAPXSettings
 * @full_update_on_metered_network: whether to allow it
 *
 * Sets whether to allow full folder update on metered network.
 *
 * Since: 3.34
 **/
void
camel_imapx_settings_set_full_update_on_metered_network (CamelIMAPXSettings *settings,
							 gboolean full_update_on_metered_network)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if (settings->priv->full_update_on_metered_network == full_update_on_metered_network)
		return;

	settings->priv->full_update_on_metered_network = full_update_on_metered_network;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_FULL_UPDATE_ON_METERED_NETWORK]);
}

/**
 * camel_imapx_settings_get_send_client_id:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to send client ID to the server, using the 'ID' extension (RFC 2971).
 *
 * Returns: whether to send client ID to the server
 *
 * Since: 3.44
 **/
gboolean
camel_imapx_settings_get_send_client_id (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->send_client_id;
}

/**
 * camel_imapx_settings_set_send_client_id:
 * @settings: a #CamelIMAPXSettings
 * @send_client_id: whether to send client ID to the server
 *
 * Sets whether to send client ID to the server, using the 'ID' extension (RFC 2971).
 *
 * Since: 3.44
 **/
void
camel_imapx_settings_set_send_client_id (CamelIMAPXSettings *settings,
					 gboolean send_client_id)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if ((settings->priv->send_client_id ? 1 : 0) == (send_client_id ? 1 : 0))
		return;

	settings->priv->send_client_id = send_client_id;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_SEND_CLIENT_ID]);
}

/**
 * camel_imapx_settings_get_single_client_mode:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether using single client mode. That is, the full folder refresh
 * flags are done only once per day, not on each folder refresh.
 *
 * Returns: whether the single client mode is enabled
 *
 * Since: 3.50
 **/
gboolean
camel_imapx_settings_get_single_client_mode (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->single_client_mode;
}

/**
 * camel_imapx_settings_set_single_client_mode:
 * @settings: a #CamelIMAPXSettings
 * @single_client_mode: value to set
 *
 * Sets whether to use a single client mode. See camel_imapx_settings_get_single_client_mode()
 * for an explanation what it means.
 *
 * Since: 3.50
 **/
void
camel_imapx_settings_set_single_client_mode (CamelIMAPXSettings *settings,
					     gboolean single_client_mode)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if ((settings->priv->single_client_mode ? 1 : 0) == (single_client_mode ? 1 : 0))
		return;

	settings->priv->single_client_mode = single_client_mode;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_SINGLE_CLIENT_MODE]);
}

/**
 * camel_imapx_settings_get_real_not_junk_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns the path to a folder where Not-Junk messages from non-virtual Junk folder
 * should be restored.
 *
 * Returns: (nullable): path to a real Not-Junk folder
 *
 * Since: 3.54
 **/
const gchar *
camel_imapx_settings_get_real_not_junk_path (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	return settings->priv->real_not_junk_path;
}

/**
 * camel_imapx_settings_dup_real_not_junk_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Thread-safe variation of camel_imapx_settings_get_real_not_junk_path().
 * Use this function when accessing @settings from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: (nullable): a newly-allocated copy of #CamelIMAPXSettings:real-not-junk-path
 *
 * Since: 3.54
 **/
gchar *
camel_imapx_settings_dup_real_not_junk_path (CamelIMAPXSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_imapx_settings_get_real_not_junk_path (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

/**
 * camel_imapx_settings_set_real_not_junk_path:
 * @settings: a #CamelIMAPXSettings
 * @real_not_junk_path: (nullable): path to a real Junk folder, or %NULL
 *
 * Sets a path to a folder where Not-Junk messages from non-virtual Junk folder
 * should be restored.
 *
 * Since: 3.54
 **/
void
camel_imapx_settings_set_real_not_junk_path (CamelIMAPXSettings *settings,
					     const gchar *real_not_junk_path)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	/* An empty string is equivalent to NULL. */
	if (real_not_junk_path != NULL && *real_not_junk_path == '\0')
		real_not_junk_path = NULL;

	g_mutex_lock (&settings->priv->property_lock);

	g_free (settings->priv->real_not_junk_path);
	settings->priv->real_not_junk_path = g_strdup (real_not_junk_path);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_REAL_NOT_JUNK_PATH]);
}

/**
 * camel_imapx_settings_get_use_real_not_junk_path:
 * @settings: a #CamelIMAPXSettings
 *
 * Returns whether to use a real Not-Junk folder, instead of Inbox, to restore
 * Not-Junk messages from real Junk folder to.
 *
 * Returns: whether to use a real Not-Junk folder
 *
 * Since: 3.54
 **/
gboolean
camel_imapx_settings_get_use_real_not_junk_path (CamelIMAPXSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings), FALSE);

	return settings->priv->use_real_not_junk_path;
}

/**
 * camel_imapx_settings_set_use_real_not_junk_path:
 * @settings: a #CamelIMAPXSettings
 * @use_real_not_junk_path: whether to use a real Not-Junk folder
 *
 * Sets whether to use a real Not-Junk folder, instead of Inbox, to restore
 * Not-Junk messages from real Junk folder to.
 *
 * Since: 3.54
 **/
void
camel_imapx_settings_set_use_real_not_junk_path (CamelIMAPXSettings *settings,
						 gboolean use_real_not_junk_path)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SETTINGS (settings));

	if ((!settings->priv->use_real_not_junk_path) == (!use_real_not_junk_path))
		return;

	settings->priv->use_real_not_junk_path = use_real_not_junk_path;

	g_object_notify_by_pspec (G_OBJECT (settings), properties[PROP_USE_REAL_NOT_JUNK_PATH]);
}
