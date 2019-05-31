/*
 * e-source-local.c
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "evolution-data-server-config.h"

#include "e-source-local.h"

#define E_SOURCE_LOCAL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_LOCAL, ESourceLocalPrivate))

struct _ESourceLocalPrivate {
	GFile *custom_file;
	gboolean writable;
};

enum {
	PROP_0,
	PROP_CUSTOM_FILE,
	PROP_WRITABLE
};

G_DEFINE_TYPE (
	ESourceLocal,
	e_source_local,
	E_TYPE_SOURCE_EXTENSION)

static void
source_local_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CUSTOM_FILE:
			e_source_local_set_custom_file (
				E_SOURCE_LOCAL (object),
				g_value_get_object (value));
			return;

		case PROP_WRITABLE:
			e_source_local_set_writable (
				E_SOURCE_LOCAL (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_local_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CUSTOM_FILE:
			g_value_take_object (
				value,
				e_source_local_dup_custom_file (
				E_SOURCE_LOCAL (object)));
			return;

		case PROP_WRITABLE:
			g_value_set_boolean (
				value,
				e_source_local_get_writable (
				E_SOURCE_LOCAL (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_local_finalize (GObject *object)
{
	ESourceLocalPrivate *priv;

	priv = E_SOURCE_LOCAL_GET_PRIVATE (object);

	if (priv->custom_file != NULL) {
		g_object_unref (priv->custom_file);
		priv->custom_file = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_local_parent_class)->finalize (object);
}

static void
e_source_local_class_init (ESourceLocalClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceLocalPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_local_set_property;
	object_class->get_property = source_local_get_property;
	object_class->finalize = source_local_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_LOCAL_BACKEND;

	g_object_class_install_property (
		object_class,
		PROP_CUSTOM_FILE,
		g_param_spec_object (
			"custom-file",
			"Custom File",
			"Custom iCalendar file",
			G_TYPE_FILE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_WRITABLE,
		g_param_spec_boolean (
			"writable",
			"Writable",
			"Whether the file can be opened in writable mode",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_local_init (ESourceLocal *extension)
{
	extension->priv = E_SOURCE_LOCAL_GET_PRIVATE (extension);
}

/**
 * e_source_local_get_custom_file:
 * @extension: an #ESourceLocal
 *
 * Get the custom file being set on the @extension.
 * The returned #GFile is owned by the @extension.
 *
 * For thread safety use e_source_local_dup_custom_file().
 *
 * Returns: (transfer none) (nullable): the #GFile instance, or %NULL
 **/
GFile *
e_source_local_get_custom_file (ESourceLocal *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_LOCAL (extension), NULL);

	return extension->priv->custom_file;
}

/**
 * e_source_local_dup_custom_file:
 * @extension: an #ESourceLocal
 *
 * A thread safe variant to get a custom file being set on the @extension.
 * Free the returned #GFile, if not %NULL, with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the #GFile instance, or %NULL
 **/
GFile *
e_source_local_dup_custom_file (ESourceLocal *extension)
{
	GFile *protected;
	GFile *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_LOCAL (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_local_get_custom_file (extension);
	duplicate = (protected != NULL) ? g_file_dup (protected) : NULL;

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_local_set_custom_file:
 * @extension: an #ESourceLocal
 * @custom_file: (nullable): a #GFile, or %NULL
 *
 * Set, or unset, when using %NULL, the custom file for the @extension.
 **/
void
e_source_local_set_custom_file (ESourceLocal *extension,
                                GFile *custom_file)
{
	g_return_if_fail (E_IS_SOURCE_LOCAL (extension));

	if (custom_file != NULL) {
		g_return_if_fail (G_IS_FILE (custom_file));
		g_object_ref (custom_file);
	}

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (extension->priv->custom_file != NULL)
		g_object_unref (extension->priv->custom_file);

	extension->priv->custom_file = custom_file;

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "custom-file");
}

/**
 * e_source_local_get_writable:
 * @extension: an #ESourceLocal
 *
 * Returns whether the backend should prefer to open the file
 * in writable mode. The default is %TRUE. The file can be still
 * opened for read-only, for example when the access to the file
 * is read-only.
 *
 * Returns: whether prefer to pen the file in writable mode
 *
 * Since: 3.34
 **/
gboolean
e_source_local_get_writable (ESourceLocal *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_LOCAL (extension), FALSE);

	return extension->priv->writable;
}

/**
 * e_source_local_set_writable:
 * @extension: an #ESourceLocal
 * @writable: value to set
 *
 * Set whether the custom file should be opened in writable mode.
 * The default is %TRUE. The file can be still opened for read-only,
 * for example when the access to the file is read-only.
 *
 * Since: 3.34
 **/
void
e_source_local_set_writable (ESourceLocal *extension,
			     gboolean writable)
{
	gboolean changed = FALSE;

	g_return_if_fail (E_IS_SOURCE_LOCAL (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	changed = (extension->priv->writable ? 1 : 0) != (writable ? 1 : 0);

	if (changed)
		extension->priv->writable = writable;

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	if (changed)
		g_object_notify (G_OBJECT (extension), "writable");
}
