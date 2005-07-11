/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
#include "e2k-encoding-utils.h"
#include "e2k-kerberos.h"
#include "e2k-marshal.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "exchange-hierarchy-foreign.h"
#include <libedataserverui/e-passwords.h>

#include <libgnome/gnome-util.h>

#include <glade/glade-xml.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtklabel.h>

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define d(x) x
#define ADS_UF_DONT_EXPIRE_PASSWORD 0x10000
#define ONE_HUNDRED_NANOSECOND 0.000000100
#define SECONDS_IN_DAY 86400
#define PASSWD_EXPIRY_NOTIFICATION_PERIOD 7
#define FILENAME CONNECTOR_GLADEDIR "/exchange-passwd-expiry.glade"
#define ROOTNODE "passwd_exp_dialog"

struct _ExchangeAccountPrivate {
	E2kContext *ctx;
	E2kGlobalCatalog *gc;
	GHashTable *standard_uris;
	ExchangeFolderSize *fsize;

	GMutex *connect_lock;
	gboolean connecting, connected, account_online;

	GPtrArray *hierarchies;
	GHashTable *hierarchies_by_folder, *foreign_hierarchies;
	ExchangeHierarchy *favorites_hierarchy;
	GHashTable *folders;
	char *uri_authority, *http_uri_schema;
	gboolean uris_use_email, offline_sync;

	char *identity_name, *identity_email, *source_uri, *password_key;
	char *username, *password, *windows_domain, *nt_domain, *ad_server;
	char *owa_url;
	E2kAutoconfigAuthPref auth_pref;
	int ad_limit, passwd_exp_warn_period, quota_limit;

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
			      e2k_marshal_NONE__OBJECT,
			      G_TYPE_NONE, 1,
			      E2K_TYPE_CONTEXT);
	signals[NEW_FOLDER] =
		g_signal_new ("new_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, new_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[REMOVED_FOLDER] =
		g_signal_new ("removed_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, removed_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
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
	account->priv->discover_data_lock = g_mutex_new ();
	account->priv->account_online = TRUE;
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
	int i;

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

	if (account->priv->hierarchies_by_folder) {
		g_hash_table_destroy (account->priv->hierarchies_by_folder);
		account->priv->hierarchies_by_folder = NULL;
	}

	if (account->priv->foreign_hierarchies) {
		g_hash_table_foreach (account->priv->foreign_hierarchies, free_name, NULL);
		g_hash_table_destroy (account->priv->foreign_hierarchies);
		account->priv->foreign_hierarchies = NULL;
	}

	if (account->priv->folders) {
		g_hash_table_foreach (account->priv->folders, free_folder, NULL);
		g_hash_table_destroy (account->priv->folders);
		account->priv->folders = NULL;
	}

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

	g_free (account->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


E2K_MAKE_TYPE (exchange_account, ExchangeAccount, class_init, init, PARENT_TYPE)


void
exchange_account_rescan_tree (ExchangeAccount *account)
{
	int i;

	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	for (i = 0; i < account->priv->hierarchies->len; i++)
		exchange_hierarchy_rescan (account->priv->hierarchies->pdata[i]);
}

/*
 * ExchangeHierarchy folder creation/deletion/xfer notifications
 */

static void
hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
		      ExchangeAccount *account)
{
	int table_updated = 0;
	const char *permanent_uri =
		e_folder_exchange_get_permanent_uri (folder);

	/* This makes the cleanup easier. We just unref it each time
	 * we find it in account->priv->folders.
	 */
	if (!g_hash_table_lookup (account->priv->folders, 
				e_folder_exchange_get_path (folder))) {
		/* Avoid dupilcations since the user could add a folder as
		  favorite even though it is already marked as favorite */
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     (char *)e_folder_exchange_get_path (folder),
				     folder);
		table_updated = 1;
	}
	if (!g_hash_table_lookup (account->priv->folders, 
				e_folder_get_physical_uri (folder))) {
		/* Avoid dupilcations since the user could add a folder as
		  favorite even though it is already marked as favorite */
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     (char *)e_folder_get_physical_uri (folder),
				     folder);
		table_updated = 1;
	}
	if (!g_hash_table_lookup (account->priv->folders, 
				e_folder_exchange_get_internal_uri (folder))) {
		/* The internal_uri for public folders and favorites folder 
		   is same !!! Without this check the folder value could 
		   overwrite the previously added folder. */
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     (char *)e_folder_exchange_get_internal_uri (folder),
				     folder);
		table_updated = 1;
	}
	if (permanent_uri && (!g_hash_table_lookup (account->priv->folders, 
					permanent_uri))) {
		g_object_ref (folder);
		g_hash_table_insert (account->priv->folders,
				     (char *)permanent_uri,
				     folder);
		table_updated = 1;
	}

	if (table_updated)
	{
		g_hash_table_insert (account->priv->hierarchies_by_folder, 
					folder, hier);

		g_signal_emit (account, signals[NEW_FOLDER], 0, folder);
	}
}

