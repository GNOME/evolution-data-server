/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-type-registry.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-folder-type-registry.h"

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (EFolderTypeRegistry, e_folder_type_registry, G_TYPE_OBJECT)

typedef struct {
	gchar *name;
	gchar *icon_name;

	gchar *display_name;
	gchar *description;

	gboolean user_creatable;

	GList *accepted_dnd_types; /* gchar * */

	GObject *handler;

} FolderType;

struct EFolderTypeRegistryPrivate {
	GHashTable *name_to_type;
};

/* FolderType handling.  */

static FolderType *
folder_type_new (const gchar *name,
		 const gchar *icon_name,
		 const gchar *display_name,
		 const gchar *description,
		 gboolean user_creatable,
		 gint num_accepted_dnd_types,
		 const gchar **accepted_dnd_types)
{
	FolderType *new;
	gint i;

	new = g_new0 (FolderType, 1);

	new->name           = g_strdup (name);
	new->icon_name      = g_strdup (icon_name);
	new->display_name   = g_strdup (display_name);
	new->description    = g_strdup (description);

	new->user_creatable = user_creatable;

	new->accepted_dnd_types = NULL;
	for (i = 0; i < num_accepted_dnd_types; i++)
		new->accepted_dnd_types = g_list_prepend (new->accepted_dnd_types,
							  g_strdup (accepted_dnd_types[i]));
	new->accepted_dnd_types = g_list_reverse (new->accepted_dnd_types);

	new->handler = NULL;

	return new;
}

static void
folder_type_free (FolderType *folder_type)
{
	g_free (folder_type->name);
	g_free (folder_type->icon_name);
	g_free (folder_type->display_name);
	g_free (folder_type->description);

	if (folder_type->handler != NULL)
		g_object_unref (folder_type->handler);

	g_free (folder_type);
}

static FolderType *
get_folder_type (EFolderTypeRegistry *folder_type_registry,
		 const gchar *type_name)
{
	EFolderTypeRegistryPrivate *priv;

	priv = folder_type_registry->priv;

	return g_hash_table_lookup (priv->name_to_type, type_name);
}

static gboolean
register_folder_type (EFolderTypeRegistry *folder_type_registry,
		      const gchar *name,
		      const gchar *icon_name,
		      const gchar *display_name,
		      const gchar *description,
		      gboolean user_creatable,
		      gint num_accepted_dnd_types,
		      const gchar **accepted_dnd_types)
{
	EFolderTypeRegistryPrivate *priv;
	FolderType *folder_type;

	priv = folder_type_registry->priv;

	/* Make sure we don't add the same type twice.  */
	if (get_folder_type (folder_type_registry, name) != NULL)
		return FALSE;

	folder_type = folder_type_new (name, icon_name,
				       display_name, description,
				       user_creatable,
				       num_accepted_dnd_types, accepted_dnd_types);
	g_hash_table_insert (priv->name_to_type, folder_type->name, folder_type);

	return TRUE;
}

static gboolean
set_handler (EFolderTypeRegistry *folder_type_registry,
	     const gchar *name,
	     GObject *handler)
{
	EFolderTypeRegistryPrivate *priv;
	FolderType *folder_type;

	priv = folder_type_registry->priv;

	folder_type = get_folder_type (folder_type_registry, name);
	if (folder_type == NULL)
		return FALSE;
	if (folder_type->handler != NULL)
		return FALSE;

	g_object_ref (handler);
	folder_type->handler = handler;

	return TRUE;
}

/* GObject methods.  */

static void
hash_forall_free_folder_type (gpointer key,
			      gpointer value,
			      gpointer data)
{
	FolderType *folder_type;

	folder_type = (FolderType *) value;
	folder_type_free (folder_type);
}

static void
impl_finalize (GObject *object)
{
	EFolderTypeRegistry *folder_type_registry;
	EFolderTypeRegistryPrivate *priv;

	folder_type_registry = E_FOLDER_TYPE_REGISTRY (object);
	priv = folder_type_registry->priv;

	g_hash_table_foreach (priv->name_to_type, hash_forall_free_folder_type, NULL);
	g_hash_table_destroy (priv->name_to_type);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
e_folder_type_registry_class_init (EFolderTypeRegistryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = impl_finalize;

	parent_class = g_type_class_ref (PARENT_TYPE);
}

static void
e_folder_type_registry_init (EFolderTypeRegistry *folder_type_registry)
{
	EFolderTypeRegistryPrivate *priv;

	priv = g_new0 (EFolderTypeRegistryPrivate, 1);
	priv->name_to_type = g_hash_table_new (g_str_hash, g_str_equal);

	folder_type_registry->priv = priv;
}

EFolderTypeRegistry *
e_folder_type_registry_new (void)
{
	return g_object_new (E_TYPE_FOLDER_TYPE_REGISTRY, NULL);
}

gboolean
e_folder_type_registry_register_type (EFolderTypeRegistry *folder_type_registry,
				      const gchar *type_name,
				      const gchar *icon_name,
				      const gchar *display_name,
				      const gchar *description,
				      gboolean user_creatable,
				      gint num_accepted_dnd_types,
				      const gchar **accepted_dnd_types)
{
	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), FALSE);
	g_return_val_if_fail (type_name != NULL, FALSE);
	g_return_val_if_fail (icon_name != NULL, FALSE);

	return register_folder_type (folder_type_registry, type_name, icon_name,
				     display_name, description, user_creatable,
				     num_accepted_dnd_types, accepted_dnd_types);
}

