/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* ExchangeHierarchyWebDAV: class for a normal WebDAV folder hierarchy
 * in the Exchange storage. Eg, the "Personal Folders" and "Public
 * Folders" hierarchies.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "libedataserverui/e-passwords.h"
#include "libedataserver/e-source-list.h"

#include "exchange-hierarchy-webdav.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-context.h"
#include "e2k-path.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "exchange-folder-size.h"
#include "exchange-esource.h"

#define d(x)

#define URI_ENCODE_CHARS "@;:/?=."

struct _ExchangeHierarchyWebDAVPrivate {
	GHashTable *folders_by_internal_path;
	gboolean deep_searchable;
	gchar *trash_path;
	gdouble total_folder_size;
};

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY
static ExchangeHierarchyClass *parent_class = NULL;

static void folder_type_map_init (void);

static void dispose (GObject *object);
static void finalize (GObject *object);
static gboolean is_empty (ExchangeHierarchy *hier);
static void rescan (ExchangeHierarchy *hier);
static ExchangeAccountFolderResult scan_subtree  (ExchangeHierarchy *hier,
						  EFolder *folder,
						  gint mode);
static ExchangeAccountFolderResult create_folder (ExchangeHierarchy *hier,
						  EFolder *parent,
						  const gchar *name,
						  const gchar *type);
static ExchangeAccountFolderResult remove_folder (ExchangeHierarchy *hier,
						  EFolder *folder);
static ExchangeAccountFolderResult xfer_folder   (ExchangeHierarchy *hier,
						  EFolder *source,
						  EFolder *dest_parent,
						  const gchar *dest_name,
						  gboolean remove_source);

static void hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
				  gpointer user_data);
static void hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
				      gpointer user_data);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *exchange_hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	folder_type_map_init ();

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	exchange_hierarchy_class->is_empty = is_empty;
	exchange_hierarchy_class->rescan = rescan;
	exchange_hierarchy_class->scan_subtree = scan_subtree;
	exchange_hierarchy_class->create_folder = create_folder;
	exchange_hierarchy_class->remove_folder = remove_folder;
	exchange_hierarchy_class->xfer_folder = xfer_folder;
}

static void
init (GObject *object)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (object);

	hwd->priv = g_new0 (ExchangeHierarchyWebDAVPrivate, 1);
	hwd->priv->folders_by_internal_path = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_object_unref);
	hwd->priv->total_folder_size = 0;

	g_signal_connect (object, "new_folder",
			  G_CALLBACK (hierarchy_new_folder), NULL);
	g_signal_connect (object, "removed_folder",
			  G_CALLBACK (hierarchy_removed_folder), NULL);
}

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (object);

	g_hash_table_destroy (hwd->priv->folders_by_internal_path);
	g_free (hwd->priv->trash_path);
	g_free (hwd->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_webdav, ExchangeHierarchyWebDAV, class_init, init, PARENT_TYPE)

typedef struct {
	const gchar *contentclass, *component;
	gboolean offline_supported;
} ExchangeFolderType;

static ExchangeFolderType folder_types[] = {
	{ "IPF.Note", "mail", FALSE },
	{ "IPF.Contact", "contacts", FALSE },
	{ "IPF.Appointment", "calendar", FALSE },
	{ "IPF.Task", "tasks", FALSE },
	{ NULL, NULL }
};
static GHashTable *folder_type_map;

static void
folder_type_map_init (void)
{
	gint i;

	folder_type_map = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; folder_types[i].contentclass; i++) {
		g_hash_table_insert (folder_type_map,
				     (gpointer) folder_types[i].contentclass,
				     &folder_types[i]);
	}
}

/* We maintain the folders_by_internal_path hash table by listening
 * to our own signal emissions. (This lets ExchangeHierarchyForeign
 * remove its folders by just calling exchange_hierarchy_removed_folder.)
 */
static void
hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
		      gpointer user_data)
{
	const gchar *internal_uri;
	gchar *mf_path;

	g_return_if_fail (E_IS_FOLDER (folder));
	internal_uri = e_folder_exchange_get_internal_uri (folder);

	/* This should ideally not be needed. But, this causes a problem when the
	server has identical folder names [ internal_uri ] for folders. Very much
	possible in the case of favorite folders */
	if (g_hash_table_lookup (EXCHANGE_HIERARCHY_WEBDAV (hier)->priv->folders_by_internal_path,
				(gchar *)e2k_uri_path (internal_uri)))
		return;

	g_hash_table_insert (EXCHANGE_HIERARCHY_WEBDAV (hier)->priv->folders_by_internal_path,
			     (gchar *)e2k_uri_path (internal_uri), g_object_ref (folder));

	mf_path = e_folder_exchange_get_storage_file (folder, "connector-metadata.xml");
	e_folder_exchange_save_to_file (folder, mf_path);
	g_free (mf_path);
}

