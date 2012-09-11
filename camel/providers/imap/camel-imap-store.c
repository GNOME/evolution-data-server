/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.c : class for an imap store */

/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *    Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-imap-command.h"
#include "camel-imap-folder.h"
#include "camel-imap-message-cache.h"
#include "camel-imap-settings.h"
#include "camel-imap-store-summary.h"
#include "camel-imap-store.h"
#include "camel-imap-summary.h"
#include "camel-imap-utils.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x)

/* Specified in RFC 2060 */
#define IMAP_PORT  143
#define IMAPS_PORT 993

#ifdef G_OS_WIN32
/* The strtok() in Microsoft's C library is MT-safe (but still uses
 * only one buffer pointer per thread, but for the use of strtok_r()
 * here that's enough).
 */
#define strtok_r(s,sep,lasts) (*(lasts)=strtok((s),(sep)))
#endif

extern gint camel_verbose_debug;

static gchar imap_tag_prefix = 'A';

static gboolean imap_store_noop_sync (CamelStore *store, GCancellable *cancellable, GError **error);
static CamelFolder *imap_store_get_junk_folder_sync (CamelStore *store, GCancellable *cancellable, GError **error);
static CamelFolder *imap_store_get_trash_folder_sync (CamelStore *store, GCancellable *cancellable, GError **error);
static guint hash_folder_name (gconstpointer key);
static gboolean equal_folder_name (gconstpointer a, gconstpointer b);

static CamelFolderInfo *imap_store_create_folder_sync (CamelStore *store, const gchar *parent_name, const gchar *folder_name, GCancellable *cancellable, GError **error);
static gboolean imap_store_delete_folder_sync (CamelStore *store, const gchar *folder_name, GCancellable *cancellable, GError **error);
static gboolean imap_store_rename_folder_sync (CamelStore *store, const gchar *old_name, const gchar *new_name, GCancellable *cancellable, GError **error);
static gboolean imap_store_folder_is_subscribed (CamelSubscribable *subscribable, const gchar *folder_name);
static gboolean imap_store_subscribe_folder_sync (CamelSubscribable *subscribable, const gchar *folder_name, GCancellable *cancellable, GError **error);
static gboolean imap_store_unsubscribe_folder_sync (CamelSubscribable *subscribable, const gchar *folder_name, GCancellable *cancellable, GError **error);

static gboolean get_folders_sync (CamelImapStore *imap_store, const gchar *pattern, GCancellable *cancellable, GError **error);

static gboolean imap_folder_effectively_unsubscribed (CamelImapStore *imap_store, const gchar *folder_name, GError **error);
static gboolean imap_check_folder_still_extant (CamelImapStore *imap_store, const gchar *full_name,  GError **error);
static void imap_forget_folder (CamelImapStore *imap_store, const gchar *folder_name, GError **error);
static void imap_set_server_level (CamelImapStore *store);

static gboolean imap_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error);
static CamelFolder * imap_store_get_folder_sync (CamelStore *store, const gchar *folder_name, CamelStoreGetFolderFlags flags, GCancellable *cancellable, GError **error);
static CamelFolderInfo * imap_store_get_folder_info_sync (CamelStore *store, const gchar *top, CamelStoreGetFolderInfoFlags flags, GCancellable *cancellable, GError **error);
static CamelFolder * get_folder_offline (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error);
static CamelFolderInfo * get_folder_info_offline (CamelStore *store, const gchar *top, guint32 flags, GError **error);

static struct {
	const gchar *name;
	guint32 flag;
} capabilities[] = {
	{ "IMAP4",		IMAP_CAPABILITY_IMAP4 },
	{ "IMAP4REV1",		IMAP_CAPABILITY_IMAP4REV1 },
	{ "STATUS",		IMAP_CAPABILITY_STATUS },
	{ "NAMESPACE",		IMAP_CAPABILITY_NAMESPACE },
	{ "UIDPLUS",		IMAP_CAPABILITY_UIDPLUS },
	{ "LITERAL+",		IMAP_CAPABILITY_LITERALPLUS },
	{ "STARTTLS",           IMAP_CAPABILITY_STARTTLS },
	{ "XGWEXTENSIONS",      IMAP_CAPABILITY_XGWEXTENSIONS },
	{ "XGWMOVE",            IMAP_CAPABILITY_XGWMOVE },
	{ "LOGINDISABLED",      IMAP_CAPABILITY_LOGINDISABLED },
	{ "QUOTA",              IMAP_CAPABILITY_QUOTA },
	{ NULL, 0 }
};

extern CamelServiceAuthType camel_imap_password_authtype;

static GInitableIface *parent_initable_interface;

/* Forward Declarations */
static void camel_imap_store_initable_init (GInitableIface *interface);
static void camel_network_service_init (CamelNetworkServiceInterface *interface);
static void camel_subscribable_init (CamelSubscribableInterface *interface);

G_DEFINE_TYPE_WITH_CODE (
	CamelImapStore,
	camel_imap_store,
	CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		camel_imap_store_initable_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SERVICE,
		camel_network_service_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_SUBSCRIBABLE,
		camel_subscribable_init))

static void
imap_store_update_store_flags (CamelStore *store)
{
	CamelService *service;
	CamelSettings *settings;
	CamelImapSettings *imap_settings;
	gboolean use_real_path;
	gchar *real_path;

	/* XXX This only responds to the service's entire settings object
	 *     being replaced, not when individual settings change.  When
	 *     individual settings change, a restart is required for them
	 *     to take effect. */

	service = CAMEL_SERVICE (store);
	settings = camel_service_ref_settings (service);
	imap_settings = CAMEL_IMAP_SETTINGS (settings);

	real_path = camel_imap_settings_dup_real_junk_path (imap_settings);
	use_real_path = camel_imap_settings_get_use_real_junk_path (imap_settings);

	if (use_real_path && real_path != NULL) {
		store->flags &= ~CAMEL_STORE_VJUNK;
		store->flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	} else {
		store->flags &= ~CAMEL_STORE_REAL_JUNK_FOLDER;
		store->flags |= CAMEL_STORE_VJUNK;
	}

	g_free (real_path);

	real_path = camel_imap_settings_dup_real_trash_path (imap_settings);
	use_real_path = camel_imap_settings_get_use_real_trash_path (imap_settings);

	if (use_real_path && real_path != NULL)
		store->flags &= ~CAMEL_STORE_VTRASH;
	else
		store->flags |= CAMEL_STORE_VTRASH;

	g_free (real_path);

	g_object_unref (settings);
}

static void
parse_capability (CamelImapStore *store,
                  gchar *capa)
{
	gchar *lasts = NULL;
	gint i;

	for (capa = strtok_r (capa, " ", &lasts); capa; capa = strtok_r (NULL, " ", &lasts)) {
		if (!strncmp (capa, "AUTH=", 5)) {
			g_hash_table_insert (
				store->authtypes,
				g_strdup (capa + 5),
				GINT_TO_POINTER (1));
			continue;
		}
		for (i = 0; capabilities[i].name; i++) {
			if (g_ascii_strcasecmp (capa, capabilities[i].name) == 0) {
				store->capabilities |= capabilities[i].flag;
				break;
			}
		}
	}
}

static gboolean free_key (gpointer key, gpointer value, gpointer user_data);

static gboolean
imap_get_capability (CamelService *service,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;
	gchar *result;

	/* Find out the IMAP capabilities */
	/* We assume we have utf8 capable search until a failed search tells us otherwise */
	store->capabilities = IMAP_CAPABILITY_utf8_search;
	if (store->authtypes) {
		g_hash_table_foreach_remove (store->authtypes, free_key, NULL);
		g_hash_table_destroy (store->authtypes);
	}
	store->authtypes = g_hash_table_new (g_str_hash, g_str_equal);
	response = camel_imap_command (store, NULL, cancellable, error, "CAPABILITY");
	if (!response)
		return FALSE;
	result = camel_imap_response_extract (store, response, "CAPABILITY ", error);
	if (!result)
		return FALSE;

	/* Skip over "* CAPABILITY ". */
	parse_capability (store, result + 13);
	g_free (result);

	/* dunno why the groupwise guys didn't just list this in capabilities */
	if (store->capabilities & IMAP_CAPABILITY_XGWEXTENSIONS) {
		/* not critical if this fails */
		response = camel_imap_command (store, NULL, cancellable, NULL, "XGWEXTENSIONS");
		if (response && (result = camel_imap_response_extract (store, response, "XGWEXTENSIONS ", NULL))) {
			parse_capability (store, result + 16);
			g_free (result);
		}
	}

	imap_set_server_level (store);

	if (store->summary->capabilities != store->capabilities) {
		store->summary->capabilities = store->capabilities;
		camel_store_summary_touch ((CamelStoreSummary *) store->summary);
		camel_store_summary_save ((CamelStoreSummary *) store->summary);
	}

	return TRUE;
}

/* folder_name is path name */
static CamelFolderInfo *
imap_build_folder_info (CamelImapStore *imap_store,
                        const gchar *folder_name)
{
	const gchar *name;
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (folder_name);
	fi->unread = -1;
	fi->total = -1;

	name = strrchr (fi->full_name, '/');
	if (name == NULL)
		name = fi->full_name;
	else
		name++;
	if (!g_ascii_strcasecmp (fi->full_name, "INBOX"))
		fi->display_name = g_strdup (_("Inbox"));
	else
		fi->display_name = g_strdup (name);

	return fi;
}

