/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-group.c
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
#include "e-source-group.h"

#define XC (const xmlChar *)
#define GC (const gchar *)

/* Private members.  */

struct _ESourceGroupPrivate {
	gchar *uid;
	gchar *name;
	gchar *base_uri;

	GSList *sources;

	gboolean ignore_source_changed;
	gboolean readonly;

	GHashTable *properties;
};

/* Signals.  */

enum {
	CHANGED,
	SOURCE_REMOVED,
	SOURCE_ADDED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

/* Callbacks.  */

static void
source_changed_callback (ESource *source,
			 ESourceGroup *group)
{
	if (!group->priv->ignore_source_changed)
		g_signal_emit (group, signals[CHANGED], 0);
}

/* GObject methods.  */

G_DEFINE_TYPE (ESourceGroup, e_source_group, G_TYPE_OBJECT)

static void
impl_dispose (GObject *object)
{
	ESourceGroupPrivate *priv = E_SOURCE_GROUP (object)->priv;

	if (priv->sources != NULL) {
		GSList *p;

		for (p = priv->sources; p != NULL; p = p->next) {
			ESource *source = E_SOURCE (p->data);

			g_signal_handlers_disconnect_by_func (source,
							      G_CALLBACK (source_changed_callback),
							      object);
			g_object_unref (source);
		}

		g_slist_free (priv->sources);
		priv->sources = NULL;
	}

	(* G_OBJECT_CLASS (e_source_group_parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	ESourceGroupPrivate *priv = E_SOURCE_GROUP (object)->priv;

	g_free (priv->uid);
	g_free (priv->name);
	g_free (priv->base_uri);

	g_hash_table_destroy (priv->properties);

	g_free (priv);

	(* G_OBJECT_CLASS (e_source_group_parent_class)->finalize) (object);
}

/* Initialization.  */

static void
e_source_group_class_init (ESourceGroupClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceGroupClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[SOURCE_ADDED] =
		g_signal_new ("source_added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceGroupClass, source_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_OBJECT);
	signals[SOURCE_REMOVED] =
		g_signal_new ("source_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceGroupClass, source_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_OBJECT);
}

static void
e_source_group_init (ESourceGroup *source_group)
{
	ESourceGroupPrivate *priv;

	priv = g_new0 (ESourceGroupPrivate, 1);
	source_group->priv = priv;

	priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, g_free);
}

static void
import_properties (ESourceGroup *source_group,
		   xmlNodePtr prop_root)
{
	ESourceGroupPrivate *priv = source_group->priv;
	xmlNodePtr prop_node;

	for (prop_node = prop_root->children; prop_node; prop_node = prop_node->next) {
		xmlChar *name, *value;

		if (!prop_node->name || strcmp (GC prop_node->name, "property"))
			continue;

		name = xmlGetProp (prop_node, XC "name");
		value = xmlGetProp (prop_node, XC "value");

		if (name && value) {
			g_hash_table_insert (priv->properties, g_strdup (GC name), g_strdup (GC value));
		}

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

static void
property_dump_cb (const gchar *key, const gchar *value, xmlNodePtr root)
{
	xmlNodePtr node;

	node = xmlNewChild (root, NULL, XC "property", NULL);
	xmlSetProp (node, XC "name", XC key);
	xmlSetProp (node, XC "value", XC value);
}

/* Public methods.  */

ESourceGroup *
e_source_group_new (const gchar *name,
		    const gchar *base_uri)
{
	ESourceGroup *new;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (base_uri != NULL, NULL);

	new = g_object_new (e_source_group_get_type (), NULL);
	new->priv->uid = e_uid_new ();

	e_source_group_set_name (new, name);
	e_source_group_set_base_uri (new, base_uri);

	return new;
}

ESourceGroup *
e_source_group_new_from_xml (const gchar *xml)
{
	xmlDocPtr doc;
	ESourceGroup *group;

	doc = xmlParseDoc (XC xml);
	if (doc == NULL)
		return NULL;

	group = e_source_group_new_from_xmldoc (doc);
	xmlFreeDoc (doc);

	return group;
}

ESourceGroup *
e_source_group_new_from_xmldoc (xmlDocPtr doc)
{
	xmlNodePtr root, p;
	xmlChar *uid;
	xmlChar *name;
	xmlChar *base_uri;
	xmlChar *readonly_str;
	ESourceGroup *new = NULL;

	g_return_val_if_fail (doc != NULL, NULL);

	root = doc->children;
	if (strcmp (GC root->name, "group") != 0)
		return NULL;

	uid = xmlGetProp (root, XC "uid");
	name = xmlGetProp (root, XC "name");
	base_uri = xmlGetProp (root, XC "base_uri");
	readonly_str = xmlGetProp (root, XC "readonly");

	if (uid == NULL || name == NULL || base_uri == NULL)
		goto done;

	new = g_object_new (e_source_group_get_type (), NULL);

	if (!new)
		goto done;

	new->priv->uid = g_strdup (GC uid);

	e_source_group_set_name (new, GC name);
	e_source_group_set_base_uri (new, GC base_uri);

	for (p = root->children; p != NULL; p = p->next) {
		ESource *new_source;

		if (p->name && !strcmp (GC p->name, "properties")) {
			import_properties (new, p);
			continue;
		}

		new_source = e_source_new_from_xml_node (p);

		if (new_source == NULL) {
			g_object_unref (new);
			new = NULL;
			goto done;
		}
		e_source_group_add_source (new, new_source, -1);
		g_object_unref (new_source);
	}

	e_source_group_set_readonly (new, readonly_str && !strcmp (GC readonly_str, "yes"));

 done:
	if (uid != NULL)
		xmlFree (uid);

	if (name != NULL)
		xmlFree (name);
	if (base_uri != NULL)
		xmlFree (base_uri);
	if (readonly_str != NULL)
		xmlFree (readonly_str);
	return new;
}

gboolean
e_source_group_update_from_xml (ESourceGroup *group,
				const gchar *xml,
				gboolean *changed_return)
{
	xmlDocPtr xmldoc;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (xml != NULL, FALSE);

	xmldoc = xmlParseDoc (XC xml);

	success = e_source_group_update_from_xmldoc (group, xmldoc, changed_return);

	xmlFreeDoc (xmldoc);

	return success;
}

gboolean
e_source_group_update_from_xmldoc (ESourceGroup *group,
				   xmlDocPtr doc,
				   gboolean *changed_return)
{
	GHashTable *new_sources_hash;
	GSList *new_sources_list = NULL;
	xmlNodePtr root, nodep;
	xmlChar *name, *base_uri, *readonly_str;
	gboolean readonly;
	gboolean changed = FALSE;
	GSList *p, *q;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (doc != NULL, FALSE);

	*changed_return = FALSE;

	root = doc->children;
	if (strcmp (GC root->name, "group") != 0)
		return FALSE;

	name = xmlGetProp (root, XC "name");
	if (name == NULL)
		return FALSE;

	base_uri = xmlGetProp (root, XC "base_uri");
	if (base_uri == NULL) {
		xmlFree (name);
		return FALSE;
	}

	if (strcmp (group->priv->name, GC name) != 0) {
		g_free (group->priv->name);
		group->priv->name = g_strdup (GC name);
		changed = TRUE;
	}
	xmlFree (name);

	if (strcmp (group->priv->base_uri, GC base_uri) != 0) {
		g_free (group->priv->base_uri);
		group->priv->base_uri = g_strdup (GC base_uri);
		changed = TRUE;
	}
	xmlFree (base_uri);

	readonly_str = xmlGetProp (root, XC "readonly");
	readonly = readonly_str && !strcmp (GC readonly_str, "yes");
	if (readonly != group->priv->readonly) {
		group->priv->readonly = readonly;
		changed = TRUE;
	}
	xmlFree (readonly_str);

	if (g_hash_table_size (group->priv->properties) && !root->children) {
		g_hash_table_destroy (group->priv->properties);
		group->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
								  g_free, g_free);
		changed = TRUE;
	}

	new_sources_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (nodep = root->children; nodep != NULL; nodep = nodep->next) {
		ESource *existing_source;
		gchar *uid;

		if (!nodep->name)
			continue;

		if (!strcmp (GC nodep->name, "properties")) {
			GHashTable *temp = group->priv->properties;
			group->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
									  g_free, g_free);
			import_properties (group, nodep);
			if (!compare_str_hashes (temp, group->priv->properties))
				changed = TRUE;
			g_hash_table_destroy (temp);
			continue;
		}

		uid = e_source_uid_from_xml_node (nodep);
		if (uid == NULL)
			continue;

		existing_source = e_source_group_peek_source_by_uid (group, uid);
		if (g_hash_table_lookup (new_sources_hash, existing_source) != NULL) {
			g_free (uid);
			continue;
		}

		if (existing_source == NULL) {
			ESource *new_source = e_source_new_from_xml_node (nodep);

			if (new_source != NULL) {
				e_source_set_group (new_source, group);
				g_signal_connect (new_source, "changed", G_CALLBACK (source_changed_callback), group);
				new_sources_list = g_slist_prepend (new_sources_list, new_source);

				g_hash_table_insert (new_sources_hash, new_source, new_source);

				g_signal_emit (group, signals[SOURCE_ADDED], 0, new_source);
				changed = TRUE;
			}
		} else {
			gboolean source_changed;

			group->priv->ignore_source_changed ++;

			if (e_source_update_from_xml_node (existing_source, nodep, &source_changed)) {
				new_sources_list = g_slist_prepend (new_sources_list, existing_source);
				g_object_ref (existing_source);
				g_hash_table_insert (new_sources_hash, existing_source, existing_source);

				if (source_changed)
					changed = TRUE;
			}

			group->priv->ignore_source_changed --;
		}

		g_free (uid);
	}

	new_sources_list = g_slist_reverse (new_sources_list);

	/* Emit "group_removed" and disconnect the "changed" signal for all the
	   groups that we haven't found in the new list.  */
	q = new_sources_list;
	for (p = group->priv->sources; p != NULL; p = p->next) {
		ESource *source = E_SOURCE (p->data);

		if (g_hash_table_lookup (new_sources_hash, source) == NULL) {
			changed = TRUE;

			g_signal_emit (group, signals[SOURCE_REMOVED], 0, source);
			g_signal_handlers_disconnect_by_func (source, source_changed_callback, group);
		}

		if (!changed && q != NULL) {
			if (q->data != p->data)
				changed = TRUE;
			q = q->next;
		}
	}

	g_hash_table_destroy (new_sources_hash);

	/* Replace the original group list with the new one.  */
	g_slist_foreach (group->priv->sources, (GFunc) g_object_unref, NULL);
	g_slist_free (group->priv->sources);

	group->priv->sources = new_sources_list;

	/* FIXME if the order changes, the function doesn't notice.  */

	if (changed) {
		g_signal_emit (group, signals[CHANGED], 0);
		*changed_return = TRUE;
	}

	return TRUE;		/* Success. */
}

gchar *
e_source_group_uid_from_xmldoc (xmlDocPtr doc)
{
	xmlNodePtr root = doc->children;
	xmlChar *name;
	gchar *retval;

	if (root && root->name) {
		if (strcmp (GC root->name, "group") != 0)
			return NULL;
	}
	else
		return NULL;

	name = xmlGetProp (root, XC "uid");
	if (name == NULL)
		return NULL;

	retval = g_strdup (GC name);
	xmlFree (name);
	return retval;
}

void
e_source_group_set_name (ESourceGroup *group,
			 const gchar *name)
{
	g_return_if_fail (E_IS_SOURCE_GROUP (group));
	g_return_if_fail (name != NULL);

	if (group->priv->readonly)
		return;

	if (group->priv->name != NULL &&
	    strcmp (group->priv->name, name) == 0)
		return;

	g_free (group->priv->name);
	group->priv->name = g_strdup (name);

	g_signal_emit (group, signals[CHANGED], 0);
}

void e_source_group_set_base_uri (ESourceGroup *group,
				  const gchar *base_uri)
{
	g_return_if_fail (E_IS_SOURCE_GROUP (group));
	g_return_if_fail (base_uri != NULL);

	if (group->priv->readonly)
		return;

	if (group->priv->base_uri == base_uri)
		return;

	g_free (group->priv->base_uri);
	group->priv->base_uri = g_strdup (base_uri);

	g_signal_emit (group, signals[CHANGED], 0);
}

void e_source_group_set_readonly (ESourceGroup *group,
				  gboolean      readonly)
{
	GSList *i;

	g_return_if_fail (E_IS_SOURCE_GROUP (group));

	if (group->priv->readonly)
		return;

	if (group->priv->readonly == readonly)
		return;

	group->priv->readonly = readonly;
	for (i = group->priv->sources; i != NULL; i = i->next)
		e_source_set_readonly (E_SOURCE (i->data), readonly);

	g_signal_emit (group, signals[CHANGED], 0);
}

const gchar *
e_source_group_peek_uid (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->uid;
}

const gchar *
e_source_group_peek_name (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->name;
}

const gchar *
e_source_group_peek_base_uri (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->base_uri;
}

gboolean
e_source_group_get_readonly (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);

	return group->priv->readonly;
}

GSList *
e_source_group_peek_sources (ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), NULL);

	return group->priv->sources;
}

ESource *
e_source_group_peek_source_by_uid (ESourceGroup *group,
				   const gchar *uid)
{
	GSList *p;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		if (strcmp (e_source_peek_uid (E_SOURCE (p->data)), uid) == 0)
			return E_SOURCE (p->data);
	}

