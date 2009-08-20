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

/* ExchangeAccount: Handles a single configured Connector account. This
 * is strictly a model object. ExchangeStorage handles the view.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-account.h"
#include "exchange-hierarchy-webdav.h"
#include "exchange-hierarchy-favorites.h"
#include "exchange-hierarchy-gal.h"
#include "exchange-folder-size.h"
#include "e-folder-exchange.h"
#include "e2k-autoconfig.h"
#include "e2k-kerberos.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "exchange-hierarchy-foreign.h"

/* This is an ugly hack to avoid API break */
/* Added for get_authtype */
#include "exchange-esource.h"
#include <libedataserverui/e-passwords.h>

#include <gtk/gtk.h>

#include <stdlib.h>
#include <string.h>

#define d(x)
#define ADS_UF_DONT_EXPIRE_PASSWORD 0x10000
#define ONE_HUNDRED_NANOSECOND 0.000000100
#define SECONDS_IN_DAY 86400

struct _ExchangeAccountPrivate {
	E2kContext *ctx;
	E2kGlobalCatalog *gc;
	GHashTable *standard_uris;
	ExchangeFolderSize *fsize;

	GMutex *connect_lock;
	gboolean connecting, connected;
	gint account_online;

	GPtrArray *hierarchies;
	GHashTable *hierarchies_by_folder, *foreign_hierarchies;
	ExchangeHierarchy *favorites_hierarchy;
	GHashTable *folders;
	GStaticRecMutex folders_lock;
	gchar *uri_authority, *http_uri_schema;
	gboolean uris_use_email, offline_sync;

	gchar *identity_name, *identity_email, *source_uri, *password_key;
	gchar *username, *password, *windows_domain, *nt_domain, *ad_server;
	gchar *owa_url;
	E2kAutoconfigAuthPref auth_pref;
	gint ad_limit, passwd_exp_warn_period, quota_limit;
	E2kAutoconfigGalAuthPref ad_auth;

	EAccountList *account_list;
	EAccount *account;

	GMutex *discover_data_lock;
	GList *discover_datas;
};

enum {
	CONNECTED,
	NEW_FOLDER,
	REMOVED_FOLDER,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void dispose (GObject *);
static void finalize (GObject *);
static void remove_hierarchy (ExchangeAccount *account, ExchangeHierarchy *hier);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* signals */
	signals[CONNECTED] =
		g_signal_new ("connected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, connected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      E2K_TYPE_CONTEXT);
	signals[NEW_FOLDER] =
		g_signal_new ("new_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, new_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[REMOVED_FOLDER] =
		g_signal_new ("removed_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, removed_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
init (GObject *object)
{
	ExchangeAccount *account = EXCHANGE_ACCOUNT (object);

	account->priv = g_new0 (ExchangeAccountPrivate, 1);
	account->priv->connect_lock = g_mutex_new ();
	account->priv->hierarchies = g_ptr_array_new ();
	account->priv->hierarchies_by_folder = g_hash_table_new (NULL, NULL);
	account->priv->foreign_hierarchies = g_hash_table_new (g_str_hash, g_str_equal);
	account->priv->folders = g_hash_table_new (g_str_hash, g_str_equal);
	g_static_rec_mutex_init (&account->priv->folders_lock);
	account->priv->discover_data_lock = g_mutex_new ();
	account->priv->account_online = UNSUPPORTED_MODE;
	account->priv->nt_domain = NULL;
	account->priv->fsize = exchange_folder_size_new ();
}

static void
free_name (gpointer name, gpointer value, gpointer data)
{
	g_free (name);
}

static void
free_folder (gpointer key, gpointer folder, gpointer data)
{
	g_object_unref (folder);
}

static void
dispose (GObject *object)
{
	ExchangeAccount *account = EXCHANGE_ACCOUNT (object);
	gint i;

	if (account->priv->account) {
		g_object_unref (account->priv->account);
		account->priv->account = NULL;
	}

	if (account->priv->account_list) {
		g_object_unref (account->priv->account_list);
		account->priv->account_list = NULL;
	}

	if (account->priv->ctx) {
		g_object_unref (account->priv->ctx);
		account->priv->ctx = NULL;
	}

	if (account->priv->gc) {
		g_object_unref (account->priv->gc);
		account->priv->gc = NULL;
	}

	if (account->priv->hierarchies) {
		for (i = 0; i < account->priv->hierarchies->len; i++)
			g_object_unref (account->priv->hierarchies->pdata[i]);
		g_ptr_array_free (account->priv->hierarchies, TRUE);
		account->priv->hierarchies = NULL;
	}

	if (account->priv->foreign_hierarchies) {
		g_hash_table_foreach (account->priv->foreign_hierarchies, free_name, NULL);
		g_hash_table_destroy (account->priv->foreign_hierarchies);
		account->priv->foreign_hierarchies = NULL;
	}

	g_static_rec_mutex_lock (&account->priv->folders_lock);

	if (account->priv->hierarchies_by_folder) {
		g_hash_table_destroy (account->priv->hierarchies_by_folder);
		account->priv->hierarchies_by_folder = NULL;
	}

	if (account->priv->folders) {
		g_hash_table_foreach (account->priv->folders, free_folder, NULL);
		g_hash_table_destroy (account->priv->folders);
		account->priv->folders = NULL;
	}

	g_static_rec_mutex_unlock (&account->priv->folders_lock);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
free_uri (gpointer name, gpointer uri, gpointer data)
{
	g_free (name);
	g_free (uri);
}

static void
finalize (GObject *object)
{
	ExchangeAccount *account = EXCHANGE_ACCOUNT (object);

	if (account->account_name)
		g_free (account->account_name);
	if (account->storage_dir)
		g_free (account->storage_dir);
	if (account->exchange_server)
		g_free (account->exchange_server);
	if (account->home_uri)
		g_free (account->home_uri);
	if (account->public_uri)
		g_free (account->public_uri);
	if (account->legacy_exchange_dn)
		g_free (account->legacy_exchange_dn);
	if (account->default_timezone)
		g_free (account->default_timezone);

	if (account->priv->standard_uris) {
		g_hash_table_foreach (account->priv->standard_uris,
				      free_uri, NULL);
		g_hash_table_destroy (account->priv->standard_uris);
	}

	if (account->priv->uri_authority)
		g_free (account->priv->uri_authority);
	if (account->priv->http_uri_schema)
		g_free (account->priv->http_uri_schema);

	if (account->priv->identity_name)
		g_free (account->priv->identity_name);
	if (account->priv->identity_email)
		g_free (account->priv->identity_email);
	if (account->priv->source_uri)
		g_free (account->priv->source_uri);
	if (account->priv->password_key)
		g_free (account->priv->password_key);

	if (account->priv->username)
		g_free (account->priv->username);
	if (account->priv->password) {
		memset (account->priv->password, 0,
			strlen (account->priv->password));
		g_free (account->priv->password);
	}
	if (account->priv->windows_domain)
		g_free (account->priv->windows_domain);

	if (account->priv->nt_domain)
		g_free (account->priv->nt_domain);

	if (account->priv->ad_server)
		g_free (account->priv->ad_server);

	if (account->priv->owa_url)
		g_free (account->priv->owa_url);

	if (account->priv->connect_lock)
		g_mutex_free (account->priv->connect_lock);

	if (account->priv->discover_data_lock)
		g_mutex_free (account->priv->discover_data_lock);

	g_static_rec_mutex_free (&account->priv->folders_lock);

	g_free (account->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_account, ExchangeAccount, class_init, init, PARENT_TYPE)

void
exchange_account_rescan_tree (ExchangeAccount *account)
{
	gint i;
	EFolder *toplevel;

	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	g_static_rec_mutex_lock (&account->priv->folders_lock);

	for (i = 0; i < account->priv->hierarchies->len; i++) {
		/* First include the toplevel folder of the hierarchy as well */
		toplevel = EXCHANGE_HIERARCHY (account->priv->hierarchies->pdata[i])->toplevel;

		exchange_hierarchy_scan_subtree (account->priv->hierarchies->pdata[i],
						toplevel, account->priv->account_online);
		exchange_hierarchy_rescan (account->priv->hierarchies->pdata[i]);
	}
	g_static_rec_mutex_unlock (&account->priv->folders_lock);
}

/*
 * ExchangeHierarchy folder creation/deletion/xfer notifications
 */

static void
hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
		      ExchangeAccount *account)
{
	gint table_updated = 0;
	const gchar *permanent_uri =
		e_folder_exchange_get_permanent_uri (folder);
	gchar *key;

	g_static_rec_mutex_lock (&account->priv->folders_lock);

	/* This makes the cleanup easier. We just unref it each time
	 * we find it in account->priv->folders.
	 */
	key = (gchar *) e_folder_exchange_get_path (folder);
	if (!g_hash_table_lookup (account->priv->folders, key)) {
		/* Avoid dupilcations since the user could add a folder as
		  favorite even though it is already marked as favorite */
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     key,
				     folder);
		table_updated = 1;
	}

	key = (gchar *) e_folder_get_physical_uri (folder);
	if (!g_hash_table_lookup (account->priv->folders, key)) {
		/* Avoid dupilcations since the user could add a folder as
		  favorite even though it is already marked as favorite */
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     key,
				     folder);
		table_updated = 1;
	}

	key = (gchar *) e_folder_exchange_get_internal_uri (folder);
	if (!g_hash_table_lookup (account->priv->folders, key)) {
		/* The internal_uri for public folders and favorites folder
		   is same !!! Without this check the folder value could
		   overwrite the previously added folder. */
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     key,
				     folder);
		table_updated = 1;
	}

	if (permanent_uri && (!g_hash_table_lookup (account->priv->folders,
					permanent_uri))) {
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     (gchar *)permanent_uri,
				     folder);
		table_updated = 1;
	}

	if (table_updated)
	{
		g_hash_table_insert (account->priv->hierarchies_by_folder,
					folder, hier);
		g_static_rec_mutex_unlock (&account->priv->folders_lock);

		g_signal_emit (account, signals[NEW_FOLDER], 0, folder);
	} else {
		g_static_rec_mutex_unlock (&account->priv->folders_lock);
	}
}

static void
hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
			  ExchangeAccount *account)
{
	gint unref_count = 0;

	g_static_rec_mutex_lock (&account->priv->folders_lock);
	if (!g_hash_table_lookup (account->priv->folders,
					e_folder_exchange_get_path (folder))) {
		g_static_rec_mutex_unlock (&account->priv->folders_lock);
		return;
	}

	if (g_hash_table_remove (account->priv->folders, e_folder_exchange_get_path (folder)))
		unref_count++;

	if (g_hash_table_remove (account->priv->folders, e_folder_get_physical_uri (folder)))
		unref_count++;

	/* Dont remove this for favorites, as the internal_uri is shared
		by the public folder as well */
	if (hier->type != EXCHANGE_HIERARCHY_FAVORITES) {
		if (g_hash_table_remove (account->priv->folders, e_folder_exchange_get_internal_uri (folder)))
			unref_count++;
	}

	g_hash_table_remove (account->priv->hierarchies_by_folder, folder);

	g_static_rec_mutex_unlock (&account->priv->folders_lock);
	g_signal_emit (account, signals[REMOVED_FOLDER], 0, folder);

	if (folder == hier->toplevel)
		remove_hierarchy (account, hier);

	/* unref only those we really removed */
	while (unref_count > 0) {
		g_object_unref (folder);
		unref_count--;
	}
}

