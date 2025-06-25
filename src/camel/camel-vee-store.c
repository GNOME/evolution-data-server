/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-db.h"
#include "camel-session.h"
#include "camel-string-utils.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"

#define d(x)

/* flags
 * 1 = delete (0 = add)
 * 2 = noselect
*/
#define CHANGE_ADD (0)
#define CHANGE_DELETE (1)
#define CHANGE_NOSELECT (2)

extern gint camel_application_is_exiting;

struct _CamelVeeStorePrivate {
	gboolean dummy;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelVeeStore, camel_vee_store, CAMEL_TYPE_STORE)

static gint
vee_folder_cmp (gconstpointer ap,
                gconstpointer bp)
{
	const gchar *full_name_a;
	const gchar *full_name_b;

	full_name_a = camel_folder_get_full_name (((CamelFolder **) ap)[0]);
	full_name_b = camel_folder_get_full_name (((CamelFolder **) bp)[0]);

	return g_strcmp0 (full_name_a, full_name_b);
}

static void
change_folder (CamelStore *store,
               const gchar *name,
               guint32 flags,
               gint count)
{
	CamelFolderInfo *fi;
	const gchar *tmp;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (name);
	tmp = strrchr (name, '/');
	if (tmp == NULL)
		tmp = name;
	else
		tmp++;
	fi->display_name = g_strdup (tmp);
	fi->unread = count;
	fi->flags = CAMEL_FOLDER_VIRTUAL;
	if (!(flags & CHANGE_DELETE))
		fi->flags |= CAMEL_FOLDER_NOCHILDREN;
	if (flags & CHANGE_NOSELECT)
		fi->flags |= CAMEL_FOLDER_NOSELECT;
	if (flags & CHANGE_DELETE)
		camel_store_folder_deleted (store, fi);
	else
		camel_store_folder_created (store, fi);
	camel_folder_info_free (fi);
}

static gchar *
vee_store_get_name (CamelService *service,
                    gboolean brief)
{
	return g_strdup ("Virtual Folder Store");
}

static CamelFolder *
vee_store_get_folder_sync (CamelStore *store,
                           const gchar *folder_name,
                           CamelStoreGetFolderFlags flags,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelVeeFolder *vf;
	CamelFolder *folder;
	gchar *name, *p;
	gsize name_len;

	vf = (CamelVeeFolder *) camel_vee_folder_new (store, folder_name, flags);
	if (vf && ((camel_vee_folder_get_flags (vf) & CAMEL_STORE_FOLDER_PRIVATE) == 0)) {
		const gchar *full_name;

		full_name = camel_folder_get_full_name (CAMEL_FOLDER (vf));

		/* Check that parents exist, if not, create dummy ones */
		name_len = strlen (full_name) + 1;
		name = alloca (name_len);
		g_strlcpy (name, full_name, name_len);
		p = name;
		while ( (p = strchr (p, '/'))) {
			*p = 0;

			folder = camel_object_bag_reserve (camel_store_get_folders_bag (store), name);
			if (folder == NULL) {
				/* create a dummy vFolder for this, makes get_folder_info simpler */
				folder = camel_vee_folder_new (store, name, flags);
				camel_object_bag_add (camel_store_get_folders_bag (store), name, folder);
				change_folder (store, name, CHANGE_ADD | CHANGE_NOSELECT, 0);
				/* FIXME: this sort of leaks folder, nobody owns a ref to it but us */
			} else {
				g_object_unref (folder);
			}
			*p++='/';
		}

		change_folder (store, full_name, CHANGE_ADD, camel_folder_get_message_count ((CamelFolder *) vf));
	}

	return (CamelFolder *) vf;
}

