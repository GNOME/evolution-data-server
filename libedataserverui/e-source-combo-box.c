/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-option-menu.c
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-source-combo-box.h"
#include "e-cell-renderer-color.h"

#define E_SOURCE_COMBO_BOX_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_COMBO_BOX, ESourceComboBoxPrivate))

struct _ESourceComboBoxPrivate {
	ESourceList *source_list;
	GHashTable *uid_index;
	gulong handler_id;
};

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

enum {
	COLUMN_COLOR,		/* GDK_TYPE_COLOR */
	COLUMN_NAME,		/* G_TYPE_STRING */
	COLUMN_SENSITIVE,	/* G_TYPE_BOOLEAN */
	COLUMN_SOURCE,		/* G_TYPE_OBJECT */
	COLUMN_UID,		/* G_TYPE_STRING */
	COLUMN_VISIBLE,		/* G_TYPE_BOOLEAN */
	NUM_COLUMNS
};

G_DEFINE_TYPE (ESourceComboBox, e_source_combo_box, GTK_TYPE_COMBO_BOX)

static gint
compare_source_names (ESource *source_a,
                      ESource *source_b)
{
	const gchar *name_a;
	const gchar *name_b;

	g_return_val_if_fail (E_IS_SOURCE (source_a), -1);
	g_return_val_if_fail (E_IS_SOURCE (source_b),  1);

	name_a = e_source_get_display_name (source_a);
	name_b = e_source_get_display_name (source_b);

	return g_utf8_collate (name_a, name_b);
}

static void
source_list_changed_cb (ESourceList *source_list,
                        ESourceComboBox *source_combo_box)
{
	ESourceComboBoxPrivate *priv;
	GtkComboBox *combo_box;
	GtkTreeModel *model;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreePath *path;
	GSList *groups;
	GSList *sources, *s;
	const gchar *name;
	const gchar *uid;
	gchar *indented_name;
	gboolean visible = FALSE;
	gboolean iter_valid;

	priv = source_combo_box->priv;

	combo_box = GTK_COMBO_BOX (source_combo_box);

	model = gtk_combo_box_get_model (combo_box);
	store = GTK_LIST_STORE (model);

	if (source_list == NULL) {
		gtk_list_store_clear (store);
		return;
	}

	/* XXX The algorithm below is needlessly complex.  Would be
	 *     easier just to clear and rebuild the store.  There's
	 *     hardly a performance issue here since source lists
	 *     are short. */

	iter_valid = gtk_tree_model_get_iter_first (model, &iter);

	for (groups = e_source_list_peek_groups (source_list);
		groups != NULL; groups = groups->next) {

		/* Only show source groups that have sources. */
		if (e_source_group_peek_sources (groups->data) == NULL)
			continue;

		name = e_source_group_peek_name (groups->data);
		if (!iter_valid)
			gtk_list_store_append (store, &iter);
		gtk_list_store_set (
			store, &iter,
			COLUMN_COLOR, NULL,
			COLUMN_NAME, name,
			COLUMN_SENSITIVE, FALSE,
			COLUMN_SOURCE, groups->data,
			-1);
		iter_valid = gtk_tree_model_iter_next (model, &iter);

		sources = e_source_group_peek_sources (groups->data);

		/* Create a shallow copy and sort by name. */
		sources = g_slist_sort (
			g_slist_copy (sources),
			(GCompareFunc) compare_source_names);

		for (s = sources; s != NULL; s = s->next) {
			const gchar *color_spec;
			GdkColor color;

			uid = e_source_get_uid (s->data);
			name = e_source_get_display_name (s->data);
			indented_name = g_strconcat ("    ", name, NULL);

			color_spec = e_source_peek_color_spec (s->data);
			if (color_spec != NULL) {
				gdk_color_parse (color_spec, &color);
				visible = TRUE;
			}

			if (!iter_valid)
				gtk_list_store_append (store, &iter);
			gtk_list_store_set (
				store, &iter,
				COLUMN_COLOR, color_spec ? &color : NULL,
				COLUMN_NAME, indented_name,
				COLUMN_SENSITIVE, TRUE,
				COLUMN_SOURCE, s->data,
				COLUMN_UID, uid,
				-1);

			path = gtk_tree_model_get_path (model, &iter);
			g_hash_table_replace (
				priv->uid_index, g_strdup (uid),
				gtk_tree_row_reference_new (model, path));
			gtk_tree_path_free (path);
			iter_valid = gtk_tree_model_iter_next (model, &iter);

			g_free (indented_name);
		}
		g_slist_free (sources);
	}

	/* Remove any extra references that might be leftover from the original model */
	while (gtk_list_store_iter_is_valid (store, &iter))
		gtk_list_store_remove (store, &iter);

	/* Set the visible column based on whether we've seen a color. */
	iter_valid = gtk_tree_model_get_iter_first (model, &iter);
	while (iter_valid) {
		gtk_list_store_set (
			store, &iter, COLUMN_VISIBLE, visible, -1);
		iter_valid = gtk_tree_model_iter_next (model, &iter);
	}
}