static gboolean
get_folder (ExchangeAccount *account, const gchar *path,
	    EFolder **folder, ExchangeHierarchy **hier)
{
	g_static_rec_mutex_lock (&account->priv->folders_lock);
	*folder = g_hash_table_lookup (account->priv->folders, path);
	if (!*folder) {
		g_static_rec_mutex_unlock (&account->priv->folders_lock);
		return FALSE;
	}
	*hier = g_hash_table_lookup (account->priv->hierarchies_by_folder,
				     *folder);
	g_static_rec_mutex_unlock (&account->priv->folders_lock);
	if (!*hier)
		return FALSE;
	return TRUE;
}

static gboolean
get_parent_and_name (ExchangeAccount *account, const gchar **path,
		     EFolder **parent, ExchangeHierarchy **hier)
{
	gchar *name, *parent_path;

	name = strrchr (*path + 1, '/');
	if (!name)
		return FALSE;

	g_static_rec_mutex_lock (&account->priv->folders_lock);
	parent_path = g_strndup (*path, name - *path);
	*parent = g_hash_table_lookup (account->priv->folders, parent_path);
	g_free (parent_path);

	if (!*parent) {
		g_static_rec_mutex_unlock (&account->priv->folders_lock);
		return FALSE;
	}

	*hier = g_hash_table_lookup (account->priv->hierarchies_by_folder,
				     *parent);
	g_static_rec_mutex_unlock (&account->priv->folders_lock);
	if (!*hier)
		return FALSE;

	*path = name + 1;
	return TRUE;
}