static void
hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
			  gpointer user_data)
{
	const gchar *internal_uri = e_folder_exchange_get_internal_uri (folder);
	gchar *mf_path;

	g_hash_table_remove (EXCHANGE_HIERARCHY_WEBDAV (hier)->priv->folders_by_internal_path,
			     (gchar *)e2k_uri_path (internal_uri));

	mf_path = e_folder_exchange_get_storage_file (folder, "connector-metadata.xml");
	g_unlink (mf_path);
	g_free (mf_path);

	e_path_rmdir (hier->account->storage_dir,
		      e_folder_exchange_get_path (folder));
}

static gboolean
is_empty (ExchangeHierarchy *hier)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);

	/* 1, not 0, because there will always be an entry for toplevel */
	return g_hash_table_size (hwd->priv->folders_by_internal_path) == 1;
}

static EFolder *
e_folder_webdav_new (ExchangeHierarchy *hier, const gchar *internal_uri,
		     EFolder *parent, const gchar *name, const gchar *type,
		     const gchar *outlook_class, gint unread,
		     gboolean offline_supported)
{
	EFolder *folder;
	gchar *real_type, *http_uri, *physical_uri, *fixed_name = NULL;

	d( g_print ("exchange-hierarchy-webdave.c:e_folder_webdave_new: internal_uri=[%s], name=[%s], type=[%s], class=[%s]\n",
		    internal_uri, name, type, outlook_class));

	if (hier->type == EXCHANGE_HIERARCHY_PUBLIC &&
	    !strstr (type, "/public"))
		real_type = g_strdup_printf ("%s/public", type);
	else if (hier->type == EXCHANGE_HIERARCHY_FOREIGN &&
		 !strcmp (type, "calendar"))
		real_type = g_strdup ("calendar/public"); /* Hack */
	else
		real_type = g_strdup (type);

	fixed_name = e2k_uri_encode (name, FALSE, URI_ENCODE_CHARS);
	physical_uri = e2k_uri_concat (e_folder_get_physical_uri (parent), fixed_name);
	g_free (fixed_name);

	if (internal_uri) {
		folder = e_folder_exchange_new (hier, name,
						real_type, outlook_class,
						physical_uri, internal_uri);
	} else {
		gchar *temp_name;
		gchar *encoded_name = NULL;
		const gchar *new_internal_uri;
		gint len;

		len = strlen (name);

		/* appending "/" here, so that hash table lookup in rescan() succeeds */
		if (name[len-1] != '/') {
			encoded_name = e2k_uri_encode (name, FALSE, URI_ENCODE_CHARS);
		} else {
			temp_name = g_strndup (name, len-1);
			encoded_name = e2k_uri_encode (temp_name, FALSE, URI_ENCODE_CHARS);
			g_free (temp_name);
		}
		temp_name = g_strdup_printf ("%s/", encoded_name);
		g_free (encoded_name);

		new_internal_uri = e_folder_exchange_get_internal_uri (parent);
		http_uri = e2k_uri_concat (new_internal_uri, temp_name);
		d(g_print ("exchange-hierarchy-webdave.c:e_folder_webdave_new: http_uri=[%s], new_internal_uri=[%s], temp_name=[%s], name[%s]\n",
			   http_uri, new_internal_uri, temp_name, name));
		g_free (temp_name);

		folder = e_folder_exchange_new (hier, name,
						real_type, outlook_class,
						physical_uri, http_uri);
		g_free (http_uri);
	}
	g_free (physical_uri);
	g_free (real_type);

	if (unread && hier->type != EXCHANGE_HIERARCHY_PUBLIC)
		e_folder_set_unread_count (folder, unread);
	if (offline_supported)
		e_folder_set_can_sync_offline (folder, offline_supported);

	/* FIXME: set is_stock */

	return folder;
}

