/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-source-revision-guards.c - Revision Guard Configuration.
 *
 * Copyright (C) 2013 Intel Corporation
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
 * SECTION: e-source-revision-guards
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension to configure revision guards
 *
 * The #ESourceRevisionGuards extension configures whether revisions
 * should be checked on modified objects. If a modified object has
 * a conflicting revision with an existing object, then an
 * %E_CLIENT_ERROR_OUT_OF_SYNC error should be produced for that object
 * and the modification should be discarded.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceRevisionGuards *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_REVISION_GUARDS);
 * ]|
 **/

#include <libedataserver/e-data-server-util.h>

#include "e-source-revision-guards.h"

#define E_SOURCE_ABC_GET_PRIVATE(obj)			\
	(G_TYPE_INSTANCE_GET_PRIVATE			\
	 ((obj), E_TYPE_SOURCE_REVISION_GUARDS,	\
	  ESourceRevisionGuardsPrivate))

struct _ESourceRevisionGuardsPrivate {
	GMutex property_lock;
	gboolean enabled;
};

enum {
	PROP_0,
	PROP_ENABLED
};

G_DEFINE_TYPE (
	ESourceRevisionGuards,
	e_source_revision_guards,
	E_TYPE_SOURCE_EXTENSION)

static void
source_revision_guards_finalize (GObject *object)
{
	ESourceRevisionGuardsPrivate *priv;

	priv = E_SOURCE_ABC_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);

	G_OBJECT_CLASS (e_source_revision_guards_parent_class)->finalize (object);
}

static void
source_revision_guards_set_property (GObject *object,
				     guint property_id,
				     const GValue *value,
				     GParamSpec *pspec)
{
	ESourceRevisionGuards *extension = E_SOURCE_REVISION_GUARDS (object);

	switch (property_id) {
	case PROP_ENABLED:
		e_source_revision_guards_set_enabled (extension, g_value_get_boolean (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_revision_guards_get_property (GObject *object,
				     guint property_id,
				     GValue *value,
				     GParamSpec *pspec)
{
	ESourceRevisionGuards *extension = E_SOURCE_REVISION_GUARDS (object);

	switch (property_id) {
	case PROP_ENABLED:
		g_value_set_boolean (value, e_source_revision_guards_get_enabled (extension));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_revision_guards_class_init (ESourceRevisionGuardsClass *class)
{
	GObjectClass          *object_class;
	ESourceExtensionClass *extension_class;

	extension_class       = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_REVISION_GUARDS;

	object_class               = G_OBJECT_CLASS (class);
	object_class->finalize     = source_revision_guards_finalize;
	object_class->get_property = source_revision_guards_get_property;
	object_class->set_property = source_revision_guards_set_property;

	g_object_class_install_property (
		object_class,
		PROP_ENABLED,
		g_param_spec_boolean (
			"enabled",
			"Enabled",
			"Whether to enable or disable the revision guards",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_type_class_add_private (class, sizeof (ESourceRevisionGuardsPrivate));
}

static void
e_source_revision_guards_init (ESourceRevisionGuards *extension)
{
	extension->priv = E_SOURCE_ABC_GET_PRIVATE (extension);
	extension->priv->enabled = FALSE;

	g_mutex_init (&extension->priv->property_lock);
}

/**
 * e_source_revision_guards_set_enabled:
 * @extension: An #ESourceRevisionGuards
 * @enabled: Whether to enable or disable the revision guards.
 *
 * Enables or disables the revision guards for a given #ESource.
 *
 * Revision guards are disabled by default.
 *
 * Since: 3.8
 */
void
e_source_revision_guards_set_enabled (ESourceRevisionGuards  *extension,
				      gboolean                enabled)
{
	g_return_if_fail (E_IS_SOURCE_REVISION_GUARDS (extension));

	enabled = !!enabled;

	g_mutex_lock (&extension->priv->property_lock);

	if (extension->priv->enabled == enabled) {
		g_mutex_unlock (&extension->priv->property_lock);
		return;
	}

	extension->priv->enabled = enabled;

	g_mutex_unlock (&extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "enabled");
}


/**
 * e_source_revision_guards_get_enabled:
 * @extension: An #ESourceRevisionGuards
 *
 * Checks whether revision guards for the given #ESource are enabled.
 *
 * Returns: %TRUE if the revision guards are enabled.
 *
 * Since: 3.8
 */
gboolean
e_source_revision_guards_get_enabled (ESourceRevisionGuards *extension)
{
	gboolean enabled;

	g_return_val_if_fail (E_IS_SOURCE_REVISION_GUARDS (extension), FALSE);

	g_mutex_lock (&extension->priv->property_lock);
	enabled = extension->priv->enabled;
	g_mutex_unlock (&extension->priv->property_lock);

	return enabled;
}
