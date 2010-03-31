/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source.c
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "e-uid.h"
#include "e-source.h"

#define ES_CLASS(obj)  E_SOURCE_CLASS (G_OBJECT_GET_CLASS (obj))

/* Private members.  */

struct _ESourcePrivate {
	ESourceGroup *group;

	gchar *uid;
	gchar *name;
	gchar *relative_uri;
	gchar *absolute_uri;

	gboolean readonly;

	gchar *color_spec;

	GHashTable *properties;
};

/* Signals.  */

enum {
	CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

/* Callbacks.  */

static void
group_weak_notify (ESource *source,
		   GObject **where_the_object_was)
{
	source->priv->group = NULL;

	g_signal_emit (source, signals[CHANGED], 0);
}

/* GObject methods.  */

G_DEFINE_TYPE (ESource, e_source, G_TYPE_OBJECT)

static void
impl_finalize (GObject *object)
{
	ESourcePrivate *priv = E_SOURCE (object)->priv;

	g_free (priv->uid);
	g_free (priv->name);
	g_free (priv->relative_uri);
	g_free (priv->absolute_uri);
	g_free (priv->color_spec);

	g_hash_table_destroy (priv->properties);

	g_free (priv);

	(* G_OBJECT_CLASS (e_source_parent_class)->finalize) (object);
}

static void
impl_dispose (GObject *object)
{
	ESourcePrivate *priv = E_SOURCE (object)->priv;

	if (priv->group != NULL) {
		g_object_weak_unref (G_OBJECT (priv->group), (GWeakNotify) group_weak_notify, object);
		priv->group = NULL;
	}

	(* G_OBJECT_CLASS (e_source_parent_class)->dispose) (object);
}

/* Initialization.  */

static void
e_source_class_init (ESourceClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
e_source_init (ESource *source)
{
	ESourcePrivate *priv;

	priv = g_new0 (ESourcePrivate, 1);
	source->priv = priv;

	priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, g_free);
}

/* Private methods. */

static gboolean
set_color_spec (ESource *source,
                const gchar *color_spec)
{
	ESourcePrivate *priv = source->priv;
	gboolean do_cmp;

	if (color_spec == priv->color_spec)
		return FALSE;

	do_cmp = (color_spec != NULL && priv->color_spec != NULL);
	if (do_cmp && g_ascii_strcasecmp (color_spec, priv->color_spec) == 0)
		return FALSE;

	g_free (priv->color_spec);
	priv->color_spec = g_strdup (color_spec);

	return TRUE;
}

/* Public methods.  */

ESource *
e_source_new  (const gchar *name,
	       const gchar *relative_uri)
{
	ESource *source;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (relative_uri != NULL, NULL);

	source = g_object_new (e_source_get_type (), NULL);
	source->priv->uid = e_uid_new ();

	e_source_set_name (source, name);
	e_source_set_relative_uri (source, relative_uri);
	return source;
}

ESource *
e_source_new_with_absolute_uri (const gchar *name,
				const gchar *absolute_uri)
{
	ESource *source;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (absolute_uri != NULL, NULL);

	source = g_object_new (e_source_get_type (), NULL);
	source->priv->uid = e_uid_new ();

	e_source_set_name (source, name);
	e_source_set_absolute_uri (source, absolute_uri);
	return source;
}

ESource *
e_source_new_from_xml_node (xmlNodePtr node)
{
	ESource *source;
	xmlChar *uid;

	uid = xmlGetProp (node, (xmlChar*)"uid");
	if (uid == NULL)
		return NULL;

	source = g_object_new (e_source_get_type (), NULL);

	source->priv->uid = g_strdup ((gchar *)uid);
	xmlFree (uid);

	if (e_source_update_from_xml_node (source, node, NULL))
		return source;

	g_object_unref (source);
	return NULL;
}

static void
import_properties (ESource *source,
		   xmlNodePtr prop_root)
{
	ESourcePrivate *priv = source->priv;
	xmlNodePtr prop_node;

	for (prop_node = prop_root->children; prop_node; prop_node = prop_node->next) {
		xmlChar *name, *value;

		if (!prop_node->name || strcmp ((gchar *)prop_node->name, "property"))
			continue;

		name = xmlGetProp (prop_node, (xmlChar*)"name");
		value = xmlGetProp (prop_node, (xmlChar*)"value");

		if (name && value)
			g_hash_table_insert (priv->properties, g_strdup ((gchar *)name), g_strdup ((gchar *)value));

		if (name)
			xmlFree (name);
		if (value)
			xmlFree (value);
	}
}

typedef struct
{
	gboolean equal;
	GHashTable *table2;
} hash_compare_data;

static void
compare_str_hash (gpointer key, gpointer value, hash_compare_data *cd)
{
	gpointer value2 = g_hash_table_lookup (cd->table2, key);
	if (value2 == NULL || g_str_equal (value, value2) == FALSE)
		cd->equal = FALSE;
}

static gboolean
compare_str_hashes (GHashTable *table1, GHashTable *table2)
{
	hash_compare_data cd;

	if (g_hash_table_size (table1) != g_hash_table_size (table2))
		return FALSE;

	cd.equal = TRUE;
	cd.table2 = table2;
	g_hash_table_foreach (table1, (GHFunc) compare_str_hash, &cd);
	return cd.equal;
}

/**
 * e_source_update_from_xml_node:
 * @source: An ESource.
 * @node: A pointer to the node to parse.
 *
 * Update the ESource properties from @node.
 *
 * Returns: %TRUE if the data in @node was recognized and parsed into
 * acceptable values for @source, %FALSE otherwise.
 **/
gboolean
e_source_update_from_xml_node (ESource *source,
			       xmlNodePtr node,
			       gboolean *changed_return)
{
	xmlChar *name;
	xmlChar *relative_uri;
	xmlChar *absolute_uri;
	xmlChar *color_spec;
	xmlChar *color;
	gboolean retval = FALSE;
	gboolean changed = FALSE;

	name = xmlGetProp (node, (xmlChar*)"name");
	relative_uri = xmlGetProp (node, (xmlChar*)"relative_uri");
	absolute_uri = xmlGetProp (node, (xmlChar*)"uri");
	color_spec = xmlGetProp (node, (xmlChar*)"color_spec");
	color = xmlGetProp (node, (xmlChar*)"color");  /* obsolete */

	if (name == NULL || (relative_uri == NULL && absolute_uri == NULL))
		goto done;

	if (color_spec != NULL && color != NULL)
		goto done;

	if (source->priv->name == NULL
	    || strcmp ((gchar *)name, source->priv->name) != 0
	    || (source->priv->relative_uri == NULL && relative_uri != NULL)
	    || (source->priv->relative_uri != NULL && relative_uri == NULL)
	    || (relative_uri && source->priv->relative_uri && strcmp ((gchar *)relative_uri, source->priv->relative_uri) != 0)) {
		gchar *abs_uri = NULL;

		g_free (source->priv->name);
		source->priv->name = g_strdup ((gchar *)name);

		if (source->priv->group) {
			abs_uri = e_source_build_absolute_uri (source);
		}

		if (abs_uri && source->priv->absolute_uri && g_str_equal (abs_uri, source->priv->absolute_uri)) {
			/* reset the absolute uri to NULL to be regenerated when asked for,
			   but only when it was generated also before */
			g_free (source->priv->absolute_uri);
			source->priv->absolute_uri = NULL;
		} else if (source->priv->absolute_uri &&
			   source->priv->relative_uri &&
			   g_str_has_suffix (source->priv->absolute_uri, source->priv->relative_uri)) {
			gchar *tmp = source->priv->absolute_uri;

			tmp [strlen (tmp) - strlen (source->priv->relative_uri)] = 0;
			source->priv->absolute_uri = g_strconcat (tmp, (gchar *)relative_uri, NULL);

			g_free (tmp);
		}

		g_free (abs_uri);

		g_free (source->priv->relative_uri);
		source->priv->relative_uri = g_strdup ((gchar *)relative_uri);

		changed = TRUE;
	}

	if (absolute_uri != NULL) {
		g_free (source->priv->absolute_uri);
		source->priv->absolute_uri = g_strdup ((gchar *)absolute_uri);
		changed = TRUE;
	}

	if (color == NULL) {
		/* It is okay for color_spec to be NULL. */
		changed |= set_color_spec (source, (gchar *)color_spec);
	} else {
		gchar buffer[8];
		g_snprintf (buffer, sizeof (buffer), "#%s", color);
		changed |= set_color_spec (source, buffer);
	}

	if (g_hash_table_size (source->priv->properties) && !node->children) {
		g_hash_table_destroy (source->priv->properties);
		source->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
								  g_free, g_free);
		changed = TRUE;
	}

