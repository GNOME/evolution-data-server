/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.c : class for a imap store */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-utils.h"
#include "camel-imapx-server.h"
#include "camel-imapx-summary.h"

/* Specified in RFC 2060 section 2.1 */
#define IMAP_PORT 143

#define FINFO_REFRESH_INTERVAL 60

G_DEFINE_TYPE (CamelIMAPXStore, camel_imapx_store, CAMEL_TYPE_OFFLINE_STORE)

static guint
imapx_name_hash(gconstpointer key)
{
	if (g_ascii_strcasecmp(key, "INBOX") == 0)
		return g_str_hash("INBOX");
	else
		return g_str_hash(key);
}

static gint
imapx_name_equal(gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal(aname, bname);
}

static void
imapx_parse_receiving_options (CamelIMAPXStore *istore, CamelURL *url)
{
	const gchar *val;

	if (camel_url_get_param (url, "use_lsub"))
		istore->rec_options |= IMAPX_SUBSCRIPTIONS;

	if (camel_url_get_param (url, "override_namespace") && camel_url_get_param (url, "namespace")) {
		istore->rec_options |= IMAPX_OVERRIDE_NAMESPACE;
		g_free(istore->namespace);
		istore->namespace = g_strdup (camel_url_get_param (url, "namespace"));
	}

	if (camel_url_get_param (url, "check_all"))
		istore->rec_options |= IMAPX_CHECK_ALL;

	if (camel_url_get_param (url, "check_lsub"))
		istore->rec_options |= IMAPX_CHECK_LSUB;

	if (camel_url_get_param (url, "filter")) {
		istore->rec_options |= IMAPX_FILTER_INBOX;
		((CamelStore *) istore)->flags |= CAMEL_STORE_FILTER_INBOX;
	}

	if (camel_url_get_param (url, "filter_junk"))
		istore->rec_options |= IMAPX_FILTER_JUNK;

	if (camel_url_get_param (url, "filter_junk_inbox"))
		istore->rec_options |= IMAPX_FILTER_JUNK_INBOX;

	if (camel_url_get_param (url, "use_idle"))
		istore->rec_options |= IMAPX_USE_IDLE;

	if (camel_url_get_param (url, "use_qresync"))
		istore->rec_options |= IMAPX_USE_QRESYNC;

	val = camel_url_get_param (url, "cachedconn");
	if (val) {
		guint n = strtod (val, NULL);
		camel_imapx_conn_manager_set_n_connections (istore->con_man, n);
	}
}

static void
imapx_store_finalize (GObject *object)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (object);

	/* force disconnect so we dont have it run later, after we've cleaned up some stuff */
	/* SIGH */

	camel_service_disconnect((CamelService *)imapx_store, TRUE, NULL);
	if (imapx_store->con_man) {
		g_object_unref (imapx_store->con_man);
		imapx_store->con_man = NULL;
	}
	g_mutex_free (imapx_store->get_finfo_lock);

	g_free (imapx_store->base_url);

	if (imapx_store->summary)
		g_object_unref (imapx_store->summary);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_store_parent_class)->finalize (object);
}

static gboolean
imapx_construct(CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, GError **error)
{
	gchar *summary;
	CamelIMAPXStore *store = (CamelIMAPXStore *)service;
	CamelServiceClass *service_class;

	service_class = CAMEL_SERVICE_CLASS (camel_imapx_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	store->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
								   CAMEL_URL_HIDE_PARAMS |
								   CAMEL_URL_HIDE_AUTH));
	imapx_parse_receiving_options (store, service->url);

	store->summary = camel_imapx_store_summary_new();
	store->storage_path = camel_session_get_storage_path(session, service, error);

	if (store->storage_path == NULL)
		return FALSE;

	summary = g_build_filename(store->storage_path, ".ev-store-summary", NULL);
	camel_store_summary_set_filename((CamelStoreSummary *)store->summary, summary);
	/* FIXME: need to remove params, passwords, etc */
	camel_store_summary_set_uri_base((CamelStoreSummary *)store->summary, service->url);
	camel_store_summary_load((CamelStoreSummary *)store->summary);

	g_free (summary);

	return TRUE;
}

extern CamelServiceAuthType camel_imapx_password_authtype;

static GList *
imapx_query_auth_types (CamelService *service, GError **error)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (service);
	CamelServiceAuthType *authtype;
	GList *sasl_types, *t, *next;
	gboolean connected;
	CamelIMAPXServer *server;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	server = camel_imapx_server_new((CamelStore *)istore, service->url);

	connected = server->stream != NULL;
	if (!connected)
		connected = imapx_connect_to_server (server, error);
	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
	if (!connected)
		return NULL;

	sasl_types = camel_sasl_authtype_list (FALSE);
	for (t = sasl_types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!g_hash_table_lookup (server->cinfo->auth_types, authtype->authproto)) {
			sasl_types = g_list_remove_link (sasl_types, t);
			g_list_free_1 (t);
		}
	}

	g_object_unref (server);

	return g_list_prepend (sasl_types, &camel_imapx_password_authtype);
}