static void
hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
			  ExchangeAccount *account)
{
	if (!g_hash_table_lookup (account->priv->folders, 
					e_folder_exchange_get_path (folder)))
		return;

	g_hash_table_remove (account->priv->folders, 
					e_folder_exchange_get_path (folder));
	g_hash_table_remove (account->priv->folders, 
					e_folder_get_physical_uri (folder));
	/* Dont remove this for favorites, as the internal_uri is shared 
		by the public folder as well */
	if (hier->type != EXCHANGE_HIERARCHY_FAVORITES) {
		g_hash_table_remove (account->priv->folders, 
					e_folder_exchange_get_internal_uri (folder));
	}
	g_hash_table_remove (account->priv->hierarchies_by_folder, folder);
	g_signal_emit (account, signals[REMOVED_FOLDER], 0, folder);

	if (folder == hier->toplevel)
		remove_hierarchy (account, hier);

	g_object_unref (folder);
	g_object_unref (folder);
	if (hier->type != EXCHANGE_HIERARCHY_FAVORITES) {
		g_object_unref (folder);
	}
}

static gboolean
get_folder (ExchangeAccount *account, const char *path,
	    EFolder **folder, ExchangeHierarchy **hier)
{
	*folder = g_hash_table_lookup (account->priv->folders, path);
	if (!*folder)
		return FALSE;
	*hier = g_hash_table_lookup (account->priv->hierarchies_by_folder,
				     *folder);
	if (!*hier)
		return FALSE;
	return TRUE;
}

static gboolean
get_parent_and_name (ExchangeAccount *account, const char **path,
		     EFolder **parent, ExchangeHierarchy **hier)
{
	char *name, *parent_path;

	name = strrchr (*path + 1, '/');
	if (!name)
		return FALSE;

	parent_path = g_strndup (*path, name - *path);
	*parent = g_hash_table_lookup (account->priv->folders, parent_path);
	g_free (parent_path);

	if (!*parent)
		return FALSE;

	*hier = g_hash_table_lookup (account->priv->hierarchies_by_folder,
				     *parent);
	if (!*hier)
		return FALSE;

	*path = name + 1;
	return TRUE;
}

