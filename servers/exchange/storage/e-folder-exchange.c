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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "libedataserver/e-source-list.h"
#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-xml-utils.h"

#include "e-folder-exchange.h"
#include "e2k-path.h"
#include "e2k-uri.h"
#include "exchange-account.h"
#include "exchange-esource.h"
#include "exchange-hierarchy.h"

#define d(x)

struct _EFolderExchangePrivate {
	ExchangeHierarchy *hier;
	gchar *internal_uri, *permanent_uri;
	gchar *outlook_class, *storage_dir;
	gchar *path;
	gint64 folder_size;
	gboolean has_subfolders;
	gboolean rescan_tree;
};

#define PARENT_TYPE E_TYPE_FOLDER
static EFolderClass *parent_class = NULL;

#define EF_CLASS(hier) (E_FOLDER_CLASS (G_OBJECT_GET_CLASS (hier)))

static void dispose (GObject *object);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void
init (GObject *object)
{
	EFolderExchange *folder = E_FOLDER_EXCHANGE (object);

	folder->priv = g_new0 (EFolderExchangePrivate, 1);
	folder->priv->rescan_tree = TRUE;
}

static void
dispose (GObject *object)
{
	EFolderExchange *folder = E_FOLDER_EXCHANGE (object);

	if (folder->priv->hier) {
		g_object_unref (folder->priv->hier);
		folder->priv->hier = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	EFolderExchange *folder = E_FOLDER_EXCHANGE (object);

	g_free (folder->priv->internal_uri);
	g_free (folder->priv->permanent_uri);
	g_free (folder->priv->outlook_class);
	g_free (folder->priv->storage_dir);
	g_free (folder->priv->path);
	g_free (folder->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (e_folder_exchange, EFolderExchange, class_init, init, PARENT_TYPE)

static gchar *
sanitize_path (const gchar *path)
{
	gchar **comps;
	gchar *new_path = NULL;

	if (!path)
		return g_strdup("");	/* ??? or NULL? */

	comps = g_strsplit (path, ";", 2);
	if (comps[1])
		new_path = g_strdup_printf ("%s%s", comps[0], comps[1]);
	else if (comps[0])
		new_path = g_strdup (comps[0]);

	g_strfreev (comps);
	return new_path;
}

#define d(x)

/**
 * e_folder_exchange_new:
 * @hier: the #ExchangeHierarchy containing the new folder
 * @name: the display name of the folder
 * @type: the Evolution type of the folder (eg, "mail")
 * @outlook_class: the Outlook IPM class of the folder (eg, "IPM.Note")
 * @physical_uri: the "exchange:" URI of the folder
 * @internal_uri: the "http:" URI of the folder
 *
 * Return value: a new #EFolderExchange
 **/
EFolder *
e_folder_exchange_new (ExchangeHierarchy *hier, const gchar *name,
		       const gchar *type, const gchar *outlook_class,
		       const gchar *physical_uri, const gchar *internal_uri)
{
	EFolderExchange *efe;
	EFolder *ef;
	gchar *sanitized_path;

	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (type != NULL, NULL);
	g_return_val_if_fail (physical_uri != NULL, NULL);
	g_return_val_if_fail (internal_uri != NULL, NULL);

	d(g_print ("e_folder_exchange_new: name=[%s], type=[%s], internal_uri=[%s], physical_uri=[%s]\n",
		   name, type, internal_uri, physical_uri));

	efe = g_object_new (E_TYPE_FOLDER_EXCHANGE, NULL);
	ef = (EFolder *)efe;

	e_folder_construct (ef, name, type, "");

	efe->priv->hier = hier;
	g_object_ref (hier);

	efe->priv->internal_uri = g_strdup (internal_uri);
	e_folder_set_physical_uri (ef, physical_uri);

	sanitized_path = sanitize_path (e2k_uri_path (physical_uri));
	e2k_uri_decode (sanitized_path);
	efe->priv->path = sanitized_path;
	d(g_print ("e_folder_exchange_new: sanitized=[%s]\n", sanitized_path));

	efe->priv->outlook_class = g_strdup (outlook_class);

	/* Add ESources */
	if (hier->type == EXCHANGE_HIERARCHY_PERSONAL ||
	    hier->type == EXCHANGE_HIERARCHY_FAVORITES) {

		if ((strcmp (type, "calendar") == 0) ||
		    (strcmp (type, "calendar/public") == 0)) {
			add_folder_esource (hier->account,
					    EXCHANGE_CALENDAR_FOLDER,
					    name,
					    physical_uri);
		}
		else if ((strcmp (type, "tasks") == 0) ||
			 (strcmp (type, "tasks/public") == 0)) {
			add_folder_esource (hier->account,
					    EXCHANGE_TASKS_FOLDER,
					    name,
					    physical_uri);
		}
		else if ((strcmp (type, "contacts") == 0) ||
			 (strcmp (type, "contacts/public") == 0)) {
			add_folder_esource (hier->account,
					    EXCHANGE_CONTACTS_FOLDER,
					    name,
					    physical_uri);
		}
	}
	return ef;
}

/**
 * e_folder_exchange_get_internal_uri:
 * @folder: an #EFolderExchange
 *
 * Returns the folder's internal (http/https) URI. The caller
 * should not cache this value, since it may change if the server
 * sends a redirect when we try to use it.
 *
 * Return value: @folder's internal (http/https) URI
 **/
const gchar *
e_folder_exchange_get_internal_uri (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->internal_uri;
}

/**
 * e_folder_exchange_set_internal_uri:
 * @folder: an #EFolderExchange
 * @internal_uri: new internal_uri value
 *
 * Updates @folder's internal URI to reflect a redirection response
 * from the server.
 **/
void
e_folder_exchange_set_internal_uri (EFolder *folder, const gchar *internal_uri)
{
	EFolderExchange *efe;

	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));
	g_return_if_fail (internal_uri != NULL);

	efe = E_FOLDER_EXCHANGE (folder);
	g_free (efe->priv->internal_uri);
	efe->priv->internal_uri = g_strdup (internal_uri);
}

/**
 * e_folder_exchange_get_path:
 * @folder: an #EFolderExchange
 *
 * Return value: @folder's path within its Evolution storage
 **/
const gchar *
e_folder_exchange_get_path (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->path;
}

/**
 * e_folder_exchange_get_permanent_uri:
 * @folder: an #EFolderExchange
 *
 * Returns the folder's permanent URI. See docs/entryids for more
 * details.
 *
 * Return value: @folder's permanent URI
 **/
const gchar *
e_folder_exchange_get_permanent_uri (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->permanent_uri;
}

/**
 * e_folder_exchange_set_permanent_uri:
 * @folder: an #EFolderExchange
 * @permanent_uri: permanent_uri value
 *
 * Sets @folder's permanent URI (which must, for obvious reasons, have
 * previously been unset).
 **/
void
e_folder_exchange_set_permanent_uri (EFolder *folder, const gchar *permanent_uri)
{
	EFolderExchange *efe;

	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	efe = E_FOLDER_EXCHANGE (folder);
	g_return_if_fail (efe->priv->permanent_uri == NULL && permanent_uri != NULL);

	efe->priv->permanent_uri = g_strdup (permanent_uri);
}

/**
 * e_folder_exchange_get_folder_size:
 * @folder: an #EFolderExchange
 *
 * Returns the folder's size. See docs/entryids for more
 * details.
 *
 * Return value: @folder's size
 **/
gint64
e_folder_exchange_get_folder_size (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), -1);

	return E_FOLDER_EXCHANGE (folder)->priv->folder_size;
}

