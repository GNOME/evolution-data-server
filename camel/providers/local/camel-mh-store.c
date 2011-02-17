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

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-mh-folder.h"
#include "camel-mh-store.h"
#include "camel-mh-summary.h"

#define d(x)

static gboolean construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, GError **error);
static CamelFolder *get_folder(CamelStore *store, const gchar *folder_name, guint32 flags, GError **error);
static CamelFolder *get_inbox (CamelStore *store, GError **error);
static gboolean delete_folder(CamelStore *store, const gchar *folder_name, GError **error);
static gboolean rename_folder(CamelStore *store, const gchar *old, const gchar *new, GError **error);
static CamelFolderInfo * get_folder_info (CamelStore *store, const gchar *top, guint32 flags, GError **error);

G_DEFINE_TYPE (CamelMhStore, camel_mh_store, CAMEL_TYPE_LOCAL_STORE)

static void
camel_mh_store_class_init (CamelMhStoreClass *class)
{
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = construct;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder = get_folder;
	store_class->get_inbox = get_inbox;
	store_class->delete_folder = delete_folder;
	store_class->rename_folder = rename_folder;
	store_class->get_folder_info = get_folder_info;
}

static void
camel_mh_store_init (CamelMhStore *mh_store)
{
}

static gboolean
construct (CamelService *service,
           CamelSession *session,
           CamelProvider *provider,
           CamelURL *url,
           GError **error)
{
	CamelServiceClass *service_class;
	CamelMhStore *mh_store = (CamelMhStore *)service;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_mh_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	if (camel_url_get_param(url, "dotfolders"))
		mh_store->flags |= CAMEL_MH_DOTFOLDERS;

	return TRUE;
}

enum {
	UPDATE_NONE,
	UPDATE_ADD,
	UPDATE_REMOVE,
	UPDATE_RENAME
};

/* update the .folders file if it exists, or create it if it doesn't */
static void
folders_update (const gchar *root,
                gint mode,
                const gchar *folder,
                const gchar *new)
{
	gchar *tmp, *tmpnew, *line = NULL;
	CamelStream *stream, *in = NULL, *out = NULL;
	gint flen = strlen(folder);

	tmpnew = g_alloca (strlen (root) + 16);
	sprintf (tmpnew, "%s.folders~", root);

	out = camel_stream_fs_new_with_name (
		tmpnew, O_WRONLY|O_CREAT|O_TRUNC, 0666, NULL);
	if (out == NULL)
		goto fail;

	tmp = g_alloca (strlen (root) + 16);
	sprintf (tmp, "%s.folders", root);
	stream = camel_stream_fs_new_with_name (tmp, O_RDONLY, 0, NULL);
	if (stream) {
		in = camel_stream_buffer_new(stream, CAMEL_STREAM_BUFFER_READ);
		g_object_unref (stream);
	}
	if (in == NULL || stream == NULL) {
		if (mode == UPDATE_ADD && camel_stream_printf (out, "%s\n", folder) == -1)
			goto fail;
		goto done;
	}

	while ((line = camel_stream_buffer_read_line((CamelStreamBuffer *)in, NULL))) {
		gint copy = TRUE;

		switch (mode) {
		case UPDATE_REMOVE:
			if (strcmp(line, folder) == 0)
				copy = FALSE;
			break;
		case UPDATE_RENAME:
			if (strncmp(line, folder, flen) == 0
			    && (line[flen] == 0 || line[flen] == '/')) {
				if (camel_stream_write(out, new, strlen(new), NULL) == -1
				    || camel_stream_write(out, line+flen, strlen(line)-flen, NULL) == -1
				    || camel_stream_write(out, "\n", 1, NULL) == -1)
					goto fail;
				copy = FALSE;
			}
			break;
		case UPDATE_ADD: {
			gint cmp = strcmp(line, folder);

			if (cmp > 0) {
				/* found insertion point */
				if (camel_stream_printf(out, "%s\n", folder) == -1)
					goto fail;
				mode = UPDATE_NONE;
			} else if (tmp == NULL) {
				/* already there */
				mode = UPDATE_NONE;
			}
			break; }
		case UPDATE_NONE:
			break;
		}

		if (copy && camel_stream_printf(out, "%s\n", line) == -1)
			goto fail;

		g_free(line);
		line = NULL;
	}

	/* add to end? */
	if (mode == UPDATE_ADD && camel_stream_printf(out, "%s\n", folder) == -1)
		goto fail;

	if (camel_stream_close(out, NULL) == -1)
		goto fail;

done:
	/* should we care if this fails?  I suppose so ... */
	g_rename (tmpnew, tmp);
fail:
	unlink(tmpnew);		/* remove it if its there */
	g_free(line);
	if (in)
		g_object_unref (in);
	if (out)
		g_object_unref (out);
}