	for (node = node->children; node; node = node->next) {
		if (!node->name)
			continue;

		if (!strcmp ((gchar *)node->name, "properties")) {
			GHashTable *temp = source->priv->properties;
			source->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
									  g_free, g_free);
			import_properties (source, node);
			if (!compare_str_hashes (temp, source->priv->properties))
				changed = TRUE;
			g_hash_table_destroy (temp);
			break;
		}
	}

	retval = TRUE;

 done:
	if (changed)
		g_signal_emit (source, signals[CHANGED], 0);

	if (changed_return != NULL)
		*changed_return = changed;

	if (name != NULL)
		xmlFree (name);
	if (relative_uri != NULL)
		xmlFree (relative_uri);
	if (absolute_uri != NULL)
		xmlFree (absolute_uri);
	if (color_spec != NULL)
		xmlFree (color_spec);
	if (color != NULL)
		xmlFree (color);

	return retval;
}

/**
 * e_source_name_from_xml_node:
 * @node: A pointer to an XML node.
 *
 * Assuming that @node is a valid ESource specification, retrieve the name of
 * the source from it.
 *
 * Returns: Name of the source in the specified @node.  The caller must
 * free the string.
 **/
gchar *
e_source_uid_from_xml_node (xmlNodePtr node)
{
	xmlChar *uid = xmlGetProp (node, (xmlChar*)"uid");
	gchar *retval;

	if (uid == NULL)
		return NULL;

	retval = g_strdup ((gchar *)uid);
	xmlFree (uid);
	return retval;
}