static ExchangeAccountFolderResult
create_folder (ExchangeHierarchy *hier, EFolder *parent,
	       const gchar *name, const gchar *type)
{
	EFolder *dest;
	E2kProperties *props;
	E2kHTTPStatus status;
	gchar *permanent_url = NULL;
	gint i, mode;

	exchange_account_is_offline (hier->account, &mode);
        if (mode != ONLINE_MODE)
                return EXCHANGE_ACCOUNT_FOLDER_OFFLINE;

	for (i = 0; folder_types[i].component; i++) {
		if (!strcmp (folder_types[i].component, type))
			break;
	}
	if (!folder_types[i].component)
		return EXCHANGE_ACCOUNT_FOLDER_UNKNOWN_TYPE;

	dest = e_folder_webdav_new (hier, NULL, parent, name, type,
				    folder_types[i].contentclass, 0,
				    folder_types[i].offline_supported);

	props = e2k_properties_new ();
	e2k_properties_set_string (props, E2K_PR_EXCHANGE_FOLDER_CLASS,
				   g_strdup (folder_types[i].contentclass));

	status = e_folder_exchange_mkcol (dest, NULL, props,
					  &permanent_url);
	e2k_properties_free (props);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		e_folder_exchange_set_permanent_uri (dest, permanent_url);
		g_free (permanent_url);
		exchange_hierarchy_new_folder (hier, dest);
		g_object_unref (dest);

		/* update the folder size table, new folder, initialize the size to 0 */
		exchange_account_folder_size_add (hier->account, name, 0);
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	}

	g_object_unref (dest);
	if (status == E2K_HTTP_METHOD_NOT_ALLOWED)
		return EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS;
	else if (status == E2K_HTTP_CONFLICT)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	else if (status == E2K_HTTP_FORBIDDEN)
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
	else
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
}

static ExchangeAccountFolderResult
remove_folder (ExchangeHierarchy *hier, EFolder *folder)
{
	E2kHTTPStatus status;
	gint mode;

        exchange_account_is_offline (hier->account, &mode);

        if (mode != ONLINE_MODE)
                return EXCHANGE_ACCOUNT_FOLDER_OFFLINE;

	if (folder == hier->toplevel)
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;

	status = e_folder_exchange_delete (folder, NULL);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		exchange_hierarchy_removed_folder (hier, folder);

		/* update the folder size info */
		exchange_account_folder_size_remove (hier->account,
					e_folder_get_name(folder));
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	} else
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
}

static ExchangeAccountFolderResult
xfer_folder (ExchangeHierarchy *hier, EFolder *source,
	     EFolder *dest_parent, const gchar *dest_name,
	     gboolean remove_source)
{
	E2kHTTPStatus status;
	EFolder *dest;
	gchar *permanent_url = NULL, *physical_uri, *source_parent;
	const gchar *folder_type = NULL, *source_folder_name;
	ExchangeAccountFolderResult ret_code;
	gint mode;

	exchange_account_is_offline (hier->account, &mode);
        if (mode != ONLINE_MODE)
                return EXCHANGE_ACCOUNT_FOLDER_OFFLINE;

	if (source == hier->toplevel)
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;

	dest = e_folder_webdav_new (hier, NULL, dest_parent, dest_name,
				    e_folder_get_type_string (source),
				    e_folder_exchange_get_outlook_class (source),
				    e_folder_get_unread_count (source),
				    e_folder_get_can_sync_offline (source));

	status = e_folder_exchange_transfer_dir (source, NULL, dest,
						 remove_source,
						 &permanent_url);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		folder_type = e_folder_get_type_string (source);
		if (permanent_url)
			e_folder_exchange_set_permanent_uri (dest, permanent_url);
		if (remove_source)
			exchange_hierarchy_removed_folder (hier, source);
		exchange_hierarchy_new_folder (hier, dest);
		scan_subtree (hier, dest, mode);
		physical_uri = g_strdup (e_folder_get_physical_uri (source));
		ret_code = EXCHANGE_ACCOUNT_FOLDER_OK;

		/* Find if folder movement or rename.
		 * update folder size in case of rename.
		 */

		source_folder_name = strrchr (physical_uri, '/') + 1;
		source_parent = g_strndup (physical_uri,
					   source_folder_name - physical_uri);
		if (!strcmp (e_folder_get_physical_uri (dest_parent), source_parent)) {
			/* rename - remove folder entry from hash, and
			 * update the hash table with new name
			 */
			exchange_account_folder_size_rename (hier->account,
				source_folder_name+1, dest_name);
		}
		g_free (source_parent);
	} else {
		physical_uri = e2k_uri_concat (
				e_folder_get_physical_uri (dest_parent),
				dest_name);
		g_object_unref (dest);
		if (status == E2K_HTTP_FORBIDDEN ||
		    status == E2K_HTTP_UNAUTHORIZED)
			ret_code = EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
		else
			ret_code = EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
	}

	/* Remove the ESource of the source folder, in case of rename/move */
	if ((hier->type == EXCHANGE_HIERARCHY_PERSONAL ||
	     hier->type == EXCHANGE_HIERARCHY_FAVORITES) && remove_source &&
	     ret_code == EXCHANGE_ACCOUNT_FOLDER_OK) {

		if ((strcmp (folder_type, "calendar") == 0) ||
		    (strcmp (folder_type, "calendar/public") == 0)) {
			remove_folder_esource (hier->account,
					       EXCHANGE_CALENDAR_FOLDER,
					       physical_uri);
		}
		else if ((strcmp (folder_type, "tasks") == 0) ||
			 (strcmp (folder_type, "tasks/public") == 0)) {
			remove_folder_esource (hier->account,
					       EXCHANGE_TASKS_FOLDER,
					       physical_uri);
		}
		else if ((strcmp (folder_type, "contacts") == 0) ||
			 (strcmp (folder_type, "contacts/public") == 0)) {
			remove_folder_esource (hier->account,
					       EXCHANGE_CONTACTS_FOLDER,
					       physical_uri);
		}
	}
	if (physical_uri)
		g_free (physical_uri);
	return ret_code;
}