static CamelFolderInfo *
vee_store_get_folder_info_sync (CamelStore *store,
                                const gchar *top,
                                CamelStoreGetFolderInfoFlags flags,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelFolderInfo *info, *res = NULL, *tail;
	GPtrArray *folders;
	GHashTable *infos_hash;
	gint i;

	d (printf ("Get folder info '%s'\n", top ? top:"<null>"));

	folders = camel_store_dup_opened_folders (store);
	if (!folders || !folders->pdata || !folders->len) {
		if (folders)
			g_ptr_array_free (folders, TRUE);

		return NULL;
	}

	qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), vee_folder_cmp);

	infos_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < folders->len; i++) {
		CamelVeeFolder *folder = folders->pdata[i];
		const gchar *full_name;
		const gchar *display_name;
		gint add = FALSE;
		gchar *pname, *tmp;
		CamelFolderInfo *pinfo;

		full_name = camel_folder_get_full_name (CAMEL_FOLDER (folder));
		display_name = camel_folder_get_display_name (CAMEL_FOLDER (folder));

		/* check we have to include this one */
		if (top) {
			gint namelen = strlen (full_name);
			gint toplen = strlen (top);

			add = ((namelen == toplen
				&& strcmp (full_name, top) == 0)
			       || ((namelen > toplen)
				   && strncmp (full_name, top, toplen) == 0
				   && full_name[toplen] == '/'
				   && ((flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
				       || strchr (full_name + toplen + 1, '/') == NULL)));
		} else {
			add = (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
				|| strchr (full_name, '/') == NULL;
		}

		if (add) {
			gint32 unread;
			unread = camel_folder_summary_get_unread_count (
				camel_folder_get_folder_summary (CAMEL_FOLDER (folder)));

			info = camel_folder_info_new ();
			info->full_name = g_strdup (full_name);
			info->display_name = g_strdup (display_name);
			info->unread = unread;
			info->flags =
				CAMEL_FOLDER_NOCHILDREN |
				CAMEL_FOLDER_VIRTUAL;
			g_hash_table_insert (infos_hash, info->full_name, info);

			if (res == NULL)
				res = info;
		} else {
			info = NULL;
		}

		/* check for parent, if present, update flags and if adding, update parent linkage */
		pname = g_strdup (full_name);
		d (printf ("looking up parent of '%s'\n", pname));
		tmp = strrchr (pname, '/');
		if (tmp) {
			*tmp = 0;
			pinfo = g_hash_table_lookup (infos_hash, pname);
		} else
			pinfo = NULL;

		if (pinfo) {
			pinfo->flags = (pinfo->flags & ~(CAMEL_FOLDER_CHILDREN | CAMEL_FOLDER_NOCHILDREN)) | CAMEL_FOLDER_CHILDREN;
			d (printf ("updating parent flags for children '%s' %08x\n", pinfo->full_name, pinfo->flags));
			tail = pinfo->child;
			if (tail == NULL)
				pinfo->child = info;
		} else if (info != res) {
			tail = res;
		} else {
			tail = NULL;
		}

		if (info && tail) {
			while (tail->next)
				tail = tail->next;
			tail->next = info;
			info->parent = pinfo;
		}

		g_free (pname);
	}
	g_ptr_array_foreach (folders, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (folders, TRUE);
	g_hash_table_destroy (infos_hash);

	return res;
}

static CamelFolder *
vee_store_get_junk_folder_sync (CamelStore *store,
                                GCancellable *cancellable,
                                GError **error)
{
	return NULL;
}

static CamelFolder *
vee_store_get_trash_folder_sync (CamelStore *store,
                                 GCancellable *cancellable,
                                 GError **error)
{
	return NULL;
}

static gboolean
vee_store_delete_folder_sync (CamelStore *store,
                              const gchar *folder_name,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelFolder *folder;

	folder = camel_object_bag_get (camel_store_get_folders_bag (store), folder_name);
	if (folder) {
		const gchar *state_filename;

		state_filename = camel_folder_get_state_filename (folder);
		if (state_filename != NULL) {
			g_unlink (state_filename);
			camel_folder_take_state_filename (folder, NULL);
		}

		if ((camel_vee_folder_get_flags (CAMEL_VEE_FOLDER (folder)) & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
			/* what about now-empty parents?  ignore? */
			change_folder (store, folder_name, CHANGE_DELETE, -1);
		}

		g_object_unref (folder);
	} else {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot delete folder: %s: No such folder"),
			folder_name);
		return FALSE;
	}

	return TRUE;
}

static gboolean
vee_store_rename_folder_sync (CamelStore *store,
                              const gchar *old,
                              const gchar *new,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelFolder *folder, *oldfolder;
	gchar *p, *name;
	gsize name_len;

	d (printf ("vee rename folder '%s' '%s'\n", old, new));

	/* See if it exists, for vfolders, all folders are in the folders hash */
	oldfolder = camel_object_bag_get (camel_store_get_folders_bag (store), old);
	if (oldfolder == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot rename folder: %s: No such folder"), old);
		return FALSE;
	}

	/* Check that new parents exist, if not, create dummy ones */
	name_len = strlen (new) + 1;
	name = alloca (name_len);
	g_strlcpy (name, new, name_len);
	p = name;
	while ( (p = strchr (p, '/'))) {
		*p = 0;

		folder = camel_object_bag_reserve (camel_store_get_folders_bag (store), name);
		if (folder == NULL) {
			/* create a dummy vFolder for this, makes get_folder_info simpler */
			folder = camel_vee_folder_new (store, name, camel_vee_folder_get_flags (CAMEL_VEE_FOLDER (oldfolder)));
			camel_object_bag_add (camel_store_get_folders_bag (store), name, folder);
			change_folder (store, name, CHANGE_ADD | CHANGE_NOSELECT, 0);
			/* FIXME: this sort of leaks folder, nobody owns a ref to it but us */
		} else {
			g_object_unref (folder);
		}
		*p++='/';
	}

	g_object_unref (oldfolder);

	return TRUE;
}

static gboolean
vee_store_get_can_auto_save_changes (CamelStore *store)
{
	/* Let only the real folder auto-save the changes */
	return FALSE;
}

static void
camel_vee_store_class_init (CamelVeeStoreClass *class)
{
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->get_name = vee_store_get_name;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = vee_store_get_folder_sync;
	store_class->get_folder_info_sync = vee_store_get_folder_info_sync;
	store_class->get_junk_folder_sync = vee_store_get_junk_folder_sync;
	store_class->get_trash_folder_sync = vee_store_get_trash_folder_sync;
	store_class->delete_folder_sync = vee_store_delete_folder_sync;
	store_class->rename_folder_sync = vee_store_rename_folder_sync;
	store_class->get_can_auto_save_changes = vee_store_get_can_auto_save_changes;
}

static void
camel_vee_store_init (CamelVeeStore *vee_store)
{
	CamelStore *store = CAMEL_STORE (vee_store);

	vee_store->priv = camel_vee_store_get_instance_private (vee_store);

	/* we don't want a vtrash/vjunk on this one */
	camel_store_set_flags (store, camel_store_get_flags (store) & ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK));
}