	return NULL;
}

ESource *
e_source_group_peek_source_by_name (ESourceGroup *group,
				    const gchar *name)
{
	GSList *p;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		if (strcmp (e_source_peek_name (E_SOURCE (p->data)), name) == 0)
			return E_SOURCE (p->data);
	}

	return NULL;
}

gboolean
e_source_group_add_source (ESourceGroup *group,
			   ESource *source,
			   gint position)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);

	if (group->priv->readonly)
		return FALSE;

	if (e_source_group_peek_source_by_uid (group, e_source_peek_uid (source)) != NULL)
		return FALSE;

	e_source_set_group (source, group);
	e_source_set_readonly (source, group->priv->readonly);
	g_object_ref (source);

	g_signal_connect (source, "changed", G_CALLBACK (source_changed_callback), group);

	group->priv->sources = g_slist_insert (group->priv->sources, source, position);
	g_signal_emit (group, signals[SOURCE_ADDED], 0, source);
	g_signal_emit (group, signals[CHANGED], 0);

	return TRUE;
}

gboolean
e_source_group_remove_source (ESourceGroup *group,
			      ESource *source)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (group->priv->readonly)
		return FALSE;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		if (E_SOURCE (p->data) == source) {
			group->priv->sources = g_slist_remove_link (group->priv->sources, p);
			g_signal_handlers_disconnect_by_func (source,
							      G_CALLBACK (source_changed_callback),
							      group);
			g_signal_emit (group, signals[SOURCE_REMOVED], 0, source);
			g_signal_emit (group, signals[CHANGED], 0);
			g_object_unref (source);
			return TRUE;
		}
	}

	return FALSE;
}

