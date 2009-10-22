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

static CamelStoreClass *parent_class = NULL;

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

static void imap_construct(CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, CamelException *ex)
{
	char *base, *summary;
	CamelIMAPXStore *store = (CamelIMAPXStore *)service;

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set(ex))
		return;

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

static GList *
imap_query_auth_types (CamelService *service, CamelException *ex)
{
	/*CamelIMAPXStore *store = CAMEL_IMAPX_STORE (service);*/
	GList *types = NULL;

        types = CAMEL_SERVICE_CLASS (parent_class)->query_auth_types (service, ex);
	if (types == NULL)
		return NULL;

#if 0
	if (connect_to_server_wrapper (service, NULL)) {
		types = g_list_concat(types, g_list_copy(store->engine->auth));
		imap_disconnect (service, TRUE, NULL);
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not connect to POP server on %s"),
				      service->url->host);
	}
#endif
	return types;
}

#if 0
static void
store_get_pass(CamelIMAPXStore *store)
{
	if (((CamelService *)store)->url->passwd == NULL) {
		char *prompt;
		CamelException ex;

		camel_exception_init(&ex);
		
		prompt = g_strdup_printf (_("%sPlease enter the IMAP password for %s@%s"),
					  store->login_error?store->login_error:"",
					  ((CamelService *)store)->url->user,
					  ((CamelService *)store)->url->host);
		((CamelService *)store)->url->passwd = camel_session_get_password(camel_service_get_session((CamelService *)store),
										  (CamelService *)store, NULL,
										  prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, &ex);
		g_free (prompt);
		if (camel_exception_is_set(&ex))
			camel_exception_throw_ex(&ex);
	}
}

static struct _CamelSasl *
store_get_sasl(struct _CamelIMAPXDriver *driver, CamelIMAPXStore *store)
{
	store_get_pass(store);

	if (((CamelService *)store)->url->authmech)
		return camel_sasl_new("imap", ((CamelService *)store)->url->authmech, (CamelService *)store);

	return NULL;
}

static void
store_get_login(struct _CamelIMAPXDriver *driver, char **login, char **pass, CamelIMAPXStore *store)
{
	store_get_pass(store);
	
	*login = g_strdup(((CamelService *)store)->url->user);
	*pass = g_strdup(((CamelService *)store)->url->passwd);
}
#endif

static gboolean
imap_connect (CamelService *service, CamelException *ex)
{
	CamelIMAPXStore *store = (CamelIMAPXStore *)service;

	/* We never really are 'connected' or 'disconnected' */
	if (store->server == NULL)
		store->server = camel_imapx_server_new((CamelStore *)store, service->url);

	camel_imapx_server_connect(store->server, 1);

	return TRUE;
}

static gboolean
imap_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelIMAPXStore *store = CAMEL_IMAPX_STORE (service);

	CAMEL_SERVICE_CLASS (parent_class)->disconnect (service, clean, ex);

	if (store->server)
		camel_imapx_server_connect(store->server, 0);
	
	return TRUE;
}

static CamelFolder *
imap_get_trash (CamelStore *store, CamelException *ex)
{
	/* no-op */
	return NULL;
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
		new_folder = camel_imapx_folder_new (store, folder_dir, folder_name);
		g_free(folder_dir);

		camel_store_summary_info_free((CamelStoreSummary *)imapx_store->summary, si);
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				      _("No such folder %s"), folder_name);
	}

	return new_folder;
}

static CamelFolder *
imap_get_folder (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)store;
	CamelFolder *folder;

	folder = get_folder_offline(store, folder_name, flags, ex);
	if (folder == NULL) {
		camel_exception_setv(ex, 2, "No such folder: %s", folder_name);
		return NULL;
	}

	return folder;
}

static CamelFolder *
imap_get_inbox(CamelStore *store, CamelException *ex)
{
	camel_exception_setv(ex, 1, "get_inbox::unimplemented");

	return NULL;
}