static void
add_href (gpointer path, gpointer folder, gpointer hrefs)
{
	const gchar *folder_type;

	folder_type = e_folder_get_type_string (folder);

	if (!folder_type)
		return;

	if (!strcmp (folder_type, "noselect"))
		return;

	g_ptr_array_add (hrefs, path);
}

/* E2K_PR_EXCHANGE_FOLDER_SIZE also can be used for reading folder size */
static const gchar *rescan_props[] = {
	E2K_PR_EXCHANGE_FOLDER_SIZE,
	E2K_PR_HTTPMAIL_UNREAD_COUNT
};
static const gint n_rescan_props = sizeof (rescan_props) / sizeof (rescan_props[0]);

static void
rescan (ExchangeHierarchy *hier)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	const gchar *prop = E2K_PR_HTTPMAIL_UNREAD_COUNT;
	const gchar *folder_size, *folder_name;
	GPtrArray *hrefs;
	E2kResultIter *iter;
	E2kResult *result;
	EFolder *folder;
	gint unread, mode;
	gboolean personal = ( hier->type == EXCHANGE_HIERARCHY_PERSONAL );
	gdouble fsize_d;

	exchange_account_is_offline (hier->account, &mode);
	if ( (mode != ONLINE_MODE) ||
		hier->type == EXCHANGE_HIERARCHY_PUBLIC)
		return;

	hrefs = g_ptr_array_new ();
	g_hash_table_foreach (hwd->priv->folders_by_internal_path,
			      add_href, hrefs);
	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		return;
	}

	g_object_ref (hier);
	iter = e_folder_exchange_bpropfind_start (hier->toplevel, NULL,
						  (const gchar **)hrefs->pdata,
						  hrefs->len,
						  rescan_props, n_rescan_props);
	g_ptr_array_free (hrefs, TRUE);

	while ((result = e2k_result_iter_next (iter))) {
		folder = g_hash_table_lookup (hwd->priv->folders_by_internal_path,
					      e2k_uri_path (result->href));
		if (!folder)
			continue;

		prop = e2k_properties_get_prop (result->props,
						E2K_PR_HTTPMAIL_UNREAD_COUNT);
		if (!prop)
			continue;
		unread = atoi (prop);

		if (unread != e_folder_get_unread_count (folder))
			e_folder_set_unread_count (folder, unread);

		folder_size = e2k_properties_get_prop (result->props,
				E2K_PR_EXCHANGE_FOLDER_SIZE);

		if (folder_size) {
			folder_name = e_folder_get_name (folder);
			fsize_d = g_ascii_strtod (folder_size, NULL)/1024;
			exchange_account_folder_size_add (hier->account,
							folder_name, fsize_d);
			if (personal)
				hwd->priv->total_folder_size =
					hwd->priv->total_folder_size + fsize_d;
		}
	}
	e2k_result_iter_free (iter);
	g_object_unref (hier);
}