/**
 * e_folder_exchange_set_folder_size:
 * @folder: an #EFolderExchange
 * @folder_size: folder size
 *
 * Sets @folder's folder_size
 **/
void
e_folder_exchange_set_folder_size (EFolder *folder, gint64 folder_size)
{
	EFolderExchange *efe;

	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	efe = E_FOLDER_EXCHANGE (folder);

	efe->priv->folder_size = folder_size;
}

/**
 * e_folder_exchange_get_has_subfolders:
 * @folder: an #EFolderExchange
 *
 * Return value: whether or not @folder has subfolders
 **/
gboolean
e_folder_exchange_get_has_subfolders (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), FALSE);

	return E_FOLDER_EXCHANGE (folder)->priv->has_subfolders;
}

/**
 * e_folder_exchange_set_has_subfolders
 * @folder: an #EFolderExchange
 * @has_subfolders: whether or not @folder has subfolders
 *
 * Sets @folder's has_subfolders flag.
 **/
void
e_folder_exchange_set_has_subfolders (EFolder *folder,
				      gboolean has_subfolders)
{
	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	E_FOLDER_EXCHANGE (folder)->priv->has_subfolders = has_subfolders;
}

/**
 * e_folder_exchange_get_rescan_tree:
 * @folder: an #EFolderExchange
 *
 * Return value: whether or not to rescan @folder tree
 **/
