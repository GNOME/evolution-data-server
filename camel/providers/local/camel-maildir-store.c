/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-maildir-folder.h"
#include "camel-maildir-store.h"
#include "camel-maildir-summary.h"

#define d(x)

static CamelFolder *get_folder(CamelStore * store, const gchar *folder_name, guint32 flags, GError **error);
static CamelFolder *get_inbox (CamelStore *store, GError **error);
static gboolean delete_folder(CamelStore * store, const gchar *folder_name, GError **error);
static gboolean maildir_rename_folder(CamelStore *store, const gchar *old, const gchar *new, GError **error);

static CamelFolderInfo * get_folder_info (CamelStore *store, const gchar *top, guint32 flags, GError **error);

static gboolean maildir_compare_folder_name(gconstpointer a, gconstpointer b);
static guint maildir_hash_folder_name(gconstpointer a);

G_DEFINE_TYPE (CamelMaildirStore, camel_maildir_store, CAMEL_TYPE_LOCAL_STORE)

static void
camel_maildir_store_class_init (CamelMaildirStoreClass *class)
{
	CamelStoreClass *store_class;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = maildir_hash_folder_name;
	store_class->compare_folder_name = maildir_compare_folder_name;
	store_class->get_folder = get_folder;
	store_class->get_inbox = get_inbox;
	store_class->delete_folder = delete_folder;
	store_class->rename_folder = maildir_rename_folder;
	store_class->get_folder_info = get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;
}

static void
camel_maildir_store_init (CamelMaildirStore *maildir_store)
{
}

/* This fixes up some historical cruft of names starting with "./" */
static const gchar *
md_canon_name (const gchar *a)
{
	if (a != NULL) {
		if (a[0] == '/')
			a++;
		if (a[0] == '.' && a[1] == '/')
			a+=2;
	}

	return a;
}

static guint
maildir_hash_folder_name (gconstpointer a)
{
	return g_str_hash (md_canon_name(a));
}

static gboolean
maildir_compare_folder_name (gconstpointer a,
                             gconstpointer b)
{
	return g_str_equal (md_canon_name (a), md_canon_name (b));
}

static CamelFolder *
get_folder (CamelStore *store,
            const gchar *folder_name,
            guint32 flags,
            GError **error)
{
	CamelStoreClass *store_class;
	gchar *name, *tmp, *cur, *new;
	struct stat st;
	CamelFolder *folder = NULL;

	folder_name = md_canon_name(folder_name);

	/* Chain up to parent's get_folder() method. */
	store_class = CAMEL_STORE_CLASS (camel_maildir_store_parent_class);
	if (!store_class->get_folder (store, folder_name, flags, error))
		return NULL;

	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	tmp = g_strdup_printf("%s/tmp", name);
	cur = g_strdup_printf("%s/cur", name);
	new = g_strdup_printf("%s/new", name);

	if (!strcmp(folder_name, ".")) {
		/* special case "." (aka inbox), may need to be created */
		if (g_stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)
		    || g_stat(cur, &st) != 0 || !S_ISDIR(st.st_mode)
		    || g_stat(new, &st) != 0 || !S_ISDIR(st.st_mode)) {
			if (g_mkdir_with_parents(tmp, 0700) != 0
			    || g_mkdir_with_parents(cur, 0700) != 0
			    || g_mkdir_with_parents(new, 0700) != 0) {
				g_set_error (
					error, G_IO_ERROR,
					g_io_error_from_errno (errno),
					_("Cannot create folder '%s': %s"),
					folder_name, g_strerror(errno));
				rmdir(tmp);
				rmdir(cur);
				rmdir(new);
				goto fail;
			}
		}
		folder = camel_maildir_folder_new(store, folder_name, flags, error);
	} else if (g_stat(name, &st) == -1) {
		/* folder doesn't exist, see if we should create it */
		if (errno != ENOENT) {
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Cannot get folder '%s': %s"),
				folder_name, g_strerror (errno));
		} else if ((flags & CAMEL_STORE_FOLDER_CREATE) == 0) {
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_NO_FOLDER,
				_("Cannot get folder '%s': folder does not exist."),
				folder_name);
		} else {
			if (g_mkdir_with_parents(name, 0700) != 0
			    || g_mkdir_with_parents(tmp, 0700) != 0
			    || g_mkdir_with_parents(cur, 0700) != 0
			    || g_mkdir_with_parents(new, 0700) != 0) {
				g_set_error (
					error, G_IO_ERROR,
					g_io_error_from_errno (errno),
					_("Cannot create folder '%s': %s"),
					folder_name, g_strerror (errno));
				rmdir(tmp);
				rmdir(cur);
				rmdir(new);
				rmdir(name);
			} else {
				folder = camel_maildir_folder_new(store, folder_name, flags, error);
			}
		}
	} else if (!S_ISDIR(st.st_mode)
		   || g_stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)
		   || g_stat(cur, &st) != 0 || !S_ISDIR(st.st_mode)
		   || g_stat(new, &st) != 0 || !S_ISDIR(st.st_mode)) {
		/* folder exists, but not maildir */
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot get folder '%s': not a maildir directory."),
			name);
	} else if (flags & CAMEL_STORE_FOLDER_EXCL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder '%s': folder exists."),
			folder_name);
	} else {
		folder = camel_maildir_folder_new(store, folder_name, flags, error);
	}