ExchangeAccountFolderResult
exchange_account_create_folder (ExchangeAccount *account,
				const char *path, const char *type)
{
	ExchangeHierarchy *hier;
	EFolder *parent;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!get_parent_and_name (account, &path, &parent, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	return exchange_hierarchy_create_folder (hier, parent, path, type);
}

ExchangeAccountFolderResult
exchange_account_remove_folder (ExchangeAccount *account, const char *path)
{
	ExchangeHierarchy *hier;
	EFolder *folder;
	const char *name;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!get_folder (account, path, &folder, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	
	name = e_folder_get_name (folder);
	if (exchange_account_get_standard_uri (account, name))
		return EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION;

	return exchange_hierarchy_remove_folder (hier, folder);
}

ExchangeAccountFolderResult
exchange_account_xfer_folder (ExchangeAccount *account,
			      const char *source_path,
			      const char *dest_path,
			      gboolean remove_source)
{
	EFolder *source, *dest_parent;
	ExchangeHierarchy *source_hier, *dest_hier;
	const char *name;

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
	int i;

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
			     (char *)hier->owner_email, hier);
	setup_hierarchy (account, hier);
}

struct discover_data {
	const char *user, *folder_name;
	E2kOperation op;
};

static ExchangeHierarchy *
get_hierarchy_for (ExchangeAccount *account, E2kGlobalCatalogEntry *entry)
{
	ExchangeHierarchy *hier;
	char *hierarchy_name, *source;
	char *physical_uri_prefix, *internal_uri_prefix;

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
	physical_uri_prefix = g_strdup_printf ("exchange://%s/%s",
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
					 const char *user,
					 const char *folder_name,
					 EFolder **folder)
{
	struct discover_data dd;
	ExchangeHierarchy *hier;
	char *email;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!account->priv->gc)
		return EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION;

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
		return (status == E2K_GLOBAL_CATALOG_ERROR) ?
			EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR :
			EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	}

	hier = get_hierarchy_for (account, entry);
	return exchange_hierarchy_foreign_add_folder (hier, folder_name, folder);
}

void
exchange_account_cancel_discover_shared_folder (ExchangeAccount *account,
						const char *user,
						const char *folder_name)
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
				       const char *path)
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
exchange_account_open_folder (ExchangeAccount *account, const char *path)
{
	ExchangeHierarchy *hier;
	EFolder *folder;
	int offline;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	if (!get_folder (account, path, &folder, &hier))
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	exchange_account_is_offline (account, &offline);
	if (offline == ONLINE_MODE && !account->priv->connected &&
	    hier == (ExchangeHierarchy *)account->priv->hierarchies->pdata[0] &&
	    folder == hier->toplevel) {
		/* The shell is asking us to open the personal folders
		 * hierarchy, but we're already planning to do that
		 * anyway. So just ignore the request for now.
		 */
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	}		
	
	return exchange_hierarchy_scan_subtree (hier, folder, (offline == OFFLINE_MODE));
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
		  const char *old_uri, const char *new_uri,
		  ExchangeAccount *account)
{
	EFolder *folder;

	folder = g_hash_table_lookup (account->priv->folders, old_uri);
	if (!folder)
		return;

	g_hash_table_remove (account->priv->folders, old_uri);
	e_folder_exchange_set_internal_uri (folder, new_uri);
	g_hash_table_insert (account->priv->folders,
			     (char *)e_folder_exchange_get_internal_uri (folder),
			     folder);
}

static void
set_sf_prop (const char *propname, E2kPropType type,
	     gpointer href, gpointer user_data)
{
	ExchangeAccount *account = user_data;

	propname = strrchr (propname, ':');
	if (!propname++)
		return;

	g_hash_table_insert (account->priv->standard_uris,
			     g_strdup (propname),
			     e2k_strdup_with_trailing_slash (href));
}