gboolean
e_source_group_remove_source_by_uid (ESourceGroup *group,
				     const gchar *uid)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (group->priv->readonly)
		return FALSE;

	for (p = group->priv->sources; p != NULL; p = p->next) {
		ESource *source = E_SOURCE (p->data);

		if (strcmp (e_source_peek_uid (source), uid) == 0) {
			group->priv->sources = g_slist_remove_link (group->priv->sources, p);
			g_signal_handlers_disconnect_by_func (source,
							      G_CALLBACK (source_changed_callback),
							      group);
			g_signal_emit (group, signals[SOURCE_REMOVED], 0, source);
			g_signal_emit (group, signals[CHANGED], 0);
			g_object_unref (source);
			return TRUE;
		}
	}

	return FALSE;
}

gchar *
e_source_group_to_xml (ESourceGroup *group)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlChar *xml_buffer;
	gchar *returned_buffer;
	gint xml_buffer_size;
	GSList *p;

	doc = xmlNewDoc (XC "1.0");

	root = xmlNewDocNode (doc, NULL, XC "group", NULL);
	xmlSetProp (root, XC "uid", XC e_source_group_peek_uid (group));
	xmlSetProp (root, XC "name", XC e_source_group_peek_name (group));
	xmlSetProp (root, XC "base_uri", XC e_source_group_peek_base_uri (group));
	xmlSetProp (root, XC "readonly", XC (group->priv->readonly ? "yes" : "no"));

	if (g_hash_table_size (group->priv->properties) != 0) {
		xmlNodePtr properties_node;

		properties_node = xmlNewChild (root, NULL, XC "properties", NULL);
		g_hash_table_foreach (group->priv->properties, (GHFunc) property_dump_cb, properties_node);
	}

	xmlDocSetRootElement (doc, root);

	for (p = group->priv->sources; p != NULL; p = p->next)
		e_source_dump_to_xml_node (E_SOURCE (p->data), root);

	xmlDocDumpMemory (doc, &xml_buffer, &xml_buffer_size);
	xmlFreeDoc (doc);

	returned_buffer = g_malloc (xml_buffer_size + 1);
	memcpy (returned_buffer, xml_buffer, xml_buffer_size);
	returned_buffer [xml_buffer_size] = '\0';
	xmlFree (xml_buffer);

	return returned_buffer;
}