fail:
	g_free(name);
	g_free(tmp);
	g_free(cur);
	g_free(new);

	return folder;
}

static CamelFolder *
get_inbox (CamelStore *store,
           GError **error)
{
	return camel_store_get_folder (
		store, ".", CAMEL_STORE_FOLDER_CREATE, error);
}

static gboolean
delete_folder (CamelStore *store,
               const gchar *folder_name,
               GError **error)
{
	gchar *name, *tmp, *cur, *new;
	struct stat st;
	gboolean success = TRUE;

	if (strcmp(folder_name, ".") == 0) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot delete folder: %s: Invalid operation"),
			_("Inbox"));
		return FALSE;
	}

	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);

	tmp = g_strdup_printf("%s/tmp", name);
	cur = g_strdup_printf("%s/cur", name);
	new = g_strdup_printf("%s/new", name);

	if (g_stat(name, &st) == -1 || !S_ISDIR(st.st_mode)
	    || g_stat(tmp, &st) == -1 || !S_ISDIR(st.st_mode)
	    || g_stat(cur, &st) == -1 || !S_ISDIR(st.st_mode)
	    || g_stat(new, &st) == -1 || !S_ISDIR(st.st_mode)) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not delete folder '%s': %s"),
			folder_name, errno ? g_strerror (errno) :
			_("not a maildir directory"));
	} else {
		gint err = 0;

		/* remove subdirs first - will fail if not empty */
		if (rmdir(cur) == -1 || rmdir(new) == -1) {
			err = errno;
		} else {
			DIR *dir;
			struct dirent *d;

			/* for tmp (only), its contents is irrelevant */
			dir = opendir(tmp);
			if (dir) {
				while ((d=readdir(dir))) {
					gchar *name = d->d_name, *file;

					if (!strcmp(name, ".") || !strcmp(name, ".."))
						continue;
					file = g_strdup_printf("%s/%s", tmp, name);
					unlink(file);
					g_free(file);
				}
				closedir(dir);
			}
			if (rmdir(tmp) == -1 || rmdir(name) == -1)
				err = errno;
		}

		if (err != 0) {
			/* easier just to mkdir all (and let them fail), than remember what we got to */
			g_mkdir_with_parents(name, 0700);
			g_mkdir_with_parents(cur, 0700);
			g_mkdir_with_parents(new, 0700);
			g_mkdir_with_parents(tmp, 0700);
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (err),
				_("Could not delete folder '%s': %s"),
				folder_name, g_strerror (err));
		} else {
			CamelStoreClass *store_class;

			/* Chain up to parent's delete_folder() method. */
			store_class = CAMEL_STORE_CLASS (camel_maildir_store_parent_class);
			success = store_class->delete_folder (
				store, folder_name, error);
		}
	}

	g_free(name);
	g_free(tmp);
	g_free(cur);
	g_free(new);

	return success;
}

static gboolean
maildir_rename_folder (CamelStore *store,
                       const gchar *old,
                       const gchar *new,
                       GError **error)
{
	CamelStoreClass *store_class;

	if (strcmp(old, ".") == 0) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot rename folder: %s: Invalid operation"),
			_("Inbox"));
		return FALSE;
	}

	/* Chain up to parent's rename_folder() method. */
	store_class = CAMEL_STORE_CLASS (camel_maildir_store_parent_class);
	return store_class->rename_folder(store, old, new, error);
}

