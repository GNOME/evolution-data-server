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

/* ExchangeHierarchyFavorites: class for the "Favorites" Public Folders
 * hierarchy (and favorites-handling code).
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-favorites.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "exchange-esource.h"

#include <libedataserver/e-source-list.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _ExchangeHierarchyFavoritesPrivate {
	gchar *public_uri, *shortcuts_uri;
	GHashTable *shortcuts;
};

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY_SOMEDAV
static ExchangeHierarchySomeDAVClass *parent_class = NULL;

static GPtrArray *get_hrefs (ExchangeHierarchySomeDAV *hsd);
static ExchangeAccountFolderResult remove_folder (ExchangeHierarchy *hier,
						  EFolder *folder);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchySomeDAVClass *somedav_class =
		EXCHANGE_HIERARCHY_SOMEDAV_CLASS (object_class);
	ExchangeHierarchyClass *hier_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
	hier_class->remove_folder = remove_folder;
	somedav_class->get_hrefs = get_hrefs;
}

static void
init (GObject *object)
{
	ExchangeHierarchyFavorites *hfav = EXCHANGE_HIERARCHY_FAVORITES (object);

	hfav->priv = g_new0 (ExchangeHierarchyFavoritesPrivate, 1);
	hfav->priv->shortcuts = g_hash_table_new_full (g_str_hash, g_str_equal,
						       g_free, g_free);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchyFavorites *hfav = EXCHANGE_HIERARCHY_FAVORITES (object);

	g_hash_table_destroy (hfav->priv->shortcuts);
	g_free (hfav->priv->public_uri);
	g_free (hfav->priv->shortcuts_uri);
	g_free (hfav->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_favorites, ExchangeHierarchyFavorites, class_init, init, PARENT_TYPE)

static void
add_hrefs (ExchangeHierarchy *hier, EFolder *folder, gpointer hrefs)
{
	g_ptr_array_add (hrefs, (gchar *)e2k_uri_path (e_folder_exchange_get_internal_uri (folder)));
}

static const gchar *shortcuts_props[] = {
	PR_FAV_DISPLAY_NAME,		/* PR_DISPLAY_NAME of referent */
	PR_FAV_DISPLAY_ALIAS,		/* if set, user-chosen display name */
	PR_FAV_PUBLIC_SOURCE_KEY,	/* PR_SOURCE_KEY of referent */
	PR_FAV_PARENT_SOURCE_KEY,	/* PR_FAV_PUBLIC_SOURCE_KEY of parent */
	PR_FAV_LEVEL_MASK		/* depth in hierarchy (first level is 1) */
};
static const gint n_shortcuts_props = G_N_ELEMENTS (shortcuts_props);

static GPtrArray *
get_hrefs (ExchangeHierarchySomeDAV *hsd)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (hsd);
	ExchangeHierarchyFavorites *hfav = EXCHANGE_HIERARCHY_FAVORITES (hsd);
	E2kContext *ctx = exchange_account_get_context (hier->account);
	GPtrArray *hrefs;
	E2kResultIter *iter;
	E2kResult *result, *results;
	E2kHTTPStatus status;
	GByteArray *source_key;
	const gchar *prop = E2K_PR_DAV_HREF, *shortcut_uri;
	gchar *perm_url, *folder_uri;
	gint i, nresults = 0, mode;

	hrefs = g_ptr_array_new ();

	exchange_account_is_offline (hier->account, &mode);
	if (mode != ONLINE_MODE) {
		exchange_hierarchy_webdav_offline_scan_subtree (EXCHANGE_HIERARCHY (hfav), add_hrefs, hrefs);
		return hrefs;
	}
	/* Scan the shortcut links and use PROPFIND to resolve the
	 * permanent_urls. Unfortunately it doesn't seem to be possible
	 * to BPROPFIND a group of permanent_urls.
	 */
	iter = e2k_context_search_start (ctx, NULL, hfav->priv->shortcuts_uri,
					 shortcuts_props, n_shortcuts_props,
					 NULL, NULL, TRUE);
	while ((result = e2k_result_iter_next (iter))) {
		shortcut_uri = result->href;
		source_key = e2k_properties_get_prop (result->props, PR_FAV_PUBLIC_SOURCE_KEY);
		if (!source_key)
			continue;

		perm_url = e2k_entryid_to_permanenturl (source_key, hfav->priv->public_uri);

		status = e2k_context_propfind (ctx, NULL, perm_url,
					       &prop, 1, &results, &nresults);
		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status) && nresults) {
			folder_uri = g_strdup (results[0].href);
			g_ptr_array_add (hrefs, folder_uri);
			g_hash_table_insert (hfav->priv->shortcuts,
					     g_strdup (folder_uri),
					     g_strdup (shortcut_uri));
			e2k_results_free (results, nresults);
		}

		g_free (perm_url);
	}

	status = e2k_result_iter_free (iter);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		/* FIXME: need to be able to return an error */
		for (i = 0; i < hrefs->len; i++)
			g_free (hrefs->pdata[i]);
		g_ptr_array_free (hrefs, TRUE);
		hrefs = NULL;
	}

	return hrefs;
}
/**
 * exchange_hierarchy_favorites_is_added:
 * @hier: the hierarchy
 * @folder: the Public Folder to check in the favorites tree
 *
 * Checks if @folder is present in the favorites hierarchy
 *
 * Return value: TRUE if @folder is already added as a favorite.
 **/

