/*
 * e-source-vcf.c
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

#include "e-source-vcf.h"

#define E_SOURCE_VCF_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_VCF, ESourceVCFPrivate))

struct _ESourceVCFPrivate {
	GMutex *property_lock;
	gchar *path;
};

enum {
	PROP_0,
	PROP_PATH
};

G_DEFINE_DYNAMIC_TYPE (
	ESourceVCF,
	e_source_vcf,
	E_TYPE_SOURCE_EXTENSION)

static void
source_vcf_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PATH:
			e_source_vcf_set_path (
				E_SOURCE_VCF (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_vcf_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PATH:
			g_value_take_string (
				value,
				e_source_vcf_dup_path (
				E_SOURCE_VCF (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_vcf_finalize (GObject *object)
{
	ESourceVCFPrivate *priv;

	priv = E_SOURCE_VCF_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_vcf_parent_class)->finalize (object);
}

static void
e_source_vcf_class_init (ESourceVCFClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceVCFPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_vcf_set_property;
	object_class->get_property = source_vcf_get_property;
	object_class->finalize = source_vcf_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_VCF_BACKEND;

	g_object_class_install_property (
		object_class,
		PROP_PATH,
		g_param_spec_string (
			"path",
			"Path",
			"Path to VCF file",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_vcf_class_finalize (ESourceVCFClass *class)
{
}

static void
e_source_vcf_init (ESourceVCF *extension)
{
	extension->priv = E_SOURCE_VCF_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();
}

void
e_source_vcf_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_source_vcf_register_type (type_module);
}

const gchar *
e_source_vcf_get_path (ESourceVCF *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_VCF (extension), NULL);

	return extension->priv->path;
}

gchar *
e_source_vcf_dup_path (ESourceVCF *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_VCF (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_vcf_get_path (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

void
e_source_vcf_set_path (ESourceVCF *extension,
                       const gchar *path)
{
	g_return_if_fail (E_IS_SOURCE_VCF (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->path, path) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->path);
	extension->priv->path = e_util_strdup_strip (path);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "path");
}