gchar *
e_source_build_absolute_uri (ESource *source)
{
	const gchar *base_uri_str;
	gchar *uri_str;

	g_return_val_if_fail (source->priv->group != NULL, NULL);

	base_uri_str = e_source_group_peek_base_uri (source->priv->group);

	/* If last character in base URI is a slash, just concat the
	 * strings.  We don't want to compress e.g. the trailing ://
	 * in a protocol specification Note: Do not use
	 * G_DIR_SEPARATOR or g_build_filename() when manipulating
	 * URIs. URIs use normal ("forward") slashes also on Windows.
	 */
	if (*base_uri_str && *(base_uri_str + strlen (base_uri_str) - 1) == '/')
		uri_str = g_strconcat (base_uri_str, source->priv->relative_uri, NULL);
	else {
		if (source->priv->relative_uri != NULL)
			uri_str = g_strconcat (base_uri_str, "/", source->priv->relative_uri,
				       NULL);
		else
			uri_str = g_strdup (base_uri_str);
	}

	return uri_str;
}

void
e_source_set_group (ESource *source,
		    ESourceGroup *group)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (group == NULL || E_IS_SOURCE_GROUP (group));

	if (source->priv->readonly)
		return;

	if (source->priv->group == group)
		return;

	if (source->priv->group != NULL)
		g_object_weak_unref (G_OBJECT (source->priv->group), (GWeakNotify) group_weak_notify, source);

	source->priv->group = group;
	if (group != NULL) {
		g_object_weak_ref (G_OBJECT (group), (GWeakNotify) group_weak_notify, source);
	}

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_name (ESource *source,
		   const gchar *name)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (name != NULL);

	if (source->priv->readonly)
		return;

	if (source->priv->name != NULL &&
	    strcmp (source->priv->name, name) == 0)
		return;

	g_free (source->priv->name);
	source->priv->name = g_strdup (name);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_relative_uri (ESource *source,
			   const gchar *relative_uri)
{
	gchar *absolute_uri, *old_abs_uri = NULL;

	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->readonly)
		return;

	if (source->priv->relative_uri == relative_uri ||
	    (source->priv->relative_uri && relative_uri && g_str_equal (source->priv->relative_uri, relative_uri)))
		return;

	if (source->priv->group)
		old_abs_uri = e_source_build_absolute_uri (source);

	g_free (source->priv->relative_uri);
	source->priv->relative_uri = g_strdup (relative_uri);

	/* reset the absolute uri, if it's a generated one */
	if (source->priv->absolute_uri &&
	    (!old_abs_uri || g_str_equal (source->priv->absolute_uri, old_abs_uri)) &&
	    (absolute_uri = e_source_build_absolute_uri (source))) {
		g_free (source->priv->absolute_uri);
		source->priv->absolute_uri = absolute_uri;
	}

	g_free (old_abs_uri);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_absolute_uri (ESource *source,
			   const gchar *absolute_uri)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if ((absolute_uri == source->priv->absolute_uri && absolute_uri == NULL)
	    || (absolute_uri && source->priv->absolute_uri && !strcmp (source->priv->absolute_uri, absolute_uri)))
		return;

	g_free (source->priv->absolute_uri);
	source->priv->absolute_uri = g_strdup (absolute_uri);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_set_readonly (ESource  *source,
		       gboolean  readonly)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (source->priv->readonly == readonly)
		return;

	source->priv->readonly = readonly;

	g_signal_emit (source, signals[CHANGED], 0);

}

