/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-db.h"
#include "camel-session.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"

#define d(x)

/* flags
   1 = delete (0 = add)
   2 = noselect
*/
#define CHANGE_ADD (0)
#define CHANGE_DELETE (1)
#define CHANGE_NOSELECT (2)

G_DEFINE_TYPE (CamelVeeStore, camel_vee_store, CAMEL_TYPE_STORE)

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
	CamelURL *url;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (name);
	tmp = strrchr (name, '/');
	if (tmp == NULL)
		tmp = name;
	else
		tmp++;
	fi->name = g_strdup (tmp);
	url = camel_url_new ("vfolder:", NULL);
	camel_url_set_path (url, ((CamelService *)store)->url->path);
	if (flags & CHANGE_NOSELECT)
		camel_url_set_param (url, "noselect", "yes");
	camel_url_set_fragment (url, name);
	fi->uri = camel_url_to_string (url, 0);
	camel_url_free (url);
	/*fi->url = g_strdup_printf ("vfolder:%s%s#%s", ((CamelService *)store)->url->path, (flags&CHANGE_NOSELECT)?";noselect=yes":"", name);*/
	fi->unread = count;
	fi->flags = CAMEL_FOLDER_VIRTUAL;
	if (!(flags & CHANGE_DELETE))
		fi->flags |= CAMEL_FOLDER_NOCHILDREN;
	if (flags & CHANGE_DELETE)
		camel_store_folder_deleted (store, fi);
	else
		camel_store_folder_created (store, fi);
	camel_folder_info_free (fi);
}

static void
cvs_free_unmatched (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
}

static void
vee_store_finalize (GObject *object)
{
	CamelVeeStore *vee_store = CAMEL_VEE_STORE (object);

	g_hash_table_foreach (vee_store->unmatched_uids, cvs_free_unmatched, NULL);
	g_hash_table_destroy (vee_store->unmatched_uids);
	g_object_unref (vee_store->folder_unmatched);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_vee_store_parent_class)->finalize (object);
}

static gboolean
vee_store_construct (CamelService *service,
                     CamelSession *session,
                     CamelProvider *provider,
                     CamelURL *url,
                     GError **error)
{
	CamelServiceClass *service_class;
	CamelStore *store;
	CamelVeeStore *vee_store;

	store = CAMEL_STORE (service);
	vee_store = CAMEL_VEE_STORE (service);

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_vee_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	/* Set up unmatched folder */
#ifndef VEE_UNMATCHED_ENABLE
	vee_store->unmatched_uids = g_hash_table_new (g_str_hash, g_str_equal);
	vee_store->folder_unmatched = g_object_new (
		CAMEL_TYPE_VEE_FOLDER,
		"full-name", CAMEL_UNMATCHED_NAME,
		"name", _("Unmatched"), "parent-store", store, NULL);
	camel_vee_folder_construct (vee_store->folder_unmatched, CAMEL_STORE_FOLDER_PRIVATE);
	camel_db_create_vfolder (store->cdb_r, _("Unmatched"), NULL);
#endif

	return TRUE;
}

static gchar *
vee_store_get_name (CamelService *service,
                    gboolean brief)
{
	return g_strdup ("Virtual Folder Store");
}

static CamelFolder *
vee_store_get_folder (CamelStore *store,
                      const gchar *folder_name,
                      guint32 flags,
                      GError **error)
{
	CamelVeeFolder *vf;
	CamelFolder *folder;
	gchar *name, *p;

	vf = (CamelVeeFolder *)camel_vee_folder_new (store, folder_name, flags);
	if (vf && ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0)) {
		const gchar *full_name;

		full_name = camel_folder_get_full_name (CAMEL_FOLDER (vf));

		/* Check that parents exist, if not, create dummy ones */
		name = alloca (strlen (full_name) + 1);
		strcpy (name, full_name);
		p = name;
		while ( (p = strchr (p, '/'))) {
			*p = 0;

			folder = camel_object_bag_reserve (store->folders, name);
			if (folder == NULL) {
				/* create a dummy vFolder for this, makes get_folder_info simpler */
				folder = camel_vee_folder_new (store, name, flags);
				camel_object_bag_add (store->folders, name, folder);
				change_folder (store, name, CHANGE_ADD|CHANGE_NOSELECT, 0);
				/* FIXME: this sort of leaks folder, nobody owns a ref to it but us */
			} else {
				g_object_unref (folder);
			}
			*p++='/';
		}

		change_folder (store, full_name, CHANGE_ADD, camel_folder_get_message_count ((CamelFolder *)vf));
	}

	return (CamelFolder *)vf;
}

