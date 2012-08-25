/*
 * e-source-extension.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

/**
 * SECTION: e-source-extension
 * @include: libedataserver/libedataserver.h
 * @short_description: Base class for #ESource extensions
 *
 * #ESourceExtension is an abstract base class for #ESource extension
 * objects.  An #ESourceExtension object basically just maps the keys in
 * a key file group to a set of #GObject properties.  The name of the key
 * file group doubles as the name of the #ESourceExtension object.
 *
 * #ESourceExtension objects are accessed through e_source_get_extension().
 **/

#include "e-source-extension.h"

#define E_SOURCE_EXTENSION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_EXTENSION, ESourceExtensionPrivate))

struct _ESourceExtensionPrivate {
	gpointer source;  /* weak pointer */
};

enum {
	PROP_0,
	PROP_SOURCE
};

G_DEFINE_ABSTRACT_TYPE (
	ESourceExtension,
	e_source_extension,
	G_TYPE_OBJECT)

static void
source_extension_set_source (ESourceExtension *extension,
                             ESource *source)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (extension->priv->source == NULL);

	extension->priv->source = source;

	g_object_add_weak_pointer (
		G_OBJECT (source), &extension->priv->source);
}

static void
source_extension_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			source_extension_set_source (
				E_SOURCE_EXTENSION (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_extension_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			g_value_set_object (
				value, e_source_extension_get_source (
				E_SOURCE_EXTENSION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_extension_dispose (GObject *object)
{
	ESourceExtensionPrivate *priv;

	priv = E_SOURCE_EXTENSION_GET_PRIVATE (object);

	if (priv->source != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->source), &priv->source);
		priv->source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_extension_parent_class)->dispose (object);
}

static void
source_extension_notify (GObject *object,
                         GParamSpec *pspec)
{
	ESource *source;
	ESourceExtension *extension;

	extension = E_SOURCE_EXTENSION (object);
	source = e_source_extension_get_source (extension);
	g_return_if_fail (source != NULL);

	if ((pspec->flags & E_SOURCE_PARAM_SETTING) != 0)
		e_source_changed (source);
}

static void
e_source_extension_class_init (ESourceExtensionClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESourceExtensionPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_extension_set_property;
	object_class->get_property = source_extension_get_property;
	object_class->dispose = source_extension_dispose;
	object_class->notify = source_extension_notify;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"The ESource being extended",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_source_extension_init (ESourceExtension *extension)
{
	extension->priv = E_SOURCE_EXTENSION_GET_PRIVATE (extension);
}

/**
 * e_source_extension_get_source:
 * @extension: an #ESourceExtension
 *
 * Returns the #ESource instance to which @extension belongs.
 *
 * Returns: (transfer none): the #ESource instance
 *
 * Since: 3.6
 **/
ESource *
e_source_extension_get_source (ESourceExtension *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EXTENSION (extension), NULL);

	/* If the ESource was finalized and our weak pointer set this
	 * to NULL, then the type cast macro will fail and we'll get a
	 * runtime warning about it, which is what we want. */
	return E_SOURCE (extension->priv->source);
}