static const char *mailbox_info_props[] = {
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
static const int n_mailbox_info_props = G_N_ELEMENTS (mailbox_info_props);

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

static gboolean
get_password (ExchangeAccount *account, E2kAutoconfig *ac, ExchangeAccountResult error)
{
	char *password;
	gboolean remember, oldremember;

	if (error != EXCHANGE_ACCOUNT_CONNECT_SUCCESS)
		e_passwords_forget_password ("Exchange", account->priv->password_key);

	password = e_passwords_get_password ("Exchange", account->priv->password_key);

	// SURF : if (exchange_component_is_interactive (global_exchange_component)) {
		if (!password) {
			char *prompt;

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
	// SURF : }

	if (password) {
		e2k_autoconfig_set_password (ac, password);
		memset (password, 0, strlen (password));
		g_free (password);
		return TRUE;
	} else
		return FALSE;
}

/* This uses the kerberos calls to check if the authentication failure
 * was due to the password getting expired. If the password has expired
 * this returns TRUE, else it returns FALSE.
 */
static gboolean
is_password_expired (ExchangeAccount *account, E2kAutoconfig *ac)
{
	char *domain;
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
	if (result != E2K_KERBEROS_OK || 
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

#if 0
static void
change_passwd_cb (GtkWidget *button, ExchangeAccount *account)
{
	char *current_passwd, *new_passwd;

	gtk_widget_hide (gtk_widget_get_toplevel(button));
	current_passwd = exchange_account_get_password (account);
	new_passwd = exchange_get_new_password (current_passwd, TRUE);
	exchange_account_set_password (account, current_passwd, new_passwd);
	g_free (current_passwd);
	g_free (new_passwd);
}
#endif

static void
display_passwd_expiry_message (int max_passwd_age, ExchangeAccount *account)
{
	GladeXML *xml;
	GtkWidget *top_widget, *change_passwd_button;
	GtkResponseType response;
	GtkLabel *warning_msg_label;
	char *passwd_expiry_msg = 
		g_strdup_printf ("Your password will expire in next %d days\n",
				  max_passwd_age);

	xml = glade_xml_new (FILENAME, ROOTNODE, NULL);
	g_return_if_fail (xml != NULL);
	top_widget = glade_xml_get_widget (xml, ROOTNODE);
	g_return_if_fail (top_widget != NULL);

	warning_msg_label = GTK_LABEL (glade_xml_get_widget (xml, 
						"passwd_exp_label"));
	gtk_label_set_text (warning_msg_label, passwd_expiry_msg);

	change_passwd_button = glade_xml_get_widget (xml, 
						"change_passwd_button");
	gtk_widget_set_sensitive (change_passwd_button, TRUE);
/*
	g_signal_connect (change_passwd_button, 
			  "clicked", 
			  G_CALLBACK (change_passwd_cb), 
			  account);
*/
	response = gtk_dialog_run (GTK_DIALOG (top_widget));

	gtk_widget_destroy (top_widget);
	g_object_unref (xml);
	g_free (passwd_expiry_msg);
}

static void
find_passwd_exp_period (ExchangeAccount *account, E2kGlobalCatalogEntry *entry)
{
	double max_pwd_age = 0;
	int max_pwd_age_days;
	E2kOperation gcop;
	E2kGlobalCatalogStatus gcstatus;

	/* If user has not selected password expiry warning option, return */
	if (account->priv->passwd_exp_warn_period == -1)
		return;

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
		return;
       
	if (entry->user_account_control & ADS_UF_DONT_EXPIRE_PASSWORD) 
		return;         /* Password is not set to expire */

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
			display_passwd_expiry_message (max_pwd_age_days, 
						       account);
		}
	} 
}

char *
exchange_account_get_password (ExchangeAccount *account)
{
	return e_passwords_get_password ("Exchange", account->priv->password_key);
}

void
exchange_account_forget_password (ExchangeAccount *account)
{
	e_passwords_forget_password ("Exchange", account->priv->password_key);
}

#ifdef HAVE_KRB5
ExchangeAccountResult
exchange_account_set_password (ExchangeAccount *account, char *old_pass, char *new_pass)
{
	E2kKerberosResult result;
	char *domain;

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
	if (result != E2K_KERBEROS_OK || result != E2K_KERBEROS_PASSWORD_TOO_WEAK) {
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
}
#endif

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

	if (account->priv->ctx) {
		g_object_unref (account->priv->ctx);
		account->priv->ctx = NULL;
	}

	account->priv->account_online = FALSE;
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
	E2kContext *ctx;
	ExchangeAccountResult result;
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), FALSE);

	if (!account->priv->account_online) {
		ctx = exchange_account_connect (account, NULL, &result); /* error not handled. */
		return ctx ? TRUE : FALSE;
	} else {
		return TRUE;
	}
}
#if 0
SURF :
/**
 * exchange_account_is_offline:
 * @account: an #ExchangeAccount
 *
 * Return value: Returns TRUE if account is offline
 **/