static CamelFolder *
get_folder (CamelStore *store,
            const gchar *folder_name,
            guint32 flags,
            GError **error)
{
	CamelStoreClass *store_class;
	gchar *name;
	struct stat st;

	/* Chain up to parent's get_folder() method. */
	store_class = CAMEL_STORE_CLASS (camel_mh_store_parent_class);
	if (store_class->get_folder (store, folder_name, flags, error) == NULL)
		return NULL;

	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);

	if (g_stat(name, &st) == -1) {
		if (errno != ENOENT) {
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Cannot get folder '%s': %s"),
				folder_name, g_strerror (errno));
			g_free (name);
			return NULL;
		}
		if ((flags & CAMEL_STORE_FOLDER_CREATE) == 0) {
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_NO_FOLDER,
				_("Cannot get folder '%s': folder does not exist."),
				folder_name);
			g_free (name);
			return NULL;
		}

		if (g_mkdir_with_parents(name, 0777) != 0) {
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Could not create folder '%s': %s"),
				folder_name, g_strerror (errno));
			g_free (name);
			return NULL;
		}

		/* add to .folders if we are supposed to */
		/* FIXME: throw exception on error */
		if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS)
			folders_update(((CamelLocalStore *)store)->toplevel_dir, UPDATE_ADD, folder_name, NULL);
	} else if (!S_ISDIR(st.st_mode)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot get folder '%s': not a directory."),
			folder_name);
		g_free (name);
		return NULL;
	} else if (flags & CAMEL_STORE_FOLDER_EXCL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder '%s': folder exists."),
			folder_name);
		g_free (name);
		return NULL;
	}

	g_free(name);

	return camel_mh_folder_new(store, folder_name, flags, error);
}

static CamelFolder *
get_inbox (CamelStore *store,
           GError **error)
{
	return get_folder (store, "inbox", 0, error);
}

static gboolean
delete_folder (CamelStore *store,
               const gchar *folder_name,
               GError **error)
{
	CamelStoreClass *store_class;
	gchar *name;

	/* remove folder directory - will fail if not empty */
	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	if (rmdir(name) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not delete folder '%s': %s"),
			folder_name, g_strerror (errno));
		g_free(name);
		return FALSE;
	}
	g_free(name);

	/* remove from .folders if we are supposed to */
	if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS)
		folders_update(((CamelLocalStore *)store)->toplevel_dir, UPDATE_REMOVE, folder_name, NULL);

	/* Chain up to parent's delete_folder() method. */
	store_class = CAMEL_STORE_CLASS (camel_mh_store_parent_class);
	return store_class->delete_folder (store, folder_name, error);
}

static gboolean
rename_folder (CamelStore *store,
               const gchar *old,
               const gchar *new,
               GError **error)
{
	CamelStoreClass *store_class;

	/* Chain up to parent's rename_folder() method. */
	store_class = CAMEL_STORE_CLASS (camel_mh_store_parent_class);
	if (!store_class->rename_folder (store, old, new, error))
		return FALSE;

	if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS) {
		/* yeah this is messy, but so is mh! */
		folders_update(((CamelLocalStore *)store)->toplevel_dir, UPDATE_RENAME, old, new);
	}

	return TRUE;
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

		/* We could: if we have no folder, and FAST isn't specified, perform a full
		   scan of all messages for their status flags.  But its probably not worth
		   it as we need to read the top of every file, i.e. very very slow */

		root = camel_local_store_get_toplevel_dir((CamelLocalStore *)store);
		path = g_strdup_printf("%s/%s.ev-summary", root, fi->full_name);
		folderpath = g_strdup_printf("%s/%s", root, fi->full_name);
		s = (CamelFolderSummary *)camel_mh_summary_new(NULL, path, folderpath, NULL);
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