static gboolean
connect_to_server (CamelService *service,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelImapStore *store = (CamelImapStore *) service;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelImapResponse *response;
	CamelStream *tcp_stream;
	CamelSockOptData sockopt;
	CamelNetworkSecurityMethod method;
	gboolean force_imap4 = FALSE;
	gboolean clean_quit = TRUE;
	gboolean success = TRUE;
	gchar *host;
	gchar *buf;

	tcp_stream = camel_network_service_connect_sync (
		CAMEL_NETWORK_SERVICE (service), cancellable, error);

	if (tcp_stream == NULL)
		return FALSE;

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	method = camel_network_settings_get_security_method (network_settings);

	g_object_unref (settings);

	store->ostream = tcp_stream;
	store->istream = camel_stream_buffer_new (tcp_stream, CAMEL_STREAM_BUFFER_READ);

	store->connected = TRUE;
	store->preauthed = FALSE;
	store->command = 0;

	/* Disable Nagle - we send a lot of small requests which nagle slows down */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt ((CamelTcpStream *) tcp_stream, &sockopt);

	/* Set keepalive - needed for some hosts/router configurations, we're idle a lot */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt ((CamelTcpStream *) tcp_stream, &sockopt);

	/* Read the greeting, if any, and deal with PREAUTH */
	if (camel_imap_store_readline (store, &buf, cancellable, error) < 0) {
		if (store->istream) {
			g_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			g_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		success = FALSE;

		goto exit;
	}

	if (!strncmp (buf, "* PREAUTH", 9))
		store->preauthed = TRUE;

	if (strstr (buf, "Courier-IMAP") || getenv ("CAMEL_IMAP_BRAINDAMAGED")) {
		/* Courier-IMAP is braindamaged. So far this flag only
		 * works around the fact that Courier-IMAP is known to
		 * give invalid BODY responses seemingly because its
		 * MIME parser sucks. In any event, we can't rely on
		 * them so we always have to request the full messages
		 * rather than getting individual parts. */
		store->braindamaged = TRUE;
	} else if (strstr (buf, "WEB.DE") || strstr (buf, "Mail2World")) {
		/* This is a workaround for servers which advertise
		 * IMAP4rev1 but which can sometimes subtly break in
		 * various ways if we try to use IMAP4rev1 queries.
		 *
		 * WEB.DE: when querying for HEADER.FIELDS.NOT, it
		 * returns an empty literal for the headers. Many
		 * complaints about empty message-list fields on the
		 * mailing lists and probably a few bugzilla bugs as
		 * well.
		 *
		 * Mail2World (aka NamePlanet): When requesting
		 * message info's, it ignores the fact that we
		 * requested BODY.PEEK[HEADER.FIELDS.NOT (RECEIVED)]
		 * and so the responses are incomplete. See bug #58766
		 * for details.
		 **/
		force_imap4 = TRUE;
	}

	g_free (buf);

	/* get the imap server capabilities */
	if (!imap_get_capability (service, cancellable, error)) {
		if (store->istream) {
			g_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			g_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		success = FALSE;

		goto exit;
	}

	if (force_imap4) {
		store->capabilities &= ~IMAP_CAPABILITY_IMAP4REV1;
		store->server_level = IMAP_LEVEL_IMAP4;
	}

	if (method != CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT)
		goto exit;  /* we're done */

	/* as soon as we send a STARTTLS command, all hope is lost of a clean QUIT if problems arise */
	clean_quit = FALSE;

	if (!(store->capabilities & IMAP_CAPABILITY_STARTTLS)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			host, _("STARTTLS not supported"));

		goto exception;
	}

	response = camel_imap_command (store, NULL, cancellable, error, "STARTTLS");
	if (!response) {
		g_object_unref (store->istream);
		store->istream = NULL;

		g_object_unref (store->ostream);
		store->ostream = NULL;

		success = FALSE;

		goto exit;
	}

	camel_imap_response_free_without_processing (store, response);

	/* Okay, now toggle SSL/TLS mode */
	if (camel_tcp_stream_ssl_enable_ssl (
		CAMEL_TCP_STREAM_SSL (tcp_stream), cancellable, error) == -1) {
		g_prefix_error (
			error,
			_("Failed to connect to IMAP server %s in secure mode: "),
			host);
		goto exception;
	}

	/* rfc2595, section 4 states that after a successful STLS
	 * command, the client MUST discard prior CAPA responses */
	if (!imap_get_capability (service, cancellable, error)) {
		if (store->istream) {
			g_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			g_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		success = FALSE;

		goto exit;
	}

	if (store->capabilities & IMAP_CAPABILITY_LOGINDISABLED ) {
		clean_quit = TRUE;
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			host, _("Unknown error"));
		goto exception;
	}

	goto exit;

exception:

	if (clean_quit && store->connected) {
		/* try to disconnect cleanly; error is already set here */
		response = camel_imap_command (store, NULL, cancellable, NULL, "LOGOUT");
		if (response)
			camel_imap_response_free_without_processing (store, response);
	}

	if (store->istream) {
		g_object_unref (store->istream);
		store->istream = NULL;
	}

	if (store->ostream) {
		g_object_unref (store->ostream);
		store->ostream = NULL;
	}

	store->connected = FALSE;

	success = FALSE;

exit:
	g_free (host);

	return success;
}

#ifndef G_OS_WIN32

/* Using custom commands to connect to IMAP servers is not supported on Win32 */

static gboolean
connect_to_server_process (CamelService *service,
                           const gchar *cmd,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelImapStore *store = (CamelImapStore *) service;
	CamelNetworkSettings *network_settings;
	CamelProvider *provider;
	CamelSettings *settings;
	CamelStream *cmd_stream;
	CamelURL url;
	gint ret, i = 0;
	gchar *buf;
	gchar *cmd_copy;
	gchar *full_cmd;
	gchar *child_env[7];
	const gchar *password;
	gchar *host;
	gchar *user;
	guint16 port;

	memset (&url, 0, sizeof (CamelURL));

	password = camel_service_get_password (service);
	provider = camel_service_get_provider (service);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	port = camel_network_settings_get_port (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	camel_url_set_protocol (&url, provider->protocol);
	camel_url_set_host (&url, host);
	camel_url_set_port (&url, port);
	camel_url_set_user (&url, user);

	/* Put full details in the environment, in case the connection
	 * program needs them */
	buf = camel_url_to_string (&url, 0);
	child_env[i++] = g_strdup_printf ("URL=%s", buf);
	g_free (buf);

	child_env[i++] = g_strdup_printf ("URLHOST=%s", host);
	if (port)
		child_env[i++] = g_strdup_printf ("URLPORT=%d", port);
	if (user)
		child_env[i++] = g_strdup_printf ("URLUSER=%s", user);
	if (password)
		child_env[i++] = g_strdup_printf ("URLPASSWD=%s", password);
	child_env[i] = NULL;

	/* Now do %h, %u, etc. substitution in cmd */
	buf = cmd_copy = g_strdup (cmd);

	full_cmd = g_strdup ("");

	for (;;) {
		gchar *pc;
		gchar *tmp;
		const gchar *var;
		gint len;

		pc = strchr (buf, '%');
	ignore:
		if (!pc) {
			tmp = g_strdup_printf ("%s%s", full_cmd, buf);
			g_free (full_cmd);
			full_cmd = tmp;
			break;
		}

		len = pc - buf;

		var = NULL;

		switch (pc[1]) {
		case 'h':
			var = host;
			break;
		case 'u':
			var = user;
			break;
		}
		if (!var) {
			/* If there wasn't a valid %-code, with an actual
			 * variable to insert, pretend we didn't see the % */
			pc = strchr (pc + 1, '%');
			goto ignore;
		}
		tmp = g_strdup_printf ("%s%.*s%s", full_cmd, len, buf, var);
		g_free (full_cmd);
		full_cmd = tmp;
		buf = pc + 2;
	}

	g_free (cmd_copy);

	g_free (host);
	g_free (user);

	cmd_stream = camel_stream_process_new ();

	ret = camel_stream_process_connect (
		CAMEL_STREAM_PROCESS (cmd_stream),
		full_cmd, (const gchar **) child_env, error);

	while (i)
		g_free (child_env[--i]);

	if (ret == -1) {
		g_object_unref (cmd_stream);
		g_free (full_cmd);
		return FALSE;
	}

	g_free (full_cmd);

	store->ostream = cmd_stream;
	store->istream = camel_stream_buffer_new (cmd_stream, CAMEL_STREAM_BUFFER_READ);

	store->connected = TRUE;
	store->preauthed = FALSE;
	store->command = 0;

	/* Read the greeting, if any, and deal with PREAUTH */
	if (camel_imap_store_readline (store, &buf, cancellable, error) < 0) {
		if (store->istream) {
			g_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			g_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		return FALSE;
	}

	if (!strncmp (buf, "* PREAUTH", 9))
		store->preauthed = TRUE;
	g_free (buf);

	/* get the imap server capabilities */
	if (!imap_get_capability (service, cancellable, error)) {
		if (store->istream) {
			g_object_unref (store->istream);
			store->istream = NULL;
		}

		if (store->ostream) {
			g_object_unref (store->ostream);
			store->ostream = NULL;
		}

		store->connected = FALSE;
		return FALSE;
	}

	return TRUE;

}

#endif

static gboolean
connect_to_server_wrapper (CamelService *service,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelSettings *settings;
	gchar *shell_command;
	gboolean use_shell_command;
	gboolean success;

	settings = camel_service_ref_settings (service);

	shell_command = camel_imap_settings_dup_shell_command (
		CAMEL_IMAP_SETTINGS (settings));
	use_shell_command = camel_imap_settings_get_use_shell_command (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

#ifndef G_OS_WIN32
	if (use_shell_command && shell_command != NULL)
		success = connect_to_server_process (
			service, shell_command, cancellable, error);
	else
		success = connect_to_server (service, cancellable, error);
#else
	success = connect_to_server (service, cancellable, error);
#endif

	g_free (shell_command);

	return success;
}

static gboolean
imap_auth_loop (CamelService *service,
                GCancellable *cancellable,
                GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelSession *session;
	gchar *mechanism;
	gchar *host;
	gboolean success = TRUE;

	session = camel_service_get_session (service);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	mechanism = camel_network_settings_dup_auth_mechanism (network_settings);

	g_object_unref (settings);

	if (store->preauthed) {
		if (camel_verbose_debug)
			fprintf (
				stderr, "Server %s has preauthenticated us.\n",
				host);
		goto exit;
	}

	if (mechanism != NULL) {
		if (!g_hash_table_lookup (store->authtypes, mechanism)) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("IMAP server %s does not support %s "
				"authentication"), host, mechanism);
			success = FALSE;
			goto exit;
		}
	}

	success = camel_session_authenticate_sync (
		session, service, mechanism, cancellable, error);

exit:
	g_free (host);
	g_free (mechanism);

	return success;
}

static gboolean
free_key (gpointer key,
          gpointer value,
          gpointer user_data)
{
	g_free (key);
	return TRUE;
}

static void
imap_store_dispose (GObject *object)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (object);

	/* This frees current_folder, folders, authtypes, streams, and namespace. */
	camel_service_disconnect_sync (
		CAMEL_SERVICE (imap_store), TRUE, NULL, NULL);

	if (imap_store->summary != NULL) {
		camel_store_summary_save (
			CAMEL_STORE_SUMMARY (imap_store->summary));
		g_object_unref (imap_store->summary);
		imap_store->summary = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imap_store_parent_class)->dispose (object);
}

static void
imap_store_finalize (GObject *object)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (object);

	g_static_rec_mutex_free (&imap_store->command_and_response_lock);
	g_hash_table_destroy (imap_store->known_alerts);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_store_parent_class)->finalize (object);
}

static gchar *
imap_store_get_name (CamelService *service,
                     gboolean brief)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	gchar *host;
	gchar *user;
	gchar *name;

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (
			_("IMAP server %s"), host);
	else
		name = g_strdup_printf (
			_("IMAP service for %s on %s"), user, host);

	g_free (host);
	g_free (user);

	return name;
}

