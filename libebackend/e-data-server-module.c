/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 *  e-data-server-module.c - Interface to e-d-s extensions
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Chris Toshok <toshok@ximian.com>
 *           Dave Camp <dave@ximian.com>
 *
 */

/**
 * SECTION: e-data-server-module
 * @short_description: Backend module loader
 *
 * An #EDataServerModule loads backend modules from the directory given
 * in e_data_server_module_init().  Each backend module must export three
 * functions.  The first two -- eds_module_initialize() and
 * eds_module_list_types() -- are called immediately after the backend
 * module is loaded.  The last one -- eds_module_shutdown() -- is called
 * when the backend module is unloaded.
 **/

#include <config.h>
#include "e-data-server-module.h"

#include <gmodule.h>

#include "libedataserver/libedataserver-private.h"

#define E_DATA_SERVER_TYPE_MODULE		(e_data_server_module_get_type ())
#define E_DATA_SERVER_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_DATA_SERVER_TYPE_MODULE, EDataServerModule))
#define E_DATA_SERVER_MODULE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), E_DATA_SERVER_TYPE_MODULE, EDataServerModule))
#define E_DATA_SERVER_IS_MODULE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_DATA_SERVER_TYPE_MODULE))
#define E_DATA_SERVER_IS_MODULE_CLASS(klass)	(G_TYPE_CLASS_CHECK_CLASS_TYPE ((klass), E_DATA_SERVER_TYPE_MODULE))

typedef struct _EDataServerModule        EDataServerModule;
typedef struct _EDataServerModuleClass   EDataServerModuleClass;

struct _EDataServerModule {
	GTypeModule parent;

	GModule *library;

	gchar *path;

	void (*initialize) (GTypeModule  *module);
	void (*shutdown)   (void);

	void (*list_types) (const GType **types,
			    gint          *num_types);

};

struct _EDataServerModuleClass {
	GTypeModuleClass parent;
};

static GType e_data_server_module_get_type (void);

static GList *module_objects = NULL;

/* FIXME: hack to keep _init as the public API */
#define e_data_server_module_init e_data_server_module_instance_init
G_DEFINE_TYPE (EDataServerModule, e_data_server_module, G_TYPE_TYPE_MODULE)
#undef e_data_server_module_init

static gboolean
e_data_server_module_load (GTypeModule *gmodule)
{
	EDataServerModule *module;

	module = E_DATA_SERVER_MODULE (gmodule);

	module->library = g_module_open (module->path,
				G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);

	if (!module->library) {
		g_warning ("%s", g_module_error ());
		return FALSE;
	}

	if (!g_module_symbol (module->library,
			      "eds_module_initialize",
			      (gpointer *)&module->initialize) ||
	    !g_module_symbol (module->library,
			      "eds_module_shutdown",
			      (gpointer *)&module->shutdown) ||
	    !g_module_symbol (module->library,
			      "eds_module_list_types",
			      (gpointer *)&module->list_types)) {

		g_warning ("%s", g_module_error ());
		g_module_close (module->library);

		return FALSE;
	}

	module->initialize (gmodule);

	return TRUE;
}

static void
e_data_server_module_unload (GTypeModule *gmodule)
{
	EDataServerModule *module;

	module = E_DATA_SERVER_MODULE (gmodule);

	module->shutdown ();

	g_module_close (module->library);

	module->initialize = NULL;
	module->shutdown = NULL;
	module->list_types = NULL;
}

static void
e_data_server_module_finalize (GObject *object)
{
	EDataServerModule *module;

	module = E_DATA_SERVER_MODULE (object);

	g_free (module->path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_server_module_parent_class)->finalize (object);
}

static void
e_data_server_module_instance_init (EDataServerModule *module)
{
}

static void
e_data_server_module_class_init (EDataServerModuleClass *class)
{
	G_OBJECT_CLASS (class)->finalize = e_data_server_module_finalize;
	G_TYPE_MODULE_CLASS (class)->load = e_data_server_module_load;
	G_TYPE_MODULE_CLASS (class)->unload = e_data_server_module_unload;
}