/**
 * e_source_set_color_spec:
 * @source: an ESource
 * @color_spec: a string specifying the color
 *
 * Store a textual representation of a color in @source.  The @color_spec
 * string should be parsable by #gdk_color_parse(), or %NULL to unset the
 * color in @source.
 *
 * Since: 1.10
 **/
void
e_source_set_color_spec (ESource *source,
			 const gchar *color_spec)
{
	g_return_if_fail (E_IS_SOURCE (source));

	if (!source->priv->readonly && set_color_spec (source, color_spec))
		g_signal_emit (source, signals[CHANGED], 0);
}

ESourceGroup *
e_source_peek_group (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->group;
}

const gchar *
e_source_peek_uid (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->uid;
}

const gchar *
e_source_peek_name (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->name;
}

const gchar *
e_source_peek_relative_uri (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->relative_uri;
}

const gchar *
e_source_peek_absolute_uri (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->absolute_uri;
}

/**
 * e_source_peek_color_spec:
 * @source: an ESource
 *
 * Return the textual representation of the color for @source, or %NULL if it
 * has none.  The returned string should be parsable by #gdk_color_parse().
 *
 * Returns: a string specifying the color
 *
 * Since: 1.10
 **/
const gchar *
e_source_peek_color_spec (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source->priv->color_spec;
}

gboolean
e_source_get_readonly (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return source->priv->readonly;
}

gchar *
e_source_get_uri (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	if (source->priv->group == NULL) {
		if (source->priv->absolute_uri != NULL)
			return g_strdup (source->priv->absolute_uri);

		g_warning ("e_source_get_uri () called on source with no absolute URI!");
		return NULL;
	}
	else if (source->priv->absolute_uri != NULL) /* source->priv->group != NULL */
		return g_strdup (source->priv->absolute_uri);
	else
		return e_source_build_absolute_uri (source);
}