static CamelFolderInfo *
folder_info_new (CamelStore *store,
                 CamelURL *url,
                 const gchar *root,
                 const gchar *path,
                 guint32 flags)
{
	/* FIXME: need to set fi->flags = CAMEL_FOLDER_NOSELECT (and possibly others) when appropriate */
	CamelFolderInfo *fi;
	gchar *base;

	base = strrchr(path, '/');

	camel_url_set_fragment (url, path);

	/* Build the folder info structure. */
	fi = camel_folder_info_new();
	fi->uri = camel_url_to_string (url, 0);
	fi->full_name = g_strdup(path);
	fi->name = g_strdup(base?base+1:path);
	fill_fi(store, fi, flags);

	d(printf("New folderinfo:\n '%s'\n '%s'\n '%s'\n", fi->full_name, fi->uri, fi->path));

	return fi;
}

/* used to find out where we've visited already */
struct _inode {
	dev_t dnode;
	ino_t inode;
};

/* Scan path, under root, for directories to add folders for.  Both
 * root and path should have a trailing "/" if they aren't empty. */
static void
recursive_scan (CamelStore *store,
                CamelURL *url,
                CamelFolderInfo **fip,
                CamelFolderInfo *parent,
                GHashTable *visited,
                const gchar *root,
                const gchar *path,
                guint32 flags)
{
	gchar *fullpath, *tmp;
	DIR *dp;
	struct dirent *d;
	struct stat st;
	CamelFolderInfo *fi;
	struct _inode in, *inew;

	/* Open the specified directory. */
	if (path[0]) {
		fullpath = alloca (strlen (root) + strlen (path) + 2);
		sprintf (fullpath, "%s/%s", root, path);
	} else
		fullpath = (gchar *)root;

	if (g_stat(fullpath, &st) == -1 || !S_ISDIR(st.st_mode))
		return;

	in.dnode = st.st_dev;
	in.inode = st.st_ino;

	/* see if we've visited already */
	if (g_hash_table_lookup(visited, &in) != NULL)
		return;

	inew = g_malloc(sizeof(*inew));
	*inew = in;
	g_hash_table_insert(visited, inew, inew);

	/* link in ... */
	fi = folder_info_new(store, url, root, path, flags);
	fi->parent = parent;
	fi->next = *fip;
	*fip = fi;

	if (((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) || parent == NULL)) {
		/* now check content for possible other directories */
		dp = opendir(fullpath);
		if (dp == NULL)
			return;

		/* Look for subdirectories to add and scan. */
		while ((d = readdir(dp)) != NULL) {
			/* Skip current and parent directory. */
			if (strcmp(d->d_name, ".") == 0
			    || strcmp(d->d_name, "..") == 0)
				continue;

			/* skip fully-numerical entries (i.e. mh messages) */
			strtoul(d->d_name, &tmp, 10);
			if (*tmp == 0)
				continue;

			/* otherwise, treat at potential node, and recurse, a bit more expensive than needed, but tough! */
			if (path[0]) {
				tmp = g_strdup_printf("%s/%s", path, d->d_name);
				recursive_scan(store, url, &fi->child, fi, visited, root, tmp, flags);
				g_free(tmp);
			} else {
				recursive_scan(store, url, &fi->child, fi, visited, root, d->d_name, flags);
			}
		}

		closedir(dp);
	}
}