static void
fill_fi (CamelStore *store,
         CamelFolderInfo *fi,
         guint32 flags)
{
	CamelFolder *folder;

	folder = camel_object_bag_peek (store->folders, fi->full_name);

	if (folder == NULL
	    && (flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0)
		folder = camel_store_get_folder(store, fi->full_name, 0, NULL);

	if (folder) {
		if ((flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0)
			camel_folder_refresh_info(folder, NULL);
		fi->unread = camel_folder_get_unread_message_count(folder);
		fi->total = camel_folder_get_message_count(folder);
		g_object_unref (folder);
	} else {
		gchar *path, *folderpath;
		CamelFolderSummary *s;
		const gchar *root;

		/* This should be fast enough not to have to test for INFO_FAST */
		root = camel_local_store_get_toplevel_dir((CamelLocalStore *)store);
		path = g_strdup_printf("%s/%s.ev-summary", root, fi->full_name);
		folderpath = g_strdup_printf("%s/%s", root, fi->full_name);
		s = (CamelFolderSummary *)camel_maildir_summary_new(NULL, path, folderpath, NULL);
		if (camel_folder_summary_header_load_from_db (s, store, fi->full_name, NULL) != -1) {
			fi->unread = s->unread_count;
			fi->total = s->saved_count;
		}
		g_object_unref (s);
		g_free(folderpath);
		g_free(path);
	}

	if (camel_local_store_is_main_store (CAMEL_LOCAL_STORE (store)) && fi->full_name
	    && (fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_NORMAL)
		fi->flags = (fi->flags & ~CAMEL_FOLDER_TYPE_MASK)
			    | camel_local_store_get_folder_type_by_full_name (CAMEL_LOCAL_STORE (store), fi->full_name);
}

struct _scan_node {
	struct _scan_node *next;
	struct _scan_node *prev;

	CamelFolderInfo *fi;

	dev_t dnode;
	ino_t inode;
};

static guint
scan_hash (gconstpointer d)
{
	const struct _scan_node *v = d;

	return v->inode ^ v->dnode;
}

static gboolean
scan_equal (gconstpointer a, gconstpointer b)
{
	const struct _scan_node *v1 = a, *v2 = b;

	return v1->inode == v2->inode && v1->dnode == v2->dnode;
}

static void
scan_free (gpointer k, gpointer v, gpointer d)
{
	g_free(k);
}

static CamelFolderInfo *
scan_fi (CamelStore *store,
         guint32 flags,
         CamelURL *url,
         const gchar *full,
         const gchar *name)
{
	CamelFolderInfo *fi;
	gchar *tmp, *cur, *new;
	struct stat st;

	fi = camel_folder_info_new();
	fi->full_name = g_strdup(full);
	fi->name = g_strdup(name);
	camel_url_set_fragment(url, fi->full_name);
	fi->uri = camel_url_to_string(url, 0);

	fi->unread = -1;
	fi->total = -1;

	/* we only calculate nochildren properly if we're recursive */
	if (((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) != 0))
		fi->flags = CAMEL_FOLDER_NOCHILDREN;

	d(printf("Adding maildir info: '%s' '%s' '%s'\n", fi->name, fi->full_name, fi->uri));

	tmp = g_build_filename(url->path, fi->full_name, "tmp", NULL);
	cur = g_build_filename(url->path, fi->full_name, "cur", NULL);
	new = g_build_filename(url->path, fi->full_name, "new", NULL);

	if (!(g_stat(tmp, &st) == 0 && S_ISDIR(st.st_mode)
	      && g_stat(cur, &st) == 0 && S_ISDIR(st.st_mode)
	      && g_stat(new, &st) == 0 && S_ISDIR(st.st_mode)))
		fi->flags |= CAMEL_FOLDER_NOSELECT;

	g_free(new);
	g_free(cur);
	g_free(tmp);

	fill_fi(store, fi, flags);

	return fi;
}

static gint
scan_dirs (CamelStore *store,
           guint32 flags,
           CamelFolderInfo *topfi,
           CamelURL *url,
           GError **error)
{
	CamelDList queue = CAMEL_DLIST_INITIALISER(queue);
	struct _scan_node *sn;
	const gchar *root = ((CamelService *)store)->url->path;
	gchar *tmp;
	GHashTable *visited;
	struct stat st;
	gint res = -1;

	visited = g_hash_table_new(scan_hash, scan_equal);

	sn = g_malloc0(sizeof(*sn));
	sn->fi = topfi;
	camel_dlist_addtail(&queue, (CamelDListNode *)sn);
	g_hash_table_insert(visited, sn, sn);

	while (!camel_dlist_empty(&queue)) {
		gchar *name;
		DIR *dir;
		struct dirent *d;
		CamelFolderInfo *last;

		sn = (struct _scan_node *)camel_dlist_remhead(&queue);

		last = (CamelFolderInfo *)&sn->fi->child;

		if (!strcmp(sn->fi->full_name, "."))
			name = g_strdup(root);
		else
			name = g_build_filename(root, sn->fi->full_name, NULL);

		dir = opendir(name);
		if (dir == NULL) {
			g_free(name);
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Could not scan folder '%s': %s"),
				root, g_strerror (errno));
			goto fail;
		}

		while ((d = readdir(dir))) {
			if (strcmp(d->d_name, "tmp") == 0
			    || strcmp(d->d_name, "cur") == 0
			    || strcmp(d->d_name, "new") == 0
			    || strcmp(d->d_name, ".#evolution") == 0
			    || strcmp(d->d_name, ".") == 0
			    || strcmp(d->d_name, "..") == 0)
				continue;

			tmp = g_build_filename(name, d->d_name, NULL);
			if (g_stat(tmp, &st) == 0 && S_ISDIR(st.st_mode)) {
				struct _scan_node in;

				in.dnode = st.st_dev;
				in.inode = st.st_ino;

				/* see if we've visited already */
				if (g_hash_table_lookup(visited, &in) == NULL) {
					struct _scan_node *snew = g_malloc(sizeof(*snew));
					gchar *full;

					snew->dnode = in.dnode;
					snew->inode = in.inode;

					if (!strcmp(sn->fi->full_name, "."))
						full = g_strdup(d->d_name);
					else
						full = g_strdup_printf("%s/%s", sn->fi->full_name, d->d_name);
					snew->fi = scan_fi(store, flags, url, full, d->d_name);
					g_free(full);

					last->next =  snew->fi;
					last = snew->fi;
					snew->fi->parent = sn->fi;

					sn->fi->flags &= ~CAMEL_FOLDER_NOCHILDREN;
					sn->fi->flags |= CAMEL_FOLDER_CHILDREN;

					g_hash_table_insert(visited, snew, snew);

					if (((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) != 0))
						camel_dlist_addtail(&queue, (CamelDListNode *)snew);
				}
			}
			g_free(tmp);
		}
		closedir(dir);
		g_free (name);
	}

	res = 0;
fail:
	g_hash_table_foreach(visited, scan_free, NULL);
	g_hash_table_destroy(visited);

	return res;
}

static CamelFolderInfo *
get_folder_info (CamelStore *store,
                 const gchar *top,
                 guint32 flags,
                 GError **error)
{
	CamelFolderInfo *fi = NULL;
	CamelLocalStore *local_store = (CamelLocalStore *)store;
	CamelURL *url;

	url = camel_url_new("maildir:", NULL);
	camel_url_set_path(url, ((CamelService *)local_store)->url->path);

	if (top == NULL || top[0] == 0) {
		CamelFolderInfo *scan;

		/* create a dummy "." parent inbox, use to scan, then put back at the top level */
		fi = scan_fi(store, flags, url, ".", _("Inbox"));
		if (scan_dirs(store, flags, fi, url, error) == -1)
			goto fail;
		fi->next = fi->child;
		scan = fi->child;
		fi->child = NULL;
		while (scan) {
			scan->parent = NULL;
			scan = scan->next;
		}
		fi->flags &= ~CAMEL_FOLDER_CHILDREN;
		fi->flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_NOCHILDREN|CAMEL_FOLDER_NOINFERIORS|CAMEL_FOLDER_TYPE_INBOX;
	} else if (!strcmp(top, ".")) {
		fi = scan_fi(store, flags, url, ".", _("Inbox"));
		fi->flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_NOCHILDREN|CAMEL_FOLDER_NOINFERIORS|CAMEL_FOLDER_TYPE_INBOX;
	} else {
		const gchar *name = strrchr(top, '/');

		fi = scan_fi(store, flags, url, top, name?name+1:top);
		if (scan_dirs(store, flags, fi, url, error) == -1)
			goto fail;
	}

	camel_url_free(url);

	return fi;

fail:
	if (fi)
		camel_store_free_folder_info_full(store, fi);

	camel_url_free(url);

	return NULL;
}