ExchangeAccountFolderResult
exchange_account_create_folder (ExchangeAccount *account,
				const gchar *path, const gchar *type)
{
	ExchangeHierarchy *hier;
	EFolder *parent;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!get_parent_and_name (account, &path, &parent, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	return exchange_hierarchy_create_folder (hier, parent, path, type);
}

static gboolean
check_if_sf (gpointer key, gpointer value, gpointer user_data)
{
	gchar *sf_href = (gchar *)value;
	gchar *int_uri = (gchar *)user_data;

	if (!strcmp (sf_href, int_uri))
		return TRUE; /* Quit calling the callback */

	return FALSE; /* Continue calling the callback till end of table */
}

ExchangeAccountFolderResult
exchange_account_remove_folder (ExchangeAccount *account, const gchar *path)
{
	ExchangeHierarchy *hier;
	EFolder *folder;
	const gchar *int_uri;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	d(g_print ("exchange_account_remove_folder: path=[%s]\n", path));

	if (!get_folder (account, path, &folder, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	int_uri = e_folder_exchange_get_internal_uri (folder);

	if (g_hash_table_find (account->priv->standard_uris,
					check_if_sf, (gchar *)int_uri)) {
		return EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION;
	}

	return exchange_hierarchy_remove_folder (hier, folder);
}

ExchangeAccountFolderResult
exchange_account_xfer_folder (ExchangeAccount *account,
			      const gchar *source_path,
			      const gchar *dest_path,
			      gboolean remove_source)
{
	EFolder *source, *dest_parent;
	ExchangeHierarchy *source_hier, *dest_hier;
	const gchar *name;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!get_folder (account, source_path, &source, &source_hier) ||
	    !get_parent_and_name (account, &dest_path, &dest_parent, &dest_hier)) {
		/* Source or dest seems to not exist */
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	}
	if (source_hier != dest_hier) {
		/* Can't move something between hierarchies */
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
	}
	if (remove_source) {
		name = e_folder_get_name (source);
		if (exchange_account_get_standard_uri (account, name))
			return EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION;

	}

	return exchange_hierarchy_xfer_folder (source_hier, source,
					       dest_parent, dest_path,
					       remove_source);
}

static void
remove_hierarchy (ExchangeAccount *account, ExchangeHierarchy *hier)
{
	gint i;

	for (i = 0; i < account->priv->hierarchies->len; i++) {
		if (account->priv->hierarchies->pdata[i] == hier) {
			g_ptr_array_remove_index_fast (account->priv->hierarchies, i);
			break;
		}
	}
	g_hash_table_remove (account->priv->foreign_hierarchies,
			     hier->owner_email);
	g_signal_handlers_disconnect_by_func (hier, hierarchy_new_folder, account);
	g_signal_handlers_disconnect_by_func (hier, hierarchy_removed_folder, account);
	g_object_unref (hier);
}

static void
setup_hierarchy (ExchangeAccount *account, ExchangeHierarchy *hier)
{
	g_ptr_array_add (account->priv->hierarchies, hier);

	g_signal_connect (hier, "new_folder",
			  G_CALLBACK (hierarchy_new_folder), account);
	g_signal_connect (hier, "removed_folder",
			  G_CALLBACK (hierarchy_removed_folder), account);

	exchange_hierarchy_add_to_storage (hier);
}

static void
setup_hierarchy_foreign (ExchangeAccount *account, ExchangeHierarchy *hier)
{
	g_hash_table_insert (account->priv->foreign_hierarchies,
			     (gchar *)hier->owner_email, hier);
	setup_hierarchy (account, hier);
}

struct discover_data {
	const gchar *user, *folder_name;
	E2kOperation op;
};

static ExchangeHierarchy *
get_hierarchy_for (ExchangeAccount *account, E2kGlobalCatalogEntry *entry)
{
	ExchangeHierarchy *hier;
	gchar *hierarchy_name, *source;
	gchar *physical_uri_prefix, *internal_uri_prefix;

	hier = g_hash_table_lookup (account->priv->foreign_hierarchies,
				    entry->email);
	if (hier)
		return hier;

	/* i18n: This is the title of an "other user's folders"
	   hierarchy. Eg, "John Doe's Folders". */
	hierarchy_name = g_strdup_printf (_("%s's Folders"),
					  entry->display_name);
	source = g_strdup_printf ("exchange://%s@%s/", entry->mailbox,
				  account->exchange_server);
	physical_uri_prefix = g_strdup_printf ("exchange://%s/;%s",
					       account->priv->uri_authority,
					       entry->email);
	internal_uri_prefix = exchange_account_get_foreign_uri (account, entry,
								NULL);

	hier = exchange_hierarchy_foreign_new (account, hierarchy_name,
					       physical_uri_prefix,
					       internal_uri_prefix,
					       entry->display_name,
					       entry->email, source);
	g_free (hierarchy_name);
	g_free (physical_uri_prefix);
	g_free (internal_uri_prefix);
	g_free (source);

	setup_hierarchy_foreign (account, hier);
	return hier;
}

ExchangeAccountFolderResult
exchange_account_discover_shared_folder (ExchangeAccount *account,
					 const gchar *user,
					 const gchar *folder_name,
					 EFolder **folder)
{
	struct discover_data dd;
	ExchangeHierarchy *hier;
	gchar *email;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!account->priv->gc)
		return EXCHANGE_ACCOUNT_FOLDER_GC_NOTREACHABLE;

	email = strchr (user, '<');
	if (email)
		email = g_strndup (email + 1, strcspn (email + 1, ">"));
	else
		email = g_strdup (user);
	hier = g_hash_table_lookup (account->priv->foreign_hierarchies, email);
	if (hier) {
		g_free (email);
		return exchange_hierarchy_foreign_add_folder (hier, folder_name, folder);
	}

	dd.user = user;
	dd.folder_name = folder_name;
	e2k_operation_init (&dd.op);

	g_mutex_lock (account->priv->discover_data_lock);
	account->priv->discover_datas =
		g_list_prepend (account->priv->discover_datas, &dd);
	g_mutex_unlock (account->priv->discover_data_lock);

	status = e2k_global_catalog_lookup (account->priv->gc, &dd.op,
					    E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
					    email,
					    E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
					    E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
					    &entry);
	g_free (email);
	e2k_operation_free (&dd.op);

	g_mutex_lock (account->priv->discover_data_lock);
	account->priv->discover_datas =
		g_list_remove (account->priv->discover_datas, &dd);
	g_mutex_unlock (account->priv->discover_data_lock);

	if (status != E2K_GLOBAL_CATALOG_OK) {
		if (status == E2K_GLOBAL_CATALOG_ERROR)
			return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
		if (status == E2K_GLOBAL_CATALOG_NO_SUCH_USER)
			return EXCHANGE_ACCOUNT_FOLDER_NO_SUCH_USER;
		else
			return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	}

	hier = get_hierarchy_for (account, entry);
	return exchange_hierarchy_foreign_add_folder (hier, folder_name, folder);
}

void
exchange_account_cancel_discover_shared_folder (ExchangeAccount *account,
						const gchar *user,
						const gchar *folder_name)
{
	struct discover_data *dd;
	GList *dds;

	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	g_mutex_lock (account->priv->discover_data_lock);
	for (dds = account->priv->discover_datas; dds; dds = dds->next) {
		dd = dds->data;
		if (!strcmp (dd->user, user) &&
		    !strcmp (dd->folder_name, folder_name))
			break;
	}
	if (!dds) {
		g_mutex_unlock (account->priv->discover_data_lock);
		return;
	}

	e2k_operation_cancel (&dd->op);
	g_mutex_unlock (account->priv->discover_data_lock);

#ifdef FIXME
	/* We can't actually cancel the hierarchy's attempt to get
	 * the folder, but we can remove the hierarchy if appropriate.
	 */
	if (dd->hier && exchange_hierarchy_is_empty (dd->hier))
		hierarchy_removed_folder (dd->hier, dd->hier->toplevel, account);
#endif
}

ExchangeAccountFolderResult
exchange_account_remove_shared_folder (ExchangeAccount *account,
				       const gchar *path)
{
	ExchangeHierarchy *hier;
	EFolder *folder;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!get_folder (account, path, &folder, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	if (!EXCHANGE_IS_HIERARCHY_FOREIGN (hier))
		return EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION;

	return exchange_hierarchy_remove_folder (hier, folder);
}

ExchangeAccountFolderResult
exchange_account_open_folder (ExchangeAccount *account, const gchar *path)
{
	ExchangeHierarchy *hier;
	EFolder *folder;
	gint mode;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	d(g_print ("exchange_account_remove_folder: path=[%s]\n", path));

	if (!get_folder (account, path, &folder, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	exchange_account_is_offline (account, &mode);
	if (mode == ONLINE_MODE && !account->priv->connected &&
	    hier == (ExchangeHierarchy *)account->priv->hierarchies->pdata[0] &&
	    folder == hier->toplevel) {
		/* The shell is asking us to open the personal folders
		 * hierarchy, but we're already planning to do that
		 * anyway. So just ignore the request for now.
		 */
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	}

	return exchange_hierarchy_scan_subtree (hier, folder, mode);
}

ExchangeAccountFolderResult
exchange_account_add_favorite (ExchangeAccount *account,
			       EFolder         *folder)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (folder), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return exchange_hierarchy_favorites_add_folder (
		account->priv->favorites_hierarchy,
		folder);
}

ExchangeAccountFolderResult
exchange_account_remove_favorite (ExchangeAccount *account,
				  EFolder         *folder)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (folder), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return exchange_hierarchy_remove_folder (
		EXCHANGE_HIERARCHY (account->priv->favorites_hierarchy),
		folder);
}

gboolean
exchange_account_is_favorite_folder (ExchangeAccount *account,
					EFolder         *folder)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (folder), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return exchange_hierarchy_favorites_is_added (
		EXCHANGE_HIERARCHY (account->priv->favorites_hierarchy),
		folder);
}

static void
context_redirect (E2kContext *ctx, E2kHTTPStatus status,
		  const gchar *old_uri, const gchar *new_uri,
		  ExchangeAccount *account)
{
	EFolder *folder;

	g_static_rec_mutex_lock (&account->priv->folders_lock);
	folder = g_hash_table_lookup (account->priv->folders, old_uri);
	if (!folder) {
		g_static_rec_mutex_unlock (&account->priv->folders_lock);
		return;
	}

	g_hash_table_remove (account->priv->folders, old_uri);
	e_folder_exchange_set_internal_uri (folder, new_uri);
	g_hash_table_insert (account->priv->folders,
			     (gchar *)e_folder_exchange_get_internal_uri (folder),
			     folder);

	g_static_rec_mutex_unlock (&account->priv->folders_lock);
}

static void
set_sf_prop (const gchar *propname, E2kPropType type,
	     gpointer phref, gpointer user_data)
{
	ExchangeAccount *account = user_data;
	const gchar *href = (const gchar *)phref;
	gchar *tmp;

	propname = strrchr (propname, ':');
	if (!propname++ || !href || !*href)
		return;

	tmp = e2k_strdup_with_trailing_slash (href);
	if (!tmp) {
		g_warning ("Failed to add propname '%s' for href '%s'\n", propname, href);
		return;
	}

	g_hash_table_insert (account->priv->standard_uris,
			     g_strdup (propname),
			     tmp);
}

static const gchar *mailbox_info_props[] = {
	E2K_PR_STD_FOLDER_CALENDAR,
	E2K_PR_STD_FOLDER_CONTACTS,
	E2K_PR_STD_FOLDER_DELETED_ITEMS,
	E2K_PR_STD_FOLDER_DRAFTS,
	E2K_PR_STD_FOLDER_INBOX,
	E2K_PR_STD_FOLDER_JOURNAL,
	E2K_PR_STD_FOLDER_NOTES,
	E2K_PR_STD_FOLDER_OUTBOX,
	E2K_PR_STD_FOLDER_SENT_ITEMS,
	E2K_PR_STD_FOLDER_TASKS,
	E2K_PR_STD_FOLDER_ROOT,
	E2K_PR_STD_FOLDER_SENDMSG,

	PR_STORE_ENTRYID,
	E2K_PR_EXCHANGE_TIMEZONE
};
static const gint n_mailbox_info_props = G_N_ELEMENTS (mailbox_info_props);

static gboolean
account_moved (ExchangeAccount *account, E2kAutoconfig *ac)
{
	E2kAutoconfigResult result;
	EAccount *eaccount;

	result = e2k_autoconfig_check_exchange (ac, NULL);
	if (result != E2K_AUTOCONFIG_OK)
		return FALSE;
	result = e2k_autoconfig_check_global_catalog (ac, NULL);
	if (result != E2K_AUTOCONFIG_OK)
		return FALSE;

	eaccount = account->priv->account;

	if (eaccount->source->url && eaccount->transport->url &&
	    !strcmp (eaccount->source->url, eaccount->transport->url)) {
		g_free (eaccount->transport->url);
		eaccount->transport->url = g_strdup (ac->account_uri);
	}
	g_free (eaccount->source->url);
	eaccount->source->url = g_strdup (ac->account_uri);

	e_account_list_change (account->priv->account_list, eaccount);
	e_account_list_save (account->priv->account_list);
	return TRUE;
}

#if 0
static gboolean
get_password (ExchangeAccount *account, E2kAutoconfig *ac, ExchangeAccountResult error)
{
	gchar *password;

	if (error != EXCHANGE_ACCOUNT_CONNECT_SUCCESS)
		e_passwords_forget_password ("Exchange", account->priv->password_key);

	password = e_passwords_get_password ("Exchange", account->priv->password_key);
#if 0
	if (exchange_component_is_interactive (global_exchange_component)) {
		gboolean remember, oldremember;
		if (!password) {
			gchar *prompt;

			prompt = g_strdup_printf (_("Enter password for %s"),
						  account->account_name);
			oldremember = remember =
					account->priv->account->source->save_passwd;
			password = e_passwords_ask_password (
					_("Enter password"),
					"Exchange",
					account->priv->password_key,
					prompt,
					E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET,
					&remember,
					NULL);
			if (remember != oldremember) {
				account->priv->account->source->save_passwd = remember;
			}
			g_free (prompt);
		}
		else if (!account->priv->account->source->save_passwd) {
			/* get_password returns the password cached but user has not
			 * selected remember password option, forget this password
			 * whis is stored temporarily by e2k_validate_user()
			 */
			e_passwords_forget_password ("Exchange", account->priv->password_key);
		}
	}
#endif
	if (!password) {
	}
	else if (!account->priv->account->source->save_passwd) {
		/* get_password returns the password cached but user has not
		 * selected remember password option, forget this password
		 * whis is stored temporarily by e2k_validate_user()
		 */
		e_passwords_forget_password ("Exchange", account->priv->password_key);
	}

	if (password) {
		e2k_autoconfig_set_password (ac, password);
		memset (password, 0, strlen (password));
		g_free (password);
		return TRUE;
	} else
		return FALSE;
}
#endif

/* This uses the kerberos calls to check if the authentication failure
 * was due to the password getting expired. If the password has expired
 * this returns TRUE, else it returns FALSE.
 */
#ifdef HAVE_KRB5
static gboolean
is_password_expired (ExchangeAccount *account, E2kAutoconfig *ac)
{
	gchar *domain;
	E2kKerberosResult result;

	if (!ac->password)
		return FALSE;

	domain = ac->w2k_domain;
	if (!domain) {
		domain = strchr (account->priv->identity_email, '@');
		if (domain)
			domain++;
	}
	if (!domain)
		return FALSE;

	result = e2k_kerberos_check_password (ac->username, domain,
					      ac->password);
	if (result != E2K_KERBEROS_OK &&
	    result != E2K_KERBEROS_PASSWORD_EXPIRED) {
		/* try again with nt domain */
		domain = ac->nt_domain;
		if (domain)
			result = e2k_kerberos_check_password (ac->username,
							      domain,
							      ac->password);
	}

	return (result == E2K_KERBEROS_PASSWORD_EXPIRED);
}
#endif

static gint
find_passwd_exp_period (ExchangeAccount *account, E2kGlobalCatalogEntry *entry)
{
	gdouble max_pwd_age = 0;
	gint max_pwd_age_days;
	E2kOperation gcop;
	E2kGlobalCatalogStatus gcstatus;

	/* If user has not selected password expiry warning option, return */
	if (account->priv->passwd_exp_warn_period == -1)
		return -1;

	/* Check for password expiry period */
	/* This needs to be invoked after is_password_expired(), i.e.,
	   only if password is not expired */

	/* Check for account control value for a user */

	e2k_operation_init (&gcop);
	gcstatus = e2k_global_catalog_lookup (account->priv->gc,
					      &gcop,
					      E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
					      account->priv->identity_email,
					      E2K_GLOBAL_CATALOG_LOOKUP_ACCOUNT_CONTROL,
					      &entry);
	e2k_operation_free (&gcop);
	if (gcstatus != E2K_GLOBAL_CATALOG_OK)
		return -1;

	if (entry->user_account_control & ADS_UF_DONT_EXPIRE_PASSWORD) {
		return -1;         /* Password is not set to expire */
	}

	/* Here we don't check not setting the password and expired password */
	/* Check for the maximum password age set */

	e2k_operation_init (&gcop);
	max_pwd_age = lookup_passwd_max_age (account->priv->gc, &gcop);
	e2k_operation_free (&gcop);

	if (max_pwd_age > 0) {
		/* Calculate password expiry period */
		max_pwd_age_days =
		( max_pwd_age * ONE_HUNDRED_NANOSECOND ) / SECONDS_IN_DAY;

		if (max_pwd_age_days <= account->priv->passwd_exp_warn_period) {
			return max_pwd_age_days;
		}
	}
	return -1;
}

gchar *
exchange_account_get_password (ExchangeAccount *account)
{
	return e_passwords_get_password ("Exchange", account->priv->password_key);
}

void
exchange_account_forget_password (ExchangeAccount *account)
{
	e_passwords_forget_password ("Exchange", account->priv->password_key);
}

ExchangeAccountResult
exchange_account_set_password (ExchangeAccount *account, gchar *old_pass, gchar *new_pass)
{
#ifdef HAVE_KRB5
	E2kKerberosResult result;
	gchar *domain;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), EXCHANGE_ACCOUNT_PASSWORD_CHANGE_FAILED);
	g_return_val_if_fail (old_pass != NULL, EXCHANGE_ACCOUNT_PASSWORD_CHANGE_FAILED);
	g_return_val_if_fail (new_pass != NULL, EXCHANGE_ACCOUNT_PASSWORD_CHANGE_FAILED);

	domain = account->priv->gc ? account->priv->gc->domain : NULL;
	if (!domain) {
		domain = strchr (account->priv->identity_email, '@');
		if (domain)
			domain++;
	}
	if (!domain) {
		/* email id is not proper, we return instead of trying nt_domain */
		return EXCHANGE_ACCOUNT_CONFIG_ERROR;
	}

	result = e2k_kerberos_change_password (account->priv->username, domain,
					       old_pass, new_pass);
	if (result != E2K_KERBEROS_OK && result != E2K_KERBEROS_PASSWORD_TOO_WEAK) {
		/* try with nt_domain */
		domain = account->priv->nt_domain;
		if (domain)
			result = e2k_kerberos_change_password (account->priv->username,
							       domain, old_pass,
							       new_pass);
	}
	switch (result) {
	case E2K_KERBEROS_OK:
		e_passwords_forget_password ("Exchange", account->priv->password_key);
		e_passwords_add_password (account->priv->password_key, new_pass);
		if (account->priv->account->source->save_passwd)
			e_passwords_remember_password ("Exchange", account->priv->password_key);
		break;

	case E2K_KERBEROS_PASSWORD_TOO_WEAK:
		return EXCHANGE_ACCOUNT_PASSWORD_WEAK_ERROR;

	case E2K_KERBEROS_FAILED:
	default:
		return EXCHANGE_ACCOUNT_PASSWORD_CHANGE_FAILED;
	}

	return EXCHANGE_ACCOUNT_PASSWORD_CHANGE_SUCCESS;
