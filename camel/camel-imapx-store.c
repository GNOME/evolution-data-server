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
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-imapx-folder.h"
#include "camel-imapx-server.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-utils.h"

/* Specified in RFC 2060 section 2.1 */
#define IMAP_PORT 143
#define IMAPS_PORT 993

#define FINFO_REFRESH_INTERVAL 60

static GInitableIface *parent_initable_interface;

/* Forward Declarations */
static void camel_imapx_store_initable_init (GInitableIface *interface);
static void camel_network_service_init (CamelNetworkServiceInterface *interface);
static void camel_subscribable_init (CamelSubscribableInterface *interface);

G_DEFINE_TYPE_WITH_CODE (
	CamelIMAPXStore,
	camel_imapx_store,
	CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		camel_imapx_store_initable_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SERVICE,
		camel_network_service_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_SUBSCRIBABLE,
		camel_subscribable_init))

static guint
imapx_name_hash (gconstpointer key)
{
	if (g_ascii_strcasecmp (key, "INBOX") == 0)
		return g_str_hash ("INBOX");
	else
		return g_str_hash (key);
}

static gboolean
imapx_name_equal (gconstpointer a,
                  gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal (aname, bname);
}

static void
imapx_store_dispose (GObject *object)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (object);

	/* Force disconnect so we dont have it run later,
	 * after we've cleaned up some stuff. */
	if (imapx_store->con_man != NULL) {
		camel_service_disconnect_sync (
			CAMEL_SERVICE (imapx_store), TRUE, NULL, NULL);
		g_object_unref (imapx_store->con_man);
		imapx_store->con_man = NULL;
	}

	if (imapx_store->authenticating_server != NULL) {
		g_object_unref (imapx_store->authenticating_server);
		imapx_store->authenticating_server = NULL;
	}

	if (imapx_store->summary != NULL) {
		g_object_unref (imapx_store->summary);
		imapx_store->summary = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_store_parent_class)->dispose (object);
}

static void
imapx_store_finalize (GObject *object)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (object);

	g_mutex_free (imapx_store->get_finfo_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_store_parent_class)->finalize (object);
}

static gchar *
imapx_get_name (CamelService *service,
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

CamelIMAPXServer *
camel_imapx_store_get_server (CamelIMAPXStore *istore,
                              const gchar *folder_name,
                              GCancellable *cancellable,
                              GError **error)
{
	return camel_imapx_conn_manager_get_connection (
		istore->con_man, folder_name, cancellable, error);
}

void
camel_imapx_store_op_done (CamelIMAPXStore *istore,
                           CamelIMAPXServer *server,
                           const gchar *folder_name)
{
	g_return_if_fail (server != NULL);

	camel_imapx_conn_manager_update_con_info (
		istore->con_man, server, folder_name);
}

static gboolean
imapx_connect_sync (CamelService *service,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) service;
	CamelIMAPXServer *server;

	server = camel_imapx_store_get_server (istore, NULL, cancellable, error);
	if (server) {
		g_object_unref (server);
		return TRUE;
	}

	return FALSE;
}

static gboolean
imapx_disconnect_sync (CamelService *service,
                       gboolean clean,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (service);
	CamelServiceClass *service_class;

	service_class = CAMEL_SERVICE_CLASS (camel_imapx_store_parent_class);
	if (!service_class->disconnect_sync (service, clean, cancellable, error))
		return FALSE;

	if (istore->con_man != NULL)
		camel_imapx_conn_manager_close_connections (istore->con_man);

	return TRUE;
}

static CamelAuthenticationResult
imapx_authenticate_sync (CamelService *service,
                         const gchar *mechanism,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (service);
	CamelIMAPXServer *server;

	/* CamelIMAPXConnManager sets this before calling
	 * camel_imapx_server_connect()(), and then clears it
	 * immediately after, all while holding the recursive
	 * connection lock (CAMEL_SERVICE_REC_CONNECT_LOCK).
	 * Otherwise we'd have no way of knowing which server
	 * is trying to authenticate. */
	server = istore->authenticating_server;

	g_return_val_if_fail (
		CAMEL_IS_IMAPX_SERVER (server),
		CAMEL_AUTHENTICATION_REJECTED);

	return camel_imapx_server_authenticate (
		server, mechanism, cancellable, error);
}

CamelServiceAuthType camel_imapx_password_authtype = {
	N_("Password"),

	N_("This option will connect to the IMAP server using a "
	   "plaintext password."),

	"",
	TRUE
};

