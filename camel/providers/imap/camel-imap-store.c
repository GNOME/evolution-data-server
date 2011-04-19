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

static gboolean construct (CamelService *service, CamelSession *session,
		       CamelProvider *provider, CamelURL *url,
		       GError **error);

static gchar *imap_get_name (CamelService *service, gboolean brief);

static gboolean imap_noop (CamelStore *store, GError **error);
static CamelFolder *imap_get_junk(CamelStore *store, GError **error);
static CamelFolder *imap_get_trash(CamelStore *store, GError **error);
static GList *query_auth_types (CamelService *service, GError **error);
static guint hash_folder_name (gconstpointer key);
static gint compare_folder_name (gconstpointer a, gconstpointer b);

static CamelFolderInfo *create_folder (CamelStore *store, const gchar *parent_name, const gchar *folder_name, GError **error);
static gboolean delete_folder (CamelStore *store, const gchar *folder_name, GError **error);
static gboolean rename_folder (CamelStore *store, const gchar *old_name, const gchar *new_name, GError **error);
static gboolean folder_is_subscribed (CamelStore *store, const gchar *folder_name);
static gboolean subscribe_folder (CamelStore *store, const gchar *folder_name,
			      GError **error);
static gboolean unsubscribe_folder (CamelStore *store, const gchar *folder_name,
				GError **error);

static gboolean get_folders_sync(CamelImapStore *imap_store, const gchar *pattern, GError **error);

static gboolean imap_folder_effectively_unsubscribed(CamelImapStore *imap_store, const gchar *folder_name, GError **error);
static gboolean imap_check_folder_still_extant (CamelImapStore *imap_store, const gchar *full_name,  GError **error);
static void imap_forget_folder(CamelImapStore *imap_store, const gchar *folder_name, GError **error);
static void imap_set_server_level (CamelImapStore *store);

static gboolean imap_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error);
static gboolean imap_connect (CamelService *service, GError **error);
static gboolean imap_disconnect (CamelService *service, gboolean clean, GError **error);
static CamelFolder * get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error);
static CamelFolderInfo * get_folder_info (CamelStore *store, const gchar *top, guint32 flags, GError **error);
static CamelFolder * get_folder_offline (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error);
static CamelFolderInfo * get_folder_info_offline (CamelStore *store, const gchar *top, guint32 flags, GError **error);

G_DEFINE_TYPE (CamelImapStore, camel_imap_store, CAMEL_TYPE_OFFLINE_STORE)

static gboolean
free_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	return TRUE;
}

static void
imap_store_dispose (GObject *object)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (object);

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

	/* This frees current_folder, folders, authtypes, streams, and namespace. */
	camel_service_disconnect (CAMEL_SERVICE (imap_store), TRUE, NULL);

	g_free (imap_store->base_url);
	g_free (imap_store->storage_path);
	g_free (imap_store->users_namespace);
	g_free (imap_store->custom_headers);
	g_free (imap_store->real_trash_path);
	g_free (imap_store->real_junk_path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_store_parent_class)->finalize (object);
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
	service_class->construct = construct;
	service_class->query_auth_types = query_auth_types;
	service_class->get_name = imap_get_name;
	service_class->connect = imap_connect;
	service_class->disconnect = imap_disconnect;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = hash_folder_name;
	store_class->compare_folder_name = compare_folder_name;
	store_class->get_folder = get_folder;
	store_class->create_folder = create_folder;
	store_class->delete_folder = delete_folder;
	store_class->rename_folder = rename_folder;
	store_class->get_folder_info = get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->folder_is_subscribed = folder_is_subscribed;
	store_class->subscribe_folder = subscribe_folder;
	store_class->unsubscribe_folder = unsubscribe_folder;
	store_class->noop = imap_noop;
	store_class->get_trash = imap_get_trash;
	store_class->get_junk = imap_get_junk;
	store_class->can_refresh_folder = imap_can_refresh_folder;
}

static void
camel_imap_store_init (CamelImapStore *imap_store)
{
	imap_store->istream = NULL;
	imap_store->ostream = NULL;

	/* TODO: support dir_sep per namespace */
	imap_store->dir_sep = '\0';
	imap_store->current_folder = NULL;
	imap_store->connected = FALSE;
	imap_store->preauthed = FALSE;
	((CamelStore *)imap_store)->flags |= CAMEL_STORE_SUBSCRIPTIONS;

	imap_store->tag_prefix = imap_tag_prefix++;
	if (imap_tag_prefix > 'Z')
		imap_tag_prefix = 'A';
}

static gboolean
construct (CamelService *service, CamelSession *session,
	   CamelProvider *provider, CamelURL *url,
	   GError **error)
{
	CamelServiceClass *service_class;
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	gchar *tmp;
	CamelURL *summary_url;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_imap_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	imap_store->storage_path = camel_session_get_storage_path (session, service, error);
	if (!imap_store->storage_path)
		return FALSE;

	/* FIXME */
	imap_store->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
								   CAMEL_URL_HIDE_PARAMS |
								   CAMEL_URL_HIDE_AUTH));

	imap_store->parameters = 0;
	if (camel_url_get_param (url, "use_lsub"))
		imap_store->parameters |= IMAP_PARAM_SUBSCRIPTIONS;
	if (camel_url_get_param (url, "override_namespace") && camel_url_get_param (url, "namespace")) {
		imap_store->parameters |= IMAP_PARAM_OVERRIDE_NAMESPACE;
		g_free(imap_store->users_namespace);
		imap_store->users_namespace = g_strdup (camel_url_get_param (url, "namespace"));
	}
	if (camel_url_get_param (url, "check_all"))
		imap_store->parameters |= IMAP_PARAM_CHECK_ALL;
	if (camel_url_get_param (url, "check_lsub"))
		imap_store->parameters |= IMAP_PARAM_CHECK_LSUB;
	if (camel_url_get_param (url, "filter")) {
		imap_store->parameters |= IMAP_PARAM_FILTER_INBOX;
		store->flags |= CAMEL_STORE_FILTER_INBOX;
	}
	if (camel_url_get_param (url, "filter_junk"))
		imap_store->parameters |= IMAP_PARAM_FILTER_JUNK;
	if (camel_url_get_param (url, "filter_junk_inbox"))
		imap_store->parameters |= IMAP_PARAM_FILTER_JUNK_INBOX;

	imap_store->headers = IMAP_FETCH_MAILING_LIST_HEADERS;
	if (camel_url_get_param (url, "all_headers"))
		imap_store->headers = IMAP_FETCH_ALL_HEADERS;
	else if (camel_url_get_param (url, "basic_headers"))
		imap_store->headers = IMAP_FETCH_MINIMAL_HEADERS;

	if (camel_url_get_param (url, "imap_custom_headers")) {
		imap_store->custom_headers = g_strdup(camel_url_get_param (url, "imap_custom_headers"));
	}

	imap_store->real_trash_path = g_strdup (camel_url_get_param (url, "real_trash_path"));
	imap_store->real_junk_path = g_strdup (camel_url_get_param (url, "real_junk_path"));

	if (imap_store->real_trash_path && !*imap_store->real_trash_path) {
		g_free (imap_store->real_trash_path);
		imap_store->real_trash_path = NULL;
	}

	if (imap_store->real_trash_path && *imap_store->real_trash_path)
		store->flags &= ~CAMEL_STORE_VTRASH;

	if (imap_store->real_junk_path && !*imap_store->real_junk_path) {
		g_free (imap_store->real_junk_path);
		imap_store->real_junk_path = NULL;
	}

	if (imap_store->real_junk_path && *imap_store->real_junk_path) {
		store->flags &= ~CAMEL_STORE_VJUNK;
		store->flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	}

	/* setup/load the store summary */
	tmp = alloca(strlen(imap_store->storage_path)+32);
	sprintf(tmp, "%s/.ev-store-summary", imap_store->storage_path);
	imap_store->summary = camel_imap_store_summary_new();
	camel_store_summary_set_filename((CamelStoreSummary *)imap_store->summary, tmp);
	summary_url = camel_url_new(imap_store->base_url, NULL);
	camel_store_summary_set_uri_base((CamelStoreSummary *)imap_store->summary, summary_url);
	camel_url_free(summary_url);
	if (camel_store_summary_load((CamelStoreSummary *)imap_store->summary) == 0) {
		CamelImapStoreSummary *is = imap_store->summary;

		if (is->namespace) {
			/* if namespace has changed, clear folder list */
			if (imap_store->users_namespace && strcmp(imap_store->users_namespace, is->namespace->full_name) != 0) {
				camel_store_summary_clear((CamelStoreSummary *)is);
			}
		}

		imap_store->capabilities = is->capabilities;
		imap_set_server_level(imap_store);
	}

	return TRUE;
}