#else
	g_warning ("exchange_account_set_password: Not implemented (no KRB5)");
	return EXCHANGE_ACCOUNT_PASSWORD_CHANGE_FAILED;
#endif
}

void
exchange_account_set_save_password (ExchangeAccount *account, gboolean save_password)
{
	account->priv->account->source->save_passwd = save_password;
}

gboolean
exchange_account_is_save_password (ExchangeAccount *account)
{
	return account->priv->account->source->save_passwd;
}

/**
 * exchange_account_set_offline:
 * @account: an #ExchangeAccount
 *
 * This nullifies the connection and sets the account as offline.
 * The caller should take care that the required data is fetched
 * before calling this method.
 *
 * Return value: Returns TRUE is successfully sets the account to
 * offline or FALSE if failed
 **/
gboolean
exchange_account_set_offline (ExchangeAccount *account)
{

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), FALSE);

	g_mutex_lock (account->priv->connect_lock);
	if (account->priv->ctx) {
		g_object_unref (account->priv->ctx);
		account->priv->ctx = NULL;
	}

	account->priv->account_online = OFFLINE_MODE;
	g_mutex_unlock (account->priv->connect_lock);
	return TRUE;
}

/**
 * exchange_account_set_online:
 * @account: an #ExchangeAccount
 *
 * This nullifies the connection and sets the account as offline.
 * The caller should take care that the required data is fetched
 * before calling this method.
 *
 * Return value: Returns TRUE is successfully sets the account to
 * offline or FALSE if failed
 **/
gboolean
exchange_account_set_online (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), FALSE);

	g_mutex_lock (account->priv->connect_lock);
	account->priv->account_online = ONLINE_MODE;
	g_mutex_unlock (account->priv->connect_lock);

	return TRUE;
}

/**
 * exchange_account_is_offline:
 * @account: an #ExchangeAccount
 *
 * Return value: Returns TRUE if account is offline
 **/
void
exchange_account_is_offline (ExchangeAccount *account, gint *state)
{
	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	*state = account->priv->account_online;
}