static void
property_dump_cb (const xmlChar *key, const xmlChar *value, xmlNodePtr root)
{
	xmlNodePtr node;

	node = xmlNewChild (root, NULL, (xmlChar*)"property", NULL);
	xmlSetProp (node, (xmlChar*)"name", key);
	xmlSetProp (node, (xmlChar*)"value", value);
}

static xmlNodePtr
dump_common_to_xml_node (ESource *source,
			 xmlNodePtr parent_node)
{
	ESourcePrivate *priv;
	xmlNodePtr node;
	const gchar *abs_uri = NULL, *relative_uri = NULL;

	priv = source->priv;

	if (parent_node)
		node = xmlNewChild (parent_node, NULL, (xmlChar*)"source", NULL);
	else
		node = xmlNewNode (NULL, (xmlChar*)"source");

	xmlSetProp (node, (xmlChar*)"uid", (xmlChar*)e_source_peek_uid (source));
	xmlSetProp (node, (xmlChar*)"name", (xmlChar*)e_source_peek_name (source));
	abs_uri = e_source_peek_absolute_uri (source);
	relative_uri = e_source_peek_relative_uri (source);
	if (abs_uri)
		xmlSetProp (node, (xmlChar*)"uri", (xmlChar*)abs_uri);
	if (relative_uri)
		xmlSetProp (node, (xmlChar*)"relative_uri", (xmlChar*)relative_uri);

	if (priv->color_spec != NULL)
		xmlSetProp (node, (xmlChar*)"color_spec", (xmlChar*)priv->color_spec);

	if (g_hash_table_size (priv->properties) != 0) {
		xmlNodePtr properties_node;

		properties_node = xmlNewChild (node, NULL, (xmlChar*)"properties", NULL);
		g_hash_table_foreach (priv->properties, (GHFunc) property_dump_cb, properties_node);
	}

	return node;
}

void
e_source_dump_to_xml_node (ESource *source,
			   xmlNodePtr parent_node)
{
	g_return_if_fail (E_IS_SOURCE (source));

	dump_common_to_xml_node (source, parent_node);
}

gchar *
e_source_to_standalone_xml (ESource *source)
{
	xmlDocPtr doc;
	xmlNodePtr node;
	xmlChar *xml_buffer;
	gchar *returned_buffer;
	gint xml_buffer_size;
	gchar *uri;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	doc = xmlNewDoc ((xmlChar*)"1.0");
	node = dump_common_to_xml_node (source, NULL);

	xmlDocSetRootElement (doc, node);

	uri = e_source_get_uri (source);
	xmlSetProp (node, (xmlChar*)"uri", (xmlChar*)uri);
	g_free (uri);

	xmlDocDumpMemory (doc, &xml_buffer, &xml_buffer_size);
	xmlFreeDoc (doc);

	returned_buffer = g_malloc (xml_buffer_size + 1);
	memcpy (returned_buffer, xml_buffer, xml_buffer_size);
	returned_buffer [xml_buffer_size] = '\0';
	xmlFree (xml_buffer);

	return returned_buffer;
}

/**
 * e_source_equal:
 * @a: An ESource
 * @b: Another ESource
 *
 * Compares if @a is equivalent to @b.
 *
 * Returns: %TRUE if @a is equivalent to @b,
 * %FALSE otherwise.
 *
 * Since: 2.24
 **/
