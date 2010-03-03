/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.c : class for a imap store */

/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2000-2002 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "camel/camel-operation.h"

#include "camel/camel-stream-buffer.h"
#include "camel/camel-session.h"
#include "camel/camel-exception.h"
#include "camel/camel-url.h"
#include "camel/camel-sasl.h"
#include "camel/camel-data-cache.h"
#include "camel/camel-tcp-stream.h"
#include "camel/camel-tcp-stream-raw.h"
#ifdef HAVE_SSL
#include "camel/camel-tcp-stream-ssl.h"
#endif
#include "camel/camel-i18n.h"

#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-exception.h"
#include "camel-imapx-utils.h"
#include "camel-imapx-server.h"
#include "camel-imapx-summary.h"
#include "camel-net-utils.h"

/* Specified in RFC 2060 section 2.1 */
#define IMAP_PORT 143

static CamelOfflineStoreClass *parent_class = NULL;

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
}
	
static void 
imapx_construct(CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex)
{
	gchar *summary;
	CamelIMAPXStore *store = (CamelIMAPXStore *)service;

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set(ex))
		return;

	store->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
								   CAMEL_URL_HIDE_PARAMS |
								   CAMEL_URL_HIDE_AUTH));
	imapx_parse_receiving_options (store, service->url);

	store->summary = camel_imapx_store_summary_new();
	store->storage_path = camel_session_get_storage_path(session, service, ex);
	if (store->storage_path) {
		summary = g_build_filename(store->storage_path, ".ev-store-summary", NULL);
		camel_store_summary_set_filename((CamelStoreSummary *)store->summary, summary);
		/* FIXME: need to remove params, passwords, etc */
		camel_store_summary_set_uri_base((CamelStoreSummary *)store->summary, service->url);
		camel_store_summary_load((CamelStoreSummary *)store->summary);
	}
}

extern CamelServiceAuthType camel_imapx_password_authtype;
extern CamelServiceAuthType camel_imapx_apop_authtype;

/* TODO implement */
static GList *
imapx_query_auth_types (CamelService *service, CamelException *ex)
{
	/*CamelIMAPXStore *store = CAMEL_IMAPX_STORE (service);*/
	GList *types = NULL;

        types = CAMEL_SERVICE_CLASS (parent_class)->query_auth_types (service, ex);
	if (types == NULL)
		return NULL;

	return types;
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

static gboolean
imapx_connect (CamelService *service, CamelException *ex)
{
	CamelIMAPXStore *store = (CamelIMAPXStore *)service;

	/* We never really are 'connected' or 'disconnected' */
	if (store->server == NULL)
		store->server = camel_imapx_server_new((CamelStore *)store, service->url);

	return camel_imapx_server_connect(store->server, 1);
}

static gboolean
imapx_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelIMAPXStore *store = CAMEL_IMAPX_STORE (service);

	CAMEL_SERVICE_CLASS (parent_class)->disconnect (service, clean, ex);

	if (store->server)
		camel_imapx_server_connect(store->server, 0);

	return TRUE;
}

static CamelFolder *
imapx_get_junk(CamelStore *store, CamelException *ex)
{
	CamelFolder *folder = CAMEL_STORE_CLASS(parent_class)->get_junk(store, ex);

	if (folder) {
		gchar *state = g_build_filename(((CamelIMAPXStore *)store)->storage_path, "system", "Junk.cmeta", NULL);

		camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state, NULL);
		g_free(state);
		/* no defaults? */
		camel_object_state_read(folder);
	}

	return folder;
}

static CamelFolder *
imapx_get_trash (CamelStore *store, CamelException *ex)
{
	CamelFolder *folder = CAMEL_STORE_CLASS(parent_class)->get_trash(store, ex);

	if (folder) {
		gchar *state = g_build_filename(((CamelIMAPXStore *)store)->storage_path, "system", "Trash.cmeta", NULL);

		camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state, NULL);
		g_free(state);
		/* no defaults? */
		camel_object_state_read(folder);
	}

	return folder;
}

static void
imapx_noop (CamelStore *store, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) store;
	
	if (CAMEL_OFFLINE_STORE(store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;
		
	if (istore->server && camel_imapx_server_connect (istore->server, 1))
		camel_imapx_server_noop (istore->server, NULL, ex);
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
		    guint32 flags, CamelException *ex)
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
		new_folder = camel_imapx_folder_new (store, folder_dir, folder_name, ex);

		g_free(folder_dir);
		camel_store_summary_info_free((CamelStoreSummary *)imapx_store->summary, si);
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				      _("No such folder %s"), folder_name);
	}

	return new_folder;
}

static CamelFolder *
imapx_get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	folder = get_folder_offline(store, folder_name, flags, ex);
	if (folder == NULL) {
		camel_exception_setv(ex, 2, "No such folder: %s", folder_name);
		return NULL;
	}

	return folder;
}