static gboolean
setup_account_hierarchies (ExchangeAccount *account)
{
	ExchangeHierarchy *hier, *personal_hier;
	ExchangeAccountFolderResult fresult;
	gchar *phys_uri_prefix, *dir;
	GDir *d;
	const gchar *dent;
	gint mode;

	exchange_account_is_offline (account, &mode);

	if (mode == UNSUPPORTED_MODE)
		return FALSE;

	/* Check if folder hierarchies are already setup. */
	if (account->priv->hierarchies->len > 0)
		goto hierarchies_created;

	/* Set up Personal Folders hierarchy */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/;personal",
					   account->priv->uri_authority);
	hier = exchange_hierarchy_webdav_new (account,
					      EXCHANGE_HIERARCHY_PERSONAL,
					      _("Personal Folders"),
					      phys_uri_prefix,
					      account->home_uri,
					      account->priv->identity_name,
					      account->priv->identity_email,
					      account->priv->source_uri,
					      TRUE);

	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);

	/* Favorite Public Folders */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/;favorites",
					   account->priv->uri_authority);
	hier = exchange_hierarchy_favorites_new (account,
						 _("Favorite Public Folders"),
						 phys_uri_prefix,
						 account->home_uri,
						 account->public_uri,
						 account->priv->identity_name,
						 account->priv->identity_email,
						 account->priv->source_uri);
	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);
	account->priv->favorites_hierarchy = hier;

	/* Public Folders */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/;public",
					   account->priv->uri_authority);
	hier = exchange_hierarchy_webdav_new (account,
					      EXCHANGE_HIERARCHY_PUBLIC,
					      /* i18n: Outlookism */
					      _("All Public Folders"),
					      phys_uri_prefix,
					      account->public_uri,
					      account->priv->identity_name,
					      account->priv->identity_email,
					      account->priv->source_uri,
					      FALSE);
	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);

	/* Global Address List */
	phys_uri_prefix = g_strdup_printf ("gal://%s/gal",
					   account->priv->uri_authority);
						     /* i18n: Outlookism */
	hier = exchange_hierarchy_gal_new (account, _("Global Address List"),
					   phys_uri_prefix);
	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);

	/* Other users' folders */
	d = g_dir_open (account->storage_dir, 0, NULL);
	if (d) {
		while ((dent = g_dir_read_name (d))) {
			if (!strchr (dent, '@'))
				continue;
			dir = g_strdup_printf ("%s/%s", account->storage_dir, dent);
			hier = exchange_hierarchy_foreign_new_from_dir (account, dir);
			g_free (dir);
			if (!hier)
				continue;

			setup_hierarchy_foreign (account, hier);
		}
		g_dir_close (d);
	}

hierarchies_created:

	/* Scan the personal and favorite folders so we can resolve references
	 * to the Calendar, Contacts, etc even if the tree isn't
	 * opened.
	 */

	/* Assuming the first element being personal hierarchy. */
	personal_hier = account->priv->hierarchies->pdata[0];

	fresult = exchange_hierarchy_scan_subtree (personal_hier,
						   personal_hier->toplevel,
						   mode);
	if (fresult != EXCHANGE_ACCOUNT_FOLDER_OK) {
		account->priv->connecting = FALSE;
		return FALSE;
	}

	account->mbox_size = exchange_hierarchy_webdav_get_total_folder_size (
					EXCHANGE_HIERARCHY_WEBDAV (personal_hier));

	fresult = exchange_hierarchy_scan_subtree (
		account->priv->favorites_hierarchy,
		account->priv->favorites_hierarchy->toplevel,
		mode);
	if (fresult != EXCHANGE_ACCOUNT_FOLDER_OK &&
	    fresult != EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST) {
		account->priv->connecting = FALSE;
		return FALSE;
	}
	return TRUE;
}

/**
 * exchange_account_connect:
 * @account: an #ExchangeAccount
 *
 * This attempts to connect to @account. If the shell has enabled user
 * interaction, then it will prompt for a password if needed, and put
 * up an error message if the connection attempt failed.
 *
 * Return value: an #E2kContext, or %NULL if the connection attempt
 * failed.
 **/
E2kContext *
exchange_account_connect (ExchangeAccount *account, const gchar *pword,
			  ExchangeAccountResult *info_result)
{
	E2kAutoconfig *ac;
	E2kAutoconfigResult result;
	E2kHTTPStatus status;
	gboolean redirected = FALSE;
	E2kResult *results;
	gint nresults = 0, mode;
	GByteArray *entryid;
	const gchar *tz;
	E2kGlobalCatalogStatus gcstatus;
	E2kGlobalCatalogEntry *entry;
	E2kOperation gcop;
	gchar *user_name = NULL;

	*info_result = EXCHANGE_ACCOUNT_UNKNOWN_ERROR;
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	*info_result = EXCHANGE_ACCOUNT_CONNECT_SUCCESS;
	exchange_account_is_offline (account, &mode);

	g_mutex_lock (account->priv->connect_lock);

	if (mode == UNSUPPORTED_MODE) {
		*info_result = EXCHANGE_ACCOUNT_CONNECT_ERROR;
		account->priv->connecting = FALSE;
		g_mutex_unlock (account->priv->connect_lock);
		return NULL;
	}

	if (account->priv->connecting || mode == OFFLINE_MODE) {
		g_mutex_unlock (account->priv->connect_lock);
		if (mode == OFFLINE_MODE) {
			setup_account_hierarchies (account);
			*info_result = EXCHANGE_ACCOUNT_OFFLINE;
		}
		else {
			*info_result = EXCHANGE_ACCOUNT_CONNECT_ERROR;
		}
		return NULL;
	} else if (account->priv->ctx) {
		g_mutex_unlock (account->priv->connect_lock);
		return account->priv->ctx;
	}

	account->priv->connecting = TRUE;

	if (account->priv->windows_domain)
		user_name = g_strdup_printf ("%s\\%s", account->priv->windows_domain, account->priv->username);
	else
		user_name = g_strdup (account->priv->username);

	ac = e2k_autoconfig_new (account->home_uri,
				 user_name,
				 NULL,
				 account->priv->auth_pref);
	g_free (user_name);

	e2k_autoconfig_set_gc_server (ac, account->priv->ad_server,
				      account->priv->ad_limit, account->priv->ad_auth);

	if (!pword) {
		account->priv->connecting = FALSE;
		g_mutex_unlock (account->priv->connect_lock);
		*info_result = EXCHANGE_ACCOUNT_PASSWORD_INCORRECT;
		e2k_autoconfig_free (ac);
		return NULL;
	}

	e2k_autoconfig_set_password (ac, pword);

 try_connect_again:
	account->priv->ctx = e2k_autoconfig_get_context (ac, NULL, &result);

	if (!account->priv->nt_domain && ac->nt_domain)
		account->priv->nt_domain = g_strdup (ac->nt_domain);
	else
		account->priv->nt_domain = NULL;

	if (result != E2K_AUTOCONFIG_OK) {
#ifdef HAVE_KRB5
		if ( is_password_expired (account, ac)) {
			*info_result = EXCHANGE_ACCOUNT_PASSWORD_EXPIRED;
			account->priv->connecting = FALSE;
			g_mutex_unlock (account->priv->connect_lock);
			e2k_autoconfig_free (ac);
			return NULL;
		}
#endif
		switch (result) {

		case E2K_AUTOCONFIG_AUTH_ERROR:
			*info_result = EXCHANGE_ACCOUNT_PASSWORD_INCORRECT;
			e2k_autoconfig_free (ac);
			account->priv->connecting = FALSE;
			g_mutex_unlock (account->priv->connect_lock);
			return NULL;

		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN:
			*info_result = EXCHANGE_ACCOUNT_DOMAIN_ERROR;
			e2k_autoconfig_free (ac);
			account->priv->connecting = FALSE;
			g_mutex_unlock (account->priv->connect_lock);
			return NULL;

		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM:
			ac->use_ntlm = 1;
			goto try_connect_again;

		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC:
			ac->use_ntlm = 0;
			goto try_connect_again;

		case E2K_AUTOCONFIG_REDIRECT:
			if (!redirected && account_moved (account, ac))
				goto try_connect_again;
			break;

		case E2K_AUTOCONFIG_TRY_SSL:
			if (account_moved (account, ac))
				goto try_connect_again;
			break;

		default:
			break;
		}

		e2k_autoconfig_free (ac);
		account->priv->connecting = FALSE;
		account->priv->account_online = OFFLINE_MODE; /* correct? */

		switch (result) {
		case E2K_AUTOCONFIG_REDIRECT:
		case E2K_AUTOCONFIG_TRY_SSL:
			*info_result = EXCHANGE_ACCOUNT_MAILBOX_NA;
			break;
		case E2K_AUTOCONFIG_EXCHANGE_5_5:
			*info_result = EXCHANGE_ACCOUNT_VERSION_ERROR;
			break;
		case E2K_AUTOCONFIG_NOT_EXCHANGE:
		case E2K_AUTOCONFIG_NO_OWA:
			*info_result = EXCHANGE_ACCOUNT_WSS_ERROR;
			break;
		case E2K_AUTOCONFIG_NO_MAILBOX:
			*info_result = EXCHANGE_ACCOUNT_NO_MAILBOX;
			break;
		case E2K_AUTOCONFIG_CANT_RESOLVE:
			*info_result = EXCHANGE_ACCOUNT_RESOLVE_ERROR;
			break;
		case E2K_AUTOCONFIG_CANT_CONNECT:
			*info_result = EXCHANGE_ACCOUNT_CONNECT_ERROR;
			break;
		case E2K_AUTOCONFIG_CANCELLED:
			break;
		default:
			*info_result = EXCHANGE_ACCOUNT_UNKNOWN_ERROR;
			break;
		}

		g_mutex_unlock (account->priv->connect_lock);
		return NULL;
	}

	account->priv->gc = e2k_autoconfig_get_global_catalog (ac, NULL);
	e2k_autoconfig_free (ac);

	status = e2k_context_propfind (account->priv->ctx, NULL,
				       account->home_uri,
				       mailbox_info_props,
				       n_mailbox_info_props,
				       &results, &nresults);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		account->priv->connecting = FALSE;
		*info_result = EXCHANGE_ACCOUNT_UNKNOWN_ERROR;
		g_mutex_unlock (account->priv->connect_lock);
		return NULL; /* FIXME: what error has happened? */
	}

	if (nresults) {
		account->priv->standard_uris =
			g_hash_table_new (e2k_ascii_strcase_hash,
					  e2k_ascii_strcase_equal);
		e2k_properties_foreach (results[0].props, set_sf_prop, account);

		/* FIXME: we should get these from the autoconfig */
		entryid = e2k_properties_get_prop (results[0].props, PR_STORE_ENTRYID);
		if (entryid)
			account->legacy_exchange_dn = g_strdup (e2k_entryid_to_dn (entryid));

		tz = e2k_properties_get_prop (results[0].props, E2K_PR_EXCHANGE_TIMEZONE);
		if (tz)
			account->default_timezone = g_strdup (tz);
	}

	if (!setup_account_hierarchies (account)) {
		*info_result = EXCHANGE_ACCOUNT_UNKNOWN_ERROR;
		g_mutex_unlock (account->priv->connect_lock);
		if (nresults)
			e2k_results_free (results, nresults);
		return NULL; /* FIXME: what error has happened? */
	}

	account->priv->account_online = ONLINE_MODE;
	account->priv->connecting = FALSE;
	account->priv->connected = TRUE;

	if (!account->priv->gc)
		goto skip_quota;
	/* Check for quota usage */
	e2k_operation_init (&gcop);
	gcstatus = e2k_global_catalog_lookup (account->priv->gc, &gcop,
                                            E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
                                            account->priv->identity_email,
					    E2K_GLOBAL_CATALOG_LOOKUP_QUOTA,
                                            &entry);
	e2k_operation_free (&gcop);

	/* FIXME: warning message should have quota limit value
	 */
	if (gcstatus == E2K_GLOBAL_CATALOG_OK) {

		if (entry->quota_norecv &&
			account->mbox_size >= entry->quota_norecv) {
				*info_result = EXCHANGE_ACCOUNT_QUOTA_RECIEVE_ERROR;
				account->priv->quota_limit = entry->quota_norecv;
		} else if (entry->quota_nosend &&
				account->mbox_size >= entry->quota_nosend) {
					*info_result = EXCHANGE_ACCOUNT_QUOTA_SEND_ERROR;
					account->priv->quota_limit = entry->quota_nosend;
		} else if (entry->quota_warn &&
				account->mbox_size >= entry->quota_warn) {
					*info_result = EXCHANGE_ACCOUNT_QUOTA_WARN;
					account->priv->quota_limit = entry->quota_warn;
		}
	}