gboolean
e_folder_exchange_get_rescan_tree (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), FALSE);

	return E_FOLDER_EXCHANGE (folder)->priv->rescan_tree;
}

/**
 * e_folder_exchange_set_rescan_tree
 * @folder: an #EFolderExchange
 * @rescan_tree: whether or not @folder needs to be rescanned
 *
 * Sets @folder's has_subfolders flag.
 **/
void
e_folder_exchange_set_rescan_tree (EFolder *folder,
				   gboolean rescan_tree)
{
	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	E_FOLDER_EXCHANGE (folder)->priv->rescan_tree = rescan_tree;
}

/**
 * e_folder_exchange_get_outlook_class:
 * @folder: an #EFolderExchange
 *
 * Return value: @folder's Outlook IPM class
 **/
const gchar *
e_folder_exchange_get_outlook_class (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->outlook_class;
}

/**
 * e_folder_exchange_get_hierarchy
 * @folder: an #EFolderExchange
 *
 * Return value: @folder's hierarchy
 **/
ExchangeHierarchy *
e_folder_exchange_get_hierarchy (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->hier;
}

/**
 * e_folder_exchange_get_storage_file:
 * @folder: an #EFolderExchange
 * @filename: name of a file
 *
 * This returns a unique filename ending in @filename in the local
 * storage space reserved for @folder.
 *
 * Return value: the full filename, which must be freed.
 **/
gchar *
e_folder_exchange_get_storage_file (EFolder *folder, const gchar *filename)
{
	EFolderExchange *efe;
	gchar *path;

	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	efe = (EFolderExchange *)folder;

	if (!efe->priv->storage_dir) {
		efe->priv->storage_dir = e_path_to_physical (
			efe->priv->hier->account->storage_dir,
			efe->priv->path);
		g_mkdir_with_parents (efe->priv->storage_dir, 0755);
	}

	path = g_build_filename (efe->priv->storage_dir, filename, NULL);
	return path;
}

/**
 * e_folder_exchange_save_to_file:
 * @folder: the folder
 * @filename: a filename
 *
 * Saves all relevant information about @folder to @filename.
 *
 * Return value: success or failure
 **/
gboolean
e_folder_exchange_save_to_file (EFolder *folder, const gchar *filename)
{
	xmlDoc *doc;
	xmlNode *root;
	const gchar *name, *type, *outlook_class;
	const gchar *physical_uri, *internal_uri, *permanent_uri;
	gchar *folder_size;
	gint64 fsize;
	gint status;

	name = e_folder_get_name (folder);
	type = e_folder_get_type_string (folder);
	outlook_class = e_folder_exchange_get_outlook_class (folder);
	physical_uri = e_folder_get_physical_uri (folder);
	internal_uri = e_folder_exchange_get_internal_uri (folder);
	permanent_uri = e_folder_exchange_get_permanent_uri (folder);

	g_return_val_if_fail (name && type && physical_uri && internal_uri,
			      FALSE);

	if ((fsize = e_folder_exchange_get_folder_size (folder)) >= 0)
		folder_size = g_strdup_printf ("%" G_GINT64_FORMAT, fsize);
	else
		return FALSE;

	doc = xmlNewDoc ((xmlChar *) "1.0");
	root = xmlNewDocNode (doc, NULL, (xmlChar *) "connector-folder", NULL);
	xmlNewProp (root, (xmlChar *) "version", (xmlChar *) "1");
	xmlDocSetRootElement (doc, root);

	xmlNewChild (
		root, NULL,
		(xmlChar *) "displayname",
		(xmlChar *) name);
	xmlNewChild (
		root, NULL,
		(xmlChar *) "type",
		(xmlChar *) type);
	xmlNewChild (
		root, NULL,
		(xmlChar *) "outlook_class",
		(xmlChar *) outlook_class);
	xmlNewChild (
		root, NULL,
		(xmlChar *) "physical_uri",
		(xmlChar *) physical_uri);
	xmlNewChild (
		root, NULL,
		(xmlChar *) "internal_uri",
		(xmlChar *) internal_uri);
	xmlNewChild (
		root, NULL,
		(xmlChar *) "folder_size",
		(xmlChar *) folder_size);
	if (permanent_uri)
		xmlNewChild (
			root, NULL,
			(xmlChar *) "permanent_uri",
			(xmlChar *) permanent_uri);

	status = e_xml_save_file (filename, doc);

	if (status < 0)
		g_unlink (filename);

	xmlFreeDoc (doc);

	g_free (folder_size);

	return status == 0;
}