static gboolean
imap_store_connect_sync (CamelService *service,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;
	CamelSettings *settings;
	CamelImapSettings *imap_settings;
	gchar *result, *name;
	gsize len;
	const gchar *namespace;
	GError *local_error = NULL;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return TRUE;

	if (!connect_to_server_wrapper (service, cancellable, error) ||
	    !imap_auth_loop (service, cancellable, error)) {
		/* reset cancellable, in case it is cancelled,
		 * thus the disconnect is run */
		if (cancellable)
			g_cancellable_reset (cancellable);
		camel_service_disconnect_sync (
			service, TRUE, cancellable, NULL);
		return FALSE;
	}

	settings = camel_service_ref_settings (service);
	imap_settings = CAMEL_IMAP_SETTINGS (settings);

	/* Get namespace and hierarchy separator */
	if (store->capabilities & IMAP_CAPABILITY_NAMESPACE) {
		struct _namespaces *namespaces;
		const gchar *namespace;

		response = camel_imap_command (store, NULL, cancellable, &local_error, "NAMESPACE");
		if (!response)
			goto done;

		result = camel_imap_response_extract (store, response, "NAMESPACE", &local_error);
		if (!result)
			goto done;

		namespaces = imap_parse_namespace_response (result);

		if (!camel_imap_settings_get_use_namespace (imap_settings))
			camel_imap_settings_set_namespace (imap_settings, NULL);

		namespace = camel_imap_settings_get_namespace (imap_settings);

		if (namespaces != NULL && namespace == NULL) {
			struct _namespace *np = NULL;

			if (namespaces->personal)
				np = namespaces->personal;
			else if (namespaces->other)
				np = namespaces->other;
			else if (namespaces->shared)
				np = namespaces->shared;

			if (np != NULL)
				camel_imap_settings_set_namespace (
					imap_settings, np->prefix);
		}

		if (namespaces) {
			#define add_all(_ns)									\
				if (_ns) {									\
					struct _namespace *ns;							\
														\
					for (ns = _ns; ns; ns = ns->next) {					\
						if (ns->prefix)							\
							camel_imap_store_summary_namespace_add_secondary	\
								(store->summary, ns->prefix, ns->delim);	\
					}									\
				}

			add_all (namespaces->personal);
			add_all (namespaces->other);
			add_all (namespaces->shared);

			#undef add_all
		}

		imap_namespaces_destroy (namespaces);

		if (camel_imap_settings_get_namespace (imap_settings) == NULL) {
			/* fallback for a broken result */
			name = camel_strstrcase (result, "NAMESPACE ((");
			if (name) {
				gchar *ns;
				gchar *sep;

				name += 12;
				ns = imap_parse_string (
					(const gchar **) &name, &len);
				camel_imap_settings_set_namespace (
					imap_settings, ns);
				g_free (ns);

				if (name && *name++ == ' ') {
					sep = imap_parse_string ((const gchar **) &name, &len);
					if (sep) {
						store->dir_sep = *sep;
						g_free (sep);
					}
				}
			}
		}
		g_free (result);
	}

	if (!store->dir_sep) {
		const gchar *use_namespace = NULL;

		if (store->summary->namespace != NULL)
			use_namespace = store->summary->namespace->full_name;

		if (use_namespace == NULL)
			use_namespace = camel_imap_settings_get_namespace (
				imap_settings);

		if (use_namespace == NULL)
			use_namespace = "";

		if (store->server_level >= IMAP_LEVEL_IMAP4REV1) {
			/* This idiom means "tell me the hierarchy separator
			 * for the given path, even if that path doesn't exist.
			 */
			response = camel_imap_command (
				store, NULL, cancellable, &local_error,
				"LIST %G \"\"", use_namespace);
		} else {
			/* Plain IMAP4 doesn't have that idiom, so we fall back
			 * to "tell me about this folder", which will fail if
			 * the folder doesn't exist (eg, if namespace is "").
			 */
			response = camel_imap_command (
				store, NULL, cancellable, &local_error,
				"LIST \"\" %G", use_namespace);
		}
		if (!response)
			goto done;

		result = camel_imap_response_extract (store, response, "LIST", NULL);
		if (result) {
			imap_parse_list_response (store, result, NULL, &store->dir_sep, NULL);
			g_free (result);
		}

		if (!store->dir_sep)
			store->dir_sep = '/';	/* Guess */

	}

	/* canonicalize the namespace to not end with dir_sep */
	namespace = camel_imap_settings_get_namespace (imap_settings);
	len = (namespace != NULL) ? strlen (namespace) : 0;
	if (len && namespace[len - 1] == store->dir_sep) {
		gchar *tmp = g_strdup (namespace);
		tmp[len - 1] = '\0';
		camel_imap_settings_set_namespace (imap_settings, tmp);
		namespace = camel_imap_settings_get_namespace (imap_settings);
		g_free (tmp);
	}

	camel_imap_store_summary_namespace_set_main (
		store->summary, namespace, store->dir_sep);

	if (camel_imap_settings_get_use_subscriptions (imap_settings) &&
	    camel_store_summary_count ((CamelStoreSummary *) store->summary) == 0) {
		CamelStoreInfo *si;

		/* look in all namespaces */
		if (!get_folders_sync (store, NULL, cancellable, &local_error))
			goto done;

		/* Make sure INBOX is present/subscribed */
		si = camel_store_summary_path ((CamelStoreSummary *) store->summary, "INBOX");
		if (si == NULL || (si->flags & CAMEL_FOLDER_SUBSCRIBED) == 0) {
			response = camel_imap_command (store, NULL, cancellable, &local_error, "SUBSCRIBE INBOX");
			if (response != NULL) {
				camel_imap_response_free (store, response);
			}
			if (si)
				camel_store_summary_info_free ((CamelStoreSummary *) store->summary, si);
			if (local_error != NULL)
				goto done;
			get_folders_sync (store, "INBOX", cancellable, &local_error);
		}

		store->refresh_stamp = time (NULL);
	}

done:
	g_object_unref (settings);

	/* save any changes we had */
	camel_store_summary_save ((CamelStoreSummary *) store->summary);

	if (local_error != NULL) {
		/* reset cancellable, in case it is cancelled,
		 * thus the disconnect is run */
		if (cancellable)
			g_cancellable_reset (cancellable);
		camel_service_disconnect_sync (
			service, TRUE, cancellable, NULL);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
imap_store_disconnect_sync (CamelService *service,
                            gboolean clean,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelSettings *settings;
	CamelImapSettings *imap_settings;

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)) && clean) {
		CamelImapResponse *response;

		response = camel_imap_command (store, NULL, NULL, NULL, "LOGOUT");
		camel_imap_response_free (store, response);
	}

	if (store->istream) {
		camel_stream_close (store->istream, cancellable, NULL);
		g_object_unref (store->istream);
		store->istream = NULL;
	}

	if (store->ostream) {
		camel_stream_close (store->ostream, cancellable, NULL);
		g_object_unref (store->ostream);
		store->ostream = NULL;
	}

	store->connected = FALSE;
	if (store->current_folder) {
		g_object_unref (store->current_folder);
		store->current_folder = NULL;
	}

	if (store->authtypes) {
		g_hash_table_foreach_remove (store->authtypes,
					     free_key, NULL);
		g_hash_table_destroy (store->authtypes);
		store->authtypes = NULL;
	}

	g_hash_table_remove_all (store->known_alerts);

	settings = camel_service_ref_settings (service);
	imap_settings = CAMEL_IMAP_SETTINGS (settings);

	if (camel_imap_settings_get_use_namespace (imap_settings))
		camel_imap_settings_set_namespace (imap_settings, NULL);

	g_object_unref (settings);

	return TRUE;
}

static CamelAuthenticationResult
imap_store_authenticate_sync (CamelService *service,
                              const gchar *mechanism,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelAuthenticationResult result;
	CamelImapResponse *response;
	CamelSasl *sasl = NULL;
	gchar *sasl_resp;
	gchar *resp;
	GError *local_error = NULL;

	/* If not using SASL, do a simple LOGIN here. */
	if (mechanism == NULL) {
		CamelNetworkSettings *network_settings;
		CamelSettings *settings;
		const gchar *password;
		gchar *user;

		password = camel_service_get_password (service);

		settings = camel_service_ref_settings (service);

		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		user = camel_network_settings_dup_user (network_settings);

		g_object_unref (settings);

		if (user == NULL) {
			g_set_error_literal (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Cannot authenticate without a username"));
			return CAMEL_AUTHENTICATION_ERROR;
		}

		if (password == NULL) {
			g_set_error_literal (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Authentication password not available"));
			g_free (user);
			return CAMEL_AUTHENTICATION_ERROR;
		}

		response = camel_imap_command (
			store, NULL, cancellable, &local_error,
			"LOGIN %S %S", user, password);

		if (response != NULL)
			camel_imap_response_free (store, response);

		g_free (user);

		goto exit;
	}

	/* Henceforth we're using SASL. */

	sasl = camel_sasl_new ("imap", mechanism, service);
	if (sasl == NULL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("No support for %s authentication"), mechanism);
		return CAMEL_AUTHENTICATION_ERROR;
	}

	response = camel_imap_command (
		store, NULL, cancellable, &local_error,
		"AUTHENTICATE %s", mechanism);
	if (response == NULL)
		goto exit;

	while (!camel_sasl_get_authenticated (sasl)) {
		resp = camel_imap_response_extract_continuation (
			store, response, &local_error);

		if (resp == NULL)
			goto exit;

		sasl_resp = camel_sasl_challenge_base64_sync (
			sasl, imap_next_word (resp),
			cancellable, &local_error);

		g_free (resp);

		if (sasl_resp == NULL)
			goto break_and_exit;

		response = camel_imap_command_continuation (
			store, NULL, sasl_resp, strlen (sasl_resp),
			cancellable, &local_error);

		g_free (sasl_resp);

		if (response == NULL)
			goto exit;
	}

	resp = camel_imap_response_extract_continuation (store, response, NULL);
	if (resp != NULL) {
		/* Oops. SASL claims we're done, but the IMAP server
		 * doesn't think so... */
		g_free (resp);
		goto exit;
	}

	goto exit;

break_and_exit:
	/* Get the server out of "waiting for continuation data" mode. */
	response = camel_imap_command_continuation (
		store, NULL, "*", 1, cancellable, NULL);
	if (response != NULL)
		camel_imap_response_free (store, response);

exit:
	/* XXX Apparently the IMAP parser sets CAMEL_SERVICE_ERROR_INVALID
	 *     for failed IMAP server responses, so I guess check for that
	 *     to know if our authentication credentials were rejected. */
	if (local_error == NULL) {
		result = CAMEL_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_INVALID)) {
		g_clear_error (&local_error);
		result = CAMEL_AUTHENTICATION_REJECTED;

		/* Some servers (eg, courier) will disconnect
		 * on a bad password, so we reconnect here. */
		if (!store->connected) {
			if (!connect_to_server_wrapper (
				service, cancellable, error))
				result = CAMEL_AUTHENTICATION_ERROR;
		}

	} else {
		g_propagate_error (error, local_error);
		result = CAMEL_AUTHENTICATION_ERROR;
	}

	if (sasl != NULL)
		g_object_unref (sasl);

	return result;
}

static GList *
imap_store_query_auth_types_sync (CamelService *service,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelServiceAuthType *authtype;
	GList *sasl_types, *t, *next;
	gboolean connected;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	connected = store->istream != NULL && store->connected;
	if (!connected)
		connected = connect_to_server_wrapper (
			service, cancellable, error);
	if (!connected)
		return NULL;

	sasl_types = camel_sasl_authtype_list (FALSE);
	for (t = sasl_types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!g_hash_table_lookup (store->authtypes, authtype->authproto)) {
			sasl_types = g_list_remove_link (sasl_types, t);
			g_list_free_1 (t);
		}
	}

	return g_list_prepend (sasl_types, &camel_imap_password_authtype);
}