static CamelFolder *
imapx_get_inbox(CamelStore *store, CamelException *ex)
{
	camel_exception_setv(ex, 1, "get_inbox::unimplemented");

	return NULL;
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

	folder = camel_object_bag_peek(store->folders, fi->full_name);
	if (folder) {
		CamelIMAPXSummary *ims;

		if (folder->summary)
			ims = (CamelIMAPXSummary *) folder->summary;
		else
			ims = (CamelIMAPXSummary *) camel_imapx_summary_new (folder, NULL);

		fi->unread = ((CamelFolderSummary *)ims)->unread_count;
		fi->total = ((CamelFolderSummary *)ims)->saved_count;

		if (!folder->summary)
			camel_object_unref (ims);
		camel_object_unref(folder);
	}
}

/* imap needs to treat inbox case insensitive */
/* we'll assume the names are normalised already */
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

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store, const gchar *top,
			 guint32 flags, CamelException *ex)
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
add_folders_to_summary (CamelIMAPXStore *istore, GPtrArray *folders, GHashTable *table, gboolean subcribed)
{
	gint i = 0;

	for (i = 0; i < folders->len; i++) {
		struct _list_info *li = folders->pdata[i];
		CamelIMAPXStoreInfo *si;
		guint32 new_flags;
		CamelFolderInfo *fi, *hfi;
		gchar *path;
		CamelURL *url;

		si = camel_imapx_store_summary_add_from_full (istore->summary, li->name, li->separator);
		if (!si)
			continue;

		new_flags = (si->info.flags & (CAMEL_STORE_INFO_FOLDER_SUBSCRIBED | CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW)) |
						(li->flags & ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED);

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

		hfi = g_hash_table_lookup (table, fi->full_name);
		if (hfi) {
			if (subcribed)
				hfi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			
			camel_folder_info_free (fi);
			continue;
		}

		/* HACK: some servers report noinferiors for all folders (uw-imapd)
		   We just translate this into nochildren, and let the imap layer enforce
		   it.  See create folder */
		if (li->flags & CAMEL_FOLDER_NOINFERIORS)
			li->flags = (li->flags & ~CAMEL_FOLDER_NOINFERIORS) | CAMEL_FOLDER_NOCHILDREN;
		fi->flags = li->flags;

		if (subcribed)
			fi->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;

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

static void
fetch_folders_for_pattern (CamelIMAPXStore *istore, const gchar *pattern, guint32 flags, GHashTable *table, CamelException *ex)
{
	GPtrArray *folders = NULL;

	folders = camel_imapx_server_list (istore->server, pattern, flags, ex);
	add_folders_to_summary (istore, folders, table, (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED));

	g_ptr_array_foreach (folders, free_list, folders);
	g_ptr_array_free (folders, TRUE);
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
fetch_folders_for_namespaces (CamelIMAPXStore *istore, const gchar *pattern, CamelException *ex)
{
	GHashTable *folders = NULL;
	GSList *namespaces = NULL, *l;

	folders = g_hash_table_new (folder_hash, folder_eq);
	namespaces = get_namespaces (istore);

	for (l = namespaces; l != NULL; l = g_slist_next (l))
	{
		CamelIMAPXStoreNamespace *ns = l->data;

		while (ns) {
			guint32 flags = 0;
			gchar *pat = NULL;

			if (!pattern) {
				if (!*ns->path)
					pat = g_strdup ("");
				else
					pat = g_strdup_printf ("%s%c", ns->path, ns->sep);
			} else
				pat = g_strdup (pattern);

			flags |= CAMEL_STORE_FOLDER_INFO_RECURSIVE;
			fetch_folders_for_pattern (istore, pat, flags, folders, ex);
			if (camel_exception_is_set (ex)) {
				g_free (pat);
				goto exception;
			}
			
			flags |= CAMEL_STORE_FOLDER_INFO_SUBSCRIBED;
			
			fetch_folders_for_pattern (istore, pat, flags, folders, ex);
			if (camel_exception_is_set (ex)) {
				g_free (pat);
				goto exception;
			}

			g_free (pat);

			if (pattern)
				return folders;

			ns = ns->next;
		}
	}

	return folders;

exception:
	g_hash_table_destroy (folders);
	return NULL;
}

static void
sync_folders (CamelIMAPXStore *istore, const gchar *pattern, CamelException *ex)
{
	GHashTable *folders_from_server;
	gint i, total;

	folders_from_server = fetch_folders_for_namespaces (istore, pattern, ex);
	if (camel_exception_is_set (ex))
		return;

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

					camel_object_trigger_event (CAMEL_OBJECT (istore), "folder_created", fi);
					camel_object_trigger_event (CAMEL_OBJECT (istore), "folder_subscribed", fi);
				}
			} else {
				gchar *dup_folder_name = g_strdup (camel_store_info_path (istore->summary, si));

				if (dup_folder_name) {
					CamelException eex;

					camel_exception_init (&eex);

					/* Delete the folder from cache */

					g_free (dup_folder_name);
					camel_exception_clear (&eex);
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
}

static CamelFolderInfo *
imapx_get_folder_info(CamelStore *store, const gchar *top, guint32 flags, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)store;
	CamelFolderInfo * fi= NULL;

	if (top == NULL)
		top = "";

	if (CAMEL_OFFLINE_STORE(store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		fi = get_folder_info_offline (store, top, flags, ex);
		return fi;
	}

	if (!camel_service_connect((CamelService *)store, ex))
		return NULL;

	sync_folders (istore, top, ex);
	camel_store_summary_save((CamelStoreSummary *) istore->summary);

	fi = get_folder_info_offline (store, top, flags, ex);
	return fi;
}

static void
imapx_delete_folder(CamelStore *store, const gchar *folder_name, CamelException *ex)
{
	camel_exception_setv(ex, 1, "delete_folder::unimplemented");
}

static void
imapx_rename_folder(CamelStore *store, const gchar *old, const gchar *new, CamelException *ex)
{
	camel_exception_setv(ex, 1, "rename_folder::unimplemented");
}

static CamelFolderInfo *
imapx_create_folder(CamelStore *store, const gchar *parent_name, const gchar *folder_name, CamelException *ex)
{
	camel_exception_setv(ex, 1, "create_folder::unimplemented");
	return NULL;
}

static gboolean
imapx_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, CamelException *ex)
{
	gboolean res;

	res = CAMEL_STORE_CLASS(parent_class)->can_refresh_folder (store, info, ex) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_all") != NULL) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_lsub") != NULL && (info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0);

	if (!res && !camel_exception_is_set (ex) && CAMEL_IS_IMAP_STORE (store)) {
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

	return res;
}

static gboolean
imapx_folder_subscribed (CamelStore *store, const gchar *folder_name)
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
camel_imapx_store_class_init(CamelIMAPXStoreClass *klass)
{
	CamelServiceClass *camel_service_class = CAMEL_SERVICE_CLASS(klass);
	CamelStoreClass *camel_store_class = CAMEL_STORE_CLASS(klass);

	parent_class = CAMEL_OFFLINE_STORE_CLASS (camel_type_get_global_classfuncs (camel_offline_store_get_type ()));

	camel_service_class->construct = imapx_construct;
	camel_service_class->query_auth_types = imapx_query_auth_types;
	camel_service_class->get_name = imapx_get_name;
	camel_service_class->connect = imapx_connect;
	camel_service_class->disconnect = imapx_disconnect;

	camel_store_class->get_trash = imapx_get_trash;
	camel_store_class->get_junk = imapx_get_junk;
	camel_store_class->noop = imapx_noop;
	camel_store_class->get_folder = imapx_get_folder;
	camel_store_class->get_inbox = imapx_get_inbox;
	camel_store_class->hash_folder_name = imapx_hash_folder_name;
	camel_store_class->compare_folder_name = imapx_compare_folder_name;

	camel_store_class->can_refresh_folder = imapx_can_refresh_folder;
	camel_store_class->create_folder = imapx_create_folder;
	camel_store_class->rename_folder = imapx_rename_folder;
	camel_store_class->delete_folder = imapx_delete_folder;
	camel_store_class->get_folder_info = imapx_get_folder_info;
	camel_store_class->folder_subscribed = imapx_folder_subscribed;
	camel_store_class->free_folder_info = camel_store_free_folder_info_full;

	((CamelStoreClass *)klass)->hash_folder_name = imapx_name_hash;
	((CamelStoreClass *)klass)->compare_folder_name = imapx_name_equal;
}

static void
camel_imapx_store_init (gpointer object, gpointer klass)
{
	CamelStore *store = (CamelStore *) object;

	store->flags |= CAMEL_STORE_ASYNC;
}

static void
imapx_store_finalise(CamelObject *object)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (object);

	/* force disconnect so we dont have it run later, after we've cleaned up some stuff */
	/* SIGH */

	camel_service_disconnect((CamelService *)imapx_store, TRUE, NULL);

	if (imapx_store->base_url)
		g_free (imapx_store->base_url);
}

CamelType
camel_imapx_store_get_type (void)
{
	static CamelType camel_imapx_store_type = CAMEL_INVALID_TYPE;

	if (!camel_imapx_store_type) {
		camel_imapx_store_type = camel_type_register(camel_offline_store_get_type (),
							    "CamelIMAPXStore",
							    sizeof (CamelIMAPXStore),
							    sizeof (CamelIMAPXStoreClass),
							    (CamelObjectClassInitFunc) camel_imapx_store_class_init,
							    NULL,
							    (CamelObjectInitFunc) camel_imapx_store_init,
							     imapx_store_finalise);
	}

	return camel_imapx_store_type;
}