gboolean
e_folder_type_registry_set_handler_for_type  (EFolderTypeRegistry *folder_type_registry,
					      const gchar *type_name,
					      GObject *handler)
{
	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), FALSE);
	/* g_return_val_if_fail (EVOLUTION_IS_SHELL_COMPONENT_CLIENT (handler), FALSE); */

	return set_handler (folder_type_registry, type_name, handler);
}

gboolean
e_folder_type_registry_type_registered  (EFolderTypeRegistry *folder_type_registry,
					 const gchar *type_name)
{
	EFolderTypeRegistryPrivate *priv;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), FALSE);
	g_return_val_if_fail (type_name != NULL, FALSE);

	priv = folder_type_registry->priv;

	if (get_folder_type (folder_type_registry, type_name) == NULL)
		return FALSE;

	return TRUE;
}

void
e_folder_type_registry_unregister_type (EFolderTypeRegistry *folder_type_registry,
					const gchar *type_name)
{
	EFolderTypeRegistryPrivate *priv;
	FolderType *folder_type;

	g_return_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry));
	g_return_if_fail (type_name != NULL);

	priv = folder_type_registry->priv;

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return;

	g_hash_table_remove (priv->name_to_type, folder_type->name);
	folder_type_free (folder_type);
}

static void
get_type_names_hash_forall (gpointer key,
			    gpointer value,
			    gpointer data)
{
	GList **type_name_list;

	type_name_list = (GList **) data;

	*type_name_list = g_list_prepend (*type_name_list, g_strdup ((const gchar *) key));
}

GList *
e_folder_type_registry_get_type_names (EFolderTypeRegistry *folder_type_registry)
{
	GList *type_name_list;
	EFolderTypeRegistryPrivate *priv;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), NULL);

	priv = folder_type_registry->priv;

	type_name_list = NULL;
	g_hash_table_foreach (priv->name_to_type, get_type_names_hash_forall, &type_name_list);

	return type_name_list;
}

const gchar *
e_folder_type_registry_get_icon_name_for_type (EFolderTypeRegistry *folder_type_registry,
					       const gchar *type_name)
{
	const FolderType *folder_type;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), NULL);
	g_return_val_if_fail (type_name != NULL, NULL);

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return NULL;

	return folder_type->icon_name;
}

GObject *
e_folder_type_registry_get_handler_for_type (EFolderTypeRegistry *folder_type_registry,
					     const gchar *type_name)
{
	const FolderType *folder_type;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), NULL);
	g_return_val_if_fail (type_name != NULL, NULL);

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return NULL;

	return folder_type->handler;
}

gboolean
e_folder_type_registry_type_is_user_creatable  (EFolderTypeRegistry *folder_type_registry,
						const gchar          *type_name)
{
	const FolderType *folder_type;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), FALSE);
	g_return_val_if_fail (type_name != NULL, FALSE);

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return FALSE;

	return folder_type->user_creatable;
}

const gchar *
e_folder_type_registry_get_display_name_for_type (EFolderTypeRegistry *folder_type_registry,
						  const gchar *type_name)
{
	const FolderType *folder_type;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), NULL);
	g_return_val_if_fail (type_name != NULL, NULL);

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return NULL;

	return folder_type->display_name;
}

const gchar *
e_folder_type_registry_get_description_for_type (EFolderTypeRegistry *folder_type_registry,
						 const gchar *type_name)
{
	const FolderType *folder_type;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), NULL);
	g_return_val_if_fail (type_name != NULL, NULL);

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return NULL;

	return folder_type->description;
}

GList *
e_folder_type_registry_get_accepted_dnd_types_for_type (EFolderTypeRegistry *folder_type_registry,
							const gchar *type_name)
{
	const FolderType *folder_type;

	g_return_val_if_fail (E_IS_FOLDER_TYPE_REGISTRY (folder_type_registry), NULL);
	g_return_val_if_fail (type_name != NULL, NULL);

	folder_type = get_folder_type (folder_type_registry, type_name);
	if (folder_type == NULL)
		return NULL;

	return folder_type->accepted_dnd_types;
}