static void
imap_migrate_to_user_cache_dir (CamelService *service)
{
	const gchar *user_data_dir, *user_cache_dir;

	g_return_if_fail (service != NULL);
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	user_data_dir = camel_service_get_user_data_dir (service);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	g_return_if_fail (user_data_dir != NULL);
	g_return_if_fail (user_cache_dir != NULL);

	/* migrate only if the source directory exists and the destination doesn't */
	if (g_file_test (user_data_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) &&
	    !g_file_test (user_cache_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		gchar *parent_dir;

		parent_dir = g_path_get_dirname (user_cache_dir);
		g_mkdir_with_parents (parent_dir, S_IRWXU);
		g_free (parent_dir);

		if (g_rename (user_data_dir, user_cache_dir) == -1)
			g_debug ("%s: Failed to migrate '%s' to '%s': %s", G_STRFUNC, user_data_dir, user_cache_dir, g_strerror (errno));
	}
}

static gboolean
imap_store_initable_init (GInitable *initable,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelImapStore *imap_store;
	CamelStore *store;
	CamelService *service;
	const gchar *user_cache_dir;
	gchar *tmp_path;

	imap_store = CAMEL_IMAP_STORE (initable);
	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);

	store->flags |= CAMEL_STORE_USE_CACHE_DIR;
	imap_migrate_to_user_cache_dir (service);

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	service = CAMEL_SERVICE (initable);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	/* setup/load the store summary */
	tmp_path = g_build_filename (user_cache_dir, ".ev-store-summary", NULL);
	imap_store->summary = camel_imap_store_summary_new ();
	camel_store_summary_set_filename ((CamelStoreSummary *) imap_store->summary, tmp_path);
	g_free (tmp_path);
	if (camel_store_summary_load ((CamelStoreSummary *) imap_store->summary) == 0) {
		CamelImapStoreSummary *is = imap_store->summary;

		/* XXX This won't work anymore.  The CamelSettings
		 *     object for this store is not yet configured. */
#if 0
		if (is->namespace) {
			const gchar *namespace;

			namespace = camel_imap_settings_get_namespace (
				CAMEL_IMAP_SETTINGS (settings));

			/* if namespace has changed, clear folder list */
			if (g_strcmp0 (namespace, is->namespace->full_name) != 0)
				camel_store_summary_clear ((CamelStoreSummary *) is);
		}
#endif

		imap_store->capabilities = is->capabilities;
		imap_set_server_level (imap_store);
	}

	return TRUE;
}

static const gchar *
imap_store_get_service_name (CamelNetworkService *service,
                             CamelNetworkSecurityMethod method)
{
	const gchar *service_name;

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			service_name = "imaps";
			break;

		default:
			service_name = "imap";
			break;
	}

	return service_name;
}

static guint16
imap_store_get_default_port (CamelNetworkService *service,
                             CamelNetworkSecurityMethod method)
{
	guint16 default_port;

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			default_port = IMAPS_PORT;
			break;

		default:
			default_port = IMAP_PORT;
			break;
	}

	return default_port;
}

static gboolean
imap_store_folder_is_subscribed (CamelSubscribable *subscribable,
                                 const gchar *folder_name)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (subscribable);
	CamelStoreInfo *si;
	gint truth = FALSE;

	si = camel_store_summary_path ((CamelStoreSummary *) imap_store->summary, folder_name);
	if (si) {
		truth = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	}

	return truth;
}

/* Note: folder_name must match a folder as listed with get_folder_info() -> full_name */
static gboolean
imap_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                  const gchar *folder_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelImapStore *imap_store;
	CamelImapResponse *response;
	CamelFolderInfo *fi;
	CamelStoreInfo *si;

	imap_store = CAMEL_IMAP_STORE (subscribable);

	if (!camel_imap_store_connected (imap_store, error))
		return FALSE;

	response = camel_imap_command (
		imap_store, NULL, cancellable, error,
		"SUBSCRIBE %F", folder_name);
	if (!response)
		return FALSE;

	camel_imap_response_free (imap_store, response);

	si = camel_store_summary_path ((CamelStoreSummary *) imap_store->summary, folder_name);
	if (si) {
		if ((si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) == 0) {
			si->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch ((CamelStoreSummary *) imap_store->summary);
			camel_store_summary_save ((CamelStoreSummary *) imap_store->summary);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	}

	if (imap_store->renaming) {
		/* we don't need to emit a "folder_subscribed" signal
		 * if we are in the process of renaming folders, so we
		 * are done here... */
		return TRUE;
	}

	fi = imap_build_folder_info (imap_store, folder_name);
	fi->flags |= CAMEL_FOLDER_NOCHILDREN;

	camel_subscribable_folder_subscribed (subscribable, fi);
	camel_folder_info_free (fi);

	return TRUE;
}

static gboolean
imap_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                    const gchar *folder_name,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelImapStore *imap_store;
	CamelImapResponse *response;

	imap_store = CAMEL_IMAP_STORE (subscribable);

	if (!camel_imap_store_connected (imap_store, error))
		return FALSE;

	response = camel_imap_command (
		imap_store, NULL, cancellable, error,
		"UNSUBSCRIBE %F", folder_name);
	if (!response)
		return FALSE;

	camel_imap_response_free (imap_store, response);

	return imap_folder_effectively_unsubscribed (
		imap_store, folder_name, error);
}

static void
camel_imap_store_class_init (CamelImapStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = imap_store_dispose;
	object_class->finalize = imap_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_IMAP_SETTINGS;
	service_class->get_name = imap_store_get_name;
	service_class->connect_sync = imap_store_connect_sync;
	service_class->disconnect_sync = imap_store_disconnect_sync;
	service_class->authenticate_sync = imap_store_authenticate_sync;
	service_class->query_auth_types_sync = imap_store_query_auth_types_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = hash_folder_name;
	store_class->equal_folder_name = equal_folder_name;
	store_class->can_refresh_folder = imap_can_refresh_folder;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->get_folder_sync = imap_store_get_folder_sync;
	store_class->get_folder_info_sync = imap_store_get_folder_info_sync;
	store_class->get_junk_folder_sync = imap_store_get_junk_folder_sync;
	store_class->get_trash_folder_sync = imap_store_get_trash_folder_sync;
	store_class->create_folder_sync = imap_store_create_folder_sync;
	store_class->delete_folder_sync = imap_store_delete_folder_sync;
	store_class->rename_folder_sync = imap_store_rename_folder_sync;
	store_class->noop_sync = imap_store_noop_sync;
}

static void
camel_imap_store_initable_init (GInitableIface *interface)
{
	parent_initable_interface = g_type_interface_peek_parent (interface);

	interface->init = imap_store_initable_init;
}

static void
camel_network_service_init (CamelNetworkServiceInterface *interface)
{
	interface->get_service_name = imap_store_get_service_name;
	interface->get_default_port = imap_store_get_default_port;
}

static void
camel_subscribable_init (CamelSubscribableInterface *interface)
{
	interface->folder_is_subscribed = imap_store_folder_is_subscribed;
	interface->subscribe_folder_sync = imap_store_subscribe_folder_sync;
	interface->unsubscribe_folder_sync = imap_store_unsubscribe_folder_sync;
}

static void
camel_imap_store_init (CamelImapStore *imap_store)
{
	g_static_rec_mutex_init (&imap_store->command_and_response_lock);

	imap_store->istream = NULL;
	imap_store->ostream = NULL;

	/* TODO: support dir_sep per namespace */
	imap_store->dir_sep = '\0';
	imap_store->current_folder = NULL;
	imap_store->connected = FALSE;
	imap_store->preauthed = FALSE;

	imap_store->tag_prefix = imap_tag_prefix++;
	if (imap_tag_prefix > 'Z')
		imap_tag_prefix = 'A';

	imap_store->known_alerts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_signal_connect (
		imap_store, "notify::settings",
		G_CALLBACK (imap_store_update_store_flags), NULL);
}

static void
imap_set_server_level (CamelImapStore *store)
{
	if (store->capabilities & IMAP_CAPABILITY_IMAP4REV1) {
		store->server_level = IMAP_LEVEL_IMAP4REV1;
		store->capabilities |= IMAP_CAPABILITY_STATUS;
	} else if (store->capabilities & IMAP_CAPABILITY_IMAP4)
		store->server_level = IMAP_LEVEL_IMAP4;
	else
		store->server_level = IMAP_LEVEL_UNKNOWN;
}

static gboolean
imap_folder_effectively_unsubscribed (CamelImapStore *imap_store,
                                      const gchar *folder_name,
                                      GError **error)
{
	CamelFolderInfo *fi;
	CamelStoreInfo *si;

	si = camel_store_summary_path ((CamelStoreSummary *) imap_store->summary, folder_name);
	if (si) {
		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			si->flags &= ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch ((CamelStoreSummary *) imap_store->summary);
			camel_store_summary_save ((CamelStoreSummary *) imap_store->summary);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	}

	if (imap_store->renaming) {
		/* we don't need to emit a "folder_unsubscribed" signal
		 * if we are in the process of renaming folders, so we
		 * are done here... */
		return TRUE;

	}

	fi = imap_build_folder_info (imap_store, folder_name);
	camel_subscribable_folder_unsubscribed (
		CAMEL_SUBSCRIBABLE (imap_store), fi);
	camel_folder_info_free (fi);

	return TRUE;
}