skip_quota:
	g_signal_connect (account->priv->ctx, "redirect",
			  G_CALLBACK (context_redirect), account);

	g_signal_emit (account, signals[CONNECTED], 0, account->priv->ctx);
	g_mutex_unlock (account->priv->connect_lock);
	if (nresults)
		e2k_results_free (results, nresults);
	return account->priv->ctx;
}

/**
 * exchange_account_is_offline_sync_set:
 * @account: an #ExchangeAccount
 *
 * Return value: TRUE if offline_sync is set for @account and FALSE if not.
 */
void
exchange_account_is_offline_sync_set (ExchangeAccount *account, gint *mode)
{
	*mode = UNSUPPORTED_MODE;

	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	if (account->priv->offline_sync)
		*mode = OFFLINE_MODE;
	else
		*mode = ONLINE_MODE;
}

/**
 * exchange_account_get_context:
 * @account: an #ExchangeAccount
 *
 * Return value: @account's #E2kContext, if it is connected and
 * online, or %NULL if not.
 **/
E2kContext *
exchange_account_get_context (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	return account->priv->ctx;
}

/**
 * exchange_account_get_global_catalog:
 * @account: an #ExchangeAccount
 *
 * Return value: @account's #E2kGlobalCatalog, if it is connected and
 * online, or %NULL if not.
 **/
E2kGlobalCatalog *
exchange_account_get_global_catalog (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	return account->priv->gc;
}

/**
 * exchange_account_fetch:
 * @acct: an #ExchangeAccount
 *
 * Return value: @account's #EAccount, if it is connected and
 * online, or %NULL if not.
 **/
EAccount *
exchange_account_fetch (ExchangeAccount *acct)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (acct), NULL);

	return acct->priv->account;
}

/**
 * exchange_account_get_account_uri_param:
 * @acct: and #ExchangeAccount
 * @param: uri param name to get
 *
 * Reads the parameter #param from the source url of the underlying EAccount.
 * Returns the value or NULL. Returned value should be freed with g_free.
 **/
gchar *
exchange_account_get_account_uri_param (ExchangeAccount *acct, const gchar *param)
{
	EAccount *account;
	E2kUri *uri;
	gchar *res;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (acct), NULL);
	g_return_val_if_fail (param != NULL, NULL);

	account = exchange_account_fetch (acct);
	g_return_val_if_fail (account != NULL, NULL);

	uri = e2k_uri_new (e_account_get_string (account, E_ACCOUNT_SOURCE_URL));
	g_return_val_if_fail (uri != NULL, NULL);

	res = g_strdup (e2k_uri_get_param (uri, param));

	e2k_uri_free (uri);

	return res;
}

/**
 * exchange_account_get_standard_uri:
 * @account: an #ExchangeAccount
 * @item: the short name of the standard URI
 *
 * Looks up the value of one of the standard URIs on @account.
 * Supported values for @item are:
 *   "calendar", "contacts", "deleteditems", "drafts", "inbox",
 *   "journal", "notes", "outbox", "sentitems", "tasks", and
 *   "sendmsg" (the special mail submission URI)
 *
 * Return value: the value of the standard URI, or %NULL if the
 * account is not connected or the property is invalid or not
 * defined on @account.
 **/
const gchar *
exchange_account_get_standard_uri (ExchangeAccount *account, const gchar *item)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	if (!account->priv->standard_uris)
		return NULL;

	return g_hash_table_lookup (account->priv->standard_uris, item);
}

/**
 * exchange_account_get_standard_uri_for:
 * @account: an #ExchangeAccount
 * @home_uri: the home URI of a user
 * @std_uri_prop: the %E2K_PR_STD_FOLDER property to look up
 *
 * Looks up the URI of a folder in another user's mailbox.
 *
 * Return value: the URI of the folder, or %NULL if either the folder
 * doesn't exist or the user doesn't have permission to access it.
 **/
gchar *
exchange_account_get_standard_uri_for (ExchangeAccount *account,
				       const gchar *home_uri,
				       const gchar *std_uri_prop)
{
	gchar *foreign_uri, *prop;
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	foreign_uri = e2k_uri_concat (home_uri, "NON_IPM_SUBTREE");
	status = e2k_context_propfind (account->priv->ctx, NULL, foreign_uri,
				       &std_uri_prop, 1,
				       &results, &nresults);
	g_free (foreign_uri);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status) || nresults == 0)
		return NULL;

	prop = e2k_properties_get_prop (results[0].props, std_uri_prop);
	if (prop)
		foreign_uri = e2k_strdup_with_trailing_slash (prop);
	else
		foreign_uri = NULL;
	e2k_results_free (results, nresults);

	return foreign_uri;
}

/**
 * exchange_account_get_foreign_uri:
 * @account: an #ExchangeAccount
 * @entry: an #E2kGlobalCatalogEntry with mailbox data
 * @std_uri_prop: the %E2K_PR_STD_FOLDER property to look up, or %NULL
 *
 * Looks up the URI of a folder in another user's mailbox. If
 * @std_uri_prop is %NULL, the URI for the top level of the user's
 * mailbox is returned.
 *
 * Return value: the URI of the folder, or %NULL if either the folder
 * doesn't exist or the user doesn't have permission to access it.
 **/
