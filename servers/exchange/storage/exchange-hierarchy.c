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

/* ExchangeHierarchy: abstract class for a hierarchy of folders
 * in an Exchange storage. Subclasses of ExchangeHierarchy implement
 * normal WebDAV hierarchies, the GAL hierarchy, and hierarchies
 * of individually-selected other users' folders.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy.h"
#include "e-folder-exchange.h"

enum {
	NEW_FOLDER,
	REMOVED_FOLDER,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

#define HIER_CLASS(hier) (EXCHANGE_HIERARCHY_CLASS (G_OBJECT_GET_CLASS (hier)))

static void dispose (GObject *object);
static void finalize (GObject *object);
static gboolean is_empty        (ExchangeHierarchy *hier);
static void add_to_storage      (ExchangeHierarchy *hier);
static ExchangeAccountFolderResult scan_subtree  (ExchangeHierarchy *hier,
						  EFolder *folder,
						  gint mode);
static void                        rescan        (ExchangeHierarchy *hier);
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

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *exchange_hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	exchange_hierarchy_class->is_empty = is_empty;
	exchange_hierarchy_class->add_to_storage = add_to_storage;
	exchange_hierarchy_class->rescan = rescan;
	exchange_hierarchy_class->scan_subtree = scan_subtree;
	exchange_hierarchy_class->create_folder = create_folder;
	exchange_hierarchy_class->remove_folder = remove_folder;
	exchange_hierarchy_class->xfer_folder = xfer_folder;

	/* signals */
	signals[NEW_FOLDER] =
		g_signal_new ("new_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchyClass, new_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[REMOVED_FOLDER] =
		g_signal_new ("removed_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchyClass, removed_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
dispose (GObject *object)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (object);

	if (hier->toplevel) {
		g_object_unref (hier->toplevel);
		hier->toplevel = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (object);

	g_free (hier->owner_name);
	g_free (hier->owner_email);
	g_free (hier->source_uri);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy, ExchangeHierarchy, class_init, NULL, PARENT_TYPE)

/**
 * exchange_hierarchy_new_folder:
 * @hier: the hierarchy
 * @folder: the new folder
 *
 * Emits a %new_folder signal.
 **/
void
exchange_hierarchy_new_folder (ExchangeHierarchy *hier,
			       EFolder *folder)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
	g_return_if_fail (E_IS_FOLDER (folder));

	g_signal_emit (hier, signals[NEW_FOLDER], 0, folder);
}

/**
 * exchange_hierarchy_removed_folder:
 * @hier: the hierarchy
 * @folder: the (about-to-be-)removed folder
 *
 * Emits a %removed_folder signal.
 **/
void
exchange_hierarchy_removed_folder (ExchangeHierarchy *hier,
				   EFolder *folder)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
        g_return_if_fail (E_IS_FOLDER (folder));

	g_signal_emit (hier, signals[REMOVED_FOLDER], 0, folder);
}

static gboolean
is_empty (ExchangeHierarchy *hier)
{
	return FALSE;
}

gboolean
exchange_hierarchy_is_empty (ExchangeHierarchy *hier)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), TRUE);

	return HIER_CLASS (hier)->is_empty (hier);
}

static ExchangeAccountFolderResult
create_folder (ExchangeHierarchy *hier, EFolder *parent,
	       const gchar *name, const gchar *type)
{
	return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
}

static ExchangeAccountFolderResult
remove_folder (ExchangeHierarchy *hier, EFolder *folder)
{
	return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
}

static ExchangeAccountFolderResult
xfer_folder (ExchangeHierarchy *hier, EFolder *source,
	     EFolder *dest_parent, const gchar *dest_name,
	     gboolean remove_source)
{
	return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
}

/**
 * exchange_hierarchy_create_folder:
 * @hier: the hierarchy
 * @parent: the parent folder of the new folder
 * @name: name of the new folder (UTF8)
 * @type: Evolution folder type of the new folder
 *
 * Attempts to create a new folder.
 *
 * Return value: the result code
 **/