static GList *
imapx_query_auth_types_sync (CamelService *service,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (service);
	CamelServiceAuthType *authtype;
	GList *sasl_types, *t, *next;
	CamelIMAPXServer *server;
	CamelIMAPXStream *stream;
	gboolean connected;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	server = camel_imapx_server_new (istore);

	stream = camel_imapx_server_ref_stream (server);
	if (stream != NULL) {
		connected = TRUE;
		g_object_unref (stream);
	} else {
		connected = imapx_connect_to_server (
			server, cancellable, error);
	}

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

static CamelFolder *
get_folder_offline (CamelStore *store,
                    const gchar *folder_name,
                    guint32 flags,
                    GError **error)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (store);
	CamelFolder *new_folder = NULL;
	CamelStoreInfo *si;
	CamelService *service;
	const gchar *user_cache_dir;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	si = camel_store_summary_path ((CamelStoreSummary *) imapx_store->summary, folder_name);
	if (!si && g_ascii_strcasecmp (folder_name, "INBOX") == 0)
		si = (CamelStoreInfo *) camel_imapx_store_summary_full_name (imapx_store->summary, folder_name);
	if (si) {
		gchar *folder_dir, *storage_path;

		storage_path = g_build_filename (user_cache_dir, "folders", NULL);
		/* Note: Although the INBOX is defined to be case-insensitive in the IMAP RFC
		 * it is still up to the server how to acutally name it in a LIST response. Since
		 * we stored the name as the server provided it us in the summary we take that name
		 * to look up the folder.
		 * But for the on-disk cache we do always capitalize the Inbox no matter what the
		 * server provided.
		 */
		folder_dir = imapx_path_to_physical (
			storage_path,
			g_ascii_strcasecmp (folder_name, "INBOX") == 0 ? "INBOX" : folder_name);
		g_free (storage_path);

		new_folder = camel_imapx_folder_new (store, folder_dir, folder_name, error);

		g_free (folder_dir);
		camel_store_summary_info_free ((CamelStoreSummary *) imapx_store->summary, si);
	} else {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder %s"), folder_name);
	}

	return new_folder;
}

/* folder_name is path name */
static CamelFolderInfo *
imapx_build_folder_info (CamelIMAPXStore *imapx_store,
                         const gchar *folder_name)
{
	CamelFolderInfo *fi;
	const gchar *name;

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
	/* Do not localize the rest, these are from a server, thus shouldn't be localized */
	/*else if (!g_ascii_strcasecmp (fi->full_name, "Drafts"))
		fi->display_name = g_strdup (_("Drafts"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Sent"))
		fi->display_name = g_strdup (_("Sent"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Templates"))
		fi->display_name = g_strdup (_("Templates"));
	else if (!g_ascii_strcasecmp (fi->full_name, "Trash"))
		fi->display_name = g_strdup (_("Trash"));*/
	else
		fi->display_name = g_strdup (name);

	return fi;
}