void
exchange_account_is_offline (ExchangeAccount *account, int *state)
{
	*state = UNSUPPORTED_MODE;

	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	exchange_component_is_offline (global_exchange_component, state);
}
#endif
void
exchange_account_is_offline (ExchangeAccount *account, int *state)
{
	// SURF : Dummy
	*state = ONLINE_MODE;
}

// SURF : Picked this from gal/util/e-util.c
/* This only makes a filename safe for usage as a filename.  It still may have shell meta-characters in it. */
static void
e_filename_make_safe (gchar *string)
{
	gchar *p, *ts;
	gunichar c;
	
	g_return_if_fail (string != NULL);
	p = string;

	while(p && *p) {
		c = g_utf8_get_char (p);
		ts = p;
		p = g_utf8_next_char (p);
		if (!g_unichar_isprint(c) || ( c < 0xff && strchr (" /'\"`&();|<>$%{}!", c&0xff ))) {
			while (ts<p) 	
				*ts++ = '_';
		}
	}
}


static gboolean
setup_account_hierarchies (ExchangeAccount *account)
{
	ExchangeHierarchy *hier, *personal_hier;
	ExchangeAccountFolderResult fresult;
	char *phys_uri_prefix, *dir;
	DIR *d;
	struct dirent *dent;
	int offline;

	exchange_account_is_offline (account, &offline);

	if (offline == UNSUPPORTED_MODE)
		return FALSE;

	/* Set up Personal Folders hierarchy */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/personal",
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
	personal_hier = hier;

	/* Favorite Public Folders */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/favorites",
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
	phys_uri_prefix = g_strdup_printf ("exchange://%s/public",
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
	d = opendir (account->storage_dir);
	if (d) {
		while ((dent = readdir (d))) {
			if (!strchr (dent->d_name, '@'))
				continue;
			dir = g_strdup_printf ("%s/%s", account->storage_dir,
					       dent->d_name);
			hier = exchange_hierarchy_foreign_new_from_dir (account, dir);
			g_free (dir);
			if (!hier)
				continue;

			setup_hierarchy_foreign (account, hier);
		}
		closedir (d);
	}

	/* Scan the personal and favorite folders so we can resolve references
	 * to the Calendar, Contacts, etc even if the tree isn't
	 * opened.
	 */
	fresult = exchange_hierarchy_scan_subtree (personal_hier,
						   personal_hier->toplevel,
						   (offline == OFFLINE_MODE));
	if (fresult != EXCHANGE_ACCOUNT_FOLDER_OK) {
		account->priv->connecting = FALSE;
		return FALSE;
	}

	account->mbox_size = exchange_hierarchy_webdav_get_total_folder_size (
					EXCHANGE_HIERARCHY_WEBDAV (personal_hier));

	fresult = exchange_hierarchy_scan_subtree (
		account->priv->favorites_hierarchy,
		account->priv->favorites_hierarchy->toplevel,
		(offline == OFFLINE_MODE));
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
exchange_account_connect (ExchangeAccount *account, const char *pword, 
			  ExchangeAccountResult *info_result)
{
	E2kAutoconfig *ac;
	E2kAutoconfigResult result;
	E2kHTTPStatus status;
	gboolean redirected = FALSE;
	E2kResult *results;
	int nresults;
	GByteArray *entryid;
	const char *timezone;
	char *old_password, *new_password;
	E2kGlobalCatalogStatus gcstatus;
	E2kGlobalCatalogEntry *entry;
	E2kOperation gcop;
	char *user_name = NULL;
	int offline;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	*info_result = EXCHANGE_ACCOUNT_CONNECT_SUCCESS;

	exchange_account_is_offline (account, &offline);
	g_mutex_lock (account->priv->connect_lock);

	if ((account->priv->connecting) || (offline == OFFLINE_MODE)){
		g_mutex_unlock (account->priv->connect_lock);
		if (offline == OFFLINE_MODE) {
			setup_account_hierarchies (account);

			*info_result = EXCHANGE_ACCOUNT_OFFLINE;
		}
		return NULL;
	} else if (account->priv->ctx) {
		g_mutex_unlock (account->priv->connect_lock);
		return account->priv->ctx;
	}

	account->priv->connecting = TRUE;
	g_mutex_unlock (account->priv->connect_lock);

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
				      account->priv->ad_limit);

 try_password_again:
	if (!pword) {
		if (!get_password (account, ac, *info_result)) {
			account->priv->connecting = FALSE;
			return NULL;
		}
	}

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
			return NULL;
/*
			old_password = exchange_account_get_password (account);
			//new_password = exchange_get_new_password (old_password, 0);

			if (new_password) {
				ExchangeAccountResult res;
				res = exchange_account_set_password (account, old_password, new_password);
				if (res == EXCHANGE_ACCOUNT_PASSWORD_CHANGE_SUCCESS) {
					e2k_autoconfig_set_password (ac, new_password);
					goto try_connect_again;
				}
				else
					*info_result = res;
				
				g_free (old_password);
				g_free (new_password);
			}
			else {
				*info_result = EXCHANGE_ACCOUNT_PASSWORD_EXPIRED;
				g_free (old_password);
			}

			result = E2K_AUTOCONFIG_CANCELLED;
*/
		}
#endif
		switch (result) {
		case E2K_AUTOCONFIG_AUTH_ERROR:
			if (!pword)
				goto try_password_again;

			*info_result = EXCHANGE_ACCOUNT_PASSWORD_INCORRECT;
			e2k_autoconfig_free (ac);
			account->priv->connecting = FALSE;
			return NULL;

		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN:
			if (!pword)
				goto try_password_again;

			*info_result = EXCHANGE_ACCOUNT_DOMAIN_ERROR;
			e2k_autoconfig_free (ac);
			account->priv->connecting = FALSE;
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

		// SURF : if (!exchange_component_is_interactive (global_exchange_component))
			// SURF : return NULL;

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
			return NULL;
		default:
			*info_result = EXCHANGE_ACCOUNT_UNKNOWN_ERROR;
			break;
		}

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

		timezone = e2k_properties_get_prop (results[0].props, E2K_PR_EXCHANGE_TIMEZONE);
		if (timezone)
			account->default_timezone = g_strdup (timezone);
	}

	if (!setup_account_hierarchies (account)) {
		*info_result = EXCHANGE_ACCOUNT_UNKNOWN_ERROR;
		return NULL; /* FIXME: what error has happened? */
	}

	/* Find the password expiery peripod and display warning */
	find_passwd_exp_period(account, entry);
	
	/* Check for quota warnings */
	e2k_operation_init (&gcop);
	gcstatus = e2k_global_catalog_lookup (account->priv->gc, &gcop,
                                            E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
                                            account->priv->identity_email,
					    E2K_GLOBAL_CATALOG_LOOKUP_QUOTA,
                                            &entry);	
	e2k_operation_free (&gcop);

	/* FIXME: quota warnings are not yet marked for translation!! */
	/* FIXME: warning message should have quota limit value and optionally current
	 * usage 
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
	
	account->priv->connected = TRUE;
	account->priv->account_online = TRUE;
	account->priv->connecting = FALSE;

	g_signal_connect (account->priv->ctx, "redirect",
			  G_CALLBACK (context_redirect), account);

	g_signal_emit (account, signals[CONNECTED], 0, account->priv->ctx);
	return account->priv->ctx;
}

/**
 * exchange_account_is_offline_sync_set:
 * @account: an #ExchangeAccount
 *
 * Return value: TRUE if offline_sync is set for @account and FALSE if not.
 */