static gchar *
imapx_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("IMAP server %s"), service->url->host);
	else
		return g_strdup_printf (_("IMAP service for %s on %s"),
					service->url->user, service->url->host);
}

CamelIMAPXServer *
camel_imapx_store_get_server (CamelIMAPXStore *istore, const gchar *folder_name, GError **error)
{
	CamelIMAPXServer *server = NULL;

	if (camel_operation_cancel_check(NULL)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			     _("Cancelled"));
		return NULL;
	}
	camel_service_lock (CAMEL_SERVICE (istore), CAMEL_SERVICE_REC_CONNECT_LOCK);

	server = camel_imapx_conn_manager_get_connection (istore->con_man, folder_name, error);

	camel_service_unlock (CAMEL_SERVICE (istore), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return server;
}

void
camel_imapx_store_op_done (CamelIMAPXStore *istore, CamelIMAPXServer *server, const gchar *folder_name)
{
	g_return_if_fail (server != NULL);

	camel_imapx_conn_manager_update_con_info (istore->con_man, server, folder_name);
}

static gboolean
imapx_connect (CamelService *service, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)service;
	CamelIMAPXServer *server;

	server = camel_imapx_store_get_server(istore, NULL, error);
	if (server) {
		g_object_unref(server);
		return TRUE;
	}

	return FALSE;
}

static gboolean
imapx_disconnect (CamelService *service, gboolean clean, GError **error)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (service);
	CamelServiceClass *service_class;

	service_class = CAMEL_SERVICE_CLASS (camel_imapx_store_parent_class);
	if (!service_class->disconnect (service, clean, error))
		return FALSE;

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (istore->con_man) {
		camel_imapx_conn_manager_close_connections (istore->con_man);
	}

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return TRUE;
}

static CamelFolder *
imapx_get_junk(CamelStore *store, GError **error)
{
	CamelFolder *folder;
	CamelStoreClass *store_class;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);
	folder = store_class->get_junk (store, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state = g_build_filename(((CamelIMAPXStore *)store)->storage_path, "system", "Junk.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free(state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
imapx_get_trash (CamelStore *store, GError **error)
{
	CamelFolder *folder;
	CamelStoreClass *store_class;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);
	folder = store_class->get_trash (store, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state = g_build_filename(((CamelIMAPXStore *)store)->storage_path, "system", "Trash.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free(state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static gboolean
imapx_noop (CamelStore *store, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	GSList *servers = NULL, *l;
	gboolean success = FALSE;

	if (CAMEL_OFFLINE_STORE(store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;

	servers = camel_imapx_conn_manager_get_connections (istore->con_man);

	for (l = servers; l != NULL; l = g_slist_next (l)) {
		CamelIMAPXServer *server = CAMEL_IMAPX_SERVER (l->data);

		/* we just return last noops value, technically not correct though */
		success = camel_imapx_server_noop (server, NULL, error);
		g_object_unref(server);
	}

	g_slist_free (servers);

	return success;
}

static guint
imapx_hash_folder_name (gconstpointer key)
{
	if (g_ascii_strcasecmp (key, "INBOX") == 0)
		return g_str_hash ("INBOX");
	else
		return g_str_hash (key);
}

static gint
imapx_compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal (aname, bname);
}

static CamelFolder *
get_folder_offline (CamelStore *store, const gchar *folder_name,
		    guint32 flags, GError **error)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (store);
	CamelFolder *new_folder = NULL;
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)imapx_store->summary, folder_name);
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

		storage_path = g_strdup_printf("%s/folders", imapx_store->storage_path);
		folder_dir = imapx_path_to_physical (storage_path, folder_name);
		g_free(storage_path);
		/* FIXME */
		new_folder = camel_imapx_folder_new (store, folder_dir, folder_name, error);

		g_free(folder_dir);
		camel_store_summary_info_free((CamelStoreSummary *)imapx_store->summary, si);
	} else {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder %s"), folder_name);
	}

	return new_folder;
}

static CamelFolder *
imapx_get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error)
{
	CamelFolder *folder;

	folder = get_folder_offline(store, folder_name, flags, NULL);
	if (folder == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	}

	return folder;
}