ExchangeAccountFolderResult
exchange_hierarchy_create_folder (ExchangeHierarchy *hier, EFolder *parent,
				  const gchar *name, const gchar *type)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (parent), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (name != NULL, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (type != NULL, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return HIER_CLASS (hier)->create_folder (hier, parent, name, type);
}

/**
 * exchange_hierarchy_remove_folder:
 * @hier: the hierarchy
 * @folder: the folder to remove
 *
 * Attempts to remove a folder.
 *
 * Return value: the result code
 **/
ExchangeAccountFolderResult
exchange_hierarchy_remove_folder (ExchangeHierarchy *hier, EFolder *folder)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (folder), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return HIER_CLASS (hier)->remove_folder (hier, folder);
}

/**
 * exchange_hierarchy_xfer_folder:
 * @hier: the hierarchy
 * @source: the source folder
 * @dest_parent: the parent of the destination folder
 * @dest_name: name of the destination (UTF8)
 * @remove_source: %TRUE if this is a move, %FALSE if it is a copy
 *
 * Attempts to move or copy a folder.
 *
 * Return value: the result code
 **/
ExchangeAccountFolderResult
exchange_hierarchy_xfer_folder (ExchangeHierarchy *hier, EFolder *source,
				EFolder *dest_parent, const gchar *dest_name,
				gboolean remove_source)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (source), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (dest_parent), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (dest_name != NULL, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return HIER_CLASS (hier)->xfer_folder (hier, source,
					       dest_parent, dest_name,
					       remove_source);
}

static void
rescan (ExchangeHierarchy *hier)
{
	;
}

/**
 * exchange_hierarchy_rescan:
 * @hier: the hierarchy
 *
 * Tells the hierarchy to rescan its folder tree
 **/
void
exchange_hierarchy_rescan (ExchangeHierarchy *hier)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));

	HIER_CLASS (hier)->rescan (hier);
}

static ExchangeAccountFolderResult
scan_subtree (ExchangeHierarchy *hier, EFolder *folder, gint mode)
{
	return EXCHANGE_ACCOUNT_FOLDER_OK;
}

/**
 * exchange_hierarchy_scan_subtree:
 * @hier: the hierarchy
 * @folder: the folder to scan under
 *
 * Scans for folders in @hier underneath @folder, emitting %new_folder
 * signals for each one found. Depending on the kind of hierarchy,
 * this may initiate a recursive scan.
 *
 * Return value: the result code
 **/
ExchangeAccountFolderResult
exchange_hierarchy_scan_subtree (ExchangeHierarchy *hier, EFolder *folder, gint mode)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (E_IS_FOLDER (folder), EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);

	return HIER_CLASS (hier)->scan_subtree (hier, folder, mode);
}

static void
add_to_storage (ExchangeHierarchy *hier)
{
	e_folder_set_sorting_priority (hier->toplevel, hier->type);
	exchange_hierarchy_new_folder (hier, hier->toplevel);
}

/**
 * exchange_hierarchy_add_to_storage:
 * @hier: the hierarchy
 *
 * Tells the hierarchy to fill in its folder tree, emitting %new_folder
 * signals as appropriate.
 **/
void
exchange_hierarchy_add_to_storage (ExchangeHierarchy *hier)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));

	HIER_CLASS (hier)->add_to_storage (hier);
}

/**
 * exchange_hierarchy_construct:
 * @hier: the hierarchy
 * @account: the hierarchy's account
 * @type: the type of hierarchy
 * @toplevel: the top-level folder of the hierarchy
 * @owner_name: the display name of the owner of this hierarchy
 * @owner_email: the email address of the owner of this hierarchy
 * @source_uri: the evolution-mail source URI of this hierarchy.
 *
 * Constructs the hierarchy. @owner_name, @owner_email, and @source_uri
 * can be %NULL if not relevant to this hierarchy.
 **/
void
exchange_hierarchy_construct (ExchangeHierarchy *hier,
			      ExchangeAccount *account,
			      ExchangeHierarchyType type,
			      EFolder *toplevel,
			      const gchar *owner_name,
			      const gchar *owner_email,
			      const gchar *source_uri)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
	g_return_if_fail (EXCHANGE_IS_ACCOUNT (account));
	g_return_if_fail (E_IS_FOLDER (toplevel));

	/* We don't ref the account since we'll be destroyed when
	 * the account is
	 */
	hier->account = account;

	hier->toplevel = toplevel;
	g_object_ref (toplevel);

	hier->type = type;
	hier->owner_name = g_strdup (owner_name);
	hier->owner_email = g_strdup (owner_email);
	hier->source_uri = g_strdup (source_uri);
}