ExchangeAccountFolderResult
exchange_hierarchy_webdav_status_to_folder_result (E2kHTTPStatus status)
{
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	else if (status == E2K_HTTP_NOT_FOUND)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	else if (status == E2K_HTTP_UNAUTHORIZED)
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
	else
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
}

gdouble
exchange_hierarchy_webdav_get_total_folder_size (ExchangeHierarchyWebDAV *hwd)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY_WEBDAV (hwd), -1);

	return hwd->priv->total_folder_size;
}

EFolder *
exchange_hierarchy_webdav_parse_folder (ExchangeHierarchyWebDAV *hwd,
					EFolder *parent,
					E2kResult *result)
{
	EFolder *folder;
	ExchangeFolderType *folder_type;
	const gchar *name, *prop, *outlook_class, *permanenturl;
	gint unread;
	gboolean hassubs;

	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY_WEBDAV (hwd), NULL);
	g_return_val_if_fail (E_IS_FOLDER (parent), NULL);

	/* It's possible to have a localized inbox folder named, eg,
	 * "Innboks", with children whose URIs go through "Inbox"
	 * instead. (See bugzilla 27065.) This is probably related to
	 * the IMAP "INBOX" convention. Anyway, the important bit is
	 * that you can't know a folder's parent URI just by looking
	 * at its own URI. Since we only ever scan one folder at a
	 * time here, we just keep track of what the parent was. If we
	 * were going to read multiple folders at once, we could deal
	 * with this by fetching DAV:parentname.
	 */

	name = e2k_properties_get_prop (result->props,
					E2K_PR_DAV_DISPLAY_NAME);
	if (!name)
		return NULL;

	prop = e2k_properties_get_prop (result->props,
					E2K_PR_HTTPMAIL_UNREAD_COUNT);
	unread = prop ? atoi (prop) : 0;
	prop = e2k_properties_get_prop (result->props,
					E2K_PR_DAV_HAS_SUBS);
	hassubs = prop && atoi (prop);

	outlook_class = e2k_properties_get_prop (result->props,
						 E2K_PR_EXCHANGE_FOLDER_CLASS);
	folder_type = NULL;
	if (outlook_class)
		folder_type = g_hash_table_lookup (folder_type_map, outlook_class);
	if (!folder_type)
		folder_type = &folder_types[0]; /* mail */
	if (!outlook_class)
		outlook_class = folder_type->contentclass;

	/*
	 * The permanenturl Field provides a unique identifier for an item
	 * across the *store* and will not change as long as the item remains
	 * in the same folder. The permanenturl Field contains the ID of the
	 * parent folder of the item, which changes when the item is moved to a
	 * different folder or deleted. Changing a field on an item will not
	 * change the permanenturl Field and neither will adding more items to
	 * the folder with the same display name or message subject.
	 */
	permanenturl = e2k_properties_get_prop (result->props,
						E2K_PR_EXCHANGE_PERMANENTURL);
	/* Check for errors */

	folder = e_folder_webdav_new (EXCHANGE_HIERARCHY (hwd),
				      result->href, parent,
				      name, folder_type->component,
				      outlook_class, unread,
				      folder_type->offline_supported);
	if (hwd->priv->trash_path && !strcmp (e2k_uri_path (result->href), hwd->priv->trash_path))
		e_folder_set_custom_icon (folder, "stock_delete");
	if (hassubs)
		e_folder_exchange_set_has_subfolders (folder, TRUE);
	if (permanenturl) {
		/* Favorite folders and subscribed folders will not have
		 * permanenturl
		 */
		e_folder_exchange_set_permanent_uri (folder, permanenturl);
	}

	return folder;
}

static void
add_folders (ExchangeHierarchy *hier, EFolder *folder, gpointer folders)
{
	g_object_ref (folder);
	g_ptr_array_add (folders, folder);
}

static const gchar *folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_HTTPMAIL_UNREAD_COUNT,
	E2K_PR_DAV_DISPLAY_NAME,
	E2K_PR_EXCHANGE_PERMANENTURL,
	E2K_PR_EXCHANGE_FOLDER_SIZE,
	E2K_PR_DAV_HAS_SUBS
};
static const gint n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static const gchar *pub_folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_DAV_DISPLAY_NAME,
	E2K_PR_EXCHANGE_PERMANENTURL,
	E2K_PR_DAV_HAS_SUBS
};
static const gint n_pub_folder_props = sizeof (pub_folder_props) / sizeof (pub_folder_props[0]);