static gboolean
vee_store_rename_folder (CamelStore *store,
                         const gchar *old,
                         const gchar *new,
                         GError **error)
{
	CamelFolder *folder, *oldfolder;
	gchar *p, *name;

	d (printf ("vee rename folder '%s' '%s'\n", old, new));

	if (strcmp (old, CAMEL_UNMATCHED_NAME) == 0) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot rename folder: %s: Invalid operation"), old);
		return FALSE;
	}

	/* See if it exists, for vfolders, all folders are in the folders hash */
	oldfolder = camel_object_bag_get (store->folders, old);
	if (oldfolder == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot rename folder: %s: No such folder"), old);
		return FALSE;
	}

	/* Check that new parents exist, if not, create dummy ones */
	name = alloca (strlen (new)+1);
	strcpy (name, new);
	p = name;
	while ( (p = strchr (p, '/'))) {
		*p = 0;

		folder = camel_object_bag_reserve (store->folders, name);
		if (folder == NULL) {
			/* create a dummy vFolder for this, makes get_folder_info simpler */
			folder = camel_vee_folder_new (store, name, ((CamelVeeFolder *)oldfolder)->flags);
			camel_object_bag_add (store->folders, name, folder);
			change_folder (store, name, CHANGE_ADD|CHANGE_NOSELECT, 0);
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
vee_store_delete_folder (CamelStore *store,
                         const gchar *folder_name,
                         GError **error)
{
	CamelFolder *folder;

	if (strcmp (folder_name, CAMEL_UNMATCHED_NAME) == 0) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot delete folder: %s: Invalid operation"),
			folder_name);
		return FALSE;
	}

	folder = camel_object_bag_get (store->folders, folder_name);
	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		const gchar *state_filename;

		state_filename = camel_object_get_state_filename (object);
		if (state_filename != NULL) {
			g_unlink (state_filename);
			camel_object_set_state_filename (object, NULL);
		}

		if ((((CamelVeeFolder *)folder)->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
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

static CamelFolderInfo *
vee_store_get_folder_info (CamelStore *store,
                           const gchar *top,
                           guint32 flags,
                           GError **error)
{
	CamelFolderInfo *info, *res = NULL, *tail;
	GPtrArray *folders;
	GHashTable *infos_hash;
	CamelURL *url;
	gint i;

	d (printf ("Get folder info '%s'\n", top?top:"<null>"));

	infos_hash = g_hash_table_new (g_str_hash, g_str_equal);
	folders = camel_object_bag_list (store->folders);
	qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), vee_folder_cmp);
	for (i=0;i<folders->len;i++) {
		CamelVeeFolder *folder = folders->pdata[i];
		const gchar *full_name;
		const gchar *name;
		gint add = FALSE;
		gchar *pname, *tmp;
		CamelFolderInfo *pinfo;

		name = camel_folder_get_name (CAMEL_FOLDER (folder));
		full_name = camel_folder_get_full_name (CAMEL_FOLDER (folder));

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
				       || strchr (full_name+toplen+1, '/') == NULL)));
		} else {
			add = (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
				|| strchr (full_name, '/') == NULL;
		}

		d (printf ("%sadding '%s'\n", add?"":"not ", name));

		if (add) {
			CamelStore *parent_store;

			/* ensures unread is correct */
			if ((flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0)
				camel_folder_refresh_info ((CamelFolder *)folder, NULL);

			parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (folder));

			info = camel_folder_info_new ();
			url = camel_url_new ("vfolder:", NULL);
			camel_url_set_path (url, ((CamelService *) parent_store)->url->path);
			camel_url_set_fragment (url, full_name);
			info->uri = camel_url_to_string (url, 0);
			camel_url_free (url);
			info->full_name = g_strdup (full_name);
			info->name = g_strdup (name);
			info->unread = camel_folder_get_unread_message_count ((CamelFolder *)folder);
			info->flags = CAMEL_FOLDER_NOCHILDREN|CAMEL_FOLDER_VIRTUAL;
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
			pinfo->flags = (pinfo->flags & ~(CAMEL_FOLDER_CHILDREN|CAMEL_FOLDER_NOCHILDREN))|CAMEL_FOLDER_CHILDREN;
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
		g_object_unref (folder);
	}
	g_ptr_array_free (folders, TRUE);
	g_hash_table_destroy (infos_hash);

	/* and always add UNMATCHED, if scanning from top/etc */
	/* FIXME[disk-summary] comment it out well */
	if ((top == NULL || top[0] == 0 || strncmp (top, CAMEL_UNMATCHED_NAME, strlen (CAMEL_UNMATCHED_NAME)) == 0)) {
		info = camel_folder_info_new ();
		url = camel_url_new ("vfolder:", NULL);
		camel_url_set_path (url, ((CamelService *)store)->url->path);
		camel_url_set_fragment (url, CAMEL_UNMATCHED_NAME);
		info->uri = camel_url_to_string (url, 0);
		camel_url_free (url);
		/*info->url = g_strdup_printf ("vfolder:%s#%s", ((CamelService *)store)->url->path, CAMEL_UNMATCHED_NAME);*/
		info->full_name = g_strdup (CAMEL_UNMATCHED_NAME);
		info->name = g_strdup (_("Unmatched"));
		info->unread = -1;
		info->flags = CAMEL_FOLDER_NOCHILDREN|CAMEL_FOLDER_NOINFERIORS|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VIRTUAL;

		if (res == NULL)
			res = info;
		else {
			tail = res;
			while (tail->next)
				tail = tail->next;
			tail->next = info;
		}
	}

	return res;
}

static CamelFolder *
vee_store_get_trash (CamelStore *store,
                     GError **error)
{
	return NULL;
}

static CamelFolder *
vee_store_get_junk (CamelStore *store,
                    GError **error)
{
	return NULL;
}

static void
camel_vee_store_class_init (CamelVeeStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = vee_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = vee_store_construct;
	service_class->get_name = vee_store_get_name;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder = vee_store_get_folder;
	store_class->rename_folder = vee_store_rename_folder;
	store_class->delete_folder = vee_store_delete_folder;
	store_class->get_folder_info = vee_store_get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->get_trash = vee_store_get_trash;
	store_class->get_junk = vee_store_get_junk;
}

static void
camel_vee_store_init (CamelVeeStore *vee_store)
{
	CamelStore *store = CAMEL_STORE (vee_store);

	/* we dont want a vtrash/vjunk on this one */
	store->flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
}

/**
 * camel_vee_store_new:
 *
 * Create a new #CamelVeeStore object.
 *
 * Returns: new #CamelVeeStore object
 **/
CamelVeeStore *
camel_vee_store_new (void)
{
	return g_object_new (CAMEL_TYPE_VEE_STORE, NULL);
}
