/*
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

/**
 * SECTION: e-source-webdav-notes
 * @include: libedataserver/libedataserver.h
 * @short_description: WebDAV Notes specific settings
 *
 * #ESourceWebDAVNotes is an extension holding specific settings
 * for the WebDAV Notes backend.
 **/

#include "e-source-webdav-notes.h"

#include <libedataserver/e-data-server-util.h>

struct _ESourceWebDAVNotesPrivate {
	gchar *default_ext;
};

enum {
	PROP_0,
	PROP_DEFAULT_EXT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (ESourceWebDAVNotes, e_source_webdav_notes, E_TYPE_SOURCE_EXTENSION)

static void
source_webdav_notes_set_property (GObject *object,
				  guint property_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DEFAULT_EXT:
			e_source_webdav_notes_set_default_ext (
				E_SOURCE_WEBDAV_NOTES (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_webdav_notes_get_property (GObject *object,
				  guint property_id,
				  GValue *value,
				  GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DEFAULT_EXT:
			g_value_take_string (
				value,
				e_source_webdav_notes_dup_default_ext (
				E_SOURCE_WEBDAV_NOTES (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_webdav_notes_finalize (GObject *object)
{
	ESourceWebDAVNotesPrivate *priv;

	priv = E_SOURCE_WEBDAV_NOTES (object)->priv;

	g_free (priv->default_ext);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_source_webdav_notes_parent_class)->finalize (object);
}

static void
e_source_webdav_notes_class_init (ESourceWebDAVNotesClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_webdav_notes_set_property;
	object_class->get_property = source_webdav_notes_get_property;
	object_class->finalize = source_webdav_notes_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_WEBDAV_NOTES;

	/**
	 * ESourceWebDAVNotes:default-ext
	 *
	 * Default file extension for new notes
	 **/
	properties[PROP_DEFAULT_EXT] =
		g_param_spec_string (
			"default-ext",
			NULL, NULL,
			".md",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
e_source_webdav_notes_init (ESourceWebDAVNotes *extension)
{
	extension->priv = e_source_webdav_notes_get_instance_private (extension);
}

/**
 * e_source_webdav_notes_get_default_ext:
 * @extension: an #ESourceWebDAVNotes
 *
 * Returns the default file extension for new notes.
 *
 * Returns: (nullable): the default file extension, or %NULL, when none is set
 *
 * Since: 3.44
 **/
const gchar *
e_source_webdav_notes_get_default_ext (ESourceWebDAVNotes *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV_NOTES (extension), NULL);

	return extension->priv->default_ext;
}

/**
 * e_source_webdav_notes_dup_default_ext:
 * @extension: an #ESourceWebDAVNotes
 *
 * Thread-safe variation of e_source_webdav_notes_get_default_ext().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: (nullable) (transfer full): a newly-allocated copy of #ESourceWebDAVNotes:default-ext,
 *    or %NULL, when none is set
 *
 * Since: 3.44
 **/
gchar *
e_source_webdav_notes_dup_default_ext (ESourceWebDAVNotes *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_WEBDAV_NOTES (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_webdav_notes_get_default_ext (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

/**
 * e_source_webdav_notes_set_default_ext:
 * @extension: an #ESourceWebDAVNotes
 * @default_ext: (nullable): a default file extension, or %NULL
 *
 * Sets the default file extension for new notes.
 *
 * The internal copy of @default_ext is automatically stripped of leading and
 * trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.44
 **/
void
e_source_webdav_notes_set_default_ext (ESourceWebDAVNotes *extension,
				       const gchar *default_ext)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV_NOTES (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->default_ext, default_ext) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->default_ext);
	extension->priv->default_ext = e_util_strdup_strip (default_ext);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_DEFAULT_EXT]);
}