/**
 * e_folder_exchange_new_from_file:
 * @hier: the hierarchy to create the folder under
 * @filename: a filename
 *
 * Loads information about a folder from a saved file.
 *
 * Return value: the folder, or %NULL on a failed load.
 **/
EFolder *
e_folder_exchange_new_from_file (ExchangeHierarchy *hier, const gchar *filename)
{
	EFolder *folder = NULL;
	xmlDoc *doc;
	xmlNode *root, *node;
	xmlChar *version, *display_name = NULL;
	xmlChar *type = NULL, *outlook_class = NULL;
	xmlChar *physical_uri = NULL, *internal_uri = NULL;
	xmlChar *permanent_uri = NULL;
	xmlChar *folder_size = NULL;

	doc = e_xml_parse_file (filename);

	if (!doc)
		return NULL;

	root = xmlDocGetRootElement (doc);
	if (root == NULL || strcmp ((gchar *) root->name, "connector-folder") != 0) {
		xmlFreeDoc (doc);
		return NULL;
	}
	version = xmlGetProp (root, (xmlChar *) "version");
	if (!version) {
		xmlFreeDoc (doc);
		return NULL;
	}
	if (strcmp ((gchar *) version, "1") != 0) {
		xmlFreeDoc (doc);
		xmlFree (version);
		return NULL;
	}
	xmlFree (version);

	node = e_xml_get_child_by_name (root, (xmlChar *) "displayname");
	if (!node)
		goto done;
	display_name = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, (xmlChar *) "type");
	if (!node)
		goto done;
	type = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, (xmlChar *) "outlook_class");
	if (!node)
		goto done;
	outlook_class = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, (xmlChar *) "physical_uri");
	if (!node)
		goto done;
	physical_uri = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, (xmlChar *) "internal_uri");
	if (!node)
		goto done;
	internal_uri = xmlNodeGetContent (node);

	if (!display_name || !type || !physical_uri || !internal_uri)
		goto done;

	folder = e_folder_exchange_new (
		hier,
		(gchar *) display_name,
		(gchar *) type,
		(gchar *) outlook_class,
		(gchar *) physical_uri,
		(gchar *) internal_uri);

	node = e_xml_get_child_by_name (root, (xmlChar *) "permanent_uri");
	if (node) {
		permanent_uri = xmlNodeGetContent (node);
		e_folder_exchange_set_permanent_uri (folder, (gchar *) permanent_uri);
	}

	node = e_xml_get_child_by_name (root, (xmlChar *) "folder_size");
	if (node) {
		folder_size = xmlNodeGetContent (node);
		e_folder_exchange_set_folder_size (folder, atoi ((gchar *) folder_size));
	}

 done:
	xmlFree (display_name);
	xmlFree (type);
	xmlFree (outlook_class);
	xmlFree (physical_uri);
	xmlFree (internal_uri);
	xmlFree (permanent_uri);
	xmlFree (folder_size);
	xmlFreeDoc (doc);

	return folder;
}