static gint
find_esource_from_uid (gconstpointer a, gconstpointer b)
{
	return g_ascii_strcasecmp (e_source_peek_uid ((ESource *)(a)), (gchar *)(b));
}

static gboolean
compare_source_lists (GSList *a, GSList *b)
{
	gboolean retval = TRUE;
	GSList *l;

	if (g_slist_length(a) != g_slist_length(b))
		return FALSE;

	for (l = a; l != NULL && retval; l = l->next) {
		GSList *elem = g_slist_find_custom (b, e_source_peek_uid ((ESource *)(l->data)), (GCompareFunc) find_esource_from_uid);

		if (!elem || !e_source_equal ((ESource *)(l->data), (ESource *)(elem->data)))
			retval = FALSE;
	}

	return retval;
}

/**
 * e_source_group_equal:
 * @a: An ESourceGroup
 * @b: Another ESourceGroup
 *
 * Compares if @a is equivalent to @b.
 *
 * Returns: %TRUE if @a is equivalent to @b,
 * %FALSE otherwise.
 *
 * Since: 2.24
 **/
gboolean
e_source_group_equal (ESourceGroup *a, ESourceGroup *b)
{
	g_return_val_if_fail (E_IS_SOURCE_GROUP (a), FALSE);
	g_return_val_if_fail (E_IS_SOURCE_GROUP (b), FALSE);

	/* Compare group stuff */
	if (a->priv->uid
	 && b->priv->uid
	 && g_ascii_strcasecmp (a->priv->uid, b->priv->uid))
		return FALSE;

	if (a->priv->name
	 && b->priv->name
	 && g_ascii_strcasecmp (a->priv->name, b->priv->name))
		return FALSE;

	if (a->priv->base_uri
	 && b->priv->base_uri
	 && g_ascii_strcasecmp (a->priv->base_uri, b->priv->base_uri))
		return FALSE;

	if (a->priv->readonly != b->priv->readonly)
		return FALSE;

	if (!compare_str_hashes (a->priv->properties, b->priv->properties))
		return FALSE;

	/* Compare ESources in the groups */
	if (!compare_source_lists (a->priv->sources, b->priv->sources))
		return FALSE;

	return TRUE;
}

