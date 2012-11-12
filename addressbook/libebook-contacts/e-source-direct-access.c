/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-source-address-book-config.c - Address Book Configuration.
 *
 * Copyright (C) 2012 Openismus GmbH
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

/**
 * SECTION: e-source-direct-access
 * @include: libebook-contacts/libebook-contacts.h
 * @short_description: #ESource extension for direct access to the address book
 *
 * The #ESourceDirectAccess extension is used to advertize direct access parameters
 * to be used in direct read access mode.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libebook-contacts/libebook-contacts.h>
 *
 *   ESourceDirectAccess *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_DIRECT_ACCESS);
 * ]|
 **/

#include "e-source-direct-access.h"

#define E_SOURCE_DIRECT_ACCESS_GET_PRIVATE(obj)		\
	(G_TYPE_INSTANCE_GET_PRIVATE			\
	 ((obj), E_TYPE_SOURCE_DIRECT_ACCESS,		\
	  ESourceDirectAccessPrivate))

struct _ESourceDirectAccessPrivate {
	GMutex *property_lock;
	gchar  *backend_path;
	gchar  *backend_name;
};

enum {
	PROP_0,
	PROP_BACKEND_PATH,
	PROP_BACKEND_NAME
};

G_DEFINE_TYPE (
	ESourceDirectAccess,
	e_source_direct_access,
	E_TYPE_SOURCE_EXTENSION)


static void
source_direct_access_finalize (GObject *object)
{
	ESourceDirectAccess        *source;
	ESourceDirectAccessPrivate *priv;

	source = E_SOURCE_DIRECT_ACCESS (object);
	priv   = source->priv;

	g_mutex_free (priv->property_lock);
	g_free (priv->backend_path);
	g_free (priv->backend_name);

	G_OBJECT_CLASS (e_source_direct_access_parent_class)->finalize (object);
}

static void
source_direct_access_set_property (GObject *object,
				   guint property_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	ESourceDirectAccess *extension = E_SOURCE_DIRECT_ACCESS (object);

	switch (property_id) {
	case PROP_BACKEND_PATH:
		e_source_direct_access_set_backend_path (extension, g_value_get_string (value));
		return;
	case PROP_BACKEND_NAME:
		e_source_direct_access_set_backend_name (extension, g_value_get_string (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_direct_access_get_property (GObject *object,
					 guint property_id,
					 GValue *value,
					 GParamSpec *pspec)
{
	ESourceDirectAccess *extension = E_SOURCE_DIRECT_ACCESS (object);

	switch (property_id) {
	case PROP_BACKEND_PATH:
		g_value_take_string (value,
				     e_source_direct_access_dup_backend_path (extension));
		return;
	case PROP_BACKEND_NAME:
		g_value_take_string (value,
				     e_source_direct_access_dup_backend_name (extension));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_direct_access_class_init (ESourceDirectAccessClass *class)
{
	GObjectClass          *object_class;
	ESourceExtensionClass *extension_class;

	extension_class       = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_DIRECT_ACCESS;

	object_class               = G_OBJECT_CLASS (class);
	object_class->finalize     = source_direct_access_finalize;
	object_class->get_property = source_direct_access_get_property;
	object_class->set_property = source_direct_access_set_property;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND_PATH,
		g_param_spec_string (
			"backend-path",
			"Backend Path",
			"The full path to the backend module to load for direct access",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_BACKEND_NAME,
		g_param_spec_string (
			"backend-name",
			"Backend Name",
			"The type name of the EBookBackendFactory to use to load the backend in direct access mode",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_type_class_add_private (class, sizeof (ESourceDirectAccessPrivate));
}

static void
e_source_direct_access_init (ESourceDirectAccess *extension)
{
	extension->priv = E_SOURCE_DIRECT_ACCESS_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();
}


/**
 * e_source_direct_access_dup_backend_path:
 * @extension: An #ESourceDirectAccess
 *
 * Fetches the path to load the #EBookBackend from directly for direct
 * access to the addressbook.
 *
 * Returns: (transfer full): The backend path
 *
 * Since: 3.8
 */
gchar *
e_source_direct_access_dup_backend_path (ESourceDirectAccess *extension)
{
	gchar *duplicate = NULL;

	g_mutex_lock (extension->priv->property_lock);

	duplicate = g_strdup (extension->priv->backend_path);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_direct_access_set_backend_path:
 * @extension: An #ESourceDirectAccess
 * @backend_path: The full path to the backend module
 *
 * Sets the path where to find the backend path for direct access
 */
void
e_source_direct_access_set_backend_path (ESourceDirectAccess *extension,
					 const gchar         *backend_path)
{
	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->backend_path, backend_path) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->backend_path);
	extension->priv->backend_path = e_util_strdup_strip (backend_path);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "backend-path");
}

/**
 * e_source_direct_access_dup_backend_name:
 * @extension: An #ESourceDirectAccess
 *
 * Fetches the name of the #EBookBackendFactory to use to create
 * backends in direct access mode.
 *
 * Returns: (transfer full): The backend factory type name
 *
 * Since: 3.8
 */
gchar *
e_source_direct_access_dup_backend_name (ESourceDirectAccess *extension)
{
	gchar *duplicate = NULL;

	g_mutex_lock (extension->priv->property_lock);

	duplicate = g_strdup (extension->priv->backend_name);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_direct_access_set_backend_name:
 * @extension: An #ESourceDirectAccess
 * @backend_path: The name of the #EBookBackendFactory to use in direct access mode
 *
 * Sets the name of the #EBookBackendFactory found in the directly loaded
 * backend to use in direct access mode.
 */
void
e_source_direct_access_set_backend_name (ESourceDirectAccess *extension,
					 const gchar         *backend_name)
{
	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->backend_name, backend_name) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->backend_name);
	extension->priv->backend_name = e_util_strdup_strip (backend_name);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "backend-name");
}