static CamelFolderInfo *
folders_build_info(CamelURL *base, struct _list_info *li)
{
	char *path, *full_name, *name;
	CamelFolderInfo *fi;

	full_name = imapx_list_get_path(li);
	name = strrchr(full_name, '/');
	if (name)
		name++;
	else
		name = full_name;

	path = alloca(strlen(full_name)+2);
	sprintf(path, "/%s", full_name);
	camel_url_set_path(base, path);

	fi = g_malloc0(sizeof(*fi));
	fi->uri = camel_url_to_string(base, CAMEL_URL_HIDE_ALL);
	fi->name = g_strdup(name);
	fi->full_name = full_name;
	fi->unread = -1;
	fi->total = -1;
	fi->flags = li->flags;

	if (!g_ascii_strcasecmp(fi->full_name, "inbox"))
		fi->flags |= CAMEL_FOLDER_SYSTEM;

	/* TODO: could look up count here ... */
	/* ?? */
	/*folder = camel_object_bag_get(store->folders, "INBOX");*/

	return fi;
}

/*
  a
  a/b
  a/b/c
  a/d
  b
  c/d

*/

/* note, pname is the raw name, not the folderinfo name */
/* note also this free's as we go, since we never go 'backwards' */
static CamelFolderInfo *
folders_build_rec(CamelURL *base, GPtrArray *folders, int *ip, CamelFolderInfo *pfi, char *pname)
{
	int plen = 0;
	CamelFolderInfo *last = NULL, *first = NULL;

	if (pfi)
		plen = strlen(pname);

	for(;(*ip)<(int)folders->len;) {
		CamelFolderInfo *fi;
		struct _list_info *li;

		li = folders->pdata[*ip];
		printf("checking '%s' is child of '%s'\n", li->name, pname);

		/* is this a child of the parent? */
		if (pfi != NULL
		    && (strncmp(pname, li->name, strlen(pname)) != 0
			|| li->name[plen] != li->separator)) {
			printf("  nope\n");
			break;
		}
		printf("  yep\n");

		/* is this not an immediate child of the parent? */
#if 0
		char *p;
		if (pfi != NULL
		    && li->separator != 0
		    && (p = strchr(li->name + plen + 1, li->separator)) != NULL) {
			if (last == NULL) {
				struct _list_info tli;

				tli.flags = CAMEL_FOLDER_NOSELECT|CAMEL_FOLDER_CHILDREN;
				tli.separator = li->separator;
				tli.name = g_strndup(li->name, p-li->name+1);
				fi = folders_build_info(base, &tli);
				fi->parent = pfi;
				if (pfi && pfi->child == NULL)
					pfi->child = fi;
				i = folders_build_rec(folders, i, fi, tli.name);
				break;
			}
		}
#endif

		fi = folders_build_info(base, li);
		fi->parent = pfi;
		if (last != NULL)
			last->next = fi;
		last = fi;
		if (first == NULL)
			first = fi;

		(*ip)++;
		fi->child = folders_build_rec(base, folders, ip, fi, li->name);
		imap_free_list(li);
	}

	return first;
}

static void
folder_info_dump(CamelFolderInfo *fi, int depth)
{
	char *s;

	s = alloca(depth+1);
	memset(s, ' ', depth);
	s[depth] = 0;
	while (fi) {
		printf("%s%s (%s)\n", s, fi->name, fi->uri);
		if (fi->child)
			folder_info_dump(fi->child, depth+2);
		fi = fi->next;
	}
	
}

// THIS IS VERY CRAP AND VERY TEMPORARY
// we re-lookup the table, after we just looked it up
// in the first place to create the list, to get the counts.
static void
folder_info_fill(CamelStore *store, CamelFolderInfo *fi)
{
//	CamelView *view;

	while (fi) {
/*		view = camel_view_summary_get(store->view_summary, fi->full_name);
		if (view) {
			fi->total = view->total_count;
			fi->unread = view->unread_count;
			camel_view_unref(view); */
//		}
		if (fi->child)
			folder_info_fill(store, fi->child);
		fi = fi->next;
	}
}


/* folder_name is path name */
static CamelFolderInfo *
imapx_build_folder_info (CamelIMAPXStore *imap_store, const gchar *folder_name)
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

