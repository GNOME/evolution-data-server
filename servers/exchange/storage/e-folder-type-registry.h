/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-type-registry.h
 *
 * Copyright (C) 2000  Ximian, Inc.
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
 *
 * Author: Ettore Perazzoli
 */

#ifndef _E_FOLDER_TYPE_REGISTRY_H_
#define _E_FOLDER_TYPE_REGISTRY_H_

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_FOLDER_TYPE_REGISTRY		(e_folder_type_registry_get_type ())
#define E_FOLDER_TYPE_REGISTRY(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_FOLDER_TYPE_REGISTRY, EFolderTypeRegistry))
#define E_FOLDER_TYPE_REGISTRY_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_FOLDER_TYPE_REGISTRY, EFolderTypeRegistryClass))
#define E_IS_FOLDER_TYPE_REGISTRY(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_FOLDER_TYPE_REGISTRY))
#define E_IS_FOLDER_TYPE_REGISTRY_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_FOLDER_TYPE_REGISTRY))

typedef struct EFolderTypeRegistry        EFolderTypeRegistry;
typedef struct EFolderTypeRegistryPrivate EFolderTypeRegistryPrivate;
typedef struct EFolderTypeRegistryClass   EFolderTypeRegistryClass;

struct EFolderTypeRegistry {
	GObject parent;

	EFolderTypeRegistryPrivate *priv;
};

struct EFolderTypeRegistryClass {
	GObjectClass parent_class;
};

GType                e_folder_type_registry_get_type   (void);
EFolderTypeRegistry *e_folder_type_registry_new        (void);

gboolean  e_folder_type_registry_register_type         (EFolderTypeRegistry           *folder_type_registry,
							const char                    *type_name,
							const char                    *icon_name,
							const char                    *display_name,
							const char                    *description,
							gboolean                       user_creatable,
							int                            num_accepted_dnd_types,
							const char                   **accepted_dnd_types);
gboolean  e_folder_type_registry_set_handler_for_type  (EFolderTypeRegistry           *folder_type_registry,
							const char                    *type_name,
							GObject                       *handler);

GList *e_folder_type_registry_get_type_names  (EFolderTypeRegistry *folder_type_registry);

gboolean  e_folder_type_registry_type_registered  (EFolderTypeRegistry *folder_type_registry,
						   const char          *type_name);
void      e_folder_type_registry_unregister_type  (EFolderTypeRegistry *folder_type_registry,
						   const char          *type_name);

const char                    *e_folder_type_registry_get_icon_name_for_type     (EFolderTypeRegistry *folder_type_registry,
										  const char          *type_name);
GObject                       *e_folder_type_registry_get_handler_for_type       (EFolderTypeRegistry *folder_type_registry,
										  const char          *type_name);
gboolean                       e_folder_type_registry_type_is_user_creatable     (EFolderTypeRegistry *folder_type_registry,
										  const char          *type_name);
const char                    *e_folder_type_registry_get_display_name_for_type  (EFolderTypeRegistry *folder_type_registry,
										  const char          *type_name);
const char                    *e_folder_type_registry_get_description_for_type   (EFolderTypeRegistry *folder_type_registry,
										  const char          *type_name);

GList *e_folder_type_registry_get_accepted_dnd_types_for_type (EFolderTypeRegistry *folder_type_registry,
							       const char *type_name);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_FOLDER_TYPE_REGISTRY_H_ */