static ExchangeAccountFolderResult
scan_subtree (ExchangeHierarchy *hier, EFolder *parent, gint mode)
{
	static E2kRestriction *folders_rn;
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	GSList *subtrees = NULL;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	EFolder *folder, *tmp;
	GPtrArray *folders;
	gint i;
	gdouble fsize_d;
	const gchar *name, *folder_size, *deleted_items_uri, *int_uri;
	gboolean personal = ( EXCHANGE_HIERARCHY (hwd)->type == EXCHANGE_HIERARCHY_PERSONAL );

	if (parent) {
		if (!e_folder_exchange_get_rescan_tree (parent)) {
			d(g_print ("%s(%d):%s: Donot RESCAN [%s] \n", __FILE__, __LINE__, __PRETTY_FUNCTION__,
				   e_folder_get_name (parent)));
			return EXCHANGE_ACCOUNT_FOLDER_OK;
		}
	}

	if (mode == OFFLINE_MODE) {
		folders = g_ptr_array_new ();
		exchange_hierarchy_webdav_offline_scan_subtree (EXCHANGE_HIERARCHY (hier), add_folders, folders);
		for (i = 0; i <folders->len; i++) {
			tmp = (EFolder *)folders->pdata[i];
			exchange_hierarchy_new_folder (hier, (EFolder *)folders->pdata[i]);
		}
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	}

	if (!folders_rn) {
		folders_rn =
			e2k_restriction_andv (
				e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
							   E2K_RELOP_EQ, TRUE),
				e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
							   E2K_RELOP_EQ, FALSE),
				NULL);
	}

	if (hier->type == EXCHANGE_HIERARCHY_PUBLIC)
		iter = e_folder_exchange_search_start (parent, NULL,
						       pub_folder_props,
						       n_pub_folder_props,
						       folders_rn, NULL, TRUE);
	else
		iter = e_folder_exchange_search_start (parent, NULL,
						       folder_props, n_folder_props,
						       folders_rn, NULL, TRUE);

	while ((result = e2k_result_iter_next (iter))) {
		folder = exchange_hierarchy_webdav_parse_folder (hwd, parent, result);
		if (!folder)
			continue;

		if (hwd->priv->deep_searchable &&
		    e_folder_exchange_get_has_subfolders (folder)) {
			e_folder_exchange_set_has_subfolders (folder, FALSE);
			subtrees = g_slist_prepend (subtrees, g_object_ref (folder));
		}
		exchange_hierarchy_new_folder (hier, folder);
		g_object_unref (folder);

		/* Check the folder size here */
		if (hier->type != EXCHANGE_HIERARCHY_PUBLIC) {
			name = e2k_properties_get_prop (result->props,
							E2K_PR_DAV_DISPLAY_NAME);
			folder_size = e2k_properties_get_prop (result->props,
							       E2K_PR_EXCHANGE_FOLDER_SIZE);

			/* FIXME : Find a better way of doing this */
			fsize_d = g_ascii_strtod (folder_size, NULL)/1024;
			exchange_account_folder_size_add (hier->account, name, fsize_d);
		}

		if (personal) {
			/* calculate mail box size only for personal folders */
			hwd->priv->total_folder_size =
				hwd->priv->total_folder_size + fsize_d;
		}

	}

	status = e2k_result_iter_free (iter);

	deleted_items_uri = exchange_account_get_standard_uri (hier->account, "deleteditems");

	while (subtrees) {
		folder = subtrees->data;
		subtrees = g_slist_remove (subtrees, folder);

		/* Dont scan the subtree for deleteditems folder */
		int_uri = e_folder_exchange_get_internal_uri (folder);
		if (int_uri && deleted_items_uri && !strcmp (int_uri, deleted_items_uri)) {
			g_object_unref (folder);
			continue;
		}

		scan_subtree (hier, folder, mode);
		g_object_unref (folder);
	}

	e_folder_exchange_set_rescan_tree (parent, FALSE);

	return exchange_hierarchy_webdav_status_to_folder_result (status);
}

struct scan_offline_data {
	ExchangeHierarchy *hier;
	ExchangeHierarchyWebDAVScanCallback callback;
	gpointer user_data;
	GPtrArray *badpaths;
};