gchar *
exchange_account_get_foreign_uri (ExchangeAccount *account,
				  E2kGlobalCatalogEntry *entry,
				  const gchar *std_uri_prop)
{
	gchar *home_uri, *foreign_uri;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	if (account->priv->uris_use_email) {
		gchar *mailbox;

		mailbox = g_strndup (entry->email, strcspn (entry->email, "@"));
		home_uri = g_strdup_printf (account->priv->http_uri_schema,
					    entry->exchange_server, mailbox);
		g_free (mailbox);
	} else {
		home_uri = g_strdup_printf (account->priv->http_uri_schema,
					    entry->exchange_server,
					    entry->mailbox);
	}
	if (!std_uri_prop)
		return home_uri;

	foreign_uri = exchange_account_get_standard_uri_for (account,
							     home_uri,
							     std_uri_prop);
	g_free (home_uri);

	return foreign_uri;
}

/* Scans the subscribed users folders. */
/*FIXME This function is not really required if the syncronization
  problem between exchange and evolution is fixed. Exchange does not get to know
 if an user's folder is subscribed from evolution */
void
exchange_account_scan_foreign_hierarchy (ExchangeAccount *account, const gchar *user_email)
{
	gchar *dir;
	ExchangeHierarchy *hier;
	gint mode;

	hier = g_hash_table_lookup (account->priv->foreign_hierarchies, user_email);
	if (hier) {
		exchange_hierarchy_rescan (hier);
		return;
	}

	dir = g_strdup_printf ("%s/%s", account->storage_dir, user_email);
	if (g_file_test (dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		hier = exchange_hierarchy_foreign_new_from_dir (account, dir);
		g_free (dir);
		if (hier) {
			exchange_account_is_offline (account, &mode);
			setup_hierarchy_foreign (account, hier);
			exchange_hierarchy_scan_subtree (hier, hier->toplevel, mode);
		}
	}
}

/**
 * exchange_account_get_hierarchy_by_email:
 * @account: an #ExchangeAccount
 * @email: email id of the foreign user
 *
 * If the hierarchy is present just return it back. Else try to get it
 * from the filesystem and return it.
 *
 * Return value: Returns the ExchangeHierarchy of the foreign user's folder.
 **/

ExchangeHierarchy *
exchange_account_get_hierarchy_by_email (ExchangeAccount *account, const gchar *email)
{
	gchar *dir;
	ExchangeHierarchy *hier = NULL;
	gint mode;

	g_return_val_if_fail (email != NULL, NULL);

	hier = g_hash_table_lookup (account->priv->foreign_hierarchies, email);
	if (!hier) {
		dir = g_strdup_printf ("%s/%s", account->storage_dir, email);
		if (g_file_test (dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
			hier = exchange_hierarchy_foreign_new_from_dir (account, dir);
			g_free (dir);
			if (hier) {
				exchange_account_is_offline (account, &mode);
				setup_hierarchy_foreign (account, hier);
			}
		}
	}

	return hier;
 }

/**
 * exchange_account_get_folder:
 * @account: an #ExchangeAccount
 * @path_or_uri: the shell path or URI referring to the folder
 *
 * Return value: an #EFolder corresponding to the indicated
 * folder.
 **/
EFolder *
exchange_account_get_folder (ExchangeAccount *account,
			     const gchar *path_or_uri)
{
	EFolder *folder;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	if (!path_or_uri)
		return NULL;
	g_static_rec_mutex_lock (&account->priv->folders_lock);
	folder = g_hash_table_lookup (account->priv->folders, path_or_uri);
	g_static_rec_mutex_unlock (&account->priv->folders_lock);

	return folder;
}

static gint
folder_comparator (gconstpointer a, gconstpointer b)
{
	EFolder **fa = (EFolder **)a;
	EFolder **fb = (EFolder **)b;

	return strcmp (e_folder_exchange_get_path (*fa),
		       e_folder_exchange_get_path (*fb));
}

struct _folders_tree {
	gchar *path;
	GPtrArray *folders;
};

static void
add_folder (gpointer key, gpointer value, gpointer folders)
{
	EFolder *folder = value;

	d(g_print ("%s:%s: key=[%s]\t folder-path=[%s]\n", G_STRLOC, G_STRFUNC,
		   key, e_folder_exchange_get_path (folder)));

	/* Each folder appears under three different keys, but
	 * we only want to add it to the results array once. So
	 * we only add when we see the "path" key.
	 */
	if (!strcmp (key, e_folder_exchange_get_path (folder)))
		g_ptr_array_add (folders, folder);
}

static void
add_folder_tree (gpointer key, gpointer value, gpointer folders)
{
	EFolder *folder = value;
	struct _folders_tree *fld_tree = (struct _folders_tree *) folders;

	if (!fld_tree || !fld_tree->path)
		return;

	if (g_str_has_prefix (key, fld_tree->path))
		add_folder (key, folder, fld_tree->folders);
}

/**
 * exchange_account_get_folders:
 * @account: an #ExchangeAccount
 *
 * Return an array of folders (sorted such that parents will occur
 * before children). If the caller wants to keep up to date with the
 * list of folders, he should also listen to %new_folder and
 * %removed_folder signals.
 *
 * Return value: an array of folders. The array should be freed with
 * g_ptr_array_free().
 **/
GPtrArray *
exchange_account_get_folders (ExchangeAccount *account)
{
	GPtrArray *folders;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	folders = g_ptr_array_new ();
	g_static_rec_mutex_lock (&account->priv->folders_lock);
	g_hash_table_foreach (account->priv->folders, add_folder, folders);
	g_static_rec_mutex_unlock (&account->priv->folders_lock);

	qsort (folders->pdata, folders->len,
	       sizeof (EFolder *), folder_comparator);

	return folders;
}

/**
 * exchange_account_get_folder_tree:
 * @account: an #ExchangeAccount
 *
 * Return an array of folders (sorted such that parents will occur
 * before children). If the caller wants to keep up to date with the
 * list of folders, he should also listen to %new_folder and
 * %removed_folder signals.
 *
 * Return value: an array of folders. The array should be freed with
 * g_ptr_array_free().
 **/
GPtrArray *
exchange_account_get_folder_tree (ExchangeAccount *account, gchar * path)
{
	GPtrArray *folders = NULL;
	EFolder *folder = NULL;
	ExchangeHierarchy *hier = NULL;

	struct _folders_tree *fld_tree = NULL;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	if (!get_folder (account, path, &folder, &hier))
		return folders;

	exchange_hierarchy_scan_subtree (hier, folder, account->priv->account_online);

	folders = g_ptr_array_new ();
	fld_tree = g_new0 (struct _folders_tree, 1);
	fld_tree->path = path;
	fld_tree->folders = folders;

	g_static_rec_mutex_lock (&account->priv->folders_lock);
	g_hash_table_foreach (account->priv->folders, add_folder_tree, fld_tree);
	g_static_rec_mutex_unlock (&account->priv->folders_lock);

	qsort (folders->pdata, folders->len,
	       sizeof (EFolder *), folder_comparator);

	g_free (fld_tree);

	return folders;
}

/**
 * exchange_account_get_quota_limit:
 * @account: an #ExchangeAccount
 *
 * Return the value of the quota limit reached.
 *
 * Return value: an gint
 **/
gint
exchange_account_get_quota_limit (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 0);

	return account->priv->quota_limit;
}

gint
exchange_account_check_password_expiry (ExchangeAccount *account)
{
	E2kGlobalCatalogEntry *entry=NULL; /* This is never set before it's used! */
	gint max_pwd_age_days = -1;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 0);

	max_pwd_age_days = find_passwd_exp_period (account, entry);
	return max_pwd_age_days;
}

gchar *
exchange_account_get_username (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	return account->priv->username;
}

/**
  * exchange_account_get_windows_domain :
  * @account : #ExchangeAccount
  *
  * Returns the Windows domain
  *
  * Return value : Windows domain
  **/
gchar *
exchange_account_get_windows_domain (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	return account->priv->windows_domain;
}

/**
  * exchange_account_get_email_id :
  * @account : #ExchangeAccount
  *
  * Retunrs user's e-mail id.
  *
  * Return value : e-mail id string.
  **/
gchar *
exchange_account_get_email_id (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	return account->priv->identity_email;
}

/**
  * exchange_account_folder_size_add :
  * @account : #ExchangeAccount
  * @folder_name :
  * @size : Size of @folder_name
  *
  * Updates the #ExchangeFolderSize object with the @size of @folder_name
  *
  * Return value : void
  **/
void
exchange_account_folder_size_add (ExchangeAccount *account,
				     const gchar *folder_name,
				     gdouble size)
{
	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	exchange_folder_size_update (account->priv->fsize, folder_name, size);
}

/**
  * exchange_account_folder_size_remove :
  * @account : #ExchangeAccount
  * @folder_name :
  *
  * Removes the entry for @folder_name in #ExchangeFolderSize object
  *
  * Return value : void
  **/
void
exchange_account_folder_size_remove (ExchangeAccount *account,
					const gchar *folder_name)
{
	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	exchange_folder_size_remove (account->priv->fsize, folder_name);
}