static void
fill_fi (CamelStore *store,
         CamelFolderInfo *fi,
         guint32 flags)
{
	CamelFolder *folder;
	CamelService *service = (CamelService *) store;
	CamelSettings *settings;
	gboolean mobile_mode;

	settings = camel_service_ref_settings (service);

	mobile_mode = camel_imapx_settings_get_mobile_mode (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	folder = camel_object_bag_peek (store->folders, fi->full_name);
	if (folder) {
		CamelIMAPXSummary *ims;

		if (folder->summary)
			ims = (CamelIMAPXSummary *) folder->summary;
		else
			ims = (CamelIMAPXSummary *) camel_imapx_summary_new (folder);

		/* Mobile clients would still love to see the total unread of actual mails
		 * than what they just have downloaded. So we override that information by giving 
		 * what the server has instead of what we have downloaded. */
		if (mobile_mode)
			fi->unread = ((CamelIMAPXFolder *) folder)->unread_on_server;
		else
			fi->unread = camel_folder_summary_get_unread_count ((CamelFolderSummary *) ims);
		fi->total = camel_folder_summary_get_saved_count ((CamelFolderSummary *) ims);

		if (!folder->summary)
			g_object_unref (ims);
		g_object_unref (folder);
	}
}

/* imap needs to treat inbox case insensitive */
/* we'll assume the names are normalized already */
static guint
folder_hash (gconstpointer ap)
{
	const gchar *a = ap;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		a = "INBOX";

	return g_str_hash (a);
}

static gint
folder_eq (gconstpointer ap,
           gconstpointer bp)
{
	const gchar *a = ap;
	const gchar *b = bp;

	if (g_ascii_strcasecmp (a, "INBOX") == 0)
		a = "INBOX";
	if (g_ascii_strcasecmp (b, "INBOX") == 0)
		b = "INBOX";

	return g_str_equal (a, b);
}

static gboolean
imapx_match_pattern (CamelIMAPXStoreNamespace *ns,
                     const gchar *pattern,
                     const gchar *name)
{
	gchar p, n, dir_sep;

	if (!ns)
		return TRUE;

	dir_sep = ns->sep;
	if (!dir_sep)
		dir_sep = '/';
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
imapx_unmark_folder_subscribed (CamelIMAPXStore *istore,
                                const gchar *folder_name,
                                gboolean emit_signal)
{
	CamelStoreInfo *si;

	si = camel_store_summary_path ((CamelStoreSummary *) istore->summary, folder_name);
	if (si) {
		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			si->flags &= ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch ((CamelStoreSummary *) istore->summary);
			camel_store_summary_save ((CamelStoreSummary *) istore->summary);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
	}

	if (emit_signal) {
		CamelFolderInfo *fi;

		fi = imapx_build_folder_info (istore, folder_name);
		camel_subscribable_folder_unsubscribed (
			CAMEL_SUBSCRIBABLE (istore), fi);
		camel_folder_info_free (fi);
	}
}

static void
imapx_mark_folder_subscribed (CamelIMAPXStore *istore,
                              const gchar *folder_name,
                              gboolean emit_signal)
{
	CamelStoreInfo *si;

	si = camel_store_summary_path ((CamelStoreSummary *) istore->summary, folder_name);
	if (si) {
		if ((si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) == 0) {
			si->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			camel_store_summary_touch ((CamelStoreSummary *) istore->summary);
			camel_store_summary_save ((CamelStoreSummary *) istore->summary);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
	}

	if (emit_signal) {
		CamelFolderInfo *fi;

		fi = imapx_build_folder_info (istore, folder_name);
		camel_subscribable_folder_subscribed (
			CAMEL_SUBSCRIBABLE (istore), fi);
		camel_folder_info_free (fi);
	}
}

static gboolean
imapx_subscribe_folder (CamelStore *store,
                        const gchar *folder_name,
                        gboolean emit_signal,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gboolean success;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return TRUE;

	server = camel_imapx_store_get_server (istore, NULL, cancellable, error);
	if (!server)
		return FALSE;

	if (folder_name && *folder_name == '/')
		folder_name++;

	success = camel_imapx_server_manage_subscription (
		server, folder_name, TRUE, cancellable, error);
	g_object_unref (server);

	if (success)
		imapx_mark_folder_subscribed (istore, folder_name, emit_signal);

	return success;
}

static gboolean
imapx_unsubscribe_folder (CamelStore *store,
                          const gchar *folder_name,
                          gboolean emit_signal,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gboolean success;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return TRUE;

	server = camel_imapx_store_get_server (istore, NULL, cancellable, error);
	if (!server)
		return FALSE;

	if (folder_name && *folder_name == '/')
		folder_name++;

	success = camel_imapx_server_manage_subscription (
		server, folder_name, FALSE, cancellable, error);
	g_object_unref (server);

	if (success)
		imapx_unmark_folder_subscribed (istore, folder_name, emit_signal);

	return success;
}

static void
imapx_delete_folder_from_cache (CamelIMAPXStore *istore,
                                const gchar *folder_name)
{
	gchar *state_file;
	gchar *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	CamelService *service;
	const gchar *user_cache_dir;

	service = CAMEL_SERVICE (istore);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	storage_path = g_build_filename (user_cache_dir, "folders", NULL);
	folder_dir = imapx_path_to_physical (storage_path, folder_name);
	g_free (storage_path);
	if (g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		goto event;
	}

	/* Delete summary and all the data */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	g_unlink (state_file);
	g_free (state_file);

	camel_db_delete_folder (((CamelStore *) istore)->cdb_w, folder_name, NULL);
	g_rmdir (folder_dir);

	state_file = g_build_filename (folder_dir, "subfolders", NULL);
	g_rmdir (state_file);
	g_free (state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

 event:
	camel_store_summary_remove_path ((CamelStoreSummary *) istore->summary, folder_name);
	camel_store_summary_save ((CamelStoreSummary *) istore->summary);

	fi = imapx_build_folder_info (istore, folder_name);
	camel_store_folder_deleted (CAMEL_STORE (istore), fi);
	camel_folder_info_free (fi);
}

static void
rename_folder_info (CamelIMAPXStore *istore,
                    const gchar *old_name,
                    const gchar *new_name)
{
	gint i, count;
	CamelStoreInfo *si;
	gint olen = strlen (old_name);
	const gchar *path;
	gchar *npath, *nfull;

	count = camel_store_summary_count ((CamelStoreSummary *) istore->summary);
	for (i = 0; i < count; i++) {
		si = camel_store_summary_index ((CamelStoreSummary *) istore->summary, i);
		if (si == NULL)
			continue;
		path = camel_store_info_path (istore->summary, si);
		if (strncmp (path, old_name, olen) == 0) {
			if (strlen (path) > olen)
				npath = g_strdup_printf ("%s/%s", new_name, path + olen + 1);
			else
				npath = g_strdup (new_name);
			nfull = camel_imapx_store_summary_path_to_full (istore->summary, npath, istore->dir_sep);

			camel_store_info_set_string ((CamelStoreSummary *) istore->summary, si, CAMEL_STORE_INFO_PATH, npath);
			camel_store_info_set_string ((CamelStoreSummary *) istore->summary, si, CAMEL_IMAPX_STORE_INFO_FULL_NAME, nfull);

			camel_store_summary_touch ((CamelStoreSummary *) istore->summary);
			g_free (nfull);
			g_free (npath);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
	}
}

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store,
                         const gchar *top,
                         guint32 flags,
                         GError **error)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (store);
	CamelService *service;
	CamelSettings *settings;
	gboolean include_inbox = FALSE;
	CamelFolderInfo *fi;
	GPtrArray *folders;
	gchar *pattern, *name;
	gboolean use_namespace;
	gboolean use_subscriptions;
	gint i;

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	use_namespace = camel_imapx_settings_get_use_namespace (
		CAMEL_IMAPX_SETTINGS (settings));

	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	if (top == NULL || top[0] == '\0') {
		include_inbox = TRUE;
		top = "";
	}

	/* get starting point */
	if (top[0] == 0) {
		gchar *namespace = NULL;

		if (use_namespace) {
			settings = camel_service_ref_settings (service);

			namespace = camel_imapx_settings_dup_namespace (
				CAMEL_IMAPX_SETTINGS (settings));

			g_object_unref (settings);
		}

		if (namespace != NULL) {
			name = g_strdup (imapx_store->summary->namespaces->personal->full_name);
			top = imapx_store->summary->namespaces->personal->path;
		} else
			name = g_strdup ("");

		g_free (namespace);
	} else {
		name = camel_imapx_store_summary_full_from_path (imapx_store->summary, top);
		if (name == NULL)
			name = camel_imapx_store_summary_path_to_full (imapx_store->summary, top, imapx_store->dir_sep);
	}

	pattern = imapx_concat (imapx_store, name, "*");

	/* folder_info_build will insert parent nodes as necessary and mark
	 * them as noselect, which is information we actually don't have at
	 * the moment. So let it do the right thing by bailing out if it's
	 * not a folder we're explicitly interested in. */

	for (i = 0; i < camel_store_summary_count ((CamelStoreSummary *) imapx_store->summary); i++) {
		CamelStoreInfo *si = camel_store_summary_index ((CamelStoreSummary *) imapx_store->summary, i);
		const gchar *full_name;
		CamelIMAPXStoreNamespace *ns;

		if (si == NULL)
			continue;

		full_name = camel_imapx_store_info_full_name (imapx_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *) imapx_store->summary, si);
			continue;
		}

		ns = camel_imapx_store_summary_namespace_find_full (imapx_store->summary, full_name);

		/* Modify the checks to see match the namespaces from preferences */
		if ((g_str_equal (name, full_name)
		     || imapx_match_pattern (ns, pattern, full_name)
		     || (include_inbox && !g_ascii_strcasecmp (full_name, "INBOX")))
		    && ( (!use_subscriptions
			    || (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) == 0)
			|| (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)
			|| (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0)) {

			fi = imapx_build_folder_info (imapx_store, camel_store_info_path ((CamelStoreSummary *) imapx_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			/* HACK: some servers report noinferiors for all folders (uw-imapd)
			 * We just translate this into nochildren, and let the imap layer enforce
			 * it.  See create folder */
			if (fi->flags & CAMEL_FOLDER_NOINFERIORS)
				fi->flags = (fi->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;

			/* blah, this gets lost somewhere, i can't be bothered finding out why */
			if (!g_ascii_strcasecmp (fi->full_name, "inbox")) {
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;
				fi->flags |= CAMEL_FOLDER_SYSTEM;
			}

			if (!(si->flags & CAMEL_FOLDER_NOSELECT))
				fill_fi ((CamelStore *) imapx_store, fi, 0);

			if (!fi->child)
				fi->flags |= CAMEL_FOLDER_NOCHILDREN;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free ((CamelStoreSummary *) imapx_store->summary, si);
	}
	g_free (pattern);

	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	g_free (name);

	return fi;
}

static void
add_folders_to_summary (CamelIMAPXStore *istore,
                        CamelIMAPXServer *server,
                        GPtrArray *folders,
                        GHashTable *table,
                        gboolean subscribed)
{
	gint i = 0;

	for (i = 0; i < folders->len; i++) {
		struct _list_info *li = folders->pdata[i];
		CamelIMAPXStoreInfo *si;
		guint32 new_flags;
		CamelFolderInfo *fi, *sfi;
		gchar *path;

		if (subscribed) {
			path = camel_imapx_store_summary_path_to_full (istore->summary, li->name, li->separator);
			sfi = g_hash_table_lookup (table, path);
			if (sfi)
				sfi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;

			g_free (path);
			continue;
		}

		si = camel_imapx_store_summary_add_from_full (istore->summary, li->name, li->separator);
		if (!si)
			continue;

		new_flags = (si->info.flags & (CAMEL_STORE_INFO_FOLDER_SUBSCRIBED | CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW)) |
						(li->flags & ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED);

		if (!(server->cinfo->capa & IMAPX_CAPABILITY_NAMESPACE))
			istore->dir_sep = li->separator;

		if (si->info.flags != new_flags) {
			si->info.flags = new_flags;
			camel_store_summary_touch ((CamelStoreSummary *) istore->summary);
		}

		fi = camel_folder_info_new ();
		fi->full_name = g_strdup (camel_store_info_path (istore->summary, si));
		if (!g_ascii_strcasecmp (fi->full_name, "inbox")) {
			li->flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;
			fi->display_name = g_strdup (_("Inbox"));
		} else
			fi->display_name = g_strdup (camel_store_info_name (istore->summary, si));

		/* HACK: some servers report noinferiors for all folders (uw-imapd)
		 * We just translate this into nochildren, and let the imap layer enforce
		 * it.  See create folder */
		if (li->flags & CAMEL_FOLDER_NOINFERIORS)
			li->flags = (li->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;
		fi->flags = li->flags;

		fi->total = -1;
		fi->unread = -1;

		g_hash_table_insert (table, fi->full_name, fi);
	}
}

static void
free_list (gpointer data,
           gpointer user_data)
{
	struct _list_info *li = data;
	imapx_free_list (li);
}

static void
imapx_get_folders_free (gpointer k,
                        gpointer v,
                        gpointer d)
{
	camel_folder_info_free (v);
}

static gboolean
fetch_folders_for_pattern (CamelIMAPXStore *istore,
                           CamelIMAPXServer *server,
                           const gchar *pattern,
                           guint32 flags,
                           const gchar *ext,
                           GHashTable *table,
                           GCancellable *cancellable,
                           GError **error)
{
	GPtrArray *folders;

	folders = camel_imapx_server_list (
		server, pattern, flags, ext, cancellable, error);
	if (folders == NULL)
		return FALSE;

	add_folders_to_summary (istore, server, folders, table, (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED));

	g_ptr_array_foreach (folders, free_list, folders);
	g_ptr_array_free (folders, TRUE);

	return TRUE;
}

static GList *
get_namespaces (CamelIMAPXStore *istore)
{
	GList *namespaces = NULL;
	CamelIMAPXNamespaceList *nsl = NULL;

	/* Add code to return the namespaces from preference else all of them */
	nsl = istore->summary->namespaces;
	if (nsl->personal)
		namespaces = g_list_append (namespaces, nsl->personal);
	if (nsl->other)
		namespaces = g_list_append (namespaces, nsl->other);
	if (nsl->shared)
		namespaces = g_list_append (namespaces, nsl->shared);

	return namespaces;
}

static GHashTable *
fetch_folders_for_namespaces (CamelIMAPXStore *istore,
                              const gchar *pattern,
                              gboolean sync,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelIMAPXServer *server;
	GHashTable *folders = NULL;
	GList *namespaces = NULL, *l;

	server = camel_imapx_store_get_server (istore, NULL, cancellable, error);
	if (!server)
		return NULL;

	folders = g_hash_table_new (folder_hash, folder_eq);
	namespaces = get_namespaces (istore);

	for (l = namespaces; l != NULL; l = g_list_next (l))
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
			if (!fetch_folders_for_pattern (
				istore, server, pat, flags, list_ext,
				folders, cancellable, error)) {
				g_free (pat);
				goto exception;
			}
			if (!list_ext) {
				/* If the server doesn't support LIST-EXTENDED then we have to
				 * issue LSUB to list the subscribed folders separately */
				flags |= CAMEL_STORE_FOLDER_INFO_SUBSCRIBED;
				if (!fetch_folders_for_pattern (
					istore, server, pat, flags, NULL,
					folders, cancellable, error)) {
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
	g_list_free (namespaces);
	g_object_unref (server);
	return folders;

exception:
	g_list_free (namespaces);
	g_object_unref (server);
	g_hash_table_destroy (folders);
	return NULL;
}

static gboolean
sync_folders (CamelIMAPXStore *istore,
              const gchar *pattern,
              gboolean sync,
              GCancellable *cancellable,
              GError **error)
{
	GHashTable *folders_from_server;
	gint i, total;

	folders_from_server = fetch_folders_for_namespaces (
		istore, pattern, sync, cancellable, error);
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
			camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
			continue;
		}

		if (!pattern || !*pattern || imapx_match_pattern (camel_imapx_store_summary_namespace_find_full (istore->summary, full_name), pattern, full_name)) {
			if ((fi = g_hash_table_lookup (folders_from_server, camel_store_info_path (istore->summary, si))) != NULL) {
				if (((fi->flags ^ si->flags) & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)) {
					si->flags = (si->flags & ~CAMEL_FOLDER_SUBSCRIBED) | (fi->flags & CAMEL_FOLDER_SUBSCRIBED);
					camel_store_summary_touch ((CamelStoreSummary *) istore->summary);

					camel_store_folder_created (CAMEL_STORE (istore), fi);
					camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (istore), fi);
				}
			} else {
				gchar *dup_folder_name = g_strdup (camel_store_info_path (istore->summary, si));

				if (dup_folder_name) {
					imapx_unmark_folder_subscribed (istore,dup_folder_name, TRUE);
					imapx_delete_folder_from_cache (istore, dup_folder_name);
					g_free (dup_folder_name);
				} else {
					camel_store_summary_remove ((CamelStoreSummary *) istore->summary, si);
				}

				total--;
				i--;
			}
		}
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
	}

	g_hash_table_foreach (folders_from_server, imapx_get_folders_free, NULL);
	g_hash_table_destroy (folders_from_server);

	return TRUE;
}

static void
imapx_refresh_finfo (CamelSession *session,
                     GCancellable *cancellable,
                     CamelIMAPXStore *store,
                     GError **error)
{
	CamelService *service;
	const gchar *display_name;

	service = CAMEL_SERVICE (store);
	display_name = camel_service_get_display_name (service);

	camel_operation_push_message (
		cancellable, _("Retrieving folder list for %s"),
		display_name);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		goto exit;

	if (!camel_service_connect_sync (
		CAMEL_SERVICE (store), cancellable, error))
		goto exit;

	/* look in all namespaces */
	sync_folders (store, "", FALSE, cancellable, error);

	camel_store_summary_save (CAMEL_STORE_SUMMARY (store->summary));

exit:
	camel_operation_pop_message (cancellable);
}

static void
discover_inbox (CamelStore *store,
                GCancellable *cancellable)
{
	CamelStoreInfo *si;
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;

	si = camel_store_summary_path ((CamelStoreSummary *) istore->summary, "INBOX");
	if (si == NULL || (si->flags & CAMEL_FOLDER_SUBSCRIBED) == 0) {
		if (imapx_subscribe_folder (store, "INBOX", FALSE, cancellable, NULL) && !si)
			sync_folders (istore, "INBOX", TRUE, cancellable, NULL);

		if (si)
			camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
	}
}

static gboolean
imapx_can_refresh_folder (CamelStore *store,
                          CamelFolderInfo *info,
                          GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStoreClass *store_class;
	gboolean check_all;
	gboolean check_subscribed;
	gboolean subscribed;
	gboolean res;
	GError *local_error = NULL;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	check_all = camel_imapx_settings_get_check_all (
		CAMEL_IMAPX_SETTINGS (settings));

	check_subscribed = camel_imapx_settings_get_check_subscribed (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	subscribed = ((info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0);

	res = store_class->can_refresh_folder (store, info, &local_error) ||
		check_all || (check_subscribed && subscribed);

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

static CamelFolder *
imapx_store_get_folder_sync (CamelStore *store,
                             const gchar *folder_name,
                             CamelStoreGetFolderFlags flags,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelFolder *folder;

	folder = get_folder_offline (store, folder_name, flags, NULL);
	if (folder == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	}

	return folder;
}

static CamelFolderInfo *
imapx_store_get_folder_info_sync (CamelStore *store,
                                  const gchar *top,
                                  CamelStoreGetFolderInfoFlags flags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelFolderInfo * fi= NULL;
	CamelService *service;
	CamelSession *session;
	CamelSettings *settings;
	gboolean initial_setup = FALSE;
	gboolean use_subscriptions;
	gchar *pattern;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	if (top == NULL)
		top = "";

	g_mutex_lock (istore->get_finfo_lock);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		fi = get_folder_info_offline (store, top, flags, error);

		g_mutex_unlock (istore->get_finfo_lock);
		return fi;
	}

	if (camel_store_summary_count ((CamelStoreSummary *) istore->summary) == 0)
		initial_setup = TRUE;

	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		time_t now = time (NULL);

		if (now - istore->last_refresh_time > FINFO_REFRESH_INTERVAL) {
			istore->last_refresh_time = time (NULL);

			camel_session_submit_job (
				session, (CamelSessionCallback)
				imapx_refresh_finfo,
				g_object_ref (store),
				(GDestroyNotify) g_object_unref);
		}

		fi = get_folder_info_offline (store, top, flags, error);
		g_mutex_unlock (istore->get_finfo_lock);
		return fi;
	}

	if (!camel_service_connect_sync (
		CAMEL_SERVICE (store), cancellable, error)) {
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

		name = camel_imapx_store_summary_full_from_path (istore->summary, top);
		if (name == NULL)
			name = camel_imapx_store_summary_path_to_full (istore->summary, top, istore->dir_sep);

		i = strlen (name);
		pattern = g_alloca (i + 5);
		strcpy (pattern, name);
		g_free (name);
	} else {
		pattern = g_alloca (1);
		pattern[0] = '\0';
	}

	if (!sync_folders (istore, pattern, TRUE, cancellable, error)) {
		g_mutex_unlock (istore->get_finfo_lock);
		return NULL;
	}

	camel_store_summary_save ((CamelStoreSummary *) istore->summary);

	/* ensure the INBOX is subscribed if lsub was preferred*/
	if (initial_setup && use_subscriptions)
		discover_inbox (store, cancellable);

	fi = get_folder_info_offline (store, top, flags, error);
	g_mutex_unlock (istore->get_finfo_lock);
	return fi;
}

static CamelFolder *
imapx_store_get_junk_folder_sync (CamelStore *store,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolder *folder;
	CamelStoreClass *store_class;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);
	folder = store_class->get_junk_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		CamelService *service;
		const gchar *user_cache_dir;
		gchar *state;

		service = CAMEL_SERVICE (store);
		user_cache_dir = camel_service_get_user_cache_dir (service);

		state = g_build_filename (
			user_cache_dir, "system", "Junk.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free (state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
imapx_store_get_trash_folder_sync (CamelStore *store,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelFolder *folder;
	CamelStoreClass *store_class;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);
	folder = store_class->get_trash_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		CamelService *service;
		const gchar *user_cache_dir;
		gchar *state;

		service = CAMEL_SERVICE (store);
		user_cache_dir = camel_service_get_user_cache_dir (service);

		state = g_build_filename (
			user_cache_dir, "system", "Trash.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free (state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolderInfo *
imapx_store_create_folder_sync (CamelStore *store,
                                const gchar *parent_name,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStoreInfo *si;
	CamelIMAPXStoreNamespace *ns;
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gchar *real_name, *full_name, *parent_real;
	CamelFolderInfo *fi = NULL;
	gchar dir_sep = 0;
	gboolean success;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return NULL;
	}

	server = camel_imapx_store_get_server (istore, NULL, cancellable, error);
	if (!server)
		return NULL;

	if (!parent_name)
		parent_name = "";

	ns = camel_imapx_store_summary_namespace_find_path (istore->summary, parent_name);
	if (ns)
		dir_sep = ns->sep;

	if (!dir_sep)
		dir_sep = '/';

	if (strchr (folder_name, dir_sep)) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_PATH,
			_("The folder name \"%s\" is invalid because it contains the character \"%c\""),
			folder_name, dir_sep);
		g_object_unref (server);
		return NULL;
	}

	parent_real = camel_imapx_store_summary_full_from_path (istore->summary, parent_name);
	if (parent_real == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Unknown parent folder: %s"), parent_name);
		g_object_unref (server);
		return NULL;
	}

	si = camel_store_summary_path ((CamelStoreSummary *) istore->summary, parent_name);
	if (si && si->flags & CAMEL_STORE_INFO_FOLDER_NOINFERIORS) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("The parent folder is not allowed to contain subfolders"));
		g_object_unref (server);
		return NULL;
	}

	if (si)
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);

	real_name = camel_imapx_store_summary_path_to_full (istore->summary, folder_name, dir_sep);
	full_name = imapx_concat (istore, parent_real, real_name);
	g_free (real_name);

	success = camel_imapx_server_create_folder (
		server, full_name, cancellable, error);
	g_object_unref (server);

	if (success) {
		CamelIMAPXStoreInfo *si;

		si = camel_imapx_store_summary_add_from_full (istore->summary, full_name, dir_sep);
		camel_store_summary_save ((CamelStoreSummary *) istore->summary);
		fi = imapx_build_folder_info (istore, camel_store_info_path (istore->summary, si));
		fi->flags |= CAMEL_FOLDER_NOCHILDREN;
		camel_store_folder_created (store, fi);
	}

	g_free (full_name);
	g_free (parent_real);

	return fi;
}

static gboolean
imapx_store_delete_folder_sync (CamelStore *store,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	gboolean success;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}
	/* Use INBOX connection as the implementation would try to select inbox to ensure
	 * we are not selected on the folder being deleted */
	server = camel_imapx_store_get_server (istore, "INBOX", cancellable, error);
	if (!server)
		return FALSE;

	success = camel_imapx_server_delete_folder (
		server, folder_name, cancellable, error);
	g_object_unref (server);

	if (success)
		imapx_delete_folder_from_cache (istore, folder_name);

	return success;
}

static gboolean
imapx_store_rename_folder_sync (CamelStore *store,
                                const gchar *old,
                                const gchar *new,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	CamelIMAPXServer *server;
	CamelService *service;
	CamelSettings *settings;
	const gchar *user_cache_dir;
	gchar *oldpath, *newpath, *storage_path;
	gboolean use_subscriptions;
	gboolean success = FALSE;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (use_subscriptions)
		imapx_unsubscribe_folder (store, old, FALSE, cancellable, NULL);

	/* Use INBOX connection as the implementation would try to select inbox to ensure
	 * we are not selected on the folder being renamed */
	server = camel_imapx_store_get_server (istore, "INBOX", cancellable, error);
	if (server) {
		success = camel_imapx_server_rename_folder (
			server, old, new, cancellable, error);
		g_object_unref (server);
	}

	if (!success) {
		imapx_subscribe_folder (store, old, FALSE, cancellable, NULL);
		return FALSE;
	}

	/* rename summary, and handle broken server */
	rename_folder_info (istore, old, new);

	if (use_subscriptions)
		success = imapx_subscribe_folder (
			store, new, FALSE, cancellable, error);

	storage_path = g_build_filename (user_cache_dir, "folders", NULL);
	oldpath = imapx_path_to_physical (storage_path, old);
	newpath = imapx_path_to_physical (storage_path, new);
	g_free (storage_path);

	/* So do we care if this didn't work?  Its just a cache? */
	if (g_rename (oldpath, newpath) == -1) {
		g_warning (
			"Could not rename message cache '%s' to '%s': %s: cache reset",
			oldpath, newpath, g_strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);

	return success;
}

static gboolean
imapx_store_noop_sync (CamelStore *store,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	GList *list, *link;
	gboolean success = TRUE;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return TRUE;

	list = camel_imapx_conn_manager_get_connections (istore->con_man);

	for (link = list; link != NULL; link = g_list_next (link)) {
		CamelIMAPXServer *server = CAMEL_IMAPX_SERVER (link->data);

		/* we just return last noops value, technically not correct though */
		success = camel_imapx_server_noop (server, NULL, cancellable, error);
		if (!success)
			break;
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	return success;
}

static void
imapx_migrate_to_user_cache_dir (CamelService *service)
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
imapx_store_initable_init (GInitable *initable,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelStore *store;
	CamelService *service;
	const gchar *user_cache_dir;
	gchar *summary;

	imapx_store = CAMEL_IMAPX_STORE (initable);
	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);

	store->flags |= CAMEL_STORE_USE_CACHE_DIR;
	imapx_migrate_to_user_cache_dir (service);

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	service = CAMEL_SERVICE (initable);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	imapx_store->summary = camel_imapx_store_summary_new ();

	summary = g_build_filename (user_cache_dir, ".ev-store-summary", NULL);
	camel_store_summary_set_filename ((CamelStoreSummary *) imapx_store->summary, summary);
	camel_store_summary_load ((CamelStoreSummary *) imapx_store->summary);

	g_free (summary);

	return TRUE;
}

static const gchar *
imapx_store_get_service_name (CamelNetworkService *service,
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
imapx_store_get_default_port (CamelNetworkService *service,
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
imapx_store_folder_is_subscribed (CamelSubscribable *subscribable,
                                  const gchar *folder_name)
{
	CamelIMAPXStore *istore = CAMEL_IMAPX_STORE (subscribable);
	CamelStoreInfo *si;
	gint is_subscribed = FALSE;

	if (folder_name && *folder_name == '/')
		folder_name++;

	si = camel_store_summary_path ((CamelStoreSummary *) istore->summary, folder_name);
	if (si) {
		is_subscribed = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free ((CamelStoreSummary *) istore->summary, si);
	}

	return is_subscribed;
}

static gboolean
imapx_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                   const gchar *folder_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	return imapx_subscribe_folder (
		CAMEL_STORE (subscribable),
		folder_name, TRUE, cancellable, error);
}

static gboolean
imapx_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                     const gchar *folder_name,
                                     GCancellable *cancellable,
                                     GError **error)
{
	return imapx_unsubscribe_folder (
		CAMEL_STORE (subscribable),
		folder_name, TRUE, cancellable, error);
}

static void
camel_imapx_store_class_init (CamelIMAPXStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = imapx_store_dispose;
	object_class->finalize = imapx_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_IMAPX_SETTINGS;
	service_class->get_name = imapx_get_name;
	service_class->connect_sync = imapx_connect_sync;
	service_class->disconnect_sync = imapx_disconnect_sync;
	service_class->authenticate_sync = imapx_authenticate_sync;
	service_class->query_auth_types_sync = imapx_query_auth_types_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = imapx_name_hash;
	store_class->equal_folder_name = imapx_name_equal;
	store_class->can_refresh_folder = imapx_can_refresh_folder;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->get_folder_sync = imapx_store_get_folder_sync;
	store_class->get_folder_info_sync = imapx_store_get_folder_info_sync;
	store_class->get_junk_folder_sync = imapx_store_get_junk_folder_sync;
	store_class->get_trash_folder_sync = imapx_store_get_trash_folder_sync;
	store_class->create_folder_sync = imapx_store_create_folder_sync;
	store_class->delete_folder_sync = imapx_store_delete_folder_sync;
	store_class->rename_folder_sync = imapx_store_rename_folder_sync;
	store_class->noop_sync = imapx_store_noop_sync;
}

static void
camel_imapx_store_initable_init (GInitableIface *interface)
{
	parent_initable_interface = g_type_interface_peek_parent (interface);

	interface->init = imapx_store_initable_init;
}

static void
camel_network_service_init (CamelNetworkServiceInterface *interface)
{
	interface->get_service_name = imapx_store_get_service_name;
	interface->get_default_port = imapx_store_get_default_port;
}

static void
camel_subscribable_init (CamelSubscribableInterface *interface)
{
	interface->folder_is_subscribed = imapx_store_folder_is_subscribed;
	interface->subscribe_folder_sync = imapx_store_subscribe_folder_sync;
	interface->unsubscribe_folder_sync = imapx_store_unsubscribe_folder_sync;
}

static void
camel_imapx_store_init (CamelIMAPXStore *istore)
{
	istore->get_finfo_lock = g_mutex_new ();
	istore->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	istore->dir_sep = '/';
	istore->con_man = camel_imapx_conn_manager_new (CAMEL_STORE (istore));

	imapx_utils_init ();
}