/* E2kContext wrappers */
#define E_FOLDER_EXCHANGE_CONTEXT(efe) (exchange_account_get_context (((EFolderExchange *)efe)->priv->hier->account))
#define E_FOLDER_EXCHANGE_URI(efe) (((EFolderExchange *)efe)->priv->internal_uri)

/**
 * e_folder_exchange_propfind:
 * @folder: the folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @props: array of properties to find
 * @nprops: length of @props
 * @results: on return, the results
 * @nresults: length of @results
 *
 * Performs a PROPFIND operation on @folder. This is a convenience
 * wrapper around e2k_context_propfind(), qv.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e_folder_exchange_propfind (EFolder *folder, E2kOperation *op,
			    const gchar **props, gint nprops,
			    E2kResult **results, gint *nresults)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), E2K_HTTP_MALFORMED);

	return e2k_context_propfind (
		E_FOLDER_EXCHANGE_CONTEXT (folder), op,
		E_FOLDER_EXCHANGE_URI (folder),
		props, nprops, results, nresults);
}

/**
 * e_folder_exchange_bpropfind_start:
 * @folder: the folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @hrefs: array of URIs, relative to @folder
 * @nhrefs: length of @hrefs
 * @props: array of properties to find
 * @nprops: length of @props
 *
 * Begins a BPROPFIND (bulk PROPFIND) operation on @folder for @hrefs.
 * This is a convenience wrapper around e2k_context_bpropfind_start(),
 * qv.
 *
 * Return value: an iterator for getting the results
 **/
E2kResultIter *
e_folder_exchange_bpropfind_start (EFolder *folder, E2kOperation *op,
				   const gchar **hrefs, gint nhrefs,
				   const gchar **props, gint nprops)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return e2k_context_bpropfind_start (
		E_FOLDER_EXCHANGE_CONTEXT (folder), op,
		E_FOLDER_EXCHANGE_URI (folder),
		hrefs, nhrefs, props, nprops);
}

/**
 * e_folder_exchange_search_start:
 * @folder: the folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @props: the properties to search for
 * @nprops: size of @props array
 * @rn: the search restriction
 * @orderby: if non-%NULL, the field to sort the search results by
 * @ascending: %TRUE for an ascending search, %FALSE for descending.
 *
 * Begins a SEARCH on the contents of @folder. This is a convenience
 * wrapper around e2k_context_search_start(), qv.
 *
 * Return value: an iterator for returning the search results
 **/
E2kResultIter *
e_folder_exchange_search_start (EFolder *folder, E2kOperation *op,
				const gchar **props, gint nprops,
				E2kRestriction *rn, const gchar *orderby,
				gboolean ascending)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return e2k_context_search_start (
		E_FOLDER_EXCHANGE_CONTEXT (folder), op,
		E_FOLDER_EXCHANGE_URI (folder),
		props, nprops, rn, orderby, ascending);
}

/**
 * e_folder_exchange_subscribe:
 * @folder: the folder to subscribe to notifications on
 * @type: the type of notification to subscribe to
 * @min_interval: the minimum interval (in seconds) between
 * notifications.
 * @callback: the callback to call when a notification has been
 * received
 * @user_data: data to pass to @callback.
 *
 * This subscribes to change notifications of the given @type on
 * @folder. This is a convenience wrapper around
 * e2k_context_subscribe(), qv.
 **/
void
e_folder_exchange_subscribe (EFolder *folder,
			     E2kContextChangeType type, gint min_interval,
			     E2kContextChangeCallback callback,
			     gpointer user_data)
{
	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	e2k_context_subscribe (E_FOLDER_EXCHANGE_CONTEXT (folder),
			       E_FOLDER_EXCHANGE_URI (folder),
			       type, min_interval, callback, user_data);
}

/**
 * e_folder_exchange_unsubscribe:
 * @folder: the folder to unsubscribe from
 *
 * Unsubscribes to all notifications on @folder. This is a convenience
 * wrapper around e2k_context_unsubscribe(), qv.
 **/
void
e_folder_exchange_unsubscribe (EFolder *folder)
{
	E2kContext *ctx;

	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	/* FIXME : This is a hack as of now. The free_folder in mail-stub
	gets called when we are in offline and the context is NULL then. */
	ctx = E_FOLDER_EXCHANGE_CONTEXT (folder);
	if (ctx) {
		e2k_context_unsubscribe (E_FOLDER_EXCHANGE_CONTEXT (folder),
					 E_FOLDER_EXCHANGE_URI (folder));
	}
}