static void
module_object_weak_notify (gpointer user_data, GObject *object)
{
	module_objects = g_list_remove (module_objects, object);
}

static void
add_module_objects (EDataServerModule *module)
{
	const GType *types;
	gint num_types;
	gint i;

	module->list_types (&types, &num_types);

	for (i = 0; i < num_types; i++) {
		e_data_server_module_add_type (types[i]);
	}
}

static EDataServerModule *
e_data_server_module_load_file (const gchar *filename)
{
	EDataServerModule *module;

	module = g_object_new (E_DATA_SERVER_TYPE_MODULE, NULL);
	module->path = g_strdup (filename);

	if (g_type_module_use (G_TYPE_MODULE (module))) {
		add_module_objects (module);
		g_type_module_unuse (G_TYPE_MODULE (module));
		return module;
	} else {
		g_object_unref (module);
		return NULL;
	}
}

/**
 * e_data_server_module_init:
 * @module_path: directory of backend modules
 * @error: return location for a #GError, or %NULL
 *
 * Loads all backend modules in @module_path.  If an error occurs,
 * the function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
e_data_server_module_init (const gchar *module_path,
                           GError **error)
{
	static gboolean initialized = FALSE;
	const gchar *name;
	GDir *dir;

	if (initialized)
		return TRUE;

	g_return_val_if_fail (module_path != NULL, FALSE);

	dir = g_dir_open (module_path, 0, error);
	if (dir == NULL)
		return FALSE;

	while ((name = g_dir_read_name (dir))) {
		if (g_str_has_suffix (name, "." G_MODULE_SUFFIX)) {
			gchar *filename;

			filename = g_build_filename (module_path, name, NULL);
			e_data_server_module_load_file (filename);
			g_free (filename);
		}
	}

	g_dir_close (dir);

	initialized = TRUE;

	return TRUE;
}

/**
 * e_data_server_get_extensions_for_type:
 * @type: a #GType
 *
 * Returns a list of objects derived from @type which have been registered
 * through eds_module_list_types() or e_data_server_module_add_type().
 *
 * Free the returned list using e_data_server_extension_list_free().
 *
 * Returns: a list of extension objects
 **/
GList *
e_data_server_get_extensions_for_type (GType type)
{
	GList *l;
	GList *ret = NULL;

	for (l = module_objects; l != NULL; l = l->next) {
		if (G_TYPE_CHECK_INSTANCE_TYPE (G_OBJECT (l->data),
						type)) {
			g_object_ref (l->data);
			g_message ("adding type `%s'", G_OBJECT_TYPE_NAME (l->data));
			ret = g_list_prepend (ret, l->data);
		}
	}

	return ret;
}

/**
 * e_data_server_extension_list_free:
 * @extensions: a list of extension objects
 *
 * Frees a list of objects returned by
 * e_data_server_get_extensions_for_type().
 **/
void
e_data_server_extension_list_free (GList *extensions)
{
	g_list_free_full (extensions, (GDestroyNotify) g_object_unref);
}

/**
 * e_data_server_module_add_type:
 * @type: a #GType
 *
 * Creates an instance of @type and adds the instance to an internal list
 * which can be queried using e_data_server_get_extensions_for_type().
 **/
void
e_data_server_module_add_type (GType type)
{
	GObject *object;

	object = g_object_new (type, NULL);
	g_object_weak_ref (object,
			   (GWeakNotify)module_object_weak_notify,
			   NULL);

	module_objects = g_list_prepend (module_objects, object);
}

/**
 * e_data_server_module_remove_unused:
 *
 * Unrefs all loaded modules, so that unused modules are unloaded from the
 * system.
 **/
void
e_data_server_module_remove_unused (void)
{
	g_list_foreach (module_objects, (GFunc) g_object_unref, NULL);
}