/* scan a .folders file */
static void
folders_scan (CamelStore *store,
              CamelURL *url,
              const gchar *root,
              const gchar *top,
              CamelFolderInfo **fip,
              guint32 flags)
{
	CamelFolderInfo *fi;
	gchar  line[512], *path, *tmp;
	CamelStream *stream, *in;
	struct stat st;
	GPtrArray *folders;
	GHashTable *visited;
	gint len;

	tmp = g_alloca (strlen (root) + 16);
	sprintf (tmp, "%s/.folders", root);
	stream = camel_stream_fs_new_with_name(tmp, 0, O_RDONLY, NULL);
	if (stream == NULL)
		return;

	in = camel_stream_buffer_new(stream, CAMEL_STREAM_BUFFER_READ);
	g_object_unref (stream);
	if (in == NULL)
		return;

	visited = g_hash_table_new(g_str_hash, g_str_equal);
	folders = g_ptr_array_new();

	while ( (len = camel_stream_buffer_gets((CamelStreamBuffer *)in, line, sizeof(line), NULL)) > 0) {
		/* ignore blank lines */
		if (len <= 1)
			continue;
		/* check for invalidly long lines, we abort evreything and fallback */
		if (line[len-1] != '\n') {
			gint i;

			for (i=0;i<folders->len;i++)
				camel_folder_info_free(folders->pdata[i]);
			g_ptr_array_set_size(folders, 0);
			break;
		}
		line[len-1] = 0;

		/* check for \r ? */

		if (top && top[0]) {
			gint toplen = strlen(top);

			/* check is dir or subdir */
			if (strncmp(top, line, toplen) != 0
			    || (line[toplen] != 0 && line[toplen] != '/'))
				continue;

			/* check is not sub-subdir if not recursive */
			if ((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) == 0
			    && (tmp = strrchr(line, '/'))
			    && tmp > line+toplen)
				continue;
		}

		if (g_hash_table_lookup(visited, line) != NULL)
			continue;

		tmp = g_strdup(line);
		g_hash_table_insert(visited, tmp, tmp);

		path = g_strdup_printf("%s/%s", root, line);
		if (g_stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			fi = folder_info_new(store, url, root, line, flags);
			g_ptr_array_add(folders, fi);
		}
		g_free(path);
	}

	if (folders->len)
		*fip = camel_folder_info_build(folders, top, '/', TRUE);
	g_ptr_array_free(folders, TRUE);

	g_hash_table_foreach(visited, (GHFunc)g_free, NULL);
	g_hash_table_destroy(visited);

	g_object_unref (in);
}

/* FIXME: move to camel-local, this is shared with maildir code */
static guint
inode_hash (gconstpointer d)
{
	const struct _inode *v = d;

	return v->inode ^ v->dnode;
}

static gboolean
inode_equal (gconstpointer a, gconstpointer b)
{
	const struct _inode *v1 = a, *v2 = b;

	return v1->inode == v2->inode && v1->dnode == v2->dnode;
}

static void
inode_free (gpointer k, gpointer v, gpointer d)
{
	g_free(k);
}

static CamelFolderInfo *
get_folder_info (CamelStore *store,
                 const gchar *top,
                 guint32 flags,
                 GError **error)
{
	CamelFolderInfo *fi = NULL;
	CamelURL *url;
	gchar *root;

	root = ((CamelService *)store)->url->path;

	url = camel_url_copy (((CamelService *) store)->url);

	/* use .folders if we are supposed to */
	if (((CamelMhStore *)store)->flags & CAMEL_MH_DOTFOLDERS) {
		folders_scan(store, url, root, top, &fi, flags);
	} else {
		GHashTable *visited = g_hash_table_new(inode_hash, inode_equal);

		if (top == NULL)
			top = "";

		recursive_scan(store, url, &fi, NULL, visited, root, top, flags);

		/* if we actually scanned from root, we have a "" root node we dont want */
		if (fi != NULL && top[0] == 0) {
			CamelFolderInfo *rfi;

			rfi = fi;
			fi = rfi->child;
			rfi->child = NULL;
			camel_folder_info_free(rfi);
		}

		g_hash_table_foreach(visited, inode_free, NULL);
		g_hash_table_destroy(visited);
	}

	camel_url_free (url);

	return fi;
}