/**
 * e_folder_exchange_transfer_start:
 * @source: the source folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @dest: the destination folder
 * @source_hrefs: an array of hrefs to move, relative to @source_folder
 * @delete_originals: whether or not to delete the original objects
 *
 * Starts a BMOVE or BCOPY (depending on @delete_originals) operation
 * on @source. This is a convenience wrapper around
 * e2k_context_transfer_start(), qv.
 *
 * Return value: the iterator for the results
 **/
E2kResultIter *
e_folder_exchange_transfer_start (EFolder *source, E2kOperation *op,
				  EFolder *dest, GPtrArray *source_hrefs,
				  gboolean delete_originals)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (source), NULL);

	return e2k_context_transfer_start (E_FOLDER_EXCHANGE_CONTEXT (source), op,
					   E_FOLDER_EXCHANGE_URI (source),
					   E_FOLDER_EXCHANGE_URI (dest),
					   source_hrefs, delete_originals);
}

/**
 * e_folder_exchange_put_new:
 * @folder: the folder to PUT the new item into
 * @op: pointer to an #E2kOperation to use for cancellation
 * @object_name: base name of the new object (not URI-encoded)
 * @test_callback: callback to use to test possible object URIs
 * @user_data: data for @test_callback
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @location: if not %NULL, will contain the Location of the POSTed
 * object on return
 * @repl_uid: if not %NULL, will contain the Repl-UID of the POSTed
 * object on return
 *
 * PUTs data into @folder with a new name based on @object_name. This
 * is a convenience wrapper around e2k_context_put_new(), qv.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e_folder_exchange_put_new (EFolder *folder,
			   E2kOperation *op,
			   const gchar *object_name,
			   E2kContextTestCallback test_callback,
			   gpointer user_data,
			   const gchar *content_type,
			   const gchar *body, gint length,
			   gchar **location, gchar **repl_uid)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), E2K_HTTP_MALFORMED);

	return e2k_context_put_new (E_FOLDER_EXCHANGE_CONTEXT (folder), op,
				    E_FOLDER_EXCHANGE_URI (folder),
				    object_name, test_callback, user_data,
				    content_type, body, length,
				    location, repl_uid);
}

/**
 * e_folder_exchange_proppatch_new:
 * @folder: the folder to PROPPATCH a new object in
 * @op: pointer to an #E2kOperation to use for cancellation
 * @object_name: base name of the new object (not URI-encoded)
 * @test_callback: callback to use to test possible object URIs
 * @user_data: data for @test_callback
 * @props: the properties to set/remove
 * @location: if not %NULL, will contain the Location of the
 * PROPPATCHed object on return
 * @repl_uid: if not %NULL, will contain the Repl-UID of the
 * PROPPATCHed object on return
 *
 * PROPPATCHes data into @folder with a new name based on
 * @object_name. This is a convenience wrapper around
 * e2k_context_proppatch_new(), qv.

 * Return value: the HTTP status
 **/
E2kHTTPStatus
e_folder_exchange_proppatch_new (EFolder *folder, E2kOperation *op,
				 const gchar *object_name,
				 E2kContextTestCallback test_callback,
				 gpointer user_data,
				 E2kProperties *props,
				 gchar **location, gchar **repl_uid)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), E2K_HTTP_MALFORMED);

	return e2k_context_proppatch_new (E_FOLDER_EXCHANGE_CONTEXT (folder), op,
					  E_FOLDER_EXCHANGE_URI (folder),
					  object_name,
					  test_callback, user_data,
					  props,
					  location, repl_uid);
}

/**
 * e_folder_exchange_bproppatch_start:
 * @folder: the folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @hrefs: array of URIs, relative to @folder
 * @nhrefs: length of @hrefs
 * @props: the properties to set/remove
 * @create: whether or not to create the objects if they do not exist
 *
 * Begins BPROPPATCHing @hrefs under @folder. This is a convenience
 * wrapper around e2k_context_bproppatch_start(), qv.
 *
 * Return value: an iterator for getting the results of the BPROPPATCH
 **/