static gchar *
imap_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("IMAP server %s"), service->url->host);
	else
		return g_strdup_printf (_("IMAP service for %s on %s"),
					service->url->user, service->url->host);
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

static void
parse_capability(CamelImapStore *store, gchar *capa)
{
	gchar *lasts = NULL;
	gint i;

	for (capa = strtok_r (capa, " ", &lasts); capa; capa = strtok_r (NULL, " ", &lasts)) {
		if (!strncmp (capa, "AUTH=", 5)) {
			g_hash_table_insert (store->authtypes,
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

static gboolean
imap_get_capability (CamelService *service, GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;
	gchar *result;

	/* Find out the IMAP capabilities */
	/* We assume we have utf8 capable search until a failed search tells us otherwise */
	store->capabilities = IMAP_CAPABILITY_utf8_search;
	store->authtypes = g_hash_table_new (g_str_hash, g_str_equal);
	response = camel_imap_command (store, NULL, error, "CAPABILITY");
	if (!response)
		return FALSE;
	result = camel_imap_response_extract (store, response, "CAPABILITY ", error);
	if (!result)
		return FALSE;

	/* Skip over "* CAPABILITY ". */
	parse_capability(store, result+13);
	g_free (result);

	/* dunno why the groupwise guys didn't just list this in capabilities */
	if (store->capabilities & IMAP_CAPABILITY_XGWEXTENSIONS) {
		/* not critical if this fails */
		response = camel_imap_command (store, NULL, NULL, "XGWEXTENSIONS");
		if (response && (result = camel_imap_response_extract (store, response, "XGWEXTENSIONS ", NULL))) {
			parse_capability(store, result+16);
			g_free (result);
		}
	}

	imap_set_server_level (store);

	if (store->summary->capabilities != store->capabilities) {
		store->summary->capabilities = store->capabilities;
		camel_store_summary_touch((CamelStoreSummary *)store->summary);
		camel_store_summary_save((CamelStoreSummary *)store->summary);
	}

	return TRUE;
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#ifdef CAMEL_HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static gboolean
connect_to_server (CamelService *service,
		   const gchar *host, const gchar *serv, gint fallback_port, gint ssl_mode, GError **error)
{
	CamelImapStore *store = (CamelImapStore *) service;
	CamelSession *session;
	gchar *socks_host;
	gint socks_port;
	CamelImapResponse *response;
	CamelStream *tcp_stream;
	CamelSockOptData sockopt;
	gboolean force_imap4 = FALSE;
	gboolean clean_quit = TRUE;
	gchar *buf;

	if (ssl_mode != MODE_CLEAR) {
#ifdef CAMEL_HAVE_SSL
		if (ssl_mode == MODE_TLS)
			tcp_stream = camel_tcp_stream_ssl_new_raw (service->session, service->url->host, STARTTLS_FLAGS);
		else
			tcp_stream = camel_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
#else
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Could not connect to %s: %s"),
			service->url->host, _("SSL unavailable"));

		return FALSE;

#endif /* CAMEL_HAVE_SSL */
	} else
		tcp_stream = camel_tcp_stream_raw_new ();

	session = camel_service_get_session (service);
	camel_session_get_socks_proxy (session, &socks_host, &socks_port);

	if (socks_host) {
		camel_tcp_stream_set_socks_proxy ((CamelTcpStream *) tcp_stream, socks_host, socks_port);
		g_free (socks_host);
	}

	if (camel_tcp_stream_connect ((CamelTcpStream *) tcp_stream, host, serv, fallback_port, error) == -1) {
		g_prefix_error (
			error, _("Could not connect to %s: "),
			service->url->host);
		g_object_unref (tcp_stream);
		return FALSE;
	}

	store->ostream = tcp_stream;
	store->istream = camel_stream_buffer_new (tcp_stream, CAMEL_STREAM_BUFFER_READ);

	store->connected = TRUE;
	store->preauthed = FALSE;
	store->command = 0;

	/* Disable Nagle - we send a lot of small requests which nagle slows down */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt((CamelTcpStream *)tcp_stream, &sockopt);

	/* Set keepalive - needed for some hosts/router configurations, we're idle a lot */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt((CamelTcpStream *)tcp_stream, &sockopt);

	/* Read the greeting, if any, and deal with PREAUTH */
	if (camel_imap_store_readline (store, &buf, error) < 0) {
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

	if (!strncmp(buf, "* PREAUTH", 9))
		store->preauthed = TRUE;

	if (strstr (buf, "Courier-IMAP") || getenv("CAMEL_IMAP_BRAINDAMAGED")) {
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
	if (!imap_get_capability (service, error)) {
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

	if (force_imap4) {
		store->capabilities &= ~IMAP_CAPABILITY_IMAP4REV1;
		store->server_level = IMAP_LEVEL_IMAP4;
	}

	if (ssl_mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}

#ifdef CAMEL_HAVE_SSL
	/* as soon as we send a STARTTLS command, all hope is lost of a clean QUIT if problems arise */
	clean_quit = FALSE;

	if (!(store->capabilities & IMAP_CAPABILITY_STARTTLS)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			service->url->host, _("STARTTLS not supported"));

		goto exception;
	}

	response = camel_imap_command (store, NULL, error, "STARTTLS");
	if (!response) {
		g_object_unref (store->istream);
		g_object_unref (store->ostream);
		store->istream = store->ostream = NULL;
		return FALSE;
	}

	camel_imap_response_free_without_processing (store, response);

	/* Okay, now toggle SSL/TLS mode */
	if (camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream)) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			service->url->host, _("SSL negotiations failed"));
		goto exception;
	}
#else
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Failed to connect to IMAP server %s in secure mode: %s"),
		service->url->host, _("SSL is not available in this build"));
	goto exception;
#endif /* CAMEL_HAVE_SSL */

	/* rfc2595, section 4 states that after a successful STLS
           command, the client MUST discard prior CAPA responses */
	if (!imap_get_capability (service, error)) {
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

	if (store->capabilities & IMAP_CAPABILITY_LOGINDISABLED ) {
		clean_quit = TRUE;
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to IMAP server %s in secure mode: %s"),
			service->url->host, _("Unknown error"));
		goto exception;
	}

	return TRUE;

exception:

	if (clean_quit && store->connected) {
		/* try to disconnect cleanly */
		response = camel_imap_command (store, NULL, error, "LOGOUT");
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

	return FALSE;
}

#ifndef G_OS_WIN32

/* Using custom commands to connect to IMAP servers is not supported on Win32 */

static gboolean
connect_to_server_process (CamelService *service, const gchar *cmd, GError **error)
{
	CamelImapStore *store = (CamelImapStore *) service;
	CamelStream *cmd_stream;
	gint ret, i = 0;
	gchar *buf;
	gchar *cmd_copy;
	gchar *full_cmd;
	gchar *child_env[7];

	/* Put full details in the environment, in case the connection
	   program needs them */
	buf = camel_url_to_string(service->url, 0);
	child_env[i++] = g_strdup_printf("URL=%s", buf);
	g_free(buf);

	child_env[i++] = g_strdup_printf("URLHOST=%s", service->url->host);
	if (service->url->port)
		child_env[i++] = g_strdup_printf("URLPORT=%d", service->url->port);
	if (service->url->user)
		child_env[i++] = g_strdup_printf("URLUSER=%s", service->url->user);
	if (service->url->passwd)
		child_env[i++] = g_strdup_printf("URLPASSWD=%s", service->url->passwd);
	if (service->url->path)
		child_env[i++] = g_strdup_printf("URLPATH=%s", service->url->path);
	child_env[i] = NULL;

	/* Now do %h, %u, etc. substitution in cmd */
	buf = cmd_copy = g_strdup(cmd);

	full_cmd = g_strdup("");

	for (;;) {
		gchar *pc;
		gchar *tmp;
		gchar *var;
		gint len;

		pc = strchr(buf, '%');
	ignore:
		if (!pc) {
			tmp = g_strdup_printf("%s%s", full_cmd, buf);
			g_free(full_cmd);
			full_cmd = tmp;
			break;
		}

		len = pc - buf;

		var = NULL;

		switch (pc[1]) {
		case 'h':
			var = service->url->host;
			break;
		case 'u':
			var = service->url->user;
			break;
		}
		if (!var) {
			/* If there wasn't a valid %-code, with an actual
			   variable to insert, pretend we didn't see the % */
			pc = strchr(pc + 1, '%');
			goto ignore;
		}
		tmp = g_strdup_printf("%s%.*s%s", full_cmd, len, buf, var);
		g_free(full_cmd);
		full_cmd = tmp;
		buf = pc + 2;
	}

	g_free(cmd_copy);

	cmd_stream = camel_stream_process_new ();

	ret = camel_stream_process_connect (CAMEL_STREAM_PROCESS(cmd_stream),
					    full_cmd, (const gchar **)child_env);

	while (i)
		g_free(child_env[--i]);

	if (ret == -1) {
		if (errno == EINTR)
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				_("Connection cancelled"));
		else
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Could not connect with command \"%s\": %s"),
				full_cmd, g_strerror (errno));

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
	if (camel_imap_store_readline (store, &buf, error) < 0) {
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

	if (!strncmp(buf, "* PREAUTH", 9))
		store->preauthed = TRUE;
	g_free (buf);

	/* get the imap server capabilities */
	if (!imap_get_capability (service, error)) {
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

static struct {
	const gchar *value;
	const gchar *serv;
	gint fallback_port;
	gint mode;
} ssl_options[] = {
	{ "",              "imaps", IMAPS_PORT, MODE_SSL   },  /* really old (1.x) */
	{ "always",        "imaps", IMAPS_PORT, MODE_SSL   },
	{ "when-possible", "imap",  IMAP_PORT,  MODE_TLS   },
	{ "never",         "imap",  IMAP_PORT,  MODE_CLEAR },
	{ NULL,            "imap",  IMAP_PORT,  MODE_CLEAR },
};

static gboolean
connect_to_server_wrapper (CamelService *service, GError **error)
{
	const gchar *ssl_mode;
	gint mode, i;
	const gchar *serv;
	gint fallback_port;

#ifndef G_OS_WIN32
	const gchar *command;

	if (camel_url_get_param(service->url, "use_command")
	    && (command = camel_url_get_param(service->url, "command")))
		return connect_to_server_process(service, command, error);
#endif

	if ((ssl_mode = camel_url_get_param (service->url, "use_ssl"))) {
		for (i = 0; ssl_options[i].value; i++)
			if (!strcmp (ssl_options[i].value, ssl_mode))
				break;
		mode = ssl_options[i].mode;
		serv = (gchar *) ssl_options[i].serv;
		fallback_port = ssl_options[i].fallback_port;
	} else {
		mode = MODE_CLEAR;
		serv = (gchar *) "imap";
		fallback_port = IMAP_PORT;
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf ((gchar *)serv, "%d", service->url->port);
		fallback_port = 0;
	}

	return connect_to_server (service, service->url->host, serv, fallback_port, mode, error);
}

extern CamelServiceAuthType camel_imap_password_authtype;

static GList *
query_auth_types (CamelService *service, GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelServiceAuthType *authtype;
	GList *sasl_types, *t, *next;
	gboolean connected;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	connected = store->istream != NULL && store->connected;
	if (!connected)
		connected = connect_to_server_wrapper (service, error);
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
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

/* folder_name is path name */
static CamelFolderInfo *
imap_build_folder_info(CamelImapStore *imap_store, const gchar *folder_name)
{
	CamelURL *url;
	const gchar *name;
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup(folder_name);
	fi->unread = -1;
	fi->total = -1;

	url = camel_url_new (imap_store->base_url, NULL);
	g_free (url->path);
	url->path = g_strdup_printf ("/%s", folder_name);
	fi->uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free(url);
	name = strrchr (fi->full_name, '/');
	if (name == NULL)
		name = fi->full_name;
	else
		name++;
	if (!g_ascii_strcasecmp (fi->full_name, "INBOX"))
		fi->name = g_strdup (_("Inbox"));
	/* Do not localize the rest, these are from a server, thus shouldn't be localized */
	/*else if (!g_ascii_strcasecmp (fi->full_name, "Drafts"))
		fi->name = g_strdup (_("Drafts"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Sent"))
		fi->name = g_strdup (_("Sent"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Templates"))
		fi->name = g_strdup (_("Templates"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Trash"))
		fi->name = g_strdup (_("Trash"));*/
	else
		fi->name = g_strdup (name);

	return fi;
}

static gboolean
imap_folder_effectively_unsubscribed (CamelImapStore *imap_store,
                                      const gchar *folder_name,
                                      GError **error)
{
	CamelFolderInfo *fi;
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			si->flags &= ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
			camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}

	if (imap_store->renaming) {
		/* we don't need to emit a "folder_unsubscribed" signal
                   if we are in the process of renaming folders, so we
                   are done here... */
		return TRUE;

	}

	fi = imap_build_folder_info(imap_store, folder_name);
	camel_store_folder_unsubscribed (CAMEL_STORE (imap_store), fi);
	camel_folder_info_free (fi);

	return TRUE;
}

static void
imap_forget_folder (CamelImapStore *imap_store, const gchar *folder_name, GError **error)
{
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

	storage_path = g_strdup_printf ("%s/folders", imap_store->storage_path);
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

	camel_db_delete_folder (((CamelStore *)imap_store)->cdb_w, folder_name, NULL);
	camel_imap_message_cache_delete (folder_dir, NULL);

	state_file = g_strdup_printf("%s/subfolders", folder_dir);
	g_rmdir(state_file);
	g_free(state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

 event:

	camel_store_summary_remove_path((CamelStoreSummary *)imap_store->summary, folder_name);
	camel_store_summary_save((CamelStoreSummary *)imap_store->summary);

	fi = imap_build_folder_info(imap_store, folder_name);
	camel_store_folder_deleted (CAMEL_STORE (imap_store), fi);
	camel_folder_info_free (fi);
}

static gboolean
imap_check_folder_still_extant (CamelImapStore *imap_store, const gchar *full_name,
				GError **error)
{
	CamelImapResponse *response;

	response = camel_imap_command (imap_store, NULL, NULL, "LIST \"\" %F",
				       full_name);

	if (response) {
		gboolean stillthere = response->untagged->len != 0;

		camel_imap_response_free_without_processing (imap_store, response);

		return stillthere;
	}

	/* if the command was rejected, there must be some other error,
	   assume it worked so we dont blow away the folder unecessarily */
	return TRUE;
}

static gboolean
try_auth (CamelImapStore *store, const gchar *mech, GError **error)
{
	CamelSasl *sasl;
	CamelImapResponse *response;
	gchar *resp;
	gchar *sasl_resp;

	response = camel_imap_command (store, NULL, error, "AUTHENTICATE %s", mech);
	if (!response)
		return FALSE;

	sasl = camel_sasl_new ("imap", mech, CAMEL_SERVICE (store));
	while (!camel_sasl_get_authenticated (sasl)) {
		resp = camel_imap_response_extract_continuation (store, response, error);
		if (!resp)
			goto lose;

		sasl_resp = camel_sasl_challenge_base64 (sasl, imap_next_word (resp), error);
		g_free (resp);
		if (!sasl_resp)
			goto break_and_lose;

		response = camel_imap_command_continuation (store, sasl_resp, strlen (sasl_resp), error);
		g_free (sasl_resp);
		if (!response)
			goto lose;
	}

	resp = camel_imap_response_extract_continuation (store, response, NULL);
	if (resp) {
		/* Oops. SASL claims we're done, but the IMAP server
		 * doesn't think so...
		 */
		g_free (resp);
		goto lose;
	}

	g_object_unref (sasl);

	return TRUE;

 break_and_lose:
	/* Get the server out of "waiting for continuation data" mode. */
	response = camel_imap_command_continuation (store, "*", 1, NULL);
	if (response)
		camel_imap_response_free (store, response);

 lose:
	g_object_unref (sasl);

	return FALSE;
}

static gboolean
imap_auth_loop (CamelService *service, GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelSession *session = camel_service_get_session (service);
	CamelServiceAuthType *authtype = NULL;
	CamelImapResponse *response;
	gchar *errbuf = NULL;
	gboolean authenticated = FALSE;
	const gchar *auth_domain;
	guint32 prompt_flags = CAMEL_SESSION_PASSWORD_SECRET;

	auth_domain = camel_url_get_param (service->url, "auth-domain");

	if (store->preauthed) {
		if (camel_verbose_debug)
			fprintf(stderr, "Server %s has preauthenticated us.\n",
				service->url->host);
		return TRUE;
	}

	if (service->url->authmech) {
		if (!g_hash_table_lookup (store->authtypes, service->url->authmech)) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("IMAP server %s does not support requested "
				  "authentication type %s"),
				service->url->host,
				service->url->authmech);
			return FALSE;
		}

		authtype = camel_sasl_authtype (service->url->authmech);
		if (!authtype) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("No support for authentication type %s"),
				service->url->authmech);
			return FALSE;
		}

		if (!authtype->need_password) {
			authenticated = try_auth (store, authtype->authproto, error);
			if (!authenticated)
				return FALSE;
		}
	}

	while (!authenticated) {
		GError *local_error = NULL;

		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			prompt_flags |= CAMEL_SESSION_PASSWORD_REPROMPT;
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}

		if (!service->url->passwd) {
			gchar *base_prompt;
			gchar *full_prompt;

			base_prompt = camel_session_build_password_prompt (
				"IMAP", service->url->user, service->url->host);

			if (errbuf != NULL)
				full_prompt = g_strconcat (errbuf, base_prompt, NULL);
			else
				full_prompt = g_strdup (base_prompt);

			service->url->passwd = camel_session_get_password (
				session, service, auth_domain, full_prompt,
				"password", prompt_flags, error);

			g_free (base_prompt);
			g_free (full_prompt);
			g_free (errbuf);
			errbuf = NULL;

			if (!service->url->passwd) {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_NEED_PASSWORD,
					_("Need password for authentication"));
				return FALSE;
			}
		}

		if (!store->connected) {
			/* Some servers (eg, courier) will disconnect on
			 * a bad password. So reconnect here. */
			if (!connect_to_server_wrapper (service, error))
				return FALSE;
		}

		if (authtype)
			authenticated = try_auth (store, authtype->authproto, &local_error);
		else {
			response = camel_imap_command (store, NULL, &local_error,
						       "LOGIN %S %S",
						       service->url->user,
						       service->url->passwd);
			if (response) {
				camel_imap_response_free (store, response);
				authenticated = TRUE;
			}
		}
		if (local_error != NULL) {
			if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
			    g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE)) {
				g_propagate_error (error, local_error);
				return FALSE;
			}

			errbuf = g_markup_printf_escaped (
				_("Unable to authenticate to IMAP server.\n%s\n\n"),
				local_error->message);
			g_clear_error (&local_error);
		}
	}

	return TRUE;
}