void
exchange_account_is_offline_sync_set (ExchangeAccount *account, int *mode)
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
const char *
exchange_account_get_standard_uri (ExchangeAccount *account, const char *item)
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
char *
exchange_account_get_standard_uri_for (ExchangeAccount *account,
				       const char *home_uri,
				       const char *std_uri_prop)
{
	char *foreign_uri, *prop;
	E2kHTTPStatus status;
	E2kResult *results;
	int nresults = 0;

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
char *
exchange_account_get_foreign_uri (ExchangeAccount *account,
				  E2kGlobalCatalogEntry *entry,
				  const char *std_uri_prop)
{
	char *home_uri, *foreign_uri;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	if (account->priv->uris_use_email) {
		char *mailbox;

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
			     const char *path_or_uri)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);
	
	return g_hash_table_lookup (account->priv->folders, path_or_uri);
}

static int
folder_comparator (const void *a, const void *b)
{
	EFolder **fa = (EFolder **)a;
	EFolder **fb = (EFolder **)b;

	return strcmp (e_folder_exchange_get_path (*fa),
		       e_folder_exchange_get_path (*fb));
}

static void
add_folder (gpointer key, gpointer value, gpointer folders)
{
	EFolder *folder = value;

	/* Each folder appears under three different keys, but
	 * we only want to add it to the results array once. So
	 * we only add when we see the "path" key.
	 */
	if (!strcmp (key, e_folder_exchange_get_path (folder))) 
		g_ptr_array_add (folders, folder);
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
	g_hash_table_foreach (account->priv->folders, add_folder, folders);

	qsort (folders->pdata, folders->len,
	       sizeof (EFolder *), folder_comparator);

	return folders;
}	