/**
 * e_source_group_xmlstr_equal:
 * @a: XML representation of an ESourceGroup
 * @b: XML representation of another ESourceGroup
 *
 * Compares if @a is equivalent to @b.
 *
 * Returns: %TRUE if @a is equivalent to @b,
 * %FALSE otherwise.
 *
 * Since: 2.24
 **/
gboolean
e_source_group_xmlstr_equal (const gchar *a, const gchar *b)
{
	ESourceGroup *grpa, *grpb;
	gboolean retval;

	grpa = e_source_group_new_from_xml (a);
	grpb = e_source_group_new_from_xml (b);

	retval = e_source_group_equal (grpa, grpb);

	g_object_unref (grpa);
	g_object_unref (grpb);

	return retval;
}

/**
 * e_source_group_get_property:
 *
 * Since: 1.12
 **/
gchar *
e_source_group_get_property (ESourceGroup *source_group,
			     const gchar *property)
{
	ESourceGroupPrivate *priv;

	g_return_val_if_fail (E_IS_SOURCE_GROUP (source_group), NULL);
	priv = source_group->priv;

	return g_strdup (g_hash_table_lookup (priv->properties, property));
}

/**
 * e_source_group_set_property:
 *
 * Since: 1.12
 **/
void
e_source_group_set_property (ESourceGroup *source_group,
			     const gchar *property,
			     const gchar *value)
{
	ESourceGroupPrivate *priv;

	g_return_if_fail (E_IS_SOURCE_GROUP (source_group));
	priv = source_group->priv;

	if (value)
		g_hash_table_replace (priv->properties, g_strdup (property), g_strdup (value));
	else
		g_hash_table_remove (priv->properties, property);

	g_signal_emit (source_group, signals[CHANGED], 0);
}

/**
 * e_source_group_foreach_property:
 *
 * Since: 1.12
 **/
void
e_source_group_foreach_property (ESourceGroup *source_group, GHFunc func, gpointer data)
{
	ESourceGroupPrivate *priv;

	g_return_if_fail (E_IS_SOURCE_GROUP (source_group));
	priv = source_group->priv;

	g_hash_table_foreach (priv->properties, func, data);
}

#undef XC
#undef GC