gboolean
e_source_equal (ESource *a, ESource *b)
{
	g_return_val_if_fail (E_IS_SOURCE (a), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (b), FALSE);

	#define ONLY_ONE_NULL(aa, bb) (((aa) == NULL && (bb) != NULL) || ((aa) != NULL && (bb) == NULL))

	/* Compare source stuff */
	if (a->priv->uid
	 && b->priv->uid
	 && g_ascii_strcasecmp (a->priv->uid, b->priv->uid))
		return FALSE;

	if (a->priv->name
	 && b->priv->name
	 && g_ascii_strcasecmp (a->priv->name, b->priv->name))
		return FALSE;

	if (a->priv->relative_uri
	 && b->priv->relative_uri
	 && g_ascii_strcasecmp (a->priv->relative_uri, b->priv->relative_uri))
		return FALSE;

	if (a->priv->absolute_uri
	 && b->priv->absolute_uri
	 && g_ascii_strcasecmp (a->priv->absolute_uri, b->priv->absolute_uri))
		return FALSE;

	if ((a->priv->color_spec
	 && b->priv->color_spec
	 && g_ascii_strcasecmp (a->priv->color_spec, b->priv->color_spec)) ||
	 (ONLY_ONE_NULL (a->priv->color_spec, b->priv->color_spec)))
		return FALSE;

	if (a->priv->readonly != b->priv->readonly)
		return FALSE;

	if (!compare_str_hashes (a->priv->properties, b->priv->properties))
		return FALSE;

	#undef ONLY_ONE_NULL

	return TRUE;
}

/**
 * e_source_xmlstr_equal:
 * @a: XML representation of an ESource
 * @b: XML representation of another ESource
 *
 * Compares if @a is equivalent to @b.
 *
 * Returns: %TRUE if @a is equivalent to @b,
 * %FALSE otherwise.
 *
 * Since: 2.24
 **/
gboolean
e_source_xmlstr_equal (const gchar *a, const gchar *b)
{
	ESource *srca, *srcb;
	gboolean retval;

	srca = e_source_new_from_standalone_xml (a);
	srcb = e_source_new_from_standalone_xml (b);

	retval = e_source_equal (srca, srcb);

	g_object_unref (srca);
	g_object_unref (srcb);

	return retval;
}

ESource *
e_source_new_from_standalone_xml (const gchar *xml)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	ESource *source;

	doc = xmlParseDoc ((xmlChar*)xml);
	if (doc == NULL)
		return NULL;

	root = doc->children;
	if (strcmp ((gchar *)root->name, "source") != 0)
		return NULL;

	source = e_source_new_from_xml_node (root);
	xmlFreeDoc (doc);

	return source;
}

const gchar *
e_source_get_property (ESource *source,
		       const gchar *property)
{
	ESourcePrivate *priv;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	priv = source->priv;

	return g_hash_table_lookup (priv->properties, property);
}

/**
 * e_source_get_duped_property:
 *
 * Since: 1.12
 **/
gchar *
e_source_get_duped_property (ESource *source, const gchar *property)
{
	ESourcePrivate *priv;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	priv = source->priv;

	return g_strdup (g_hash_table_lookup (priv->properties, property));
}

void
e_source_set_property (ESource *source,
		       const gchar *property,
		       const gchar *value)
{
	ESourcePrivate *priv;

	g_return_if_fail (E_IS_SOURCE (source));
	priv = source->priv;

	if (value)
		g_hash_table_replace (priv->properties, g_strdup (property), g_strdup (value));
	else
		g_hash_table_remove (priv->properties, property);

	g_signal_emit (source, signals[CHANGED], 0);
}

void
e_source_foreach_property (ESource *source, GHFunc func, gpointer data)
{
	ESourcePrivate *priv;

	g_return_if_fail (E_IS_SOURCE (source));
	priv = source->priv;

	g_hash_table_foreach (priv->properties, func, data);
}

static void
copy_property (const gchar *key, const gchar *value, ESource *new_source)
{
	e_source_set_property (new_source, key, value);
}

ESource *
e_source_copy (ESource *source)
{
	ESource *new_source;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	new_source = g_object_new (e_source_get_type (), NULL);
	new_source->priv->uid = g_strdup (e_source_peek_uid (source));

	e_source_set_name (new_source, e_source_peek_name (source));

	new_source->priv->color_spec = g_strdup (source->priv->color_spec);

	new_source->priv->absolute_uri = g_strdup (e_source_peek_absolute_uri (source));

	e_source_foreach_property (source, (GHFunc) copy_property, new_source);

	return new_source;
}