/**
 * exchange_account_get_quota_limit:
 * @account: an #ExchangeAccount
 *
 * Return the value of the quota limit reached.
 *
 * Return value: an int
 **/
int
exchange_account_get_quota_limit (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 0);

	return account->priv->quota_limit;
}

int
exchange_account_check_password_expiry (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), 0);
	return -1;
}

char *
exchange_account_get_username (ExchangeAccount *account)
{
	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);
	
	return account->priv->username;
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
				     const char *folder_name,
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
					const char *folder_name)
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
					const char *old_name,
					const char *new_name)
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
	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));

	return exchange_folder_size_get_model (account->priv->fsize);
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
	char *enc_user, *mailbox;
	const char *param, *proto="http", *owa_path, *pf_server, *owa_url; 
	const char *passwd_exp_warn_period, *offline_sync;
	E2kUri *uri;

	uri = e2k_uri_new (adata->source->url);
	if (!uri) {
		g_warning ("Could not parse exchange uri '%s'",
			   adata->source->url);
		return NULL;
	}

	account = g_object_new (EXCHANGE_TYPE_ACCOUNT, NULL);
	account->priv->account_list = account_list;
	g_object_ref (account_list);
	account->priv->account = adata;
	g_object_ref (adata);

	account->account_name = g_strdup (adata->name);

	account->storage_dir = g_strdup_printf ("%s/.evolution/exchange/%s@%s",
						g_get_home_dir (),
						uri->user, uri->host);
	account->account_filename = strrchr (account->storage_dir, '/') + 1;
	e_filename_make_safe (account->account_filename);

	/* Identity info */
	account->priv->identity_name = g_strdup (adata->id->name);
	account->priv->identity_email = g_strdup (adata->id->address);

	/* URI, etc, info */
	enc_user = e2k_uri_encode (uri->user, FALSE, "@/;:");
	account->priv->uri_authority = g_strdup_printf ("%s@%s", enc_user,
							uri->host);
	g_free (enc_user);

	account->priv->source_uri = g_strdup_printf ("exchange://%s/", account->priv->uri_authority);

	/* Backword compatibility; FIXME, we should just migrate the
	 * password from this to source_uri.
	 */
	account->priv->password_key = g_strdup_printf ("exchange://%s", account->priv->uri_authority);

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