static gint 
imap_match_pattern(gchar dir_sep, const gchar *pattern, const gchar *name)
{
	gchar p, n;

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
		if (imapx_store->namespace && imapx_store->namespace[0]) {
			name = g_strdup(imapx_store->summary->namespace->full_name);
			top = imapx_store->summary->namespace->path;
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

		if (si == NULL)
			continue;

		full_name = camel_imapx_store_info_full_name (imapx_store->summary, si);
		if (!full_name || !*full_name) {
			camel_store_summary_info_free ((CamelStoreSummary *)imapx_store->summary, si);
			continue;
		}

		if ((!strcmp(name, full_name)
		     || imap_match_pattern(imapx_store->dir_sep, pattern, full_name)
		     || (include_inbox && !g_ascii_strcasecmp (full_name, "INBOX")))
//		    && ((imapx_store->parameters & IMAP_PARAM_SUBSCRIPTIONS) == 0
			&&((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) == 0
			|| (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED))) {

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
			if (!g_ascii_strcasecmp(fi->full_name, "inbox"))
				fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK) | CAMEL_FOLDER_TYPE_INBOX;

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

static CamelFolderInfo *
imap_get_folder_info(CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)store;
	CamelFolderInfo * fi= NULL;
	GPtrArray *folders = NULL;
	CamelURL *base;
	CamelIterator *iter;
	int i;

	if (istore->server == NULL) {
		camel_service_connect((CamelService *)store, ex);
		if (camel_exception_is_set(ex))
			return NULL;
	}

	if (top == NULL)
		top = "";

	if (folders == NULL) {
		folders = camel_imapx_server_list(istore->server, "", flags, ex);
		for (i=0;!camel_exception_is_set(ex) && i<folders->len;i++) {
			struct _list_info *li = folders->pdata[i];
			char *name;

			name = imapx_list_get_path(li);
			g_free(name);
		}
	}

	i = 0;
	base = camel_url_copy(((CamelService *)store)->url);
	fi = folders_build_rec(base, folders, &i, NULL, NULL);
	camel_url_free(base);

	g_ptr_array_free(folders, TRUE);

	// FIXME: temporary
	folder_info_fill(store, fi);

	return fi;
}

static void
imap_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{
	camel_exception_setv(ex, 1, "delete_folder::unimplemented");
}

static void
imap_rename_folder(CamelStore *store, const char *old, const char *new, CamelException *ex)
{
	camel_exception_setv(ex, 1, "rename_folder::unimplemented");
}

static CamelFolderInfo *
imap_create_folder(CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex)
{
	camel_exception_setv(ex, 1, "create_folder::unimplemented");
	return NULL;
}

/* ********************************************************************** */
#if 0
static int store_resp_fetch(CamelIMAPXEngine *ie, guint32 id, void *data)
{
	struct _fetch_info *finfo;
	CamelIMAPXStore *istore = data;
	CamelMessageInfo *info;
	struct _pending_fetch *pending;

	finfo = imap_parse_fetch(ie->stream);
	if (istore->selected) {
		if ((finfo->got & FETCH_UID) == 0) {
			printf("didn't get uid in fetch response?\n");
		} else {
			info = camel_folder_summary_index(((CamelFolder *)istore->selected)->summary, id-1);
			/* exists, check/update */
			if (info) {
				if (strcmp(finfo->uid, camel_message_info_uid(info)) != 0) {
					printf("summary at index %d has uid %s expected %s\n", id, camel_message_info_uid(info), finfo->uid);
					/* uid mismatch???  try do it based on uid instead? try to reorder?  i dont know? */
					camel_message_info_free(info);
					info = camel_folder_summary_uid(((CamelFolder *)istore->selected)->summary, finfo->uid);
				}
			}

			if (info) {
				if (finfo->got & (FETCH_FLAGS)) {
					printf("updating flags for uid '%s'\n", finfo->uid);
					info->flags = finfo->flags;
					camel_folder_change_info_change_uid(istore->selected->changes, finfo->uid);
				}
				if (finfo->got & FETCH_MINFO) {
					printf("got envelope unexpectedly?\n");
				}
				/* other things go here, like body fetches */
			} else {
				pending = g_hash_table_lookup(istore->pending_fetch_table, finfo->uid);

				/* we need to create a new info, we only care about flags and minfo */

				if (pending)
					info = pending->info;
				else {
					info = camel_folder_summary_info_new(((CamelFolder *)istore->selected)->summary);
					camel_message_info_set_uid(info, g_strdup(finfo->uid));
				}

				if (finfo->got & FETCH_FLAGS)
					info->flags = finfo->flags;

				if (finfo->got & FETCH_MINFO) {
					/* if we only use ENVELOPE? */
					camel_message_info_set_subject(info, g_strdup(camel_message_info_subject(finfo->minfo)));
					camel_message_info_set_from(info, g_strdup(camel_message_info_from(finfo->minfo)));
					camel_message_info_set_to(info, g_strdup(camel_message_info_to(finfo->minfo)));
					camel_message_info_set_cc(info, g_strdup(camel_message_info_cc(finfo->minfo)));
					info->date_sent = finfo->minfo->date_sent;
					camel_folder_summary_add(((CamelFolder *)istore->selected)->summary, info);
					camel_folder_change_info_add_uid(istore->selected->changes, finfo->uid);
					if (pending) {
						e_dlist_remove((EDListNode *)pending);
						g_hash_table_remove(istore->pending_fetch_table, finfo->uid);
						/*e_memchunk_free(istore->pending_fetch_chunks, pending);*/
					}
				} else if (finfo->got & FETCH_HEADER) {
					/* if we only use HEADER? */
					CamelMimeParser *mp;

					if (pending == NULL)
						camel_message_info_free(info);
					mp = camel_mime_parser_new();
					camel_mime_parser_init_with_stream(mp, finfo->header);
					info = camel_folder_summary_info_new_from_parser(((CamelFolder *)istore->selected)->summary, mp);
					camel_object_unref(mp);
					camel_message_info_set_uid(info, g_strdup(finfo->uid));

					camel_folder_summary_add(((CamelFolder *)istore->selected)->summary, info);
					camel_folder_change_info_add_uid(istore->selected->changes, finfo->uid);
					if (pending) {
						/* FIXME: use a dlist */
						e_dlist_remove((EDListNode *)pending);
						g_hash_table_remove(istore->pending_fetch_table, camel_message_info_uid(pending->info));
						camel_message_info_free(pending->info);
						/*e_memchunk_free(istore->pending_fetch_chunks, pending);*/
					}
				} else if (finfo->got & FETCH_FLAGS) {
					if (pending == NULL) {
						pending = e_memchunk_alloc(istore->pending_fetch_chunks);
						pending->info = info;
						g_hash_table_insert(istore->pending_fetch_table, (char *)camel_message_info_uid(info), pending);
						e_dlist_addtail(&istore->pending_fetch_list, (EDListNode *)pending);
					}
				} else {
					if (pending == NULL)
						camel_message_info_free(info);
					printf("got unexpected fetch response?\n");
					imap_dump_fetch(finfo);
				}
			}
		}
	} else {
		printf("unexpected fetch response, no folder selected?\n");
	}
	/*imap_dump_fetch(finfo);*/
	imap_free_fetch(finfo);

	return camel_imapx_engine_skip(ie);
}
#endif

/* ********************************************************************** */

/* should be moved to imapx-utils?
   stuff in imapx-utils should be moved to imapx-parse? */

/* ********************************************************************** */

#if 0
void
camel_imapx_store_folder_selected(CamelIMAPXStore *store, CamelIMAPXFolder *folder, CamelIMAPXSelectResponse *select)
{
	CamelIMAPXCommand * volatile ic = NULL;
	CamelIMAPXStore *istore = (CamelIMAPXStore *)store;
	int i;
	struct _uidset_state ss;
	GPtrArray *fetch;
	CamelMessageInfo *info;
	struct _pending_fetch *fw, *fn;

	printf("imap folder selected\n");

	if (select->uidvalidity == folder->uidvalidity
	    && select->exists == folder->exists
	    && select->recent == folder->recent
	    && select->unseen == folder->unseen) {
		/* no work to do? */
		return;
	}

	istore->pending_fetch_table = g_hash_table_new(g_str_hash, g_str_equal);
	istore->pending_fetch_chunks = e_memchunk_new(256, sizeof(struct _pending_fetch));

	/* perform an update - flags first (and see what we have) */
	CAMEL_TRY {
		ic = camel_imapx_engine_command_new(istore->engine, "FETCH", NULL, "FETCH 1:%d (UID FLAGS)", select->exists);
		camel_imapx_engine_command_queue(istore->engine, ic);
		while (camel_imapx_engine_iterate(istore->engine, ic) > 0)
			;

		if (ic->status->result != IMAP_OK)
			camel_exception_throw(1, "fetch failed: %s", ic->status->text);

		/* pending_fetch_list now contains any new messages */
		/* FIXME: how do we work out no-longer present messages? */
		printf("now fetching info for messages?\n");
		uidset_init(&ss, store->engine);
		ic = camel_imapx_engine_command_new(istore->engine, "FETCH", NULL, "UID FETCH ");
		fw = (struct _pending_fetch *)istore->pending_fetch_list.head;
		fn = fw->next;
		while (fn) {
			info = fw->info;
			/* if the uid set fills, then flush the command out */
			if (uidset_add(&ss, ic, camel_message_info_uid(info))
			    || (fn->next == NULL && uidset_done(&ss, ic))) {
				camel_imapx_engine_command_add(istore->engine, ic, " (FLAGS RFC822.HEADER)");
				camel_imapx_engine_command_queue(istore->engine, ic);
				while (camel_imapx_engine_iterate(istore->engine, ic) > 0)
					;
				if (ic->status->result != IMAP_OK)
					camel_exception_throw(1, "fetch failed: %s", ic->status->text);
				/* if not end ... */
				camel_imapx_engine_command_free(istore->engine, ic);
				ic = camel_imapx_engine_command_new(istore->engine, "FETCH", NULL, "UID FETCH ");
			}
			fw = fn;
			fn = fn->next;
		}

		printf("The pending list should now be empty: %s\n", e_dlist_empty(&istore->pending_fetch_list)?"TRUE":"FALSE");
		for (i=0;i<10;i++) {
			info = camel_folder_summary_index(((CamelFolder *)istore->selected)->summary, i);
			if (info) {
				printf("message info [%d] =\n", i);
				camel_message_info_dump(info);
				camel_message_info_free(info);
			}
		}
	} CAMEL_CATCH (e) {
		/* FIXME: cleanup */
		camel_exception_throw_ex(e);
	} CAMEL_DONE;

	g_hash_table_destroy(istore->pending_fetch_table);
	istore->pending_fetch_table = NULL;
	e_memchunk_destroy(istore->pending_fetch_chunks);

	camel_imapx_engine_command_free(istore->engine, ic);
}
#endif

#if 0
/*char *uids[] = {"1", "2", "4", "5", "6", "7", "9", "11", "12", 0};*/
/*char *uids[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", 0};*/
char *uids[] = {"1", "3", "5", "7", "9", "11", "12", "13", "14", "15", "20", "21", "24", "25", "26", 0};

void
uidset_test(CamelIMAPXEngine *ie)
{
	struct _uidset_state ss;
	CamelIMAPXCommand *ic;
	int i;

	/*ic = camel_imapx_engine_command_new(ie, 0, "FETCH", NULL, "FETCH ");*/
	uidset_init(&ss, 0, 0);
	for (i=0;uids[i];i++) {
		if (uidset_add(&ss, uids[i])) {
			printf("\n[%d] flushing uids\n", i);
		}
	}

	if (uidset_done(&ss)) {
		printf("\nflushing uids\n");
	}
}
#endif

static void
camel_imapx_store_class_init(CamelIMAPXStoreClass *klass)
{
	CamelServiceClass *camel_service_class = CAMEL_SERVICE_CLASS(klass);
	CamelStoreClass *camel_store_class = CAMEL_STORE_CLASS(klass);

	parent_class = CAMEL_STORE_CLASS(camel_type_get_global_classfuncs(camel_store_get_type()));
	
	camel_service_class->construct = imap_construct;
	camel_service_class->query_auth_types = imap_query_auth_types;
	camel_service_class->connect = imap_connect;
	camel_service_class->disconnect = imap_disconnect;

	camel_store_class->get_trash = imap_get_trash;
	camel_store_class->get_folder = imap_get_folder;
	camel_store_class->get_inbox = imap_get_inbox;

	camel_store_class->create_folder = imap_create_folder;
	camel_store_class->rename_folder = imap_rename_folder;
	camel_store_class->delete_folder = imap_delete_folder;
	camel_store_class->get_folder_info = imap_get_folder_info;

	((CamelStoreClass *)klass)->hash_folder_name = imapx_name_hash;
	((CamelStoreClass *)klass)->compare_folder_name = imapx_name_equal;
}

static void
camel_imapx_store_init (gpointer object, gpointer klass)
{
	/*CamelIMAPXStore *istore = object;*/
}

static void
imapx_store_finalise(CamelObject *object)
{
	CamelIMAPXStore *imap_store = CAMEL_IMAPX_STORE (object);

	/* force disconnect so we dont have it run later, after we've cleaned up some stuff */
	/* SIGH */

	camel_service_disconnect((CamelService *)imap_store, TRUE, NULL);
}

CamelType
camel_imapx_store_get_type (void)
{
	static CamelType camel_imapx_store_type = CAMEL_INVALID_TYPE;

	if (!camel_imapx_store_type) {
		camel_imapx_store_type = camel_type_register(CAMEL_STORE_TYPE,
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