/* folder_name is path name */
static CamelFolderInfo *
imapx_build_folder_info (CamelIMAPXStore *imapx_store, const gchar *folder_name)
{
	CamelURL *url;
	const gchar *name;
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup(folder_name);
	fi->unread = -1;
	fi->total = -1;

	url = camel_url_new (imapx_store->base_url, NULL);
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

static void
fill_fi(CamelStore *store, CamelFolderInfo *fi, guint32 flags)
{
	CamelFolder *folder;
	CamelService *service = (CamelService *)store;
	
	folder = camel_object_bag_peek(store->folders, fi->full_name);
	if (folder) {
		CamelIMAPXSummary *ims;

		if (folder->summary)
			ims = (CamelIMAPXSummary *) folder->summary;
		else
			ims = (CamelIMAPXSummary *) camel_imapx_summary_new (folder, NULL);

		if (camel_url_get_param (service->url, "mobile"))
			fi->unread = ((CamelIMAPXFolder *) folder)->unread_on_server;
		else
			fi->unread = ((CamelFolderSummary *)ims)->unread_count;

		fi->total = ((CamelFolderSummary *)ims)->saved_count;

		if (!folder->summary)
			g_object_unref (ims);
		g_object_unref (folder);
	}
}

/* imap needs to treat inbox case insensitive */
/* we'll assume the names are normalized already */
static guint
folder_hash(gconstpointer ap)
{
	const gchar *a = ap;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		a = "INBOX";

	return g_str_hash(a);
}

static gint
folder_eq(gconstpointer ap, gconstpointer bp)
{
	const gchar *a = ap;
	const gchar *b = bp;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		a = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		b = "INBOX";

	return g_str_equal(a, b);
}

static gboolean
imapx_match_pattern(CamelIMAPXStoreNamespace *ns, const gchar *pattern, const gchar *name)
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

static void
imapx_unmark_folder_subscribed (CamelIMAPXStore *istore, const gchar *folder_name, gboolean emit_signal)
{
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)istore->summary, folder_name);
	if (si) {
		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			si->flags &= ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch((CamelStoreSummary *)istore->summary);
			camel_store_summary_save((CamelStoreSummary *)istore->summary);
		}
		camel_store_summary_info_free((CamelStoreSummary *)istore->summary, si);
	}

	if (emit_signal) {
		CamelFolderInfo *fi;

		fi = imapx_build_folder_info(istore, folder_name);
		camel_store_folder_unsubscribed (CAMEL_STORE (istore), fi);
		camel_folder_info_free (fi);
	}
}

static void
imapx_mark_folder_subscribed (CamelIMAPXStore *istore, const gchar *folder_name, gboolean emit_signal)
{
	CamelStoreInfo *si;

	si = camel_store_summary_path((CamelStoreSummary *)istore->summary, folder_name);
	if (si) {
		if ((si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) == 0) {
			si->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch((CamelStoreSummary *)istore->summary);
			camel_store_summary_save((CamelStoreSummary *)istore->summary);
		}
		camel_store_summary_info_free((CamelStoreSummary *)istore->summary, si);
	}

	if (emit_signal) {
		CamelFolderInfo *fi;

		fi = imapx_build_folder_info(istore, folder_name);
		camel_store_folder_subscribed (CAMEL_STORE (istore), fi);
		camel_folder_info_free (fi);
	}
}