gboolean
exchange_hierarchy_favorites_is_added (ExchangeHierarchy *hier, EFolder *folder)
{
	ExchangeHierarchyFavorites *hfav =
		EXCHANGE_HIERARCHY_FAVORITES (hier);
	const gchar *folder_uri, *shortcut_uri;

	folder_uri = e_folder_exchange_get_internal_uri (folder);
	shortcut_uri = g_hash_table_lookup (hfav->priv->shortcuts, folder_uri);

	return shortcut_uri ? TRUE : FALSE;
}

static ExchangeAccountFolderResult
remove_folder (ExchangeHierarchy *hier, EFolder *folder)
{
	ExchangeHierarchyFavorites *hfav =
		EXCHANGE_HIERARCHY_FAVORITES (hier);
	const gchar *folder_uri, *shortcut_uri;
	E2kHTTPStatus status;
	const gchar *folder_type, *physical_uri;

	folder_uri = e_folder_exchange_get_internal_uri (folder);
	shortcut_uri = g_hash_table_lookup (hfav->priv->shortcuts, folder_uri);
	if (!shortcut_uri)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	status = e2k_context_delete (
		exchange_account_get_context (hier->account), NULL,
		shortcut_uri);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_hash_table_remove (hfav->priv->shortcuts, folder_uri);
		exchange_hierarchy_removed_folder (hier, folder);

		/* Temp Fix for remove fav folders. see #59168 */
		/* remove ESources */
		folder_type = e_folder_get_type_string (folder);
		physical_uri = e_folder_get_physical_uri (folder);

		if (strcmp (folder_type, "calendar") == 0) {
			remove_folder_esource (hier->account,
					       EXCHANGE_CALENDAR_FOLDER,
					       physical_uri);
		}
		else if (strcmp (folder_type, "tasks") == 0) {
			remove_folder_esource (hier->account,
					       EXCHANGE_TASKS_FOLDER,
					       physical_uri);
		}
		else if (strcmp (folder_type, "contacts") == 0) {
			remove_folder_esource (hier->account,
					       EXCHANGE_CONTACTS_FOLDER,
					       physical_uri);
		}
	}

	return exchange_hierarchy_webdav_status_to_folder_result (status);
}

/**
 * exchange_hierarchy_favorites_add_folder:
 * @hier: the hierarchy
 * @folder: the Public Folder to add to the favorites tree
 *
 * Adds a new folder to @hier.
 *
 * Return value: the folder result.
 **/
ExchangeAccountFolderResult
exchange_hierarchy_favorites_add_folder (ExchangeHierarchy *hier,
					 EFolder *folder)
{
	ExchangeHierarchyFavorites *hfav;
	E2kProperties *props;
	E2kHTTPStatus status;
	const gchar *folder_uri, *permanent_uri;
	gchar *shortcut_uri;

	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (folder), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (e_folder_exchange_get_hierarchy (folder)->type == EXCHANGE_HIERARCHY_PUBLIC, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	hfav = EXCHANGE_HIERARCHY_FAVORITES (hier);
	permanent_uri = e_folder_exchange_get_permanent_uri (folder);

	props = e2k_properties_new ();
	e2k_properties_set_string (props, PR_FAV_DISPLAY_NAME,
				   g_strdup (e_folder_get_name (folder)));
	if (permanent_uri)
		e2k_properties_set_binary (props, PR_FAV_PUBLIC_SOURCE_KEY,
				   e2k_permanenturl_to_entryid (permanent_uri));
	e2k_properties_set_int (props, PR_FAV_LEVEL_MASK, 1);

	status = e2k_context_proppatch_new (
		exchange_account_get_context (hier->account), NULL,
		hfav->priv->shortcuts_uri,
		e_folder_get_name (folder), NULL, NULL,
		props, &shortcut_uri, NULL);
	e2k_properties_free (props);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		folder_uri = e_folder_exchange_get_internal_uri (folder);

		g_hash_table_insert (hfav->priv->shortcuts,
				     g_strdup (folder_uri), shortcut_uri);
		return exchange_hierarchy_somedav_add_folder (EXCHANGE_HIERARCHY_SOMEDAV (hier),
							      folder_uri);
	} else
		return exchange_hierarchy_webdav_status_to_folder_result (status);
}

/**
 * exchange_hierarchy_favorites_new:
 * @account: an #ExchangeAccount
 * @hierarchy_name: the name of the hierarchy
 * @physical_uri_prefix: prefix for physical URIs in this hierarchy
 * @home_uri: the home URI of the owner's mailbox
 * @public_uri: the URI of the public folder tree
 * @owner_name: display name of the owner of the hierarchy
 * @owner_email: email address of the owner of the hierarchy
 * @source_uri: account source URI for folders in this hierarchy
 *
 * Creates a new Favorites hierarchy
 *
 * Return value: the new hierarchy.
 **/
ExchangeHierarchy *
exchange_hierarchy_favorites_new (ExchangeAccount *account,
				  const gchar *hierarchy_name,
				  const gchar *physical_uri_prefix,
				  const gchar *home_uri,
				  const gchar *public_uri,
				  const gchar *owner_name,
				  const gchar *owner_email,
				  const gchar *source_uri)
{
	ExchangeHierarchy *hier;
	ExchangeHierarchyFavorites *hfav;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	hier = g_object_new (EXCHANGE_TYPE_HIERARCHY_FAVORITES, NULL);

	hfav = (ExchangeHierarchyFavorites *)hier;
	hfav->priv->public_uri = g_strdup (public_uri);
	hfav->priv->shortcuts_uri = e2k_uri_concat (home_uri, "NON_IPM_SUBTREE/Shortcuts");

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hier),
					     account,
					     EXCHANGE_HIERARCHY_FAVORITES,
					     hierarchy_name,
					     physical_uri_prefix,
					     public_uri,
					     owner_name, owner_email,
					     source_uri,
					     TRUE);
	return hier;
}