static void
imap_forget_folder (CamelImapStore *imap_store,
                    const gchar *folder_name,
                    GError **error)
{
	CamelService *service;
	const gchar *user_cache_dir;
	gchar *state_file;
	gchar *journal_file;
	gchar *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const gchar *name;

	name = strrchr (folder_name, imap_store->dir_sep);
	if (name)
		name++;
	else
		name = folder_name;

	service = CAMEL_SERVICE (imap_store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	storage_path = g_build_filename (user_cache_dir, "folders", NULL);
	folder_dir = imap_path_to_physical (storage_path, folder_name);
	g_free (storage_path);
	if (g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		goto event;
	}

	/* Delete summary and all the data */
	journal_file = g_strdup_printf ("%s/journal", folder_dir);
	g_unlink (journal_file);
	g_free (journal_file);

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	g_unlink (state_file);
	g_free (state_file);

	camel_db_delete_folder (((CamelStore *) imap_store)->cdb_w, folder_name, NULL);
	camel_imap_message_cache_delete (folder_dir, NULL);

	state_file = g_strdup_printf ("%s/subfolders", folder_dir);
	g_rmdir (state_file);
	g_free (state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

 event:

	camel_store_summary_remove_path ((CamelStoreSummary *) imap_store->summary, folder_name);
	camel_store_summary_save ((CamelStoreSummary *) imap_store->summary);

	fi = imap_build_folder_info (imap_store, folder_name);
	camel_store_folder_deleted (CAMEL_STORE (imap_store), fi);
	camel_folder_info_free (fi);
}

static gboolean
imap_check_folder_still_extant (CamelImapStore *imap_store,
                                const gchar *full_name,
                                GError **error)
{
	CamelImapResponse *response;

	response = camel_imap_command (
		imap_store, NULL, NULL, NULL, "LIST \"\" %F", full_name);

	if (response) {
		gboolean stillthere = response->untagged->len != 0;

		camel_imap_response_free_without_processing (imap_store, response);

		return stillthere;
	}

	/* if the command was rejected, there must be some other error,
	 * assume it worked so we dont blow away the folder unecessarily */
	return TRUE;
}

static gboolean
imap_summary_is_dirty (CamelFolderSummary *summary)
{
	CamelImapMessageInfo *info;
	gint i;
	gboolean found = FALSE;
	GPtrArray *known_uids;

	known_uids = camel_folder_summary_get_array (summary);
	g_return_val_if_fail (known_uids != NULL, FALSE);

	for (i = 0; i < known_uids->len && !found; i++) {
		info = (CamelImapMessageInfo *) camel_folder_summary_get (summary, g_ptr_array_index (known_uids, i));
		if (info) {
			found = info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED;
			camel_message_info_free (info);
		}
	}

	camel_folder_summary_free_array (known_uids);

	return found;
}

static gboolean
imap_store_noop_sync (CamelStore *store,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelImapStore *imap_store = (CamelImapStore *) store;
	CamelImapResponse *response;
	CamelFolder *current_folder;
	gboolean success = TRUE;

	if (!camel_imap_store_connected (imap_store, error))
		return FALSE;

	current_folder = imap_store->current_folder;
	if (current_folder && imap_summary_is_dirty (current_folder->summary)) {
		/* let's sync the flags instead.  NB: must avoid folder lock */
		success = CAMEL_FOLDER_GET_CLASS (current_folder)->synchronize_sync (
			current_folder, FALSE, cancellable, error);
	} else {
		response = camel_imap_command (imap_store, NULL, cancellable, error, "NOOP");
		if (response)
			camel_imap_response_free (imap_store, response);
		else
			success = FALSE;
	}

	return success;
}

static CamelFolder *
imap_store_get_trash_folder_sync (CamelStore *store,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelFolder *folder = NULL;
	const gchar *user_cache_dir;
	gchar *trash_path;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	settings = camel_service_ref_settings (service);

	trash_path = camel_imap_settings_dup_real_trash_path (
		CAMEL_IMAP_SETTINGS (settings));
	if (trash_path != NULL) {
		folder = camel_store_get_folder_sync (
			store, trash_path, 0, cancellable, NULL);
		if (folder == NULL)
			camel_imap_settings_set_real_trash_path (
				CAMEL_IMAP_SETTINGS (settings), NULL);
	}
	g_free (trash_path);

	g_object_unref (settings);

	if (folder)
		return folder;

	folder = CAMEL_STORE_CLASS (camel_imap_store_parent_class)->
		get_trash_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state;

		state = g_build_filename (
			user_cache_dir, "system", "Trash.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free (state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
imap_store_get_junk_folder_sync (CamelStore *store,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelFolder *folder = NULL;
	const gchar *user_cache_dir;
	gchar *junk_path;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	settings = camel_service_ref_settings (service);

	junk_path = camel_imap_settings_dup_real_junk_path (
		CAMEL_IMAP_SETTINGS (settings));
	if (junk_path != NULL) {
		folder = camel_store_get_folder_sync (
			store, junk_path, 0, cancellable, NULL);
		if (folder == NULL)
			camel_imap_settings_set_real_junk_path (
				CAMEL_IMAP_SETTINGS (settings), NULL);
	}
	g_free (junk_path);

	g_object_unref (settings);

	if (folder)
		return folder;

	folder = CAMEL_STORE_CLASS (camel_imap_store_parent_class)->
		get_junk_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state;

		state = g_build_filename (
			user_cache_dir, "system", "Junk.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free (state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static guint
hash_folder_name (gconstpointer key)
{
	if (g_ascii_strcasecmp (key, "INBOX") == 0)
		return g_str_hash ("INBOX");
	else
		return g_str_hash (key);
}

static gboolean
equal_folder_name (gconstpointer a,
                   gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal (aname, bname);
}

struct imap_status_item {
	struct imap_status_item *next;
	gchar *name;
	guint32 value;
};

static void
imap_status_item_free (struct imap_status_item *items)
{
	struct imap_status_item *next;

	while (items != NULL) {
		next = items->next;
		g_free (items->name);
		g_free (items);
		items = next;
	}
}

static struct imap_status_item *
get_folder_status (CamelImapStore *imap_store,
                   const gchar *folder_name,
                   const gchar *type)
{
	struct imap_status_item *items, *item, *tail;
	CamelImapResponse *response;
	gchar *status, *name, *p;

	/* FIXME: we assume the server is STATUS-capable */

	response = camel_imap_command (
		imap_store, NULL, NULL, NULL,
		"STATUS %F (%s)", folder_name, type);

	if (!response) {
		if (imap_check_folder_still_extant (imap_store, folder_name, NULL) == FALSE) {
			imap_folder_effectively_unsubscribed (imap_store, folder_name, NULL);
			imap_forget_folder (imap_store, folder_name, NULL);
		}
		return NULL;
	}

	if (!(status = camel_imap_response_extract (imap_store, response, "STATUS", NULL)))
		return NULL;

	p = status + strlen ("* STATUS ");
	while (*p == ' ')
		p++;

	/* skip past the mailbox string */
	if (*p == '"') {
		p++;
		while (*p != '\0') {
			if (*p == '"' && p[-1] != '\\') {
				p++;
				break;
			}

			p++;
		}
	} else {
		while (*p != ' ')
			p++;
	}

	while (*p == ' ')
		p++;

	if (*p++ != '(') {
		g_free (status);
		return NULL;
	}

	while (*p == ' ')
		p++;

	if (*p == ')') {
		g_free (status);
		return NULL;
	}

	items = NULL;
	tail = (struct imap_status_item *) &items;

	do {
		name = p;
		while (*p != ' ')
			p++;

		item = g_malloc (sizeof (struct imap_status_item));
		item->next = NULL;
		item->name = g_strndup (name, p - name);
		item->value = strtoul (p, &p, 10);

		tail->next = item;
		tail = item;

		while (*p == ' ')
			p++;
	} while (*p != ')');

	g_free (status);

	return items;
}

static CamelFolder *
imap_store_get_folder_sync (CamelStore *store,
                            const gchar *folder_name,
                            CamelStoreGetFolderFlags flags,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	CamelFolder *new_folder;
	CamelService *service;
	const gchar *user_cache_dir;
	gchar *folder_dir, *storage_path;
	GError *local_error = NULL;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	/* Try to get it locally first, if it is, then the client will
	 * force a select when necessary */
	new_folder = get_folder_offline (store, folder_name, flags, &local_error);
	if (new_folder)
		return new_folder;

	g_clear_error (&local_error);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	if (!camel_imap_store_connected (imap_store, error))
		return NULL;

	if (!g_ascii_strcasecmp (folder_name, "INBOX"))
		folder_name = "INBOX";

	if (imap_store->current_folder) {
		g_object_unref (imap_store->current_folder);
		imap_store->current_folder = NULL;
	}

	response = camel_imap_command (imap_store, NULL, cancellable, &local_error, "SELECT %F", folder_name);
	if (!response) {
		gchar *folder_real, *parent_name, *parent_real;
		const gchar *c;

		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_propagate_error (error, local_error);
			return NULL;
		}

		g_clear_error (&local_error);

		if (!(flags & CAMEL_STORE_FOLDER_CREATE)) {
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_NO_FOLDER,
				_("No such folder %s"), folder_name);
			return NULL;
		}

		parent_name = strrchr (folder_name, '/');
		c = parent_name ? parent_name + 1 : folder_name;
		while (*c && *c != imap_store->dir_sep && !strchr ("#%*", *c))
			c++;

		if (*c != '\0') {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_PATH,
				_("The folder name \"%s\" is invalid because it contains the character \"%c\""),
				folder_name, *c);
			return NULL;
		}

		if (parent_name) {
			parent_name = g_strndup (folder_name, parent_name - folder_name);
			parent_real = camel_imap_store_summary_path_to_full (imap_store->summary, parent_name, imap_store->dir_sep);
		} else {
			parent_real = NULL;
		}

		if (parent_real != NULL) {
			gboolean need_convert = FALSE;
			gchar *resp, *thisone;
			gint flags;
			gint i;

			if (!(response = camel_imap_command (imap_store, NULL, cancellable, error, "LIST \"\" %G", parent_real))) {
				g_free (parent_name);
				g_free (parent_real);
				return NULL;
			}

			/* FIXME: does not handle unexpected circumstances very well */
			for (i = 0; i < response->untagged->len; i++) {
				resp = response->untagged->pdata[i];

				if (!imap_parse_list_response (imap_store, resp, &flags, NULL, &thisone))
					continue;

				if (!strcmp (parent_name, thisone)) {
					if (flags & CAMEL_FOLDER_NOINFERIORS)
						need_convert = TRUE;
				}

				g_free (thisone);
			}

			camel_imap_response_free (imap_store, response);

			/* if not, check if we can delete it and recreate it */
			if (need_convert) {
				struct imap_status_item *items, *item;
				guint32 messages = 0;
				gchar *name;

				item = items = get_folder_status (imap_store, parent_name, "MESSAGES");
				while (item != NULL) {
					if (!g_ascii_strcasecmp (item->name, "MESSAGES")) {
						messages = item->value;
						break;
					}

					item = item->next;
				}

				imap_status_item_free (items);

				if (messages > 0) {
					g_set_error (
						error, CAMEL_FOLDER_ERROR,
						CAMEL_FOLDER_ERROR_INVALID_STATE,
						_("The parent folder is not allowed to contain subfolders"));
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				}

				/* delete the old parent and recreate it */
				if (!imap_store_delete_folder_sync (
					store, parent_name, cancellable, error)) {
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				}

				/* add the dirsep to the end of parent_name */
				name = g_strdup_printf ("%s%c", parent_real, imap_store->dir_sep);
				response = camel_imap_command (
					imap_store, NULL,
					cancellable, error,
					"CREATE %G", name);
				g_free (name);

				if (!response) {
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				} else
					camel_imap_response_free (imap_store, response);
			}

			g_free (parent_real);
		}

		g_free (parent_name);

		folder_real = camel_imap_store_summary_path_to_full (imap_store->summary, folder_name, imap_store->dir_sep);
		response = camel_imap_command (imap_store, NULL, cancellable, error, "CREATE %G", folder_real);
		if (response) {
			camel_imap_store_summary_add_from_full (imap_store->summary, folder_real, imap_store->dir_sep);

			camel_imap_response_free (imap_store, response);

			response = camel_imap_command (imap_store, NULL, NULL, NULL, "SELECT %F", folder_name);
		}
		g_free (folder_real);
		if (!response) {
			return NULL;
		}
	} else if (flags & CAMEL_STORE_FOLDER_EXCL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder '%s': folder exists."),
			folder_name);

		camel_imap_response_free_without_processing (imap_store, response);

		return NULL;
	}

	storage_path = g_build_filename (user_cache_dir, "folders", NULL);
	folder_dir = imap_path_to_physical (storage_path, folder_name);
	g_free (storage_path);
	new_folder = camel_imap_folder_new (store, folder_name, folder_dir, error);
	g_free (folder_dir);
	if (new_folder) {
		if (imap_store->current_folder)
			g_object_unref (imap_store->current_folder);
		imap_store->current_folder = g_object_ref (new_folder);
		if (!camel_imap_folder_selected (
			new_folder, response, cancellable, error)) {

			g_object_unref (imap_store->current_folder);
			imap_store->current_folder = NULL;
			g_object_unref (new_folder);
			new_folder = NULL;
		}
	}
	camel_imap_response_free_without_processing (imap_store, response);

	return new_folder;
}

static CamelFolder *
get_folder_offline (CamelStore *store,
                    const gchar *folder_name,
                    guint32 flags,
                    GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelFolder *new_folder = NULL;
	CamelStoreInfo *si;
	CamelService *service;
	const gchar *user_cache_dir;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	si = camel_store_summary_path ((CamelStoreSummary *) imap_store->summary, folder_name);
	if (si) {
		gchar *folder_dir, *storage_path;

		/* Note: Although the INBOX is defined to be case-insensitive in the IMAP RFC
		 * it is still up to the server how to acutally name it in a LIST response. Since
		 * we stored the name as the server provided it us in the summary we take that name
		 * to look up the folder.
		 * But for the on-disk cache we do always capitalize the Inbox no matter what the
		 * server provided.
		 */
		if (!g_ascii_strcasecmp (folder_name, "INBOX"))
			folder_name = "INBOX";

		storage_path = g_build_filename (user_cache_dir, "folders", NULL);
		folder_dir = imap_path_to_physical (storage_path, folder_name);
		g_free (storage_path);
		new_folder = camel_imap_folder_new (store, folder_name, folder_dir, error);
		g_free (folder_dir);

		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	} else {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder %s"), folder_name);
	}

	return new_folder;
}

static gboolean
imap_store_delete_folder_sync (CamelStore *store,
                               const gchar *folder_name,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	gboolean success = TRUE;

	if (!camel_imap_store_connected (imap_store, error))
		return FALSE;

	/* make sure this folder isn't currently SELECTed */
	response = camel_imap_command (imap_store, NULL, cancellable, error, "SELECT INBOX");
	if (!response)
		return FALSE;

	camel_imap_response_free_without_processing (imap_store, response);
	if (imap_store->current_folder)
		g_object_unref (imap_store->current_folder);
	/* no need to actually create a CamelFolder for INBOX */
	imap_store->current_folder = NULL;

	response = camel_imap_command (imap_store, NULL, cancellable, error, "DELETE %F", folder_name);
	if (response) {
		camel_imap_response_free (imap_store, response);
		imap_forget_folder (imap_store, folder_name, NULL);
	} else
		success = FALSE;

	return success;
}

static void
manage_subscriptions (CamelStore *store,
                      const gchar *old_name,
                      gboolean subscribe,
                      GCancellable *cancellable)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelStoreInfo *si;
	gint olen = strlen (old_name);
	const gchar *path;
	gint i, count;

	count = camel_store_summary_count ((CamelStoreSummary *) imap_store->summary);
	for (i = 0; i < count; i++) {
		si = camel_store_summary_index ((CamelStoreSummary *) imap_store->summary, i);
		if (si) {
			path = camel_store_info_path (imap_store->summary, si);
			if (strncmp (path, old_name, olen) == 0) {
				if (subscribe)
					imap_store_subscribe_folder_sync (
						CAMEL_SUBSCRIBABLE (store),
						path, cancellable, NULL);
				else
					imap_store_unsubscribe_folder_sync (
						CAMEL_SUBSCRIBABLE (store),
						path, cancellable, NULL);
			}
			camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
		}
	}
}

static void
rename_folder_info (CamelImapStore *imap_store,
                    const gchar *old_name,
                    const gchar *new_name)
{
	gint i, count;
	CamelStoreInfo *si;
	gint olen = strlen (old_name);
	const gchar *path;
	gchar *npath, *nfull;

	count = camel_store_summary_count ((CamelStoreSummary *) imap_store->summary);
	for (i = 0; i < count; i++) {
		si = camel_store_summary_index ((CamelStoreSummary *) imap_store->summary, i);
		if (si == NULL)
			continue;
		path = camel_store_info_path (imap_store->summary, si);
		if (strncmp (path, old_name, olen) == 0) {
			if (strlen (path) > olen)
				npath = g_strdup_printf ("%s/%s", new_name, path + olen + 1);
			else
				npath = g_strdup (new_name);
			nfull = camel_imap_store_summary_path_to_full (imap_store->summary, npath, imap_store->dir_sep);

			/* workaround for broken server (courier uses '.') that doesn't rename
			 * subordinate folders as required by rfc 2060 */
			if (imap_store->dir_sep == '.') {
				CamelImapResponse *response;

				response = camel_imap_command (imap_store, NULL, NULL, NULL, "RENAME %F %G", path, nfull);
				if (response)
					camel_imap_response_free (imap_store, response);
			}

			camel_store_info_set_string ((CamelStoreSummary *) imap_store->summary, si, CAMEL_STORE_INFO_PATH, npath);
			camel_store_info_set_string ((CamelStoreSummary *) imap_store->summary, si, CAMEL_IMAP_STORE_INFO_FULL_NAME, nfull);

			camel_store_summary_touch ((CamelStoreSummary *) imap_store->summary);
			g_free (nfull);
			g_free (npath);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	}
}

static gboolean
imap_store_rename_folder_sync (CamelStore *store,
                               const gchar *old_name,
                               const gchar *new_name_in,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	CamelService *service;
	CamelSettings *settings;
	const gchar *user_cache_dir;
	gchar *oldpath, *newpath, *storage_path;
	gboolean use_subscriptions;
	gboolean success = TRUE;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imap_settings_get_use_subscriptions (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

	if (!camel_imap_store_connected (imap_store, error)) {
		success = FALSE;
		goto fail;
	}

	/* make sure this folder isn't currently SELECTed - it's
	 * actually possible to rename INBOX but if you do another
	 * INBOX will immediately be created by the server */
	response = camel_imap_command (imap_store, NULL, cancellable, error, "SELECT INBOX");
	if (!response) {
		success = FALSE;
		goto fail;
	}

	camel_imap_response_free_without_processing (imap_store, response);
	if (imap_store->current_folder)
		g_object_unref (imap_store->current_folder);
	/* no need to actually create a CamelFolder for INBOX */
	imap_store->current_folder = NULL;

	imap_store->renaming = TRUE;
	if (use_subscriptions)
		manage_subscriptions (
			store, old_name, FALSE, cancellable);

	response = camel_imap_command (imap_store, NULL, cancellable, error, "RENAME %F %F", old_name, new_name_in);
	if (!response) {
		if (use_subscriptions)
			manage_subscriptions (
				store, old_name, TRUE, cancellable);
		success = FALSE;
		goto fail;
	}

	camel_imap_response_free (imap_store, response);

	/* rename summary, and handle broken server */
	rename_folder_info (imap_store, old_name, new_name_in);

	if (use_subscriptions)
		manage_subscriptions (
			store, new_name_in, TRUE, cancellable);

	storage_path = g_build_filename (user_cache_dir, "folders", NULL);
	oldpath = imap_path_to_physical (storage_path, old_name);
	newpath = imap_path_to_physical (storage_path, new_name_in);

	/* So do we care if this didn't work?  Its just a cache? */
	if (g_rename (oldpath, newpath) == -1) {
		g_warning (
			"Could not rename message cache "
			"'%s' to '%s': %s: cache reset",
			oldpath, newpath, g_strerror (errno));
	}

	if (CAMEL_STORE (imap_store)->folders) {
		CamelFolder *folder;

		folder = camel_object_bag_get (CAMEL_STORE (imap_store)->folders, old_name);
		if (folder) {
			CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

			if (imap_folder && imap_folder->journal) {
				gchar *folder_dir = imap_path_to_physical (storage_path, new_name_in);
				gchar *path = g_strdup_printf ("%s/journal", folder_dir);

				camel_offline_journal_set_filename (imap_folder->journal, path);

				g_free (path);
				g_free (folder_dir);
			}

			g_object_unref (folder);
		}
	}

	g_free (storage_path);
	g_free (oldpath);
	g_free (newpath);
fail:
	imap_store->renaming = FALSE;

	return success;
}

static CamelFolderInfo *
imap_store_create_folder_sync (CamelStore *store,
                               const gchar *parent_name,
                               const gchar *folder_name,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	gchar *full_name, *resp, *thisone, *parent_real, *real_name;
	CamelImapResponse *response;
	CamelFolderInfo *root = NULL;
	gboolean need_convert;
	gint i = 0, flags;
	const gchar *c;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	if (!parent_name)
		parent_name = "";

	c = folder_name;
	while (*c && *c != imap_store->dir_sep && !strchr ("#%*", *c))
		c++;

	if (*c != '\0') {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_PATH,
			_("The folder name \"%s\" is invalid because it contains the character \"%c\""),
			folder_name, *c);
		return NULL;
	}

	/* check if the parent allows inferiors */

	/* FIXME: use storesummary directly */
	parent_real = camel_imap_store_summary_full_from_path (imap_store->summary, parent_name);
	if (parent_real == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Unknown parent folder: %s"), parent_name);
		return NULL;
	}

	need_convert = FALSE;
	response = camel_imap_command (
		imap_store, NULL, cancellable, error,
		"LIST \"\" %G", parent_real);
	if (!response) /* whoa, this is bad */ {
		g_free (parent_real);
		return NULL;
	}

	/* FIXME: does not handle unexpected circumstances very well */
	for (i = 0; i < response->untagged->len && !need_convert; i++) {
		resp = response->untagged->pdata[i];

		if (!imap_parse_list_response (imap_store, resp, &flags, NULL, &thisone))
			continue;

		if (strcmp (thisone, parent_name) == 0) {
			if (flags & CAMEL_FOLDER_NOINFERIORS)
				need_convert = TRUE;
		}

		g_free (thisone);
	}

	camel_imap_response_free (imap_store, response);

	/* if not, check if we can delete it and recreate it */
	if (need_convert) {
		struct imap_status_item *items, *item;
		guint32 messages = 0;
		gchar *name;

		item = items = get_folder_status (imap_store, parent_name, "MESSAGES");
		while (item != NULL) {
			if (!g_ascii_strcasecmp (item->name, "MESSAGES")) {
				messages = item->value;
				break;
			}

			item = item->next;
		}

		imap_status_item_free (items);

		if (messages > 0) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_STATE,
				_("The parent folder is not allowed to contain subfolders"));
			g_free (parent_real);
			return NULL;
		}

		/* delete the old parent and recreate it */
		if (!imap_store_delete_folder_sync (store, parent_name, cancellable, error))
			return NULL;

		/* add the dirsep to the end of parent_name */
		name = g_strdup_printf ("%s%c", parent_real, imap_store->dir_sep);
		response = camel_imap_command (
			imap_store, NULL, cancellable, error,
			"CREATE %G", name);
		g_free (name);

		if (!response) {
			g_free (parent_real);
			return NULL;
		} else
			camel_imap_response_free (imap_store, response);

		root = imap_build_folder_info (imap_store, parent_name);
	}

	/* ok now we can create the folder */
	real_name = camel_imap_store_summary_path_to_full (imap_store->summary, folder_name, imap_store->dir_sep);
	full_name = imap_concat (imap_store, parent_real, real_name);
	g_free (real_name);
	response = camel_imap_command (
		imap_store, NULL, cancellable, error,
		"CREATE %G", full_name);

	if (response) {
		CamelImapStoreInfo *si;
		CamelFolderInfo *fi;

		camel_imap_response_free (imap_store, response);

		si = camel_imap_store_summary_add_from_full (imap_store->summary, full_name, imap_store->dir_sep);
		camel_store_summary_save ((CamelStoreSummary *) imap_store->summary);
		fi = imap_build_folder_info (imap_store, camel_store_info_path (imap_store->summary, si));
		fi->flags |= CAMEL_FOLDER_NOCHILDREN;
		if (root) {
			root->child = fi;
			fi->parent = root;
		} else {
			root = fi;
		}
		camel_store_folder_created (store, root);
	} else if (root) {
		/* need to re-recreate the folder we just deleted */
		camel_store_folder_created (store, root);
		camel_folder_info_free (root);
		root = NULL;
	}

	g_free (full_name);
	g_free (parent_real);

	return root;
}