static gboolean
scan_offline_cb (const gchar *physical_path, const gchar *path, gpointer data)
{
	struct scan_offline_data *sod = data;
	EFolder *folder;
	gchar *mf_name;

	mf_name = g_build_filename (physical_path, "connector-metadata.xml", NULL);
	folder = e_folder_exchange_new_from_file (sod->hier, mf_name);
	if (!folder) {
		g_unlink (mf_name);
		g_free (mf_name);
		if (!sod->badpaths)
			sod->badpaths = g_ptr_array_new ();
		g_ptr_array_add (sod->badpaths, g_strdup (path));
		return TRUE;
	}
	g_free (mf_name);

	sod->callback (sod->hier, folder, sod->user_data);
	g_object_unref (folder);

	return TRUE;
}

/**
 * exchange_hierarchy_webdav_offline_scan_subtree:
 * @hier: a (webdav) hierarchy
 * @callbackb: a callback
 * @user_data: data for @cb
 *
 * Scans the offline folder tree cache for @hier and calls @cb
 * with each folder successfully constructed from offline data
 **/
void
exchange_hierarchy_webdav_offline_scan_subtree (ExchangeHierarchy *hier,
						ExchangeHierarchyWebDAVScanCallback callback,
						gpointer user_data)
{
	struct scan_offline_data sod;
	const gchar *path;
	gchar *dir, *prefix;
	gint i;

	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));

	sod.hier = hier;
	sod.callback = callback;
	sod.user_data = user_data;
	sod.badpaths = NULL;

	path = e_folder_exchange_get_path (hier->toplevel);
	prefix = e2k_strdup_with_trailing_slash (path);
	dir = e_path_to_physical (hier->account->storage_dir, prefix);
	g_free (prefix);
	e_path_find_folders (dir, scan_offline_cb, &sod);

	if (sod.badpaths) {
		for (i = 0; i < sod.badpaths->len; i++) {
			e_path_rmdir (dir, sod.badpaths->pdata[i]);
			g_free (sod.badpaths->pdata[i]);
		}
		g_ptr_array_free (sod.badpaths, TRUE);
	}

	g_free (dir);
}

void
exchange_hierarchy_webdav_construct (ExchangeHierarchyWebDAV *hwd,
				     ExchangeAccount *account,
				     ExchangeHierarchyType type,
				     const gchar *hierarchy_name,
				     const gchar *physical_uri_prefix,
				     const gchar *internal_uri_prefix,
				     const gchar *owner_name,
				     const gchar *owner_email,
				     const gchar *source_uri,
				     gboolean deep_searchable)
{
	EFolder *toplevel;

	g_return_if_fail (EXCHANGE_IS_HIERARCHY_WEBDAV (hwd));
	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	hwd->priv->deep_searchable = deep_searchable;

	toplevel = e_folder_exchange_new (EXCHANGE_HIERARCHY (hwd),
					  hierarchy_name,
					  "noselect", NULL,
					  physical_uri_prefix,
					  internal_uri_prefix);
	e_folder_set_custom_icon (toplevel, "stock_folder");
	e_folder_exchange_set_has_subfolders (toplevel, TRUE);
	exchange_hierarchy_construct (EXCHANGE_HIERARCHY (hwd),
				      account, type, toplevel,
				      owner_name, owner_email, source_uri);
	g_object_unref (toplevel);

	if (type == EXCHANGE_HIERARCHY_PERSONAL) {
		const gchar *trash_uri;

		trash_uri = exchange_account_get_standard_uri (account, "deleteditems");
		if (trash_uri)
			hwd->priv->trash_path = e2k_strdup_with_trailing_slash (e2k_uri_path (trash_uri));
	}
}

ExchangeHierarchy *
exchange_hierarchy_webdav_new (ExchangeAccount *account,
			       ExchangeHierarchyType type,
			       const gchar *hierarchy_name,
			       const gchar *physical_uri_prefix,
			       const gchar *internal_uri_prefix,
			       const gchar *owner_name,
			       const gchar *owner_email,
			       const gchar *source_uri,
			       gboolean deep_searchable)
{
	ExchangeHierarchy *hier;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	hier = g_object_new (EXCHANGE_TYPE_HIERARCHY_WEBDAV, NULL);

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hier),
					     account, type, hierarchy_name,
					     physical_uri_prefix,
					     internal_uri_prefix,
					     owner_name, owner_email,
					     source_uri, deep_searchable);
	return hier;
}