E2kResultIter *
e_folder_exchange_bproppatch_start (EFolder *folder, E2kOperation *op,
				    const gchar **hrefs, gint nhrefs,
				    E2kProperties *props, gboolean create)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return e2k_context_bproppatch_start (E_FOLDER_EXCHANGE_CONTEXT (folder), op,
					     E_FOLDER_EXCHANGE_URI (folder),
					     hrefs, nhrefs, props, create);
}

/**
 * e_folder_exchange_bdelete_start:
 * @folder: the folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @hrefs: array of URIs, relative to @folder, to delete
 * @nhrefs: length of @hrefs
 *
 * Begins a BDELETE (bulk DELETE) operation in @folder for @hrefs.
 * This is a convenience wrapper around e2k_context_bdelete_start(),
 * qv.
 *
 * Return value: an iterator for returning the results
 **/
E2kResultIter *
e_folder_exchange_bdelete_start (EFolder *folder, E2kOperation *op,
				 const gchar **hrefs, gint nhrefs)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return e2k_context_bdelete_start (E_FOLDER_EXCHANGE_CONTEXT (folder), op,
					  E_FOLDER_EXCHANGE_URI (folder),
					  hrefs, nhrefs);
}

/**
 * e_folder_exchange_mkcol:
 * @folder: the folder to create
 * @op: pointer to an #E2kOperation to use for cancellation
 * @props: properties to set on the new folder, or %NULL
 * @permanent_url: if not %NULL, will contain the permanent URL of the
 * new folder on return
 *
 * Performs a MKCOL operation to create @folder, with optional
 * additional properties. This is a convenience wrapper around
 * e2k_context_mkcol(), qv.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e_folder_exchange_mkcol (EFolder *folder, E2kOperation *op,
			 E2kProperties *props,
			 gchar **permanent_url)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), E2K_HTTP_MALFORMED);

	return e2k_context_mkcol (E_FOLDER_EXCHANGE_CONTEXT (folder), op,
				  E_FOLDER_EXCHANGE_URI (folder),
				  props, permanent_url);
}

/**
 * e_folder_exchange_delete:
 * @folder: the folder to delete
 * @op: pointer to an #E2kOperation to use for cancellation
 *
 * Attempts to DELETE @folder. This is a convenience wrapper around
 * e2k_context_delete(), qv.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e_folder_exchange_delete (EFolder *folder, E2kOperation *op)
{
	ExchangeHierarchy *hier;
	const gchar *folder_type, *physical_uri;

	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), E2K_HTTP_MALFORMED);
	/* remove ESources */
	hier = e_folder_exchange_get_hierarchy (folder);

	if (hier->type == EXCHANGE_HIERARCHY_PERSONAL ||
	    hier->type == EXCHANGE_HIERARCHY_FAVORITES) {
		folder_type = e_folder_get_type_string (folder);
		physical_uri = e_folder_get_physical_uri (folder);

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

	return e2k_context_delete (E_FOLDER_EXCHANGE_CONTEXT (folder), op,
				   E_FOLDER_EXCHANGE_URI (folder));
}

/**
 * e_folder_exchange_transfer_dir:
 * @source: source folder
 * @op: pointer to an #E2kOperation to use for cancellation
 * @dest: destination folder
 * @delete_original: whether or not to delete the original folder
 * @permanent_url: if not %NULL, will contain the permanent URL of the
 * new folder on return
 *
 * Performs a MOVE or COPY (depending on @delete_original) operation
 * on @source. This is a convenience wrapper around
 * e2k_context_transfer_dir(), qv.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e_folder_exchange_transfer_dir (EFolder *source, E2kOperation *op,
				EFolder *dest, gboolean delete_original,
				gchar **permanent_url)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (source), E2K_HTTP_MALFORMED);

	return e2k_context_transfer_dir (E_FOLDER_EXCHANGE_CONTEXT (source), op,
					 E_FOLDER_EXCHANGE_URI (source),
					 E_FOLDER_EXCHANGE_URI (dest),
					 delete_original, permanent_url);
}