static CamelFolderInfo *
parse_list_response_as_folder_info (CamelImapStore *imap_store,
                                    const gchar *response)
{
	CamelFolderInfo *fi;
	gint flags;
	gchar sep, *dir;
	CamelImapStoreInfo *si;
	guint32 newflags;

	if (!imap_parse_list_response (imap_store, response, &flags, &sep, &dir))
		return NULL;

	/* FIXME: should use imap_build_folder_info, note the differences with param setting tho */

	si = camel_imap_store_summary_add_from_full (imap_store->summary, dir, sep ? sep : '/');
	g_free (dir);
	if (si == NULL)
		return NULL;

	newflags = (si->info.flags & (CAMEL_STORE_INFO_FOLDER_SUBSCRIBED | CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW)) | (flags & ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED);
	if (si->info.flags != newflags) {
		si->info.flags = newflags;
		camel_store_summary_touch ((CamelStoreSummary *) imap_store->summary);
	}

	flags = (flags & ~CAMEL_FOLDER_SUBSCRIBED) | (si->info.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED);

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (camel_store_info_path (imap_store->summary, si));
	if (!g_ascii_strcasecmp (fi->full_name, "inbox")) {
		flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;
		fi->display_name = g_strdup (_("Inbox"));
	} else
		fi->display_name = g_strdup (camel_store_info_name (imap_store->summary, si));

	/* HACK: some servers report noinferiors for all folders (uw-imapd)
	 * We just translate this into nochildren, and let the imap layer enforce
	 * it.  See create folder */
	if (flags & CAMEL_FOLDER_NOINFERIORS)
		flags = (flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;
	fi->flags = flags;

	fi->total = -1;
	fi->unread = -1;

	return fi;
}

static gint imap_match_pattern (CamelImapStoreNamespace *ns, const gchar *pattern, const gchar *name)
{
	gchar p, n, dir_sep;

	if (!ns)
		return TRUE;

	dir_sep = ns->sep;

	p = *pattern++;
	n = *name++;
	while (n && p) {
		if (n == p) {
			p = *pattern++;
			n = *name++;
		} else if (p == '%') {
			if (n != dir_sep) {
				n = *name++;
			} else {
				p = *pattern++;
			}
		} else if (p == '*') {
			return TRUE;
		} else
			return FALSE;
	}

	return n == 0 && (p == '%' || p == 0);
}

/* imap needs to treat inbox case insensitive */
/* we'll assume the names are normalized already */
static guint folder_hash (gconstpointer ap)
{
	const gchar *a = ap;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		a = "INBOX";

	return g_str_hash (a);
}

static gint folder_eq (gconstpointer ap, gconstpointer bp)
{
	const gchar *a = ap;
	const gchar *b = bp;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		a = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		b = "INBOX";

	return g_str_equal (a, b);
}

static void
get_folders_free (gpointer k,
                  gpointer v,
                  gpointer d)
{
	camel_folder_info_free (v);
}

static gboolean
get_folders_sync (CamelImapStore *imap_store,
                  const gchar *ppattern,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelImapResponse *response;
	CamelFolderInfo *fi, *hfi;
	gchar *list;
	gint i, count, j, k;
	GHashTable *present;
	CamelStoreInfo *si;
	const gchar *pattern = ppattern;
	CamelImapStoreNamespace *ns;
	gboolean success = TRUE, first_namespace = TRUE;

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	/* We do a LIST followed by LSUB, and merge the results.  LSUB may not be a strict
	 * subset of LIST for some servers, so we can't use either or separately */
	present = g_hash_table_new (folder_hash, folder_eq);

	if (!pattern)
		pattern = "";

	for (ns = imap_store->summary->namespace;
	     ns && !g_cancellable_is_cancelled (cancellable);
	     ns = ns->next, first_namespace = FALSE) {
		for (k = 0; k < 2; k++) {
			gchar *tmp = NULL;

			if (!ppattern) {
				if (!ns->full_name || !*ns->full_name) {
					if (k == 1)
						break;
					tmp = g_strdup ("*");
				} else if (k == 0)
					tmp = g_strdup_printf ("%s%c", ns->full_name, ns->sep);
				else
					tmp = g_strdup_printf ("%s%c*", ns->full_name, ns->sep);
				pattern = tmp;
			}

			for (j = 0; j < 2; j++) {
				response = camel_imap_command (
					imap_store, NULL, cancellable,
					first_namespace ? error : NULL,
					"%s \"\" %G",
					j == 1 ? "LSUB" : "LIST",
					pattern);
				if (!response) {
					/* do not worry if checking in some namespace fails */
					if (!ppattern)
						continue;

					success = FALSE;
					g_free (tmp);
					goto fail;
				}

				for (i = 0; i < response->untagged->len; i++) {
					list = response->untagged->pdata[i];
					fi = parse_list_response_as_folder_info (imap_store, list);
					if (fi && *fi->full_name) {
						hfi = g_hash_table_lookup (present, fi->full_name);
						if (hfi == NULL) {
							if (j == 1) {
								fi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
								if ((fi->flags & (CAMEL_IMAP_FOLDER_MARKED | CAMEL_IMAP_FOLDER_UNMARKED)))
									imap_store->capabilities |= IMAP_CAPABILITY_useful_lsub;
							}
							g_hash_table_insert (present, fi->full_name, fi);
						} else {
							if (j == 1)
								hfi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
							camel_folder_info_free (fi);
						}
					} else if (fi) {
						camel_folder_info_free (fi);
					}
				}

				camel_imap_response_free (imap_store, response);
			}

			g_free (tmp);

			/* look for matching only, if ppattern was non-NULL */
			if (ppattern)
				break;
		}
	}

	if (g_cancellable_is_cancelled (cancellable))
		goto fail;

	/* Sync summary to match */

	/* FIXME: we need to emit folder_create/subscribed/etc events for any new folders */
	count = camel_store_summary_count ((CamelStoreSummary *) imap_store->summary);

	for (i = 0; i < count; i++) {
		const gchar *full_name;

		si = camel_store_summary_index ((CamelStoreSummary *) imap_store->summary, i);
		if (si == NULL)
			continue;

		full_name = camel_imap_store_info_full_name (imap_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
			continue;
		}

		if (!ppattern || imap_match_pattern (camel_imap_store_summary_namespace_find_full (imap_store->summary, full_name), pattern, full_name)) {
			if ((fi = g_hash_table_lookup (present, camel_store_info_path (imap_store->summary, si))) != NULL) {
				if (((fi->flags ^ si->flags) & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)) {
					si->flags = (si->flags & ~CAMEL_FOLDER_SUBSCRIBED) | (fi->flags & CAMEL_FOLDER_SUBSCRIBED);
					camel_store_summary_touch ((CamelStoreSummary *) imap_store->summary);

					camel_store_folder_created (CAMEL_STORE (imap_store), fi);
					camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (imap_store), fi);
				}
			} else {
				gchar *dup_folder_name = g_strdup (camel_store_info_path (imap_store->summary, si));

				if (dup_folder_name) {
					imap_folder_effectively_unsubscribed (imap_store, dup_folder_name, NULL);
					imap_forget_folder (imap_store, dup_folder_name, NULL);

					g_free (dup_folder_name);
				} else {
					camel_store_summary_remove ((CamelStoreSummary *) imap_store->summary, si);
				}

				count--;
				i--;
			}
		}
		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	}
fail:
	g_hash_table_foreach (present, get_folders_free, NULL);
	g_hash_table_destroy (present);

	return success;
}