static gboolean
imapx_subscribe_folder (CamelStore *store, const gchar *folder_name, gboolean emit_signal, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gboolean success;

	if (CAMEL_OFFLINE_STORE(store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;

	server = camel_imapx_store_get_server (istore, NULL, error);
	if (!server)
		return FALSE;

	success = camel_imapx_server_manage_subscription (server, folder_name, TRUE, error);
	g_object_unref(server);

	if (success)
		imapx_mark_folder_subscribed (istore, folder_name, emit_signal);

	return success;
}

static gboolean
imapx_unsubscribe_folder (CamelStore *store, const gchar *folder_name, gboolean emit_signal, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gboolean success;

	if (CAMEL_OFFLINE_STORE(store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return TRUE;

	server = camel_imapx_store_get_server (istore, NULL, error);
	if (!server)
		return FALSE;

	success = camel_imapx_server_manage_subscription (server, folder_name, FALSE, error);
	g_object_unref(server);

	if (success)
		imapx_unmark_folder_subscribed (istore, folder_name, emit_signal);

	return success;
}

static gboolean
imapx_store_subscribe_folder (CamelStore *store, const gchar *folder_name, GError **error)
{
	return imapx_subscribe_folder (store, folder_name, TRUE, error);
}

static gboolean
imapx_store_unsubscribe_folder (CamelStore *store, const gchar *folder_name, GError **error)
{
	return imapx_unsubscribe_folder (store, folder_name, TRUE, error);
}

static void
imapx_delete_folder_from_cache (CamelIMAPXStore *istore, const gchar *folder_name)
{
	gchar *state_file;
	gchar *folder_dir, *storage_path;
	CamelFolderInfo *fi;

	storage_path = g_strdup_printf ("%s/folders", istore->storage_path);
	folder_dir = imapx_path_to_physical (storage_path, folder_name);
	g_free (storage_path);
	if (g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		goto event;
	}

	/* Delete summary and all the data */
	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	g_unlink (state_file);
	g_free (state_file);

	camel_db_delete_folder (((CamelStore *)istore)->cdb_w, folder_name, NULL);
	g_rmdir (folder_dir);

	state_file = g_strdup_printf("%s/subfolders", folder_dir);
	g_rmdir(state_file);
	g_free(state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

 event:
	camel_store_summary_remove_path((CamelStoreSummary *)istore->summary, folder_name);
	camel_store_summary_save((CamelStoreSummary *)istore->summary);

	fi = imapx_build_folder_info(istore, folder_name);
	camel_store_folder_deleted (CAMEL_STORE (istore), fi);
	camel_folder_info_free (fi);
}

static gboolean
imapx_delete_folder (CamelStore *store, const gchar *folder_name, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gboolean success;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}
	/* Use INBOX connection as the implementation would try to select inbox to ensure
	   we are not selected on the folder being deleted */
	server = camel_imapx_store_get_server (istore, "INBOX", error);
	if (!server)
		return FALSE;

	success = camel_imapx_server_delete_folder (server, folder_name, error);
	g_object_unref(server);

	if (success)
		imapx_delete_folder_from_cache (istore, folder_name);

	return success;
}

static void
rename_folder_info (CamelIMAPXStore *istore, const gchar *old_name, const gchar *new_name)
{
	gint i, count;
	CamelStoreInfo *si;
	gint olen = strlen(old_name);
	const gchar *path;
	gchar *npath, *nfull;

	count = camel_store_summary_count((CamelStoreSummary *)istore->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index((CamelStoreSummary *)istore->summary, i);
		if (si == NULL)
			continue;
		path = camel_store_info_path(istore->summary, si);
		if (strncmp(path, old_name, olen) == 0) {
			if (strlen(path) > olen)
				npath = g_strdup_printf("%s/%s", new_name, path+olen+1);
			else
				npath = g_strdup(new_name);
			nfull = camel_imapx_store_summary_path_to_full(istore->summary, npath, istore->dir_sep);

			camel_store_info_set_string((CamelStoreSummary *)istore->summary, si, CAMEL_STORE_INFO_PATH, npath);
			camel_store_info_set_string((CamelStoreSummary *)istore->summary, si, CAMEL_IMAPX_STORE_INFO_FULL_NAME, nfull);

			camel_store_summary_touch((CamelStoreSummary *)istore->summary);
			g_free(nfull);
			g_free(npath);
		}
		camel_store_summary_info_free((CamelStoreSummary *)istore->summary, si);
	}
}

static gboolean
imapx_rename_folder (CamelStore *store, const gchar *old, const gchar *new, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gchar *oldpath, *newpath, *storage_path;
	gboolean success = FALSE;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (istore->rec_options & IMAPX_SUBSCRIPTIONS)
		imapx_unsubscribe_folder (store, old, FALSE, NULL);

	/* Use INBOX connection as the implementation would try to select inbox to ensure
	   we are not selected on the folder being renamed */
	server = camel_imapx_store_get_server(istore, "INBOX", error);
	if (server) {
		success = camel_imapx_server_rename_folder (server, old, new, error);
		g_object_unref(server);
	}

	if (!success) {
		imapx_subscribe_folder (store, old, FALSE, NULL);
		return FALSE;
	}

	/* rename summary, and handle broken server */
	rename_folder_info(istore, old, new);

	if (istore->rec_options & IMAPX_SUBSCRIPTIONS)
		success = imapx_subscribe_folder (store, new, FALSE, error);

	storage_path = g_strdup_printf("%s/folders", istore->storage_path);
	oldpath = imapx_path_to_physical (storage_path, old);
	newpath = imapx_path_to_physical (storage_path, new);
	g_free(storage_path);

	/* So do we care if this didn't work?  Its just a cache? */
	if (g_rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
			   oldpath, newpath, g_strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);

	return success;
}

static CamelFolderInfo *
imapx_create_folder (CamelStore *store, const gchar *parent_name, const gchar *folder_name, GError **error)
{
	CamelStoreInfo *si;
	CamelIMAPXStoreNamespace *ns;
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gchar *real_name, *full_name, *parent_real;
	CamelFolderInfo *fi = NULL;
	gchar dir_sep;
	gboolean success;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	server = camel_imapx_store_get_server(istore, NULL, error);
	if (!server)
		return NULL;

	if (!parent_name)
		parent_name = "";

	ns = camel_imapx_store_summary_namespace_find_path (istore->summary, parent_name);
	if (ns)
		dir_sep = ns->sep;
	else
		dir_sep = '/';

	if (strchr(folder_name, dir_sep)) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_PATH,
			_("The folder name \"%s\" is invalid because it contains the character \"%c\""),
			folder_name, dir_sep);
		g_object_unref(server);
		return NULL;
	}

	parent_real = camel_imapx_store_summary_full_from_path(istore->summary, parent_name);
	if (parent_real == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Unknown parent folder: %s"), parent_name);
		g_object_unref(server);
		return NULL;
	}

	si = camel_store_summary_path ((CamelStoreSummary *)istore->summary, parent_name);
	if (si && si->flags & CAMEL_STORE_INFO_FOLDER_NOINFERIORS) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("The parent folder is not allowed to contain subfolders"));
		g_object_unref(server);
		return NULL;
	}

	if (si)
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);

	real_name = camel_imapx_store_summary_path_to_full (istore->summary, folder_name, dir_sep);
	full_name = imapx_concat (istore, parent_real, real_name);
	g_free(real_name);

	success = camel_imapx_server_create_folder (server, full_name, error);
	g_object_unref(server);

	if (success) {
		CamelIMAPXStoreInfo *si;

		si = camel_imapx_store_summary_add_from_full(istore->summary, full_name, dir_sep);
		camel_store_summary_save((CamelStoreSummary *)istore->summary);
		fi = imapx_build_folder_info(istore, camel_store_info_path(istore->summary, si));
		fi->flags |= CAMEL_FOLDER_NOCHILDREN;
		camel_store_folder_created (store, fi);
	}

	g_free (full_name);
	g_free(parent_real);

	return fi;
}

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store, const gchar *top,
			 guint32 flags, GError **error)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (store);
	gboolean include_inbox = FALSE;
	CamelFolderInfo *fi;
	GPtrArray *folders;
	gchar *pattern, *name;
	gint i;

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	if (top == NULL || top[0] == '\0') {
		include_inbox = TRUE;
		top = "";
	}

	/* get starting point */
	if (top[0] == 0) {
		if (imapx_store->namespace && imapx_store->namespace[0]) {
			name = g_strdup(imapx_store->summary->namespaces->personal->full_name);
			top = imapx_store->summary->namespaces->personal->path;
		} else
			name = g_strdup("");
	} else {
		name = camel_imapx_store_summary_full_from_path(imapx_store->summary, top);
		if (name == NULL)
			name = camel_imapx_store_summary_path_to_full(imapx_store->summary, top, imapx_store->dir_sep);
	}

	pattern = imapx_concat(imapx_store, name, "*");

	/* folder_info_build will insert parent nodes as necessary and mark
	 * them as noselect, which is information we actually don't have at
	 * the moment. So let it do the right thing by bailing out if it's
	 * not a folder we're explicitly interested in. */

	for (i=0;i<camel_store_summary_count((CamelStoreSummary *)imapx_store->summary);i++) {
		CamelStoreInfo *si = camel_store_summary_index((CamelStoreSummary *)imapx_store->summary, i);
		const gchar *full_name;
		CamelIMAPXStoreNamespace *ns;

		if (si == NULL)
			continue;

		full_name = camel_imapx_store_info_full_name (imapx_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *)imapx_store->summary, si);
			continue;
		}

		ns = camel_imapx_store_summary_namespace_find_full (imapx_store->summary, full_name);

		/* Modify the checks to see match the namespaces from preferences */
		if ((g_str_equal (name, full_name)
		     || imapx_match_pattern (ns, pattern, full_name)
		     || (include_inbox && !g_ascii_strcasecmp (full_name, "INBOX")))
		    && ( ((imapx_store->rec_options & IMAPX_SUBSCRIPTIONS) == 0
			    || (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) == 0)
			|| (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)
			|| (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0)) {

			fi = imapx_build_folder_info(imapx_store, camel_store_info_path((CamelStoreSummary *)imapx_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			/* HACK: some servers report noinferiors for all folders (uw-imapd)
			   We just translate this into nochildren, and let the imap layer enforce
			   it.  See create folder */
			if (fi->flags & CAMEL_FOLDER_NOINFERIORS)
				fi->flags = (fi->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;

			/* blah, this gets lost somewhere, i can't be bothered finding out why */
			if (!g_ascii_strcasecmp(fi->full_name, "inbox")) {
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;
				fi->flags |= CAMEL_FOLDER_SYSTEM;
			}

			if (si->flags & CAMEL_FOLDER_NOSELECT) {
				CamelURL *url = camel_url_new(fi->uri, NULL);

				camel_url_set_param (url, "noselect", "yes");
				g_free(fi->uri);
				fi->uri = camel_url_to_string (url, 0);
				camel_url_free (url);
			} else {
				fill_fi((CamelStore *)imapx_store, fi, 0);
			}
			if (!fi->child)
				fi->flags |= CAMEL_FOLDER_NOCHILDREN;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free((CamelStoreSummary *)imapx_store->summary, si);
	}
	g_free(pattern);

	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	g_free(name);

	return fi;
}

static void
add_folders_to_summary (CamelIMAPXStore *istore, CamelIMAPXServer *server, GPtrArray *folders, GHashTable *table, gboolean subscribed)
{
	gint i = 0;

	for (i = 0; i < folders->len; i++) {
		struct _list_info *li = folders->pdata[i];
		CamelIMAPXStoreInfo *si;
		guint32 new_flags;
		CamelFolderInfo *fi, *sfi;
		gchar *path;
		CamelURL *url;

		if (subscribed) {
			path = camel_imapx_store_summary_path_to_full (istore->summary, li->name, li->separator);
			sfi = g_hash_table_lookup (table, path);
			if (sfi)
				sfi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;

			g_free(path);
			continue;
		}

		si = camel_imapx_store_summary_add_from_full (istore->summary, li->name, li->separator);
		if (!si) {
			g_object_unref(server);
			continue;
		}

		new_flags = (si->info.flags & (CAMEL_STORE_INFO_FOLDER_SUBSCRIBED | CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW)) |
						(li->flags & ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED);

		if (!(server->cinfo->capa & IMAPX_CAPABILITY_NAMESPACE))
			istore->dir_sep = li->separator;

		if (si->info.flags != new_flags) {
			si->info.flags = new_flags;
			camel_store_summary_touch ((CamelStoreSummary *) istore->summary);
		}

		fi = camel_folder_info_new ();
		fi->full_name = g_strdup(camel_store_info_path(istore->summary, si));
		if (!g_ascii_strcasecmp(fi->full_name, "inbox")) {
			li->flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
			fi->name = g_strdup (_("Inbox"));
		} else
			fi->name = g_strdup(camel_store_info_name(istore->summary, si));

		/* HACK: some servers report noinferiors for all folders (uw-imapd)
		   We just translate this into nochildren, and let the imap layer enforce
		   it.  See create folder */
		if (li->flags & CAMEL_FOLDER_NOINFERIORS)
			li->flags = (li->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;
		fi->flags = li->flags;

		url = camel_url_new (istore->base_url, NULL);
		path = alloca(strlen(fi->full_name)+2);
		sprintf(path, "/%s", fi->full_name);
		camel_url_set_path(url, path);

		if (li->flags & CAMEL_FOLDER_NOSELECT || fi->name[0] == 0)
			camel_url_set_param (url, "noselect", "yes");
		fi->uri = camel_url_to_string (url, 0);
		camel_url_free (url);

		fi->total = -1;
		fi->unread = -1;

		g_hash_table_insert (table, fi->full_name, fi);
	}
}

static void
free_list (gpointer data, gpointer user_data)
{
	struct _list_info *li = data;
	imapx_free_list (li);
}

static void
imapx_get_folders_free(gpointer k, gpointer v, gpointer d)
{
	camel_folder_info_free(v);
}

static gboolean
fetch_folders_for_pattern (CamelIMAPXStore *istore,
                           CamelIMAPXServer *server,
                           const gchar *pattern,
                           guint32 flags,
                           const gchar *ext,
                           GHashTable *table,
                           GError **error)
{
	GPtrArray *folders;

	folders = camel_imapx_server_list (server, pattern, flags, ext, error);
	if (folders == NULL)
		return FALSE;

	add_folders_to_summary (istore, server, folders, table, (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED));

	g_ptr_array_foreach (folders, free_list, folders);
	g_ptr_array_free (folders, TRUE);

	return TRUE;
}

static GSList *
get_namespaces (CamelIMAPXStore *istore)
{
	GSList *namespaces = NULL;
	CamelIMAPXNamespaceList *nsl = NULL;

	/* Add code to return the namespaces from preference else all of them */
	nsl = istore->summary->namespaces;
	if (nsl->personal)
		namespaces = g_slist_append (namespaces, nsl->personal);
	if (nsl->other)
		namespaces = g_slist_append (namespaces, nsl->other);
	if (nsl->shared)
		namespaces = g_slist_append (namespaces, nsl->shared);

	return namespaces;
}

static GHashTable *
fetch_folders_for_namespaces (CamelIMAPXStore *istore, const gchar *pattern, gboolean sync, GError **error)
{
	CamelIMAPXServer *server;
	GHashTable *folders = NULL;
	GSList *namespaces = NULL, *l;

	server = camel_imapx_store_get_server (istore, NULL, error);
	if (!server)
		return NULL;

	folders = g_hash_table_new (folder_hash, folder_eq);
	namespaces = get_namespaces (istore);

	for (l = namespaces; l != NULL; l = g_slist_next (l))
	{
		CamelIMAPXStoreNamespace *ns = l->data;

		while (ns) {
			guint32 flags = 0;
			gchar *pat = NULL;
			const gchar *list_ext = NULL;

			if (!pattern) {
				if (!*ns->path)
					pat = g_strdup ("");
				else
					pat = g_strdup_printf ("%s%c", ns->path, ns->sep);
			} else
				pat = g_strdup (pattern);

			if (sync)
				flags |= CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST;

			if (server->cinfo->capa & IMAPX_CAPABILITY_LIST_EXTENDED)
				list_ext = "RETURN (SUBSCRIBED)";

			flags |= CAMEL_STORE_FOLDER_INFO_RECURSIVE;
			if (!fetch_folders_for_pattern (istore, server, pat, flags, list_ext, folders, error)) {
				g_free (pat);
				goto exception;
			}
			if (!list_ext) {
				/* If the server doesn't support LIST-EXTENDED then we have to
				   issue LSUB to list the subscribed folders separately */
				flags |= CAMEL_STORE_FOLDER_INFO_SUBSCRIBED;
				if (!fetch_folders_for_pattern (istore, server, pat, flags, NULL, folders, error)) {
					g_free (pat);
					goto exception;
				}
			}
			g_free (pat);

			if (pattern)
				goto out;

			ns = ns->next;
		}
	}
 out:
	g_object_unref(server);
	return folders;

exception:
	g_object_unref(server);
	g_hash_table_destroy (folders);
	return NULL;
}

static gboolean
sync_folders (CamelIMAPXStore *istore, const gchar *pattern, gboolean sync, GError **error)
{
	GHashTable *folders_from_server;
	gint i, total;

	folders_from_server = fetch_folders_for_namespaces (istore, pattern, sync, error);
	if (folders_from_server == NULL)
		return FALSE;

	total = camel_store_summary_count ((CamelStoreSummary *) istore->summary);
	for (i = 0; i < total; i++) {
		CamelStoreInfo *si;
		const gchar *full_name;
		CamelFolderInfo *fi;

		si = camel_store_summary_index ((CamelStoreSummary *) istore->summary, i);
		if (!si)
			continue;

		full_name = camel_imapx_store_info_full_name (istore->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *)istore->summary, si);
			continue;
		}

		if (!pattern || !*pattern || imapx_match_pattern (camel_imapx_store_summary_namespace_find_full (istore->summary, full_name), pattern, full_name)) {
			if ((fi = g_hash_table_lookup(folders_from_server, camel_store_info_path(istore->summary, si))) != NULL) {
				if (((fi->flags ^ si->flags) & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)) {
					si->flags = (si->flags & ~CAMEL_FOLDER_SUBSCRIBED) | (fi->flags & CAMEL_FOLDER_SUBSCRIBED);
					camel_store_summary_touch((CamelStoreSummary *)istore->summary);

					camel_store_folder_created (CAMEL_STORE (istore), fi);
					camel_store_folder_subscribed (CAMEL_STORE (istore), fi);
				}
			} else {
				gchar *dup_folder_name = g_strdup (camel_store_info_path (istore->summary, si));

				if (dup_folder_name) {
					imapx_unmark_folder_subscribed (istore,dup_folder_name, TRUE);
					imapx_delete_folder_from_cache (istore, dup_folder_name);
					g_free (dup_folder_name);
				} else {
					camel_store_summary_remove ((CamelStoreSummary *)istore->summary, si);
				}

				total--;
				i--;
			}
		}
		camel_store_summary_info_free((CamelStoreSummary *)istore->summary, si);
	}

	g_hash_table_foreach (folders_from_server, imapx_get_folders_free, NULL);
	g_hash_table_destroy (folders_from_server);

	return TRUE;
}

struct _imapx_refresh_msg {
	CamelSessionThreadMsg msg;

	CamelStore *store;
	GError *error;
};

static void
imapx_refresh_finfo (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _imapx_refresh_msg *m = (struct _imapx_refresh_msg *)msg;
	CamelIMAPXStore *istore = (CamelIMAPXStore *)m->store;

	if (CAMEL_OFFLINE_STORE(istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (!camel_service_connect((CamelService *)istore, &m->error))
		return;

	/* look in all namespaces */
	sync_folders (istore, "", FALSE, &m->error);
	camel_store_summary_save ((CamelStoreSummary *)istore->summary);
}

static void
imapx_refresh_free(CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _imapx_refresh_msg *m = (struct _imapx_refresh_msg *)msg;

	g_object_unref (m->store);
	g_clear_error (&m->error);
}

static CamelSessionThreadOps imapx_refresh_ops = {
	imapx_refresh_finfo,
	imapx_refresh_free,
};

static void
discover_inbox (CamelStore *store)
{
	CamelStoreInfo *si;
	CamelIMAPXStore *istore = (CamelIMAPXStore *)store;

	si = camel_store_summary_path((CamelStoreSummary *) istore->summary, "INBOX");
	if (si == NULL || (si->flags & CAMEL_FOLDER_SUBSCRIBED) == 0) {
		if (imapx_subscribe_folder (store, "INBOX", FALSE, NULL) && !si)
			sync_folders (istore, "INBOX", TRUE, NULL);

		if (si)
			camel_store_summary_info_free((CamelStoreSummary *) istore->summary, si);
	}
}

static CamelFolderInfo *
imapx_get_folder_info(CamelStore *store, const gchar *top, guint32 flags, GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)store;
	CamelFolderInfo * fi= NULL;
	gboolean initial_setup = FALSE;
	gchar *pattern;

	if (top == NULL)
		top = "";

	g_mutex_lock (istore->get_finfo_lock);

	if (CAMEL_OFFLINE_STORE(store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		fi = get_folder_info_offline (store, top, flags, error);

		g_mutex_unlock (istore->get_finfo_lock);
		return fi;
	}

	if (camel_store_summary_count ((CamelStoreSummary *) istore->summary) == 0)
		initial_setup = TRUE;

	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		time_t now = time (NULL);

		if (now - istore->last_refresh_time > FINFO_REFRESH_INTERVAL) {
			struct _imapx_refresh_msg *m;

			istore->last_refresh_time = time (NULL);
			m = camel_session_thread_msg_new(((CamelService *)store)->session, &imapx_refresh_ops, sizeof(*m));
			m->store = g_object_ref (store);
			camel_session_thread_queue(((CamelService *)store)->session, &m->msg, 0);
		}

		fi = get_folder_info_offline (store, top, flags, error);
		g_mutex_unlock (istore->get_finfo_lock);
		return fi;
	}

	if (!camel_service_connect((CamelService *)store, error)) {
		g_mutex_unlock (istore->get_finfo_lock);
		return NULL;
	}

	if (*top && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) {
		fi = get_folder_info_offline (store, top, flags, error);
		g_mutex_unlock (istore->get_finfo_lock);
		return fi;
	}

	if (*top) {
		gchar *name;
		gint i;

		name = camel_imapx_store_summary_full_from_path(istore->summary, top);
		if (name == NULL)
			name = camel_imapx_store_summary_path_to_full(istore->summary, top, istore->dir_sep);

		i = strlen(name);
		pattern = g_alloca(i+5);
		strcpy(pattern, name);
		g_free(name);
	} else {
		pattern = g_alloca (1);
		pattern[0] = '\0';
	}

	if (!sync_folders (istore, pattern, TRUE, error)) {
		g_mutex_unlock (istore->get_finfo_lock);
		return NULL;
	}

	camel_store_summary_save((CamelStoreSummary *) istore->summary);

	/* ensure the INBOX is subscribed if lsub was preferred*/
	if (initial_setup && istore->rec_options & IMAPX_SUBSCRIPTIONS)
		discover_inbox (store);

	fi = get_folder_info_offline (store, top, flags, error);
	g_mutex_unlock (istore->get_finfo_lock);
	return fi;
}

static gboolean
imapx_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	CamelStoreClass *store_class;
	gboolean res;
	GError *local_error = NULL;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);

	res = store_class->can_refresh_folder (store, info, &local_error) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_all") != NULL) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_lsub") != NULL && (info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0);

	if (!res && local_error == NULL && CAMEL_IS_IMAPX_STORE (store)) {
		CamelStoreInfo *si;
		CamelStoreSummary *sm = CAMEL_STORE_SUMMARY (((CamelIMAPXStore *)(store))->summary);

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

static gboolean
imapx_folder_is_subscribed (CamelStore *store,
                            const gchar *folder_name)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (store);
	CamelStoreInfo *si;
	gint is_subscribed = FALSE;

	si = camel_store_summary_path((CamelStoreSummary *)istore->summary, folder_name);
	if (si) {
		is_subscribed = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free((CamelStoreSummary *)istore->summary, si);
	}

	return is_subscribed;
}

static void
camel_imapx_store_class_init(CamelIMAPXStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imapx_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = imapx_construct;
	service_class->query_auth_types = imapx_query_auth_types;
	service_class->get_name = imapx_get_name;
	service_class->connect = imapx_connect;
	service_class->disconnect = imapx_disconnect;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_trash = imapx_get_trash;
	store_class->get_junk = imapx_get_junk;
	store_class->noop = imapx_noop;
	store_class->get_folder = imapx_get_folder;
	store_class->hash_folder_name = imapx_hash_folder_name;
	store_class->compare_folder_name = imapx_compare_folder_name;
	store_class->can_refresh_folder = imapx_can_refresh_folder;
	store_class->create_folder = imapx_create_folder;
	store_class->rename_folder = imapx_rename_folder;
	store_class->delete_folder = imapx_delete_folder;
	store_class->subscribe_folder = imapx_store_subscribe_folder;
	store_class->unsubscribe_folder = imapx_store_unsubscribe_folder;
	store_class->get_folder_info = imapx_get_folder_info;
	store_class->folder_is_subscribed = imapx_folder_is_subscribed;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->hash_folder_name = imapx_name_hash;
	store_class->compare_folder_name = imapx_name_equal;
}

static void
camel_imapx_store_init (CamelIMAPXStore *istore)
{
	CamelStore *store = CAMEL_STORE (istore);

	store->flags |= CAMEL_STORE_ASYNC | CAMEL_STORE_SUBSCRIPTIONS;
	istore->get_finfo_lock = g_mutex_new ();
	istore->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	istore->dir_sep = '/';
	istore->con_man = camel_imapx_conn_manager_new (store);
}