static GObject *
e_source_combo_box_constructor (GType type,
                                guint n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
	ESourceComboBox *combo_box;
	GtkCellRenderer *renderer;
	GtkCellLayout *layout;
	GtkListStore *store;
	GObject *object;

	/* Chain up to parent's "constructor" method. */
	object = G_OBJECT_CLASS (e_source_combo_box_parent_class)->constructor (
		type, n_construct_properties, construct_properties);

	combo_box = E_SOURCE_COMBO_BOX (object);

	store = gtk_list_store_new (
		NUM_COLUMNS,
		GDK_TYPE_COLOR,		/* COLUMN_COLOR */
		G_TYPE_STRING,		/* COLUMN_NAME */
		G_TYPE_BOOLEAN,		/* COLUMN_SENSITIVE */
		G_TYPE_OBJECT,		/* COLUMN_SOURCE */
		G_TYPE_STRING,		/* COLUMN_UID */
		G_TYPE_BOOLEAN);	/* COLUMN_VISIBLE */
	gtk_combo_box_set_model (
		GTK_COMBO_BOX (combo_box),
		GTK_TREE_MODEL (store));
	g_object_unref (store);

	gtk_combo_box_set_id_column (GTK_COMBO_BOX (combo_box), COLUMN_UID);

	layout = GTK_CELL_LAYOUT (combo_box);

	renderer = e_cell_renderer_color_new ();
	gtk_cell_layout_pack_start (layout, renderer, FALSE);
	gtk_cell_layout_set_attributes (
		layout, renderer,
		"color", COLUMN_COLOR,
		"sensitive", COLUMN_SENSITIVE,
		"visible", COLUMN_VISIBLE,
		NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (layout, renderer, TRUE);
	gtk_cell_layout_set_attributes (
		layout, renderer,
		"text", COLUMN_NAME,
		"sensitive", COLUMN_SENSITIVE,
		NULL);

	return object;
}

static void
e_source_combo_box_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			e_source_combo_box_set_source_list (
				E_SOURCE_COMBO_BOX (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_combo_box_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (
				value, e_source_combo_box_get_source_list (
				E_SOURCE_COMBO_BOX (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_combo_box_dispose (GObject *object)
{
	ESourceComboBoxPrivate *priv;

	priv = E_SOURCE_COMBO_BOX_GET_PRIVATE (object);

	if (priv->source_list != NULL) {
		g_signal_handler_disconnect (
			priv->source_list, priv->handler_id);
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	g_hash_table_remove_all (priv->uid_index);

	/* Chain up to parent's "dispose" method. */
	G_OBJECT_CLASS (e_source_combo_box_parent_class)->dispose (object);
}

static void
e_source_combo_box_finalize (GObject *object)
{
	ESourceComboBoxPrivate *priv;

	priv = E_SOURCE_COMBO_BOX_GET_PRIVATE (object);

	g_hash_table_destroy (priv->uid_index);

	/* Chain up to parent's "finalize" method. */
	G_OBJECT_CLASS (e_source_combo_box_parent_class)->finalize (object);
}

static void
e_source_combo_box_class_init (ESourceComboBoxClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	g_type_class_add_private (class, sizeof (ESourceComboBoxPrivate));

	object_class->constructor = e_source_combo_box_constructor;
	object_class->set_property = e_source_combo_box_set_property;
	object_class->get_property = e_source_combo_box_get_property;
	object_class->dispose = e_source_combo_box_dispose;
	object_class->finalize = e_source_combo_box_finalize;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			"source-list",
			"List of sources to choose from",
			E_TYPE_SOURCE_LIST,
			G_PARAM_READWRITE));
}

static void
e_source_combo_box_init (ESourceComboBox *source_combo_box)
{
	source_combo_box->priv =
		E_SOURCE_COMBO_BOX_GET_PRIVATE (source_combo_box);
	source_combo_box->priv->uid_index =
		g_hash_table_new_full (
			g_str_hash, g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) gtk_tree_row_reference_free);
}

/**
 * e_source_combo_box_new:
 * @source_list: an #ESourceList
 *
 * Creates a new #ESourceComboBox widget that lets the user pick an #ESource
 * from the provided #ESourceList.
 *
 * Returns: a new #ESourceComboBox
 *
 * Since: 2.22
 **/
GtkWidget *
e_source_combo_box_new (ESourceList *source_list)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	return g_object_new (
		E_TYPE_SOURCE_COMBO_BOX,
		"source-list", source_list, NULL);
}

/**
 * e_source_combo_box_get_source_list:
 * @combo_box: an #ESourceComboBox
 *
 * Returns the #ESourceList which is acting as a data source for
 * @combo_box.
 *
 * Returns: an #ESourceList
 *
 * Since: 2.22
 **/
ESourceList *
e_source_combo_box_get_source_list (ESourceComboBox *combo_box)
{
	g_return_val_if_fail (E_IS_SOURCE_COMBO_BOX (combo_box), NULL);

	return combo_box->priv->source_list;
}

/**
 * e_source_combo_box_set_source_list:
 * @combo_box: an #ESourceComboBox
 * @source_list: an #ESourceList
 *
 * Sets the source list used by @source_combo_box to be @source_list.  This
 * causes the contents of @source_combo_box to be regenerated.
 *
 * Since: 2.22
 **/
void
e_source_combo_box_set_source_list (ESourceComboBox *combo_box,
                                    ESourceList *source_list)
{
	g_return_if_fail (E_IS_SOURCE_COMBO_BOX (combo_box));

	if (source_list != NULL) {
		g_return_if_fail (E_IS_SOURCE_LIST (source_list));
		g_object_ref (source_list);
	}

	if (combo_box->priv->source_list != NULL) {
		g_signal_handler_disconnect (
			combo_box->priv->source_list,
			combo_box->priv->handler_id);
		g_object_unref (combo_box->priv->source_list);
		combo_box->priv->handler_id = 0;
	}

	combo_box->priv->source_list = source_list;

	/* Reset the tree store. */
	source_list_changed_cb (source_list, combo_box);

	/* Watch for source list changes. */
	if (source_list != NULL) {
		combo_box->priv->handler_id =
			g_signal_connect_object (
				source_list, "changed",
				G_CALLBACK (source_list_changed_cb),
				combo_box, 0);
	}

	g_object_notify (G_OBJECT (combo_box), "source-list");
}

/**
 * e_source_combo_box_ref_active:
 * @combo_box: an #ESourceComboBox
 *
 * Returns the #ESource corresponding to the currently active item,
 * or %NULL if there is no active item.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: an #ESource or %NULL
 *
 * Since: 3.6
 **/
ESource *
e_source_combo_box_ref_active (ESourceComboBox *combo_box)
{
	GtkComboBox *gtk_combo_box;
	GtkTreeIter iter;
	ESource *source;

	g_return_val_if_fail (E_IS_SOURCE_COMBO_BOX (combo_box), NULL);

	gtk_combo_box = GTK_COMBO_BOX (combo_box);

	if (!gtk_combo_box_get_active_iter (gtk_combo_box, &iter))
		return NULL;

	gtk_tree_model_get (
		gtk_combo_box_get_model (gtk_combo_box),
		&iter, COLUMN_SOURCE, &source, -1);

	return source;
}

/**
 * e_source_combo_box_set_active:
 * @combo_box: an #ESourceComboBox
 * @source: an #ESource
 *
 * Sets the active item to the one corresponding to @source.
 *
 * Since: 2.22
 **/
void
e_source_combo_box_set_active (ESourceComboBox *combo_box,
                               ESource *source)
{
	GtkComboBox *gtk_combo_box;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_COMBO_BOX (combo_box));
	g_return_if_fail (E_IS_SOURCE (source));

	uid = e_source_get_uid (source);

	gtk_combo_box = GTK_COMBO_BOX (combo_box);
	gtk_combo_box_set_active_id (gtk_combo_box, uid);
}