#if 0
static void
dumpfi (CamelFolderInfo *fi)
{
	gint depth;
	CamelFolderInfo *n = fi;

	if (fi == NULL)
		return;

	depth = 0;
	while (n->parent) {
		depth++;
		n = n->parent;
	}

	while (fi) {
		printf ("%-25s %-25s %*s\n", fi->name, fi->full_name, (gint)(depth * 2 + strlen (fi->uri)), fi->uri);
		if (fi->child)
			dumpfi (fi->child);
		fi = fi->next;
	}
}
#endif

static void
fill_fi (CamelStore *store,
         CamelFolderInfo *fi,
         guint32 flags)
{
	CamelFolder *folder;

	folder = camel_object_bag_peek (store->folders, fi->full_name);
	if (folder) {
		CamelImapSummary *ims;

		if (folder->summary)
			ims = (CamelImapSummary *) folder->summary;
		else
			ims = (CamelImapSummary *) camel_imap_summary_new (folder);

		fi->unread = camel_folder_summary_get_unread_count ((CamelFolderSummary *) ims);
		fi->total = camel_folder_summary_get_saved_count ((CamelFolderSummary *) ims);

		if (!folder->summary)
			g_object_unref (ims);
		g_object_unref (folder);
	}
}

static void
refresh_refresh (CamelSession *session,
                 GCancellable *cancellable,
                 CamelImapStore *store,
                 GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	gchar *namespace;

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	namespace = camel_imap_settings_dup_namespace (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

	camel_operation_push_message (
		cancellable,
		_("Retrieving list of folders at '%s'"),
		camel_service_get_display_name (service));

	if (!camel_imap_store_connected (store, error))
		goto done;

	if (namespace != NULL) {
		if (!get_folders_sync (store, "INBOX", cancellable, error))
			goto done;
	} else {
		/* this can fail on some servers, thus just try it, but do not skip
		 * look in all namespaces, unless the operation was cancelled */
		if (!get_folders_sync (store, "*", cancellable, NULL) &&
		    g_cancellable_is_cancelled (cancellable))
			goto done;
	}

	/* look in all namespaces */
	get_folders_sync (store, NULL, cancellable, error);

	if (!g_cancellable_is_cancelled (cancellable))
		camel_store_summary_save (CAMEL_STORE_SUMMARY (store->summary));

done:
	camel_operation_pop_message (cancellable);

	g_free (namespace);
}

static CamelFolderInfo *
imap_store_get_folder_info_sync (CamelStore *store,
                                 const gchar *top,
                                 CamelStoreGetFolderInfoFlags flags,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelFolderInfo *tree = NULL;
	CamelService *service;
	CamelSession *session;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	/* If we have a list of folders already, use that, but if we haven't
	 * updated for a while, then trigger an asynchronous rescan.  Otherwise
	 * we update the list first, and then build it from that */

	if (top == NULL)
		top = "";

	if (camel_debug ("imap:folder_info"))
		printf ("get folder info online\n");

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		tree = get_folder_info_offline (store, top, flags, error);
		return tree;
	}

	if ((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
	    && camel_store_summary_count ((CamelStoreSummary *) imap_store->summary) > 0) {
		time_t now;
		gint ref;

		now = time (NULL);
		ref = now > imap_store->refresh_stamp + 60 * 60 * 1;
		if (ref) {
			ref = now > imap_store->refresh_stamp + 60 * 60 * 1;
			if (ref) {
				imap_store->refresh_stamp = now;

				camel_session_submit_job (
					session, (CamelSessionCallback)
					refresh_refresh,
					g_object_ref (store),
					(GDestroyNotify) g_object_unref);
			}
		}
	} else {
		gchar *pattern;
		gint i;
		CamelImapStoreNamespace *ns;

		if (!camel_imap_store_connected ((CamelImapStore *) store, error))
			return NULL;

		if (top[0] == 0) {
			pattern = g_alloca (3);
			pattern[0] = '*';
			pattern[1] = 0;
			i = 0;
		} else {
			gchar *name;

			name = camel_imap_store_summary_full_from_path (imap_store->summary, top);
			if (name == NULL)
				name = camel_imap_store_summary_path_to_full (imap_store->summary, top, imap_store->dir_sep);

			i = strlen (name);
			pattern = g_alloca (i + 5);
			strcpy (pattern, name);
			g_free (name);
		}

		ns = camel_imap_store_summary_get_main_namespace (imap_store->summary);
		if (!get_folders_sync (imap_store, pattern, cancellable, error))
			return NULL;
		if (pattern[0] != '*' && ns) {
			pattern[i] = ns->sep;
			pattern[i + 1] = (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) ? '*':'%';
			pattern[i + 2] = 0;
			get_folders_sync (imap_store, pattern, cancellable, NULL);
		}
		camel_store_summary_save ((CamelStoreSummary *) imap_store->summary);
	}

	tree = get_folder_info_offline (store, top, flags, error);
	return tree;
}

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store,
                         const gchar *top,
                         guint32 flags,
                         GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	gboolean include_inbox = FALSE;
	CamelFolderInfo *fi;
	CamelService *service;
	CamelSettings *settings;
	GPtrArray *folders;
	gchar *pattern, *name;
	gint i;
	CamelImapStoreNamespace *main_ns, *ns;
	gboolean use_subscriptions;
	gchar *junk_path;
	gchar *trash_path;

	if (camel_debug ("imap:folder_info"))
		printf ("get folder info offline\n");

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imap_settings_get_use_subscriptions (
		CAMEL_IMAP_SETTINGS (settings));

	junk_path = camel_imap_settings_dup_real_junk_path (
		CAMEL_IMAP_SETTINGS (settings));

	trash_path = camel_imap_settings_dup_real_trash_path (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

	/* So we can safely compare strings. */
	if (junk_path == NULL)
		junk_path = g_strdup ("");

	/* So we can safely compare strings. */
	if (trash_path == NULL)
		trash_path = g_strdup ("");

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	if (top == NULL || top[0] == '\0') {
		include_inbox = TRUE;
		top = "";
	}

	/* get starting point */
	if (top[0] == 0) {
		name = g_strdup ("");
	} else {
		name = camel_imap_store_summary_full_from_path (imap_store->summary, top);
		if (name == NULL)
			name = camel_imap_store_summary_path_to_full (imap_store->summary, top, imap_store->dir_sep);
	}

	main_ns = camel_imap_store_summary_get_main_namespace (imap_store->summary);
	pattern = imap_concat (imap_store, name, "*");

	/* folder_info_build will insert parent nodes as necessary and mark
	 * them as noselect, which is information we actually don't have at
	 * the moment. So let it do the right thing by bailing out if it's
	 * not a folder we're explicitly interested in. */

	for (i = 0; i < camel_store_summary_count ((CamelStoreSummary *) imap_store->summary); i++) {
		CamelStoreInfo *si = camel_store_summary_index ((CamelStoreSummary *) imap_store->summary, i);
		const gchar *full_name;
		gboolean folder_is_junk;
		gboolean folder_is_trash;

		if (si == NULL)
			continue;

		full_name = camel_imap_store_info_full_name (imap_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
			continue;
		}

		ns = camel_imap_store_summary_namespace_find_full (imap_store->summary, full_name);

		if ((g_str_equal (name, full_name)
		     || imap_match_pattern (ns, pattern, full_name)
		     || (include_inbox && !g_ascii_strcasecmp (full_name, "INBOX")))
		    && ((ns == main_ns &&
			(!use_subscriptions
			   || (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) == 0))
			|| (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)
			|| (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0)) {

			fi = imap_build_folder_info (imap_store, camel_store_info_path ((CamelStoreSummary *) imap_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			/* HACK: some servers report noinferiors for all folders (uw-imapd)
			 * We just translate this into nochildren, and let the imap layer enforce
			 * it.  See create folder */
			if (fi->flags & CAMEL_FOLDER_NOINFERIORS)
				fi->flags = (fi->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;

			/* blah, this gets lost somewhere, i can't be bothered finding out why */
			if (!g_ascii_strcasecmp (fi->full_name, "inbox"))
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;

			folder_is_trash =
				(fi->flags & CAMEL_FOLDER_TYPE_MASK) == 0 &&
				g_ascii_strcasecmp (fi->full_name, trash_path) == 0;
			if (folder_is_trash) {
				fi->flags &= ~CAMEL_FOLDER_TYPE_MASK;
				fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
			}

			folder_is_junk =
				(fi->flags & CAMEL_FOLDER_TYPE_MASK) == 0 &&
				g_ascii_strcasecmp (fi->full_name, junk_path) == 0;

			if (folder_is_junk) {
				fi->flags &= ~CAMEL_FOLDER_TYPE_MASK;
				fi->flags |= CAMEL_FOLDER_TYPE_JUNK;
			}

			if (!(si->flags & CAMEL_FOLDER_NOSELECT))
				fill_fi ((CamelStore *) imap_store, fi, 0);

			if (!fi->child)
				fi->flags |= CAMEL_FOLDER_NOCHILDREN;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) imap_store->summary, si);
	}
	g_free (pattern);

	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	g_free (name);

	g_free (junk_path);
	g_free (trash_path);

	return fi;
}

/* Use this whenever you need to ensure you're both connected and
 * online. */
gboolean
camel_imap_store_connected (CamelImapStore *store,
                            GError **error)
{
	CamelService *service;
	CamelOfflineStore *offline_store;
	gboolean success;
	GError *local_error = NULL;

	/* This looks stupid ... because it is.
	 *
	 * camel-service-connect will return OK if we connect in 'offline mode',
	 * which isn't what we want at all.  So we have to recheck we actually
	 * did connect anyway ... */

	if (store->istream != NULL)
		return TRUE;

	service = CAMEL_SERVICE (store);
	offline_store = CAMEL_OFFLINE_STORE (store);

	success =
		camel_offline_store_get_online (offline_store) &&
		camel_service_connect_sync (service, NULL, &local_error);

	if (success && store->istream != NULL)
		return TRUE;

	if (local_error != NULL)
		g_propagate_error (error, local_error);
	else
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online "
			"to complete this operation"));

	return FALSE;
}

gssize
camel_imap_store_readline (CamelImapStore *store,
                           gchar **dest,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelStreamBuffer *stream;
	gchar linebuf[1024] = {0};
	GByteArray *ba;
	gssize nread;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAP_STORE (store), -1);
	g_return_val_if_fail (dest, -1);

	*dest = NULL;

	/* Check for connectedness. Failed (or cancelled) operations will
	 * close the connection. We can't expect a read to have any
	 * meaning if we reconnect, so always set an exception.
	 */

	if (!camel_imap_store_connected (store, error))
		return -1;

	stream = CAMEL_STREAM_BUFFER (store->istream);

	ba = g_byte_array_new ();
	while ((nread = camel_stream_buffer_gets (stream, linebuf, sizeof (linebuf), cancellable, &local_error)) > 0) {
		g_byte_array_append (ba, (const guint8 *) linebuf, nread);
		if (linebuf[nread - 1] == '\n')
			break;
	}

	if (nread <= 0 || local_error) {
		if (!local_error)
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Server unexpectedly disconnected"));
		else {
			g_propagate_error (error, local_error);
			g_prefix_error (
				error, _("Server unexpectedly disconnected: "));
		}

		/* do not pass cancellable, the connection is gone or
		 * the cancellable cancelled, thus there will be no I/O */
		camel_service_disconnect_sync (
			CAMEL_SERVICE (store), FALSE, NULL, NULL);
		g_byte_array_free (ba, TRUE);
		return -1;
	}

	if (camel_verbose_debug) {
		fprintf (stderr, "received: ");
		fwrite (ba->data, 1, ba->len, stderr);
	}

	/* camel-imap-command.c:imap_read_untagged expects the CRLFs
	 * to be stripped off and be nul-terminated *sigh* */
	nread = ba->len - 1;
	ba->data[nread] = '\0';
	if (ba->data[nread - 1] == '\r') {
		ba->data[nread - 1] = '\0';
		nread--;
	}

	*dest = (gchar *) ba->data;
	g_byte_array_free (ba, FALSE);

	return nread;
}

static gboolean
imap_can_refresh_folder (CamelStore *store,
                         CamelFolderInfo *info,
                         GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	gboolean res;
	gboolean check_all;
	gboolean check_subscribed;
	gboolean subscribed;
	GError *local_error = NULL;

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	check_all = camel_imap_settings_get_check_all (
		CAMEL_IMAP_SETTINGS (settings));

	check_subscribed = camel_imap_settings_get_check_subscribed (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

	subscribed = ((info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0);

	res = CAMEL_STORE_CLASS (camel_imap_store_parent_class)->
		can_refresh_folder (store, info, &local_error) ||
		check_all || (check_subscribed && subscribed);

	if (!res && local_error == NULL && CAMEL_IS_IMAP_STORE (store)) {
		CamelStoreInfo *si;
		CamelStoreSummary *sm = CAMEL_STORE_SUMMARY (((CamelImapStore *)(store))->summary);

		if (!sm)
			return FALSE;

		si = camel_store_summary_path (sm, info->full_name);
		if (si) {
			res = (si->flags & CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW) != 0 ? TRUE : FALSE;

			camel_store_summary_info_free (sm, si);
		}
	}

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return res;
}