static gboolean
imap_connect (CamelService *service, GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);
	CamelImapResponse *response;
	gchar *result, *name;
	gsize len;
	GError *local_error = NULL;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	if (!connect_to_server_wrapper (service, error) ||
	    !imap_auth_loop (service, error)) {
		camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		camel_service_disconnect (service, TRUE, NULL);
		return FALSE;
	}

	/* Get namespace and hierarchy separator */
	if (store->capabilities & IMAP_CAPABILITY_NAMESPACE) {
		struct _namespaces *namespaces;

		response = camel_imap_command (store, NULL, &local_error, "NAMESPACE");
		if (!response)
			goto done;

		result = camel_imap_response_extract (store, response, "NAMESPACE", &local_error);
		if (!result)
			goto done;

		namespaces = imap_parse_namespace_response (result);

		if (!(store->parameters & IMAP_PARAM_OVERRIDE_NAMESPACE)) {
			g_free (store->users_namespace);
			store->users_namespace = NULL;
		}

		if (namespaces && !store->users_namespace) {
			struct _namespace *np = NULL;

			if (namespaces->personal)
				np = namespaces->personal;
			else if (namespaces->other)
				np = namespaces->other;
			else if (namespaces->shared)
				np = namespaces->shared;

			if (np) {
				store->users_namespace = g_strdup (np->prefix);
			}
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

		if (!store->users_namespace) {
			/* fallback for a broken result */
			name = camel_strstrcase (result, "NAMESPACE ((");
			if (name) {
				gchar *sep;

				name += 12;
				store->users_namespace = imap_parse_string ((const gchar **) &name, &len);
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

	if (!store->users_namespace)
		store->users_namespace = g_strdup ("");

	if (!store->dir_sep) {
		const gchar *use_namespace = store->summary->namespace ? store->summary->namespace->full_name : NULL;

		if (!use_namespace)
			use_namespace = store->users_namespace;

		if (store->server_level >= IMAP_LEVEL_IMAP4REV1) {
			/* This idiom means "tell me the hierarchy separator
			 * for the given path, even if that path doesn't exist.
			 */
			response = camel_imap_command (store, NULL, &local_error,
						       "LIST %G \"\"",
						       use_namespace);
		} else {
			/* Plain IMAP4 doesn't have that idiom, so we fall back
			 * to "tell me about this folder", which will fail if
			 * the folder doesn't exist (eg, if namespace is "").
			 */
			response = camel_imap_command (store, NULL, &local_error,
						       "LIST \"\" %G",
						       use_namespace);
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
	len = strlen (store->users_namespace);
	if (len && store->users_namespace[len - 1] == store->dir_sep)
		store->users_namespace[len - 1] = 0;

	camel_imap_store_summary_namespace_set_main (store->summary, store->users_namespace, store->dir_sep);

	if ((store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
	    && camel_store_summary_count((CamelStoreSummary *)store->summary) == 0) {
		CamelStoreInfo *si;

		/* look in all namespaces */
		if (!get_folders_sync (store, NULL, &local_error))
			goto done;

		/* Make sure INBOX is present/subscribed */
		si = camel_store_summary_path((CamelStoreSummary *)store->summary, "INBOX");
		if (si == NULL || (si->flags & CAMEL_FOLDER_SUBSCRIBED) == 0) {
			response = camel_imap_command (store, NULL, &local_error, "SUBSCRIBE INBOX");
			if (response != NULL) {
				camel_imap_response_free (store, response);
			}
			if (si)
				camel_store_summary_info_free((CamelStoreSummary *)store->summary, si);
			if (local_error != NULL)
				goto done;
			get_folders_sync(store, "INBOX", &local_error);
		}

		store->refresh_stamp = time(NULL);
	}

done:
	/* save any changes we had */
	camel_store_summary_save((CamelStoreSummary *)store->summary);

	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (local_error != NULL) {
		camel_service_disconnect (service, TRUE, NULL);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
imap_disconnect (CamelService *service, gboolean clean, GError **error)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (service);

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL && clean) {
		CamelImapResponse *response;

		response = camel_imap_command (store, NULL, NULL, "LOGOUT");
		camel_imap_response_free (store, response);
	}

	if (store->istream) {
		camel_stream_close(store->istream, NULL);
		g_object_unref (store->istream);
		store->istream = NULL;
	}

	if (store->ostream) {
		camel_stream_close(store->ostream, NULL);
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

	if (store->users_namespace && !(store->parameters & IMAP_PARAM_OVERRIDE_NAMESPACE)) {
		g_free (store->users_namespace);
		store->users_namespace = NULL;
	}

	return TRUE;
}

static gboolean
imap_summary_is_dirty (CamelFolderSummary *summary)
{
	CamelImapMessageInfo *info;
	gint max, i;
	gint found = FALSE;

	max = camel_folder_summary_count (summary);
	for (i = 0; i < max && !found; i++) {
		info = (CamelImapMessageInfo *)camel_folder_summary_index (summary, i);
		if (info) {
			found = info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED;
			camel_message_info_free(info);
		}
	}

	return FALSE;
}

static gboolean
imap_noop (CamelStore *store,
           GError **error)
{
	CamelImapStore *imap_store = (CamelImapStore *) store;
	CamelImapResponse *response;
	CamelFolder *current_folder;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected(imap_store, error)) {
		success = FALSE;
		goto done;
	}

	current_folder = imap_store->current_folder;
	if (current_folder && imap_summary_is_dirty (current_folder->summary)) {
		/* let's sync the flags instead.  NB: must avoid folder lock */
		success = CAMEL_FOLDER_GET_CLASS (current_folder)->sync (current_folder, FALSE, error);
	} else {
		response = camel_imap_command (imap_store, NULL, error, "NOOP");
		if (response)
			camel_imap_response_free (imap_store, response);
		else
			success = FALSE;
	}
done:
	camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static CamelFolder *
imap_get_trash(CamelStore *store, GError **error)
{
	CamelFolder *folder = NULL;
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);

	if (imap_store->real_trash_path && *imap_store->real_trash_path) {
		folder = camel_store_get_folder (store, imap_store->real_trash_path, 0, NULL);
		if (!folder) {
			/* cannot find configured folder, just report on console and unset in a store structure to not try again */
			g_free (imap_store->real_trash_path);
			imap_store->real_trash_path = NULL;
		}
	}

	if (folder)
		return folder;

	folder = CAMEL_STORE_CLASS(camel_imap_store_parent_class)->get_trash(store, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state = g_build_filename(((CamelImapStore *)store)->storage_path, "system", "Trash.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free(state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
imap_get_junk(CamelStore *store, GError **error)
{
	CamelFolder *folder = NULL;
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);

	if (imap_store->real_junk_path && *imap_store->real_junk_path) {
		folder = camel_store_get_folder (store, imap_store->real_junk_path, 0, NULL);
		if (!folder) {
			/* cannot find configured folder, just report on console and unset in a store structure to not try again */
			g_free (imap_store->real_junk_path);
			imap_store->real_junk_path = NULL;
		}
	}

	if (folder)
		return folder;

	folder = CAMEL_STORE_CLASS(camel_imap_store_parent_class)->get_junk(store, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state = g_build_filename(((CamelImapStore *)store)->storage_path, "system", "Junk.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free(state);
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

static gint
compare_folder_name (gconstpointer a, gconstpointer b)
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
get_folder_status (CamelImapStore *imap_store, const gchar *folder_name, const gchar *type)
{
	struct imap_status_item *items, *item, *tail;
	CamelImapResponse *response;
	gchar *status, *name, *p;

	/* FIXME: we assume the server is STATUS-capable */

	response = camel_imap_command (imap_store, NULL, NULL,
				       "STATUS %F (%s)",
				       folder_name,
				       type);

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
get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	CamelFolder *new_folder;
	gchar *folder_dir, *storage_path;
	GError *local_error = NULL;

	/* Try to get it locally first, if it is, then the client will
	   force a select when necessary */
	new_folder = get_folder_offline(store, folder_name, flags, &local_error);
	if (new_folder)
		return new_folder;

	g_clear_error (&local_error);

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	camel_service_lock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected(imap_store, error)) {
		camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return NULL;
	}

	if (!g_ascii_strcasecmp (folder_name, "INBOX"))
		folder_name = "INBOX";

	if (imap_store->current_folder) {
		g_object_unref (imap_store->current_folder);
		imap_store->current_folder = NULL;
	}

	response = camel_imap_command (imap_store, NULL, &local_error, "SELECT %F", folder_name);
	if (!response) {
		gchar *folder_real, *parent_name, *parent_real;
		const gchar *c;

		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			g_propagate_error (error, local_error);
			return NULL;
		}

		g_clear_error (&local_error);

		if (!(flags & CAMEL_STORE_FOLDER_CREATE)) {
			camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_NO_FOLDER,
				_("No such folder %s"), folder_name);
			return NULL;
		}

		parent_name = strrchr(folder_name, '/');
		c = parent_name ? parent_name+1 : folder_name;
		while (*c && *c != imap_store->dir_sep && !strchr ("#%*", *c))
			c++;

		if (*c != '\0') {
			camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
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

			if (!(response = camel_imap_command (imap_store, NULL, error, "LIST \"\" %G", parent_real))) {
				camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
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
					camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				}

				/* delete the old parent and recreate it */
				if (!delete_folder (store, parent_name, error)) {
					camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				}

				/* add the dirsep to the end of parent_name */
				name = g_strdup_printf ("%s%c", parent_real, imap_store->dir_sep);
				response = camel_imap_command (imap_store, NULL, error, "CREATE %G",
							       name);
				g_free (name);

				if (!response) {
					camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
					g_free (parent_name);
					g_free (parent_real);
					return NULL;
				} else
					camel_imap_response_free (imap_store, response);
			}

			g_free (parent_real);
		}

		g_free (parent_name);

		folder_real = camel_imap_store_summary_path_to_full(imap_store->summary, folder_name, imap_store->dir_sep);
		response = camel_imap_command (imap_store, NULL, error, "CREATE %G", folder_real);
		if (response) {
			camel_imap_store_summary_add_from_full(imap_store->summary, folder_real, imap_store->dir_sep);

			camel_imap_response_free (imap_store, response);

			response = camel_imap_command (imap_store, NULL, NULL, "SELECT %F", folder_name);
		}
		g_free(folder_real);
		if (!response) {
			camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			return NULL;
		}
	} else if (flags & CAMEL_STORE_FOLDER_EXCL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder '%s': folder exists."),
			folder_name);

		camel_imap_response_free_without_processing (imap_store, response);

		camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

		return NULL;
	}

	storage_path = g_strdup_printf("%s/folders", imap_store->storage_path);
	folder_dir = imap_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	new_folder = camel_imap_folder_new (store, folder_name, folder_dir, error);
	g_free (folder_dir);
	if (new_folder) {
		imap_store->current_folder = g_object_ref (new_folder);
		if (!camel_imap_folder_selected (new_folder, response, error)) {

			g_object_unref (imap_store->current_folder);
			imap_store->current_folder = NULL;
			g_object_unref (new_folder);
			new_folder = NULL;
		}
	}
	camel_imap_response_free_without_processing (imap_store, response);

	camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return new_folder;
}

static CamelFolder *
get_folder_offline (CamelStore *store, const gchar *folder_name,
		    guint32 flags, GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelFolder *new_folder = NULL;
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
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

		storage_path = g_strdup_printf("%s/folders", imap_store->storage_path);
		folder_dir = imap_path_to_physical (storage_path, folder_name);
		g_free(storage_path);
		new_folder = camel_imap_folder_new (store, folder_name, folder_dir, error);
		g_free(folder_dir);

		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	} else {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder %s"), folder_name);
	}

	return new_folder;
}

static gboolean
delete_folder (CamelStore *store,
               const gchar *folder_name,
               GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected(imap_store, error)) {
		success = FALSE;
		goto fail;
	}

	/* make sure this folder isn't currently SELECTed */
	response = camel_imap_command (imap_store, NULL, error, "SELECT INBOX");
	if (!response) {
		success = FALSE;
		goto fail;
	}

	camel_imap_response_free_without_processing (imap_store, response);
	if (imap_store->current_folder)
		g_object_unref (imap_store->current_folder);
	/* no need to actually create a CamelFolder for INBOX */
	imap_store->current_folder = NULL;

	response = camel_imap_command(imap_store, NULL, error, "DELETE %F", folder_name);
	if (response) {
		camel_imap_response_free (imap_store, response);
		imap_forget_folder (imap_store, folder_name, NULL);
	} else
		success = FALSE;

fail:
	camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static void
manage_subscriptions (CamelStore *store, const gchar *old_name, gboolean subscribe)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelStoreInfo *si;
	gint olen = strlen(old_name);
	const gchar *path;
	gint i, count;

	count = camel_store_summary_count((CamelStoreSummary *)imap_store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		if (si) {
			path = camel_store_info_path(imap_store->summary, si);
			if (strncmp(path, old_name, olen) == 0) {
				if (subscribe)
					subscribe_folder(store, path, NULL);
				else
					unsubscribe_folder(store, path, NULL);
			}
			camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
		}
	}
}

static void
rename_folder_info (CamelImapStore *imap_store, const gchar *old_name, const gchar *new_name)
{
	gint i, count;
	CamelStoreInfo *si;
	gint olen = strlen(old_name);
	const gchar *path;
	gchar *npath, *nfull;

	count = camel_store_summary_count((CamelStoreSummary *)imap_store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		if (si == NULL)
			continue;
		path = camel_store_info_path(imap_store->summary, si);
		if (strncmp(path, old_name, olen) == 0) {
			if (strlen(path) > olen)
				npath = g_strdup_printf("%s/%s", new_name, path+olen+1);
			else
				npath = g_strdup(new_name);
			nfull = camel_imap_store_summary_path_to_full(imap_store->summary, npath, imap_store->dir_sep);

			/* workaround for broken server (courier uses '.') that doesn't rename
			   subordinate folders as required by rfc 2060 */
			if (imap_store->dir_sep == '.') {
				CamelImapResponse *response;

				response = camel_imap_command (imap_store, NULL, NULL, "RENAME %F %G", path, nfull);
				if (response)
					camel_imap_response_free (imap_store, response);
			}

			camel_store_info_set_string((CamelStoreSummary *)imap_store->summary, si, CAMEL_STORE_INFO_PATH, npath);
			camel_store_info_set_string((CamelStoreSummary *)imap_store->summary, si, CAMEL_IMAP_STORE_INFO_FULL_NAME, nfull);

			camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
			g_free(nfull);
			g_free(npath);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}
}

static gboolean
rename_folder (CamelStore *store,
               const gchar *old_name,
               const gchar *new_name_in,
               GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	gchar *oldpath, *newpath, *storage_path;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected(imap_store, error)) {
		success = FALSE;
		goto fail;
	}

	/* make sure this folder isn't currently SELECTed - it's
           actually possible to rename INBOX but if you do another
           INBOX will immediately be created by the server */
	response = camel_imap_command (imap_store, NULL, error, "SELECT INBOX");
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
	if (imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
		manage_subscriptions(store, old_name, FALSE);

	response = camel_imap_command (imap_store, NULL, error, "RENAME %F %F", old_name, new_name_in);
	if (!response) {
		if (imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
			manage_subscriptions(store, old_name, TRUE);
		success = FALSE;
		goto fail;
	}

	camel_imap_response_free (imap_store, response);

	/* rename summary, and handle broken server */
	rename_folder_info(imap_store, old_name, new_name_in);

	if (imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS)
		manage_subscriptions(store, new_name_in, TRUE);

	storage_path = g_strdup_printf("%s/folders", imap_store->storage_path);
	oldpath = imap_path_to_physical (storage_path, old_name);
	newpath = imap_path_to_physical (storage_path, new_name_in);

	/* So do we care if this didn't work?  Its just a cache? */
	if (g_rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
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
	camel_service_unlock (CAMEL_SERVICE (imap_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static CamelFolderInfo *
create_folder (CamelStore *store, const gchar *parent_name,
	       const gchar *folder_name, GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	gchar *full_name, *resp, *thisone, *parent_real, *real_name;
	CamelImapResponse *response;
	CamelFolderInfo *root = NULL;
	gboolean need_convert;
	gint i = 0, flags;
	const gchar *c;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
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
	parent_real = camel_imap_store_summary_full_from_path(imap_store->summary, parent_name);
	if (parent_real == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Unknown parent folder: %s"), parent_name);
		return NULL;
	}

	need_convert = FALSE;
	response = camel_imap_command (imap_store, NULL, error, "LIST \"\" %G",
				       parent_real);
	if (!response) /* whoa, this is bad */ {
		g_free(parent_real);
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

		g_free(thisone);
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
			g_free(parent_real);
			return NULL;
		}

		/* delete the old parent and recreate it */
		if (!delete_folder (store, parent_name, error))
			return NULL;

		/* add the dirsep to the end of parent_name */
		name = g_strdup_printf ("%s%c", parent_real, imap_store->dir_sep);
		response = camel_imap_command (imap_store, NULL, error, "CREATE %G",
					       name);
		g_free (name);

		if (!response) {
			g_free(parent_real);
			return NULL;
		} else
			camel_imap_response_free (imap_store, response);

		root = imap_build_folder_info(imap_store, parent_name);
	}

	/* ok now we can create the folder */
	real_name = camel_imap_store_summary_path_to_full(imap_store->summary, folder_name, imap_store->dir_sep);
	full_name = imap_concat (imap_store, parent_real, real_name);
	g_free(real_name);
	response = camel_imap_command (imap_store, NULL, error, "CREATE %G", full_name);

	if (response) {
		CamelImapStoreInfo *si;
		CamelFolderInfo *fi;

		camel_imap_response_free (imap_store, response);

		si = camel_imap_store_summary_add_from_full(imap_store->summary, full_name, imap_store->dir_sep);
		camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		fi = imap_build_folder_info(imap_store, camel_store_info_path(imap_store->summary, si));
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
		camel_folder_info_free(root);
		root = NULL;
	}

	g_free (full_name);
	g_free(parent_real);

	return root;
}

static CamelFolderInfo *
parse_list_response_as_folder_info (CamelImapStore *imap_store,
				    const gchar *response)
{
	CamelFolderInfo *fi;
	gint flags;
	gchar sep, *dir, *path;
	CamelURL *url;
	CamelImapStoreInfo *si;
	guint32 newflags;

	if (!imap_parse_list_response (imap_store, response, &flags, &sep, &dir))
		return NULL;

	/* FIXME: should use imap_build_folder_info, note the differences with param setting tho */

	si = camel_imap_store_summary_add_from_full(imap_store->summary, dir, sep?sep:'/');
	g_free(dir);
	if (si == NULL)
		return NULL;

	newflags = (si->info.flags & (CAMEL_STORE_INFO_FOLDER_SUBSCRIBED | CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW)) | (flags & ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED);
	if (si->info.flags != newflags) {
		si->info.flags = newflags;
		camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
	}

	flags = (flags & ~CAMEL_FOLDER_SUBSCRIBED) | (si->info.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED);

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup(camel_store_info_path(imap_store->summary, si));
	if (!g_ascii_strcasecmp(fi->full_name, "inbox")) {
		flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
		fi->name = g_strdup (_("Inbox"));
	} else
		fi->name = g_strdup(camel_store_info_name(imap_store->summary, si));

	/* HACK: some servers report noinferiors for all folders (uw-imapd)
	   We just translate this into nochildren, and let the imap layer enforce
	   it.  See create folder */
	if (flags & CAMEL_FOLDER_NOINFERIORS)
		flags = (flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;
	fi->flags = flags;

	url = camel_url_new (imap_store->base_url, NULL);
	path = alloca(strlen(fi->full_name)+2);
	sprintf(path, "/%s", fi->full_name);
	camel_url_set_path(url, path);

	if ((flags & CAMEL_FOLDER_NOSELECT) != 0 || fi->name[0] == 0)
		camel_url_set_param (url, "noselect", "yes");
	else
		camel_url_set_param (url, "noselect", NULL);
	fi->uri = camel_url_to_string (url, 0);
	camel_url_free (url);

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
static guint folder_hash(gconstpointer ap)
{
	const gchar *a = ap;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		a = "INBOX";

	return g_str_hash(a);
}

static gint folder_eq(gconstpointer ap, gconstpointer bp)
{
	const gchar *a = ap;
	const gchar *b = bp;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		a = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		b = "INBOX";

	return g_str_equal(a, b);
}

static void
get_folders_free(gpointer k, gpointer v, gpointer d)
{
	camel_folder_info_free(v);
}

static gboolean
get_folders_sync (CamelImapStore *imap_store, const gchar *ppattern, GError **error)
{
	CamelImapResponse *response;
	CamelFolderInfo *fi, *hfi;
	gchar *list;
	gint i, count, j, k;
	GHashTable *present;
	CamelStoreInfo *si;
	const gchar *pattern = ppattern;
	CamelImapStoreNamespace *ns;
	gboolean success = TRUE;

	/* We do a LIST followed by LSUB, and merge the results.  LSUB may not be a strict
	   subset of LIST for some servers, so we can't use either or separately */
	present = g_hash_table_new(folder_hash, folder_eq);

	if (!pattern)
		pattern = "";

	for (ns = imap_store->summary->namespace; ns; ns = ns->next) {
		for (k = 0; k < 2; k++) {
			gchar *tmp = NULL;

			if (!ppattern) {
				if (!ns->full_name || !*ns->full_name) {
					tmp = g_strdup ("*");
					if (k == 1)
						break;
				} else if (k == 0)
					tmp = g_strdup_printf ("%s%c", ns->full_name, ns->sep);
				else
					tmp = g_strdup_printf ("%s%c*", ns->full_name, ns->sep);
				pattern = tmp;
			}

			for (j = 0; j < 2; j++) {
				response = camel_imap_command (imap_store, NULL, error,
								"%s \"\" %G", j==1 ? "LSUB" : "LIST",
								pattern);
				if (!response) {
					success = FALSE;
					goto fail;
				}

				for (i = 0; i < response->untagged->len; i++) {
					list = response->untagged->pdata[i];
					fi = parse_list_response_as_folder_info (imap_store, list);
					if (fi && *fi->full_name) {
						hfi = g_hash_table_lookup(present, fi->full_name);
						if (hfi == NULL) {
							if (j == 1) {
								fi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
								if ((fi->flags & (CAMEL_IMAP_FOLDER_MARKED | CAMEL_IMAP_FOLDER_UNMARKED)))
									imap_store->capabilities |= IMAP_CAPABILITY_useful_lsub;
							}
							g_hash_table_insert(present, fi->full_name, fi);
						} else {
							if (j == 1)
								hfi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
							camel_folder_info_free(fi);
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

	/* Sync summary to match */

	/* FIXME: we need to emit folder_create/subscribed/etc events for any new folders */
	count = camel_store_summary_count((CamelStoreSummary *)imap_store->summary);

	for (i=0;i<count;i++) {
		const gchar *full_name;

		si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		if (si == NULL)
			continue;

		full_name = camel_imap_store_info_full_name (imap_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *)imap_store->summary, si);
			continue;
		}

		if (!ppattern || imap_match_pattern (camel_imap_store_summary_namespace_find_full (imap_store->summary, full_name), pattern, full_name)) {
			if ((fi = g_hash_table_lookup(present, camel_store_info_path(imap_store->summary, si))) != NULL) {
				if (((fi->flags ^ si->flags) & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)) {
					si->flags = (si->flags & ~CAMEL_FOLDER_SUBSCRIBED) | (fi->flags & CAMEL_FOLDER_SUBSCRIBED);
					camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);

					camel_store_folder_created (CAMEL_STORE (imap_store), fi);
					camel_store_folder_subscribed (CAMEL_STORE (imap_store), fi);
				}
			} else {
				gchar *dup_folder_name = g_strdup (camel_store_info_path (imap_store->summary, si));

				if (dup_folder_name) {
					imap_folder_effectively_unsubscribed (imap_store, dup_folder_name, NULL);
					imap_forget_folder (imap_store, dup_folder_name, NULL);

					g_free (dup_folder_name);
				} else {
					camel_store_summary_remove ((CamelStoreSummary *)imap_store->summary, si);
				}

				count--;
				i--;
			}
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}
fail:
	g_hash_table_foreach(present, get_folders_free, NULL);
	g_hash_table_destroy(present);

	return success;
}

#if 0
static void
dumpfi(CamelFolderInfo *fi)
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
		printf("%-25s %-25s %*s\n", fi->name, fi->full_name, (gint)(depth*2+strlen(fi->uri)), fi->uri);
		if (fi->child)
			dumpfi(fi->child);
		fi = fi->next;
	}
}
#endif

static void
fill_fi(CamelStore *store, CamelFolderInfo *fi, guint32 flags)
{
	CamelFolder *folder;

	folder = camel_object_bag_peek(store->folders, fi->full_name);
	if (folder) {
		CamelImapSummary *ims;

		if (folder->summary)
			ims = (CamelImapSummary *) folder->summary;
		else
			ims = (CamelImapSummary *) camel_imap_summary_new (folder, NULL);

		fi->unread = ((CamelFolderSummary *)ims)->unread_count;
		fi->total = ((CamelFolderSummary *)ims)->saved_count;

		if (!folder->summary)
			g_object_unref (ims);
		g_object_unref (folder);
	}
}

struct _refresh_msg {
	CamelSessionThreadMsg msg;

	CamelStore *store;
	GError *error;
};

static void
refresh_refresh(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _refresh_msg *m = (struct _refresh_msg *)msg;
	CamelImapStore *store = (CamelImapStore *)m->store;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected (store, &m->error))
		goto done;

	if (store->users_namespace && store->users_namespace[0]) {
		if (!get_folders_sync (store, "INBOX", &m->error))
			goto done;
	} else {
		get_folders_sync (store, "*", NULL);
	}

	/* look in all namespaces */
	get_folders_sync (store, NULL, &m->error);
	camel_store_summary_save ((CamelStoreSummary *)store->summary);
done:
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
}

static void
refresh_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _refresh_msg *m = (struct _refresh_msg *)msg;

	g_object_unref (m->store);
	g_clear_error (&m->error);
}

static CamelSessionThreadOps refresh_ops = {
	refresh_refresh,
	refresh_free,
};

static CamelFolderInfo *
get_folder_info (CamelStore *store,
                 const gchar *top,
                 guint32 flags,
                 GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelFolderInfo *tree = NULL;

	/* If we have a list of folders already, use that, but if we haven't
	   updated for a while, then trigger an asynchronous rescan.  Otherwise
	   we update the list first, and then build it from that */

	if (top == NULL)
		top = "";

	if (camel_debug("imap:folder_info"))
		printf("get folder info online\n");

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		tree = get_folder_info_offline (store, top, flags, error);
		return tree;
	}

	if ((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
	    && camel_store_summary_count((CamelStoreSummary *)imap_store->summary) > 0) {
		time_t now;
		gint ref;

		now = time(NULL);
		ref = now > imap_store->refresh_stamp+60*60*1;
		if (ref) {
			camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			ref = now > imap_store->refresh_stamp+60*60*1;
			if (ref) {
				struct _refresh_msg *m;

				imap_store->refresh_stamp = now;

				m = camel_session_thread_msg_new(((CamelService *)store)->session, &refresh_ops, sizeof(*m));
				m->store = g_object_ref (store);
				m->error = NULL;
				camel_session_thread_queue(((CamelService *)store)->session, &m->msg, 0);
			}
			camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		}
	} else {
		gchar *pattern;
		gint i;
		CamelImapStoreNamespace *ns;

		camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

		if (!camel_imap_store_connected((CamelImapStore *)store, error))
			goto fail;

		if (top[0] == 0) {
			pattern = g_alloca (2);
			pattern[0] = '*';
			pattern[1] = 0;
			i = 0;
		} else {
			gchar *name;

			name = camel_imap_store_summary_full_from_path(imap_store->summary, top);
			if (name == NULL)
				name = camel_imap_store_summary_path_to_full(imap_store->summary, top, imap_store->dir_sep);

			i = strlen(name);
			pattern = g_alloca(i+5);
			strcpy(pattern, name);
			g_free(name);
		}

		ns = camel_imap_store_summary_get_main_namespace (imap_store->summary);
		if (!get_folders_sync (imap_store, pattern, error))
			goto fail;
		if (pattern[0] != '*' && ns) {
			pattern[i] = ns->sep;
			pattern[i+1] = (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)?'*':'%';
			pattern[i+2] = 0;
			get_folders_sync(imap_store, pattern, NULL);
		}
		camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	}

	tree = get_folder_info_offline(store, top, flags, error);
	return tree;

fail:
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	return NULL;
}

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store, const gchar *top,
			 guint32 flags, GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	gboolean include_inbox = FALSE;
	CamelFolderInfo *fi;
	GPtrArray *folders;
	gchar *pattern, *name;
	gint i;
	CamelImapStoreNamespace *main_ns, *ns;

	if (camel_debug("imap:folder_info"))
		printf("get folder info offline\n");

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	if (top == NULL || top[0] == '\0') {
		include_inbox = TRUE;
		top = "";
	}

	/* get starting point */
	if (top[0] == 0) {
		name = g_strdup("");
	} else {
		name = camel_imap_store_summary_full_from_path(imap_store->summary, top);
		if (name == NULL)
			name = camel_imap_store_summary_path_to_full(imap_store->summary, top, imap_store->dir_sep);
	}

	main_ns = camel_imap_store_summary_get_main_namespace (imap_store->summary);
	pattern = imap_concat(imap_store, name, "*");

	/* folder_info_build will insert parent nodes as necessary and mark
	 * them as noselect, which is information we actually don't have at
	 * the moment. So let it do the right thing by bailing out if it's
	 * not a folder we're explicitly interested in. */

	for (i=0;i<camel_store_summary_count((CamelStoreSummary *)imap_store->summary);i++) {
		CamelStoreInfo *si = camel_store_summary_index((CamelStoreSummary *)imap_store->summary, i);
		const gchar *full_name;

		if (si == NULL)
			continue;

		full_name = camel_imap_store_info_full_name (imap_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *)imap_store->summary, si);
			continue;
		}

		ns = camel_imap_store_summary_namespace_find_full (imap_store->summary, full_name);

		if ((g_str_equal (name, full_name)
		     || imap_match_pattern (ns, pattern, full_name)
		     || (include_inbox && !g_ascii_strcasecmp (full_name, "INBOX")))
		    && ((ns == main_ns &&
			((imap_store->parameters & IMAP_PARAM_SUBSCRIPTIONS) == 0
			   || (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) == 0))
			|| (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)
			|| (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0)) {

			fi = imap_build_folder_info(imap_store, camel_store_info_path((CamelStoreSummary *)imap_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			/* HACK: some servers report noinferiors for all folders (uw-imapd)
			   We just translate this into nochildren, and let the imap layer enforce
			   it.  See create folder */
			if (fi->flags & CAMEL_FOLDER_NOINFERIORS)
				fi->flags = (fi->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;

			/* blah, this gets lost somewhere, i can't be bothered finding out why */
			if (!g_ascii_strcasecmp(fi->full_name, "inbox"))
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;

			if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == 0 &&
			    imap_store->real_trash_path && *imap_store->real_trash_path &&
			    g_ascii_strcasecmp (fi->full_name, imap_store->real_trash_path) == 0) {
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_TRASH;
			}

			if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == 0 &&
			    imap_store->real_junk_path && *imap_store->real_junk_path &&
			    g_ascii_strcasecmp (fi->full_name, imap_store->real_junk_path) == 0) {
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_JUNK;
			}

			if (si->flags & CAMEL_FOLDER_NOSELECT) {
				CamelURL *url = camel_url_new(fi->uri, NULL);

				camel_url_set_param (url, "noselect", "yes");
				g_free(fi->uri);
				fi->uri = camel_url_to_string (url, 0);
				camel_url_free (url);
			} else {
				fill_fi((CamelStore *)imap_store, fi, 0);
			}
			if (!fi->child)
				fi->flags |= CAMEL_FOLDER_NOCHILDREN;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}
	g_free(pattern);

	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	g_free(name);

	return fi;
}

static gboolean
folder_is_subscribed (CamelStore *store,
                      const gchar *folder_name)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelStoreInfo *si;
	gint truth = FALSE;

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		truth = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}

	return truth;
}

/* Note: folder_name must match a folder as listed with get_folder_info() -> full_name */
static gboolean
subscribe_folder (CamelStore *store,
                  const gchar *folder_name,
                  GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	CamelFolderInfo *fi;
	CamelStoreInfo *si;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected (imap_store, error)) {
		success = FALSE;
		goto done;
	}

	response = camel_imap_command (imap_store, NULL, error,
				       "SUBSCRIBE %F", folder_name);
	if (!response) {
		success = FALSE;
		goto done;
	}

	camel_imap_response_free (imap_store, response);

	si = camel_store_summary_path((CamelStoreSummary *)imap_store->summary, folder_name);
	if (si) {
		if ((si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) == 0) {
			si->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch((CamelStoreSummary *)imap_store->summary);
			camel_store_summary_save((CamelStoreSummary *)imap_store->summary);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imap_store->summary, si);
	}

	if (imap_store->renaming) {
		/* we don't need to emit a "folder_subscribed" signal
                   if we are in the process of renaming folders, so we
                   are done here... */
		goto done;
	}

	fi = imap_build_folder_info(imap_store, folder_name);
	fi->flags |= CAMEL_FOLDER_NOCHILDREN;

	camel_store_folder_subscribed (store, fi);
	camel_folder_info_free (fi);
done:
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static gboolean
unsubscribe_folder (CamelStore *store,
                    const gchar *folder_name,
                    GError **error)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (store);
	CamelImapResponse *response;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_imap_store_connected (imap_store, error)) {
		success = FALSE;
		goto done;
	}

	response = camel_imap_command (imap_store, NULL, error,
				       "UNSUBSCRIBE %F", folder_name);
	if (!response) {
		success = FALSE;
		goto done;
	}

	camel_imap_response_free (imap_store, response);

	success = imap_folder_effectively_unsubscribed (imap_store, folder_name, error);

done:
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

#if 0
static gboolean
folder_flags_have_changed (CamelFolder *folder)
{
	CamelMessageInfo *info;
	gint i, max;

	max = camel_folder_summary_count (folder->summary);
	for (i = 0; i < max; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		if (!info)
			continue;
		if (info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) {
			return TRUE;
		}
	}

	return FALSE;
}
#endif

/* Use this whenever you need to ensure you're both connected and
   online. */
gboolean
camel_imap_store_connected (CamelImapStore *store, GError **error)
{
	/* This looks stupid ... because it is.

	   camel-service-connect will return OK if we connect in 'offline mode',
	   which isn't what we want at all.  So we have to recheck we actually
	   did connect anyway ... */

	if (store->istream != NULL
	    || (((CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL)
		&& camel_service_connect((CamelService *)store, NULL)
		&& store->istream != NULL)))
		return TRUE;

	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_UNAVAILABLE,
		_("You must be working online to complete this operation"));

	return FALSE;
}

gssize
camel_imap_store_readline (CamelImapStore *store, gchar **dest, GError **error)
{
	CamelStreamBuffer *stream;
	gchar linebuf[1024] = {0};
	GByteArray *ba;
	gssize nread;

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
	while ((nread = camel_stream_buffer_gets (stream, linebuf, sizeof (linebuf), error)) > 0) {
		g_byte_array_append (ba, (const guint8 *) linebuf, nread);
		if (linebuf[nread - 1] == '\n')
			break;
	}

	if (nread <= 0) {
		if (nread == 0)
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Server unexpectedly disconnected"));
		else
			g_prefix_error (
				error, _("Server unexpectedly disconnected: "));

		camel_service_disconnect (CAMEL_SERVICE (store), FALSE, NULL);
		g_byte_array_free (ba, TRUE);
		return -1;
	}

	if (camel_verbose_debug) {
		fprintf (stderr, "received: ");
		fwrite (ba->data, 1, ba->len, stderr);
	}

	/* camel-imap-command.c:imap_read_untagged expects the CRLFs
           to be stripped off and be nul-terminated *sigh* */
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
	gboolean res;
	GError *local_error = NULL;

	res = CAMEL_STORE_CLASS(camel_imap_store_parent_class)->can_refresh_folder (store, info, &local_error) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_all") != NULL) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_lsub") != NULL && (info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0);

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