/**
  * exchange_account_folder_size_rename :
  * @account : #ExchangeAccount
  * @old_name : Old name of the folder
  * @new_name : New name of the folder
  *
  * Removes the entry for @old_name in #ExchangeFolderSize object and adds
  * a new entry for @new_name with the same folder size
  *
  * Return value : void
  **/
void
exchange_account_folder_size_rename (ExchangeAccount *account,
					const gchar *old_name,
					const gchar *new_name)
{
	gdouble cached_size;

	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	cached_size = exchange_folder_size_get (account->priv->fsize,
					old_name);
	if (cached_size >= 0) {
		exchange_folder_size_remove (account->priv->fsize, old_name);
		exchange_folder_size_update (account->priv->fsize,
						new_name, cached_size);
	}

}

/**
  * exchange_account_folder_size_get_model :
  * @account : #ExchangeAccount
  *
  * Returns the model store of #ExchangeFolderSize object
  *
  * Return value : The model store. A GtkListStore
  **/
GtkListStore *
exchange_account_folder_size_get_model (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	return exchange_folder_size_get_model (account->priv->fsize);
}

gchar *
exchange_account_get_authtype (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	if (account->priv->auth_pref == E2K_AUTOCONFIG_USE_BASIC)
		return g_strdup ("Basic");
	else if (account->priv->auth_pref == E2K_AUTOCONFIG_USE_NTLM)
		return g_strdup ("NTLM");

	return NULL;
}

/**
 * exchange_account_new:
 * @adata: an #EAccount
 *
 * An #ExchangeAccount is essentially an #E2kContext with
 * associated configuration.
 *
 * Return value: a new #ExchangeAccount corresponding to @adata
 **/
ExchangeAccount *
exchange_account_new (EAccountList *account_list, EAccount *adata)
{
	ExchangeAccount *account;
	gchar *enc_user, *mailbox;
	const gchar *param, *proto="http", *owa_path, *pf_server, *owa_url;
	const gchar *passwd_exp_warn_period, *offline_sync;
	E2kUri *uri;

	uri = e2k_uri_new (adata->source->url);
	if (!uri) {
		g_warning ("Could not parse exchange uri '%s'",
			   adata->source->url);
		return NULL;
	}

	account = g_object_new (EXCHANGE_TYPE_ACCOUNT, NULL);
	if (!account)
		return NULL;
	account->priv->account_list = account_list;
	g_object_ref (account_list);
	account->priv->account = adata;
	g_object_ref (adata);

	account->account_name = g_strdup (adata->name);

	account->storage_dir = g_strdup_printf ("%s/.evolution/exchange/%s@%s",
						g_get_home_dir (),
						uri->user, uri->host);
	/*account->account_filename = strrchr (account->storage_dir, '/') + 1;
	e_filename_make_safe (account->account_filename); */

	/* Identity info */
	account->priv->identity_name = g_strdup (adata->id->name);
	account->priv->identity_email = g_strdup (adata->id->address);

	/* URI, etc, info */
	enc_user = e2k_uri_encode (uri->user, FALSE, "@/;:");

	if (uri->authmech)
		account->priv->uri_authority = g_strdup_printf ("%s;auth=%s@%s", enc_user,
								uri->authmech, uri->host);
	else
		account->priv->uri_authority = g_strdup_printf ("%s@%s", enc_user,
								uri->host);
	g_free (enc_user);

	account->account_filename = account->priv->uri_authority;

	account->priv->source_uri = g_strdup_printf ("exchange://%s/", account->priv->uri_authority);

	/* Backword compatibility; FIXME, we should just migrate the
	 * password from this to source_uri.
	 * old_uri_authority = g_strdup_printf ("%s@%s", enc_user,
	 *					uri->host);
	 * old_uri_authority needs to be used in the key for migrating
	 * passwords remembered.
	 */
	account->priv->password_key = g_strdup_printf ("exchange://%s/",
							account->priv->uri_authority);

	account->priv->username = g_strdup (uri->user);
	if (uri->domain)
		account->priv->windows_domain = g_strdup (uri->domain);
	else
		account->priv->windows_domain = NULL;
	account->exchange_server = g_strdup (uri->host);
	if (uri->authmech && !strcmp (uri->authmech, "Basic"))
		account->priv->auth_pref = E2K_AUTOCONFIG_USE_BASIC;
	else
		account->priv->auth_pref = E2K_AUTOCONFIG_USE_NTLM;
	param = e2k_uri_get_param (uri, "ad_server");
	if (param && *param) {
		account->priv->ad_server = g_strdup (param);
		param = e2k_uri_get_param (uri, "ad_limit");
		if (param)
			account->priv->ad_limit = atoi (param);
		param = e2k_uri_get_param (uri, "ad_auth");
		if (!param || g_ascii_strcasecmp (param, "default") == 0)
			account->priv->ad_auth = E2K_AUTOCONFIG_USE_GAL_DEFAULT;
		else if (g_ascii_strcasecmp (param, "basic") == 0)
			account->priv->ad_auth = E2K_AUTOCONFIG_USE_GAL_BASIC;
		else if (g_ascii_strcasecmp (param, "ntlm") == 0)
			account->priv->ad_auth = E2K_AUTOCONFIG_USE_GAL_NTLM;
		else
			account->priv->ad_auth = E2K_AUTOCONFIG_USE_GAL_DEFAULT;
	}

	passwd_exp_warn_period = e2k_uri_get_param (uri, "passwd_exp_warn_period");
	if (!passwd_exp_warn_period || !*passwd_exp_warn_period)
		account->priv->passwd_exp_warn_period = -1;
	else
		account->priv->passwd_exp_warn_period = atoi (passwd_exp_warn_period);

	offline_sync = e2k_uri_get_param (uri, "offline_sync");
	if (!offline_sync)
		account->priv->offline_sync = FALSE;
	else
		account->priv->offline_sync = TRUE;

	owa_path = e2k_uri_get_param (uri, "owa_path");
	if (!owa_path || !*owa_path)
		owa_path = "exchange";
	else if (*owa_path == '/')
		owa_path++;

	pf_server = e2k_uri_get_param (uri, "pf_server");
	if (!pf_server || !*pf_server)
		pf_server = uri->host;

	/* We set protocol reading owa_url, instead of having use_ssl parameter
	 * because we don't have SSL section anymore in the account creation
	 * druid and account editor
	 */
	/* proto = e2k_uri_get_param (uri, "use_ssl") ? "https" : "http"; */

	owa_url = e2k_uri_get_param (uri, "owa_url");
	if (owa_url) {
		account->priv->owa_url = g_strdup (owa_url);
		if (!strncmp (owa_url, "https:", 6))
			proto = "https";
	}

	if (uri->port != 0) {
		account->priv->http_uri_schema =
			g_strdup_printf ("%s://%%s:%d/%s/%%s/",
					 proto, uri->port, owa_path);
		account->public_uri =
			g_strdup_printf ("%s://%s:%d/public",
					 proto, pf_server, uri->port);
	} else {
		account->priv->http_uri_schema =
			g_strdup_printf ("%s://%%s/%s/%%s/", proto, owa_path);
		account->public_uri =
			g_strdup_printf ("%s://%s/public", proto, pf_server);
	}

	param = e2k_uri_get_param (uri, "mailbox");
	if (!param || !*param)
		param = uri->user;
	else if (!g_ascii_strncasecmp (param, account->priv->identity_email, strlen (param)))
		account->priv->uris_use_email = TRUE;
	mailbox = e2k_uri_encode (param, TRUE, "/");
	account->home_uri = g_strdup_printf (account->priv->http_uri_schema,
					     uri->host, mailbox);
	g_free (mailbox);

	param = e2k_uri_get_param (uri, "filter");
	if (param)
		account->filter_inbox = TRUE;
	param = e2k_uri_get_param (uri, "filter_junk");
	if (param)
		account->filter_junk = TRUE;
	param = e2k_uri_get_param (uri, "filter_junk_inbox");
	if (param)
		account->filter_junk_inbox_only = TRUE;

	e2k_uri_free (uri);

	return account;
}

/**
 * exchange_account_get_hierarchy_by_type:
 * @account: an #ExchangeAccount
 * @type: Hierarchy type
 *
 * Returns the non-foreign hierarchy pointer for the requested type
 *
 * Return value: Returns the hierarchy pointer for the requested type
 **/

ExchangeHierarchy*
exchange_account_get_hierarchy_by_type (ExchangeAccount* acct,
					ExchangeHierarchyType type)
{
	gint i;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (acct), NULL);
	g_return_val_if_fail (type != EXCHANGE_HIERARCHY_FOREIGN, NULL);

	for (i = 0; i < acct->priv->hierarchies->len; i++) {
		if (EXCHANGE_HIERARCHY (acct->priv->hierarchies->pdata[i])->type == type)
			return EXCHANGE_HIERARCHY (acct->priv->hierarchies->pdata[i]);
	}
	return NULL;
}
