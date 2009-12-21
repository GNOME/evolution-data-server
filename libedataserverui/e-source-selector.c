/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-selector.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include "e-data-server-ui-marshal.h"
#include "e-source-selector.h"

#define E_SOURCE_SELECTOR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_SELECTOR, ESourceSelectorPrivate))

struct _ESourceSelectorPrivate {
	ESourceList *list;

	GtkTreeStore *tree_store;

	GHashTable *selected_sources;
	GtkTreeRowReference *saved_primary_selection;
	ESourceGroup *primary_source_group;

	gint rebuild_model_idle_id;

	gboolean toggled_last;
	gboolean checkboxes_shown;
	gboolean select_new;
};

typedef struct {
	ESourceSelector *selector;

	GHashTable *remaining_uids;
	GSList *deleted_uids;

	gboolean selection_changed;
} ESourceSelectorRebuildData;

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

enum {
	SELECTION_CHANGED,
	PRIMARY_SELECTION_CHANGED,
	POPUP_EVENT,
	DATA_DROPPED,
	NUM_SIGNALS
};
static guint signals[NUM_SIGNALS] = { 0 };

G_DEFINE_TYPE (ESourceSelector, e_source_selector, GTK_TYPE_TREE_VIEW)

/* safe-toggle-renderer definition - it'll not call 'toggled' signal on 'activate', when mouse is not over the toggle */

typedef struct _ECellRendererSafeToggle {
	GtkCellRendererToggle parent;
} ECellRendererSafeToggle;

typedef struct _ECellRendererSafeToggleClass {
	GtkCellRendererToggleClass parent_class;
} ECellRendererSafeToggleClass;

GType e_cell_renderer_safe_toggle_get_type (void);

G_DEFINE_TYPE (ECellRendererSafeToggle, e_cell_renderer_safe_toggle, GTK_TYPE_CELL_RENDERER_TOGGLE)

static gboolean
safe_toggle_activate (GtkCellRenderer      *cell,
		      GdkEvent             *event,
		      GtkWidget            *widget,
		      const gchar          *path,
		      GdkRectangle         *background_area,
		      GdkRectangle         *cell_area,
		      GtkCellRendererState  flags)
{
	if (event->type == GDK_BUTTON_PRESS && cell_area) {
		GdkRegion *reg = gdk_region_rectangle (cell_area);

		if (!gdk_region_point_in (reg, event->button.x, event->button.y)) {
			gdk_region_destroy (reg);
			return FALSE;
		}

		gdk_region_destroy (reg);
	}

	return GTK_CELL_RENDERER_CLASS (e_cell_renderer_safe_toggle_parent_class)->activate (cell, event, widget, path, background_area, cell_area, flags);
}

static void
e_cell_renderer_safe_toggle_class_init (ECellRendererSafeToggleClass *klass)
{
	GtkCellRendererClass *rndr_class;

	rndr_class = GTK_CELL_RENDERER_CLASS (klass);
	rndr_class->activate = safe_toggle_activate;
}

static void
e_cell_renderer_safe_toggle_init (ECellRendererSafeToggle *obj)
{
}

static GtkCellRenderer *
e_cell_renderer_safe_toggle_new (void)
{
	return g_object_new (e_cell_renderer_safe_toggle_get_type (), NULL);
}

/* Selection management.  */

static GHashTable *
create_selected_sources_hash (void)
{
	return g_hash_table_new_full (g_direct_hash, g_direct_equal,
				      (GDestroyNotify) g_object_unref, NULL);
}

static ESourceSelectorRebuildData *
create_rebuild_data (ESourceSelector *selector)
{
	ESourceSelectorRebuildData *rebuild_data;

	rebuild_data = g_new0 (ESourceSelectorRebuildData, 1);

	rebuild_data->selector = selector;
	rebuild_data->remaining_uids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
							      (GDestroyNotify) gtk_tree_row_reference_free);
	rebuild_data->deleted_uids = NULL;

	return rebuild_data;
}

static void
free_rebuild_data (ESourceSelectorRebuildData *rebuild_data)
{
	GSList *p;

	g_hash_table_destroy (rebuild_data->remaining_uids);
	for (p = rebuild_data->deleted_uids; p; p = p->next)
		gtk_tree_row_reference_free (p->data);
	g_slist_free (rebuild_data->deleted_uids);

	g_free (rebuild_data);
}

static void
clear_saved_primary_selection (ESourceSelector *selector)
{
	if (selector->priv->saved_primary_selection != NULL) {
		gtk_tree_row_reference_free (selector->priv->saved_primary_selection);
		selector->priv->saved_primary_selection = NULL;
	}
}

static gboolean
source_is_selected (ESourceSelector *selector,
		    ESource *source)
{
	if (g_hash_table_lookup (selector->priv->selected_sources, source) == NULL)
		return FALSE;
	else
		return TRUE;
}

static void
select_source (ESourceSelector *selector,
	       ESource *source)
{
	if (g_hash_table_lookup (selector->priv->selected_sources, source) != NULL)
		return;

	g_hash_table_insert (selector->priv->selected_sources, source, source);
	g_object_ref (source);
}

static void
unselect_source (ESourceSelector *selector,
		 ESource *source)
{
	if (g_hash_table_lookup (selector->priv->selected_sources, source) == NULL)
		return;

	/* (This will unref the source.)  */
	g_hash_table_remove (selector->priv->selected_sources, source);
}

static gboolean
find_source_iter (ESourceSelector *selector, ESource *source, GtkTreeIter *parent_iter, GtkTreeIter *source_iter)
{
	GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);

	if (gtk_tree_model_get_iter_first (model, parent_iter)) {
		do {
			if (gtk_tree_model_iter_children (model, source_iter, parent_iter)) {
				do {
					gpointer data;

					gtk_tree_model_get (model, source_iter, 0, &data, -1);
					g_assert (E_IS_SOURCE (data));

					if (E_SOURCE (data) == source) {
						g_object_unref (data);

						return TRUE;
					}

					g_object_unref (data);
				} while (gtk_tree_model_iter_next (model, source_iter));
			}
		} while (gtk_tree_model_iter_next (model, parent_iter));
	}

	return FALSE;
}

/* Setting up the model.  */
static gboolean
rebuild_existing_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	ESourceSelectorRebuildData *rebuild_data = data;
	gpointer node;
	const gchar *uid;

	gtk_tree_model_get (model, iter, 0, &node, -1);

	if (E_IS_SOURCE_GROUP (node)) {
		uid = e_source_group_peek_uid (E_SOURCE_GROUP (node));

		if (e_source_list_peek_group_by_uid (rebuild_data->selector->priv->list, uid)) {
			g_hash_table_insert (rebuild_data->remaining_uids, g_strdup (uid),
					     gtk_tree_row_reference_new (model, path));
		} else {
			rebuild_data->deleted_uids = g_slist_prepend (rebuild_data->deleted_uids,
								      gtk_tree_row_reference_new (model, path));
		}
	} else {
		uid = e_source_peek_uid (E_SOURCE (node));
		if (e_source_list_peek_source_by_uid (rebuild_data->selector->priv->list, uid)) {
			g_hash_table_insert (rebuild_data->remaining_uids, g_strdup (uid),
					     gtk_tree_row_reference_new (model, path));
		} else {
			rebuild_data->deleted_uids = g_slist_prepend (rebuild_data->deleted_uids,
								      gtk_tree_row_reference_new (model, path));

			if (g_hash_table_remove (rebuild_data->selector->priv->selected_sources, node))
				rebuild_data->selection_changed = TRUE;
		}
	}

	g_object_unref (node);

	return FALSE;
}

static ESource *
find_source (ESourceSelector *selector, ESource *source)
{
	GSList *groups, *p;

	g_return_val_if_fail (selector != NULL, source);
	g_return_val_if_fail (E_IS_SOURCE (source), source);

	groups = e_source_list_peek_groups (selector->priv->list);
	for (p = groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GSList *sources, *q;

		sources = e_source_group_peek_sources (group);
		for (q = sources; q != NULL; q = q->next) {
			ESource *s = E_SOURCE (q->data);

			if (e_source_equal (s, source))
				return s;
		}
	}

	return source;
}

/**
 * compare_source_names
 * Compares sources by name.
 **/
static gint
compare_source_names (gconstpointer a, gconstpointer b)
{
	g_return_val_if_fail (E_IS_SOURCE (a), -1);
	g_return_val_if_fail (E_IS_SOURCE (b),  1);

	return g_utf8_collate (e_source_peek_name (E_SOURCE (a)), e_source_peek_name (E_SOURCE (b)));
}

/**
 * get_sorted_sources
 * Creates copy of GSList of sources (do not increase reference count for data members),
 * and sorts this list alphabetically by source names.
 *
 * @param sources List of sources.
 * @return New GSList of sorted sources, should be freed by g_slist_free,
 *         but do not unref data members.
 **/
static GSList *
get_sorted_sources (GSList *sources)
{
	GSList *res = NULL, *p;

	if (!sources)
		return NULL;

	for (p = sources; p != NULL; p = p->next)
		res = g_slist_prepend (res, p->data);

	res = g_slist_sort (res, compare_source_names);

	return res;
}

static void
rebuild_model (ESourceSelector *selector)
{
	ESourceSelectorRebuildData *rebuild_data;
	GtkTreeStore *tree_store;
	GtkTreeIter iter;
	GSList *groups, *p;
	gboolean set_primary;

	tree_store = selector->priv->tree_store;

	rebuild_data = create_rebuild_data (selector);
	set_primary = e_source_selector_peek_primary_selection (selector) != NULL;

	/* Remove any delete sources or groups */
	gtk_tree_model_foreach (GTK_TREE_MODEL (tree_store), rebuild_existing_cb, rebuild_data);
	for (p = rebuild_data->deleted_uids; p; p = p->next) {
		GtkTreeRowReference *row_ref = p->data;
		GtkTreePath *path;

		path = gtk_tree_row_reference_get_path (row_ref);
		gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_store), &iter, path);
		gtk_tree_store_remove (tree_store, &iter);

		gtk_tree_path_free (path);
	}

	/* Add new sources/groups or call row_changed in case they were renamed */
	groups = e_source_list_peek_groups (selector->priv->list);
	for (p = groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GSList *sources, *q;
		GtkTreeRowReference *row_ref;
		gint position;

		sources = get_sorted_sources (e_source_group_peek_sources (group));
		if (sources == NULL)
			continue;

		row_ref = g_hash_table_lookup (rebuild_data->remaining_uids, e_source_group_peek_uid (group));
		if (!row_ref) {
			gtk_tree_store_append (GTK_TREE_STORE (tree_store), &iter, NULL);
			gtk_tree_store_set (GTK_TREE_STORE (tree_store), &iter, 0, group, -1);
		} else {
			GtkTreePath *path;

			path = gtk_tree_row_reference_get_path (row_ref);
			gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_store), &iter, path);

			gtk_tree_model_row_changed (GTK_TREE_MODEL (tree_store), path, &iter);

			gtk_tree_path_free (path);
		}

		for (q = sources, position = 0; q != NULL; q = q->next, position++) {
			ESource *source = E_SOURCE (q->data);
			GtkTreeIter child_iter;

			row_ref = g_hash_table_lookup (rebuild_data->remaining_uids, e_source_peek_uid (source));
			if (!row_ref) {
				if (selector->priv->select_new) {
					select_source (selector, source);
					rebuild_data->selection_changed = TRUE;
				}

				gtk_tree_store_insert (GTK_TREE_STORE (tree_store), &child_iter, &iter, position);
				gtk_tree_store_set (GTK_TREE_STORE (tree_store), &child_iter, 0, source, -1);
			} else {
				GtkTreePath *path;

				path = gtk_tree_row_reference_get_path (row_ref);
				gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_store), &child_iter, path);

				gtk_tree_model_row_changed (GTK_TREE_MODEL (tree_store), path, &child_iter);

				gtk_tree_path_free (path);
			}
		}

		g_slist_free (sources);
	}

	if (rebuild_data->selection_changed)
		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);

	if (set_primary && !e_source_selector_peek_primary_selection (selector))
		e_source_selector_set_primary_selection	(selector, e_source_list_peek_source_any (selector->priv->list));

	free_rebuild_data (rebuild_data);
}

static gint
on_idle_rebuild_model_callback (ESourceSelector *selector)
{
	rebuild_model (selector);
	selector->priv->rebuild_model_idle_id = 0;

	return FALSE;
}

static void
list_changed_callback (ESourceList *list,
		       ESourceSelector *selector)
{
	ESourceSelectorPrivate *priv = selector->priv;

	if (priv->rebuild_model_idle_id == 0)
		priv->rebuild_model_idle_id = g_idle_add ((GSourceFunc) on_idle_rebuild_model_callback,
							  selector);
}

/* Data functions for rendering the model.  */

static void
toggle_cell_data_func (GtkTreeViewColumn *column,
		       GtkCellRenderer *renderer,
		       GtkTreeModel *model,
		       GtkTreeIter *iter,
		       ESourceSelector *selector)
{
	gpointer data;

	gtk_tree_model_get (model, iter, 0, &data, -1);
	if (data == NULL) {
		g_object_set (renderer, "visible", FALSE, NULL);
		return;
	}

	if (E_IS_SOURCE_GROUP (data)) {
		g_object_set (renderer, "visible", FALSE, NULL);
	} else {
		g_assert (E_IS_SOURCE (data));

		g_object_set (renderer, "visible", selector->priv->checkboxes_shown, NULL);
		if (source_is_selected (selector, E_SOURCE (data)))
			g_object_set (renderer, "active", TRUE, NULL);
		else
			g_object_set (renderer, "active", FALSE, NULL);
	}

	g_object_unref (data);
}

static void
text_cell_data_func (GtkTreeViewColumn *column,
		     GtkCellRenderer *renderer,
		     GtkTreeModel *model,
		     GtkTreeIter *iter,
		     ESourceSelector *selector)
{
	gpointer data;

	gtk_tree_model_get (model, iter, 0, &data, -1);
	if (data == NULL) {
		g_object_set (renderer, "visible", FALSE, NULL);
		return;
	}

	if (E_IS_SOURCE_GROUP (data)) {
		g_object_set (renderer,
			      "text", e_source_group_peek_name (E_SOURCE_GROUP (data)),
			      "weight", PANGO_WEIGHT_BOLD,
			      "foreground_set", FALSE,
			      NULL);
	} else {
		ESource *source;

		g_assert (E_IS_SOURCE (data));
		source = E_SOURCE (data);

		g_object_set (renderer,
			      "text", e_source_peek_name (source),
			      "weight", PANGO_WEIGHT_NORMAL,
			      "foreground_set", FALSE,
			      NULL);
	}

	g_object_unref (data);
}

static void
pixbuf_cell_data_func (GtkTreeViewColumn *column,
		       GtkCellRenderer *renderer,
		       GtkTreeModel *model,
		       GtkTreeIter *iter,
		       ESourceSelector *selector)
{
	gpointer data;

	gtk_tree_model_get (model, iter, 0, &data, -1);
	g_return_if_fail (G_IS_OBJECT (data));

	if (E_IS_SOURCE_GROUP (data)) {
		g_object_set (renderer, "visible", FALSE, NULL);

	} else if (E_IS_SOURCE (data)) {
		ESource *source;
		GdkPixbuf *pixbuf = NULL;
		const gchar *color_spec;
		GdkColor color;

		source = E_SOURCE (data);
		color_spec = e_source_peek_color_spec (source);
		if (color_spec != NULL && gdk_color_parse (color_spec, &color)) {
			guint32 rgba;

			pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);

			rgba = (((color.red & 0xff00) << 8) |
				((color.green & 0xff00)) |
				((color.blue & 0xff00) >> 8)) << 8;

			gdk_pixbuf_fill (pixbuf, rgba);
		}

		g_object_set (renderer,
			      "visible", pixbuf != NULL,
			      "pixbuf", pixbuf,
			      NULL);

		if (pixbuf)
			g_object_unref (pixbuf);
	}

	g_object_unref (data);
}

/* Custom selection function to make groups non selectable.  */
static gboolean
selection_func (GtkTreeSelection *selection,
		GtkTreeModel *model,
		GtkTreePath *path,
		gboolean path_currently_selected,
		ESourceSelector *selector)
{
	GtkTreeIter iter;
	gpointer data;

	if (selector->priv->toggled_last) {
		selector->priv->toggled_last = FALSE;

		return FALSE;
	}

	if (path_currently_selected)
		return TRUE;

	if (! gtk_tree_model_get_iter (model, &iter, path))
		return FALSE;

	gtk_tree_model_get (model, &iter, 0, &data, -1);
	if (E_IS_SOURCE_GROUP (data)) {
		g_object_unref (data);

		return FALSE;
	}

	clear_saved_primary_selection (selector);
	g_object_unref (data);

	return TRUE;
}

/* Callbacks.  */

static void
text_cell_edited_cb (ESourceSelector *selector,
                     const gchar *path_string,
                     const gchar *new_name)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	ESource *source;

	tree_view = GTK_TREE_VIEW (selector);
	model = gtk_tree_view_get_model (tree_view);
	path = gtk_tree_path_new_from_string (path_string);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, 0, &source, -1);
	gtk_tree_path_free (path);

	g_return_if_fail (E_IS_SOURCE (source));

	if (new_name != NULL && *new_name != '\0')
		e_source_set_name (source, new_name);
}

static void
cell_toggled_callback (GtkCellRendererToggle *renderer,
		       const gchar *path_string,
		       ESourceSelector *selector)
{
	GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter iter;
	ESource *source;
	gpointer data;

	if (! gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_path_free (path);
		return;
	}

	gtk_tree_model_get (model, &iter, 0, &data, -1);
	if (!E_IS_SOURCE_GROUP (data)) {
		source = E_SOURCE (data);

		if (source_is_selected (selector, source))
			unselect_source (selector, source);
		else
			select_source (selector, source);

		selector->priv->toggled_last = TRUE;

		gtk_tree_model_row_changed (model, path, &iter);
		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);
	}

	gtk_tree_path_free (path);

	g_object_unref (data);
}

static void
selection_changed_callback (GtkTreeSelection *selection,
			    ESourceSelector *selector)
{
	g_signal_emit (selector, signals[PRIMARY_SELECTION_CHANGED], 0);
}

static gboolean
test_collapse_row_callback (GtkTreeView *treeview, GtkTreeIter *iter, GtkTreePath *path, gpointer data)
{
	ESourceSelector *selector = data;
	ESourceSelectorPrivate *priv;
	GtkTreeIter child_iter;

	priv = selector->priv;

	/* Clear this because something else has been clicked on now */
	priv->toggled_last = FALSE;

	if (priv->saved_primary_selection)
		return FALSE;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (selector)), NULL, &child_iter))
		return FALSE;

	if (gtk_tree_store_is_ancestor (priv->tree_store, iter, &child_iter)) {
		GtkTreePath *child_path;

		child_path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->tree_store), &child_iter);
		priv->saved_primary_selection = gtk_tree_row_reference_new (GTK_TREE_MODEL (priv->tree_store), child_path);
		gtk_tree_path_free (child_path);
	}

	return FALSE;
}

static gboolean
row_expanded_callback (GtkTreeView *treeview, GtkTreeIter *iter, GtkTreePath *path, gpointer data)
{
	ESourceSelector *selector = data;
	ESourceSelectorPrivate *priv;
	GtkTreePath *child_path;
	GtkTreeIter child_iter;

	priv = selector->priv;

	if (!priv->saved_primary_selection)
		return FALSE;

	child_path = gtk_tree_row_reference_get_path (priv->saved_primary_selection);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->tree_store), &child_iter, child_path);

	if (gtk_tree_store_is_ancestor (priv->tree_store, iter, &child_iter)) {
		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector));

		gtk_tree_selection_select_iter (selection, &child_iter);
		clear_saved_primary_selection (selector);
	}

	gtk_tree_path_free (child_path);

	return FALSE;
}

static gboolean
selector_button_press_event (GtkWidget *widget, GdkEventButton *event, ESourceSelector *selector)
{
	ESourceSelectorPrivate *priv = selector->priv;
	GtkTreePath *path;
	ESource *source = NULL;
	gboolean res = FALSE;

	priv->toggled_last = FALSE;

	/* only process right-clicks */
	if (event->button != 3 || event->type != GDK_BUTTON_PRESS)
		return FALSE;

	/* Get the source/group */
	if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), event->x, event->y, &path, NULL, NULL, NULL)) {
		GtkTreeIter iter;
		gpointer data;

		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->tree_store), &iter, path)) {
			gtk_tree_model_get (GTK_TREE_MODEL (priv->tree_store), &iter, 0, &data, -1);

			/* Do not emit popup since we will not be able to get the source */
			if (E_IS_SOURCE_GROUP (data)) {
				/* do i need to ref it here */
				ESourceGroup *group;

				group = E_SOURCE_GROUP (data);g_object_ref (group);
				priv->primary_source_group = group;
				/* data shuld be unreffed after creating the
				 * new source*/
				return res;
			}

			source = E_SOURCE (data);
		}
	}

	if (source)
		e_source_selector_set_primary_selection (selector, source);

	g_signal_emit(selector, signals[POPUP_EVENT], 0, source, event, &res);

	if (source)
		g_object_unref (source);

	return res;
}

/* GObject methods.  */

static void
source_selector_set_source_list (ESourceSelector *selector,
                                 ESourceList *source_list)
{
	g_return_if_fail (E_IS_SOURCE_LIST (source_list));
	g_return_if_fail (selector->priv->list == NULL);

	selector->priv->list = g_object_ref (source_list);

	rebuild_model (selector);

	g_signal_connect_object (
		source_list, "changed",
		G_CALLBACK (list_changed_callback),
		G_OBJECT (selector), 0);

	gtk_tree_view_expand_all (GTK_TREE_VIEW (selector));
}

static void
source_selector_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			source_selector_set_source_list (
				E_SOURCE_SELECTOR (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_selector_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (
				value, e_source_selector_get_source_list (
				E_SOURCE_SELECTOR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_selector_dispose (GObject *object)
{
	ESourceSelectorPrivate *priv = E_SOURCE_SELECTOR (object)->priv;

	g_hash_table_remove_all (priv->selected_sources);

	if (priv->rebuild_model_idle_id != 0) {
		g_source_remove (priv->rebuild_model_idle_id);
		priv->rebuild_model_idle_id = 0;
	}

	if (priv->list != NULL) {
		g_object_unref (priv->list);
		priv->list = NULL;
	}

	if (priv->tree_store != NULL) {
		g_object_unref (priv->tree_store);
		priv->tree_store = NULL;
	}

	clear_saved_primary_selection (E_SOURCE_SELECTOR (object));

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_selector_parent_class)->dispose (object);
}

static void
source_selector_finalize (GObject *object)
{
	ESourceSelectorPrivate *priv = E_SOURCE_SELECTOR (object)->priv;

	g_hash_table_destroy (priv->selected_sources);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_selector_parent_class)->finalize (object);
}

static void
source_selector_drag_leave (GtkWidget *widget,
                            GdkDragContext *context,
                            guint time_)
{
	GtkTreeView *tree_view;
	GtkTreeViewDropPosition pos;

	tree_view = GTK_TREE_VIEW (widget);
	pos = GTK_TREE_VIEW_DROP_BEFORE;

	gtk_tree_view_set_drag_dest_row (tree_view, NULL, pos);
}

static gboolean
source_selector_drag_motion (GtkWidget *widget,
                             GdkDragContext *context,
                             gint x,
                             gint y,
                             guint time_)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	GtkTreeViewDropPosition pos;
	GdkDragAction action = 0;
	gpointer object = NULL;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	if (!gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, NULL))
		goto exit;

	if (!gtk_tree_model_get_iter (model, &iter, path))
		goto exit;

	gtk_tree_model_get (model, &iter, 0, &object, -1);

	if (E_IS_SOURCE_GROUP (object) || e_source_get_readonly (object))
		goto exit;

	pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
	gtk_tree_view_set_drag_dest_row (tree_view, path, pos);

	if (context->actions & GDK_ACTION_MOVE)
		action = GDK_ACTION_MOVE;
	else
		action = context->suggested_action;

exit:
	if (path != NULL)
		gtk_tree_path_free (path);

	if (object != NULL)
		g_object_unref (object);

	gdk_drag_status (context, action, time_);

	return TRUE;
}

static gboolean
source_selector_drag_drop (GtkWidget *widget,
                           GdkDragContext *context,
                           gint x,
                           gint y,
                           guint time_)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	gboolean drop_zone;
	gboolean valid;
	gpointer object;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	if (!gtk_tree_view_get_path_at_pos (
		tree_view, x, y, &path, NULL, NULL, NULL))
		return FALSE;

	valid = gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);
	g_return_val_if_fail (valid, FALSE);

	gtk_tree_model_get (model, &iter, 0, &object, -1);
	drop_zone = E_IS_SOURCE (object);
	g_object_unref (object);

	return drop_zone;
}

static void
source_selector_drag_data_received (GtkWidget *widget,
                                    GdkDragContext *context,
                                    gint x,
                                    gint y,
                                    GtkSelectionData *selection_data,
                                    guint info,
                                    guint time_)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	gpointer object = NULL;
	gboolean delete;
	gboolean success = FALSE;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);
	delete = (context->action == GDK_ACTION_MOVE);

	if (!gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, NULL))
		goto exit;

	if (!gtk_tree_model_get_iter (model, &iter, path))
		goto exit;

	gtk_tree_model_get (model, &iter, 0, &object, -1);

	if (!E_IS_SOURCE (object) || e_source_get_readonly (object))
		goto exit;

	g_signal_emit (
		widget, signals[DATA_DROPPED], 0, selection_data,
		object, context->action, info, &success);

exit:
	if (path != NULL)
		gtk_tree_path_free (path);

	if (object != NULL)
		g_object_unref (object);

	gtk_drag_finish (context, success, delete, time_);
}

static gboolean
source_selector_popup_menu (GtkWidget *widget)
{
	ESourceSelector *selector;
	ESource *source;
	gboolean res = FALSE;

	selector = E_SOURCE_SELECTOR (widget);
	source = e_source_selector_peek_primary_selection (selector);
	g_signal_emit (selector, signals[POPUP_EVENT], 0, source, NULL, &res);

	return res;
}

/* Initialization.  */
static gboolean
ess_bool_accumulator(GSignalInvocationHint *ihint, GValue *out, const GValue *in, gpointer data)
{
	gboolean val = g_value_get_boolean(in);

	g_value_set_boolean(out, val);

	return !val;
}

static void
e_source_selector_class_init (ESourceSelectorClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	g_type_class_add_private (class, sizeof (ESourceSelectorPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_selector_set_property;
	object_class->get_property = source_selector_get_property;
	object_class->dispose  = source_selector_dispose;
	object_class->finalize = source_selector_finalize;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->drag_leave = source_selector_drag_leave;
	widget_class->drag_motion = source_selector_drag_motion;
	widget_class->drag_drop = source_selector_drag_drop;
	widget_class->drag_data_received = source_selector_drag_data_received;
	widget_class->popup_menu = source_selector_popup_menu;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			NULL,
			NULL,
			E_TYPE_SOURCE_LIST,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	signals[SELECTION_CHANGED] =
		g_signal_new ("selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, selection_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[PRIMARY_SELECTION_CHANGED] =
		g_signal_new ("primary_selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, primary_selection_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[POPUP_EVENT] =
		g_signal_new ("popup_event",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, popup_event),
			      ess_bool_accumulator, NULL,
			      e_data_server_ui_marshal_BOOLEAN__OBJECT_BOXED,
			      G_TYPE_BOOLEAN, 2, G_TYPE_OBJECT,
			      GDK_TYPE_EVENT|G_SIGNAL_TYPE_STATIC_SCOPE);
	signals[DATA_DROPPED] =
		g_signal_new ("data_dropped",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, data_dropped),
			      NULL, NULL,
			      e_data_server_ui_marshal_BOOLEAN__BOXED_OBJECT_FLAGS_UINT,
			      G_TYPE_BOOLEAN, 4,
			      GTK_TYPE_SELECTION_DATA | G_SIGNAL_TYPE_STATIC_SCOPE,
			      E_TYPE_SOURCE,
			      GDK_TYPE_DRAG_ACTION,
			      G_TYPE_UINT);
}

static gboolean
group_search_function   (GtkTreeModel *model,
			 gint column,
			 const gchar *key,
			 GtkTreeIter *iter,
			 gpointer dummy)
{
	gpointer data;
	const gchar *name = NULL;
	gboolean status = TRUE;

	gtk_tree_model_get (model, iter, 0, &data, -1);

	if (E_IS_SOURCE_GROUP (data))
		name = e_source_group_peek_name (E_SOURCE_GROUP (data));
	else {
		g_assert (E_IS_SOURCE (data));

		name = e_source_peek_name (E_SOURCE (data));
	}

	if (name)
		status = g_ascii_strncasecmp (name, key, strlen(key)) != 0;

	g_object_unref (data);

	return status;
}

static void
e_source_selector_init (ESourceSelector *selector)
{
	ESourceSelectorPrivate *priv;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell_renderer;
	GtkTreeSelection *selection;
	GtkTreeView *tree_view;

	selector->priv = E_SOURCE_SELECTOR_GET_PRIVATE (selector);
	priv = selector->priv;

	tree_view = GTK_TREE_VIEW (selector);

	gtk_tree_view_set_search_column (tree_view, 0);
	gtk_tree_view_set_search_equal_func (tree_view, group_search_function, NULL, NULL);
	gtk_tree_view_set_enable_search (tree_view, TRUE);

	g_signal_connect (G_OBJECT (selector), "button_press_event",
			  G_CALLBACK (selector_button_press_event), selector);

	priv->toggled_last = FALSE;
	priv->checkboxes_shown = TRUE;
	priv->select_new = FALSE;

	priv->selected_sources = create_selected_sources_hash ();

	priv->tree_store = gtk_tree_store_new (1, G_TYPE_OBJECT);
	gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (priv->tree_store));

	column = gtk_tree_view_column_new ();
	gtk_tree_view_append_column (tree_view, column);

	cell_renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (G_OBJECT (cell_renderer), "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);
	gtk_tree_view_column_pack_start (column, cell_renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer, (GtkTreeCellDataFunc) pixbuf_cell_data_func, selector, NULL);
	cell_renderer = e_cell_renderer_safe_toggle_new ();
	gtk_tree_view_column_pack_start (column, cell_renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer, (GtkTreeCellDataFunc) toggle_cell_data_func, selector, NULL);
	g_signal_connect (cell_renderer, "toggled", G_CALLBACK (cell_toggled_callback), selector);

	cell_renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (cell_renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	g_signal_connect_swapped (
		cell_renderer, "edited",
		G_CALLBACK (text_cell_edited_cb), selector);
	gtk_tree_view_column_pack_start (column, cell_renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer, (GtkTreeCellDataFunc) text_cell_data_func, selector, NULL);

	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_select_function (selection, (GtkTreeSelectionFunc) selection_func, selector, NULL);
	g_signal_connect_object (selection, "changed", G_CALLBACK (selection_changed_callback), G_OBJECT (selector), 0);

	gtk_tree_view_set_headers_visible (tree_view, FALSE);

	g_signal_connect (G_OBJECT (selector), "test-collapse-row", G_CALLBACK (test_collapse_row_callback), selector);
	g_signal_connect (G_OBJECT (selector), "row-expanded", G_CALLBACK (row_expanded_callback), selector);
}

/* Public API.  */

/**
 * e_source_selector_new:
 * @list: A source list.
 *
 * Create a new view for @list.  The view will update automatically when @list
 * changes.
 *
 * Return value: The newly created widget.
 **/
GtkWidget *
e_source_selector_new (ESourceList *list)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);

	return g_object_new (
		E_TYPE_SOURCE_SELECTOR, "source-list", list, NULL);
}

/**
 * e_source_selector_get_source_list:
 * @selector: an #ESourceSelector
 *
 * Returns the #ESourceList that @selector is rendering.
 *
 * Returns: an #ESourceList
 **/
ESourceList *
e_source_selector_get_source_list (ESourceSelector *selector)
{
	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), NULL);

	return selector->priv->list;
}

/**
 * e_source_selector_get_selection:
 * @selector: an #ESourceSelector
 *
 * Get the list of selected sources, i.e. those that were enabled through the
 * corresponding checkboxes in the tree.
 *
 * Return value: A list of the ESources currently selected.  The sources will
 * be in the same order as they appear on the screen, and the list should be
 * freed using e_source_selector_free_selection().
 **/
GSList *
e_source_selector_get_selection (ESourceSelector *selector)
{
	GSList *selection_list;
	GSList *groups;
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), NULL);

	selection_list = NULL;

	groups = e_source_list_peek_groups (selector->priv->list);
	for (p = groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GSList *sources;
		GSList *q;

		sources = e_source_group_peek_sources (group);
		for (q = sources; q != NULL; q = q->next) {
			ESource *source = E_SOURCE (q->data);

			if (source_is_selected (selector, source)) {
				selection_list = g_slist_prepend (selection_list, source);
				g_object_ref (source);
			}
		}
	}

	return g_slist_reverse (selection_list);
}

/**
 * e_source_selector_get_primary_source_group:
 * @selector: an #ESourceSelector
 *
 * Gets the primary source group associated with the selector.
 *
 * Return value: primary_source_group if selector is valid, NULL otherwise.
 **/
ESourceGroup *
e_source_selector_get_primary_source_group (ESourceSelector *selector)
{
	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), NULL);

	return selector->priv->primary_source_group;

}

/**
 * e_source_list_free_selection:
 * @list: A selection list returned by e_source_selector_get_selection().
 *
 * Free the selection list.
 **/
void
e_source_selector_free_selection (GSList *list)
{
	g_slist_foreach (list, (GFunc) g_object_unref, NULL);
	g_slist_free (list);
}

/**
 * e_source_selector_show_selection:
 * @selector: An ESourceSelector widget
 *
 * Specify whether the checkboxes in the ESourceSelector should be shown or
 * not.
 **/
void
e_source_selector_show_selection (ESourceSelector *selector,
				  gboolean show)
{
	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));

	show = !! show;
	if (show == selector->priv->checkboxes_shown)
		return;

	selector->priv->checkboxes_shown = show;

	gtk_tree_model_foreach (GTK_TREE_MODEL (selector->priv->tree_store),
				(GtkTreeModelForeachFunc) gtk_tree_model_row_changed,
				NULL);
}

/**
 * e_source_selector_selection_shown:
 * @selector: an #ESourceSelector
 *
 * Check whether the checkboxes in the ESourceSelector are being shown or not.
 *
 * Return value: %TRUE if the checkboxes are shown, %FALSE otherwise.
 **/
gboolean
e_source_selector_selection_shown (ESourceSelector *selector)
{
	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), FALSE);

	return selector->priv->checkboxes_shown;
}

/**
 * e_source_selector_set_select_new:
 * @selector: An #ESourceSelector widget
 * @state: A gboolean
 *
 * Set whether or not to select new sources added to @selector.
 **/
void
e_source_selector_set_select_new (ESourceSelector *selector, gboolean state)
{
	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));

	selector->priv->select_new = state;
}

/**
 * e_source_selector_select_source:
 * @selector: An #ESourceSelector widget
 * @source: An #ESource.
 *
 * Select @source in @selector.
 **/
void
e_source_selector_select_source (ESourceSelector *selector,
				 ESource *source)
{
	GtkTreeIter parent_iter, source_iter;

	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));
	g_return_if_fail (E_IS_SOURCE (source));

	source = find_source (selector, source);

	if (!source || source_is_selected (selector, source))
		return;

	select_source (selector, source);

	if (find_source_iter (selector, source, &parent_iter, &source_iter)) {
		GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
		GtkTreePath *path;

		path = gtk_tree_model_get_path (model, &source_iter);
		gtk_tree_model_row_changed (model, path, &source_iter);
		gtk_tree_path_free (path);

		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);
	}
}

/**
 * e_source_selector_unselect_source:
 * @selector: An #ESourceSelector widget
 * @source: An #ESource.
 *
 * Unselect @source in @selector.
 **/
void
e_source_selector_unselect_source (ESourceSelector *selector,
				   ESource *source)
{
	GtkTreeIter parent_iter, source_iter;

	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));
	g_return_if_fail (E_IS_SOURCE (source));

	source = find_source (selector, source);

	if (!source || !source_is_selected (selector, source))
		return;

	unselect_source (selector, source);

	if (find_source_iter (selector, source, &parent_iter, &source_iter)) {
		GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
		GtkTreePath *path;

		path = gtk_tree_model_get_path (model, &source_iter);
		gtk_tree_model_row_changed (model, path, &source_iter);
		gtk_tree_path_free (path);

		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);
	}
}

/**
 * e_source_selector_source_is_selected:
 * @selector: An #ESourceSelector widget
 * @source: An #ESource.
 *
 * Check whether @source is selected in @selector.
 *
 * Return value: %TRUE if @source is currently selected, %FALSE otherwise.
 **/
gboolean
e_source_selector_source_is_selected (ESourceSelector *selector,
				      ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	source = find_source (selector, source);

	return source && source_is_selected (selector, source);
}

/**
 * e_source_selector_edit_primary_selection:
 * @selector: An #ESourceSelector widget
 *
 * Allows the user to rename the primary selected source by opening an
 * entry box directly in @selector.
 **/
void
e_source_selector_edit_primary_selection (ESourceSelector *selector)
{
	GtkTreeRowReference *reference;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	GList *list;

	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));

	tree_view = GTK_TREE_VIEW (selector);
	column = gtk_tree_view_get_column (tree_view, 0);
	reference = selector->priv->saved_primary_selection;
	selection = gtk_tree_view_get_selection (tree_view);

	if (reference != NULL)
		path = gtk_tree_row_reference_get_path (reference);
	else if (gtk_tree_selection_get_selected (selection, &model, &iter))
		path = gtk_tree_model_get_path (model, &iter);

	if (path == NULL)
		return;

	/* XXX Because we stuff three renderers in a single column,
	 *     we have to manually hunt for the text renderer. */
	renderer = NULL;
	list = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
	while (list != NULL) {
		renderer = list->data;
		if (GTK_IS_CELL_RENDERER_TEXT (renderer))
			break;
		list = g_list_delete_link (list, list);
	}
	g_list_free (list);

	/* Make the text cell renderer editable, but only temporarily.
	 * We don't want editing to be activated by simply clicking on
	 * the source name.  Too easy for accidental edits to occur. */
	g_object_set (renderer, "editable", TRUE, NULL);
	gtk_tree_view_expand_to_path (tree_view, path);
	gtk_tree_view_set_cursor_on_cell (
		tree_view, path, column, renderer, TRUE);
	g_object_set (renderer, "editable", FALSE, NULL);

	gtk_tree_path_free (path);
}

/**
 * e_source_selector_peek_primary_selection:
 * @selector: An #ESourceSelector widget
 *
 * Get the primary selected source.  The primary selection is the one that is
 * highlighted through the normal #GtkTreeView selection mechanism (as opposed
 * to the "normal" selection, which is the set of source whose checkboxes are
 * checked).
 *
 * Return value: The selected source.
 **/
ESource *
e_source_selector_peek_primary_selection (ESourceSelector *selector)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean have_iter = FALSE;
	gpointer data = NULL;

	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), NULL);

	model = GTK_TREE_MODEL (selector->priv->tree_store);

	if (selector->priv->saved_primary_selection) {
		GtkTreePath *child_path;

		child_path = gtk_tree_row_reference_get_path (selector->priv->saved_primary_selection);
		if (child_path) {
			if (gtk_tree_model_get_iter (GTK_TREE_MODEL (selector->priv->tree_store), &iter, child_path))
				have_iter = TRUE;
			gtk_tree_path_free (child_path);
		}
	}

	if (!have_iter && ! gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (selector)), NULL, &iter))
		return NULL;

	gtk_tree_model_get (model, &iter, 0, &data, -1);
	if (!data)
		return NULL;

	if (! E_IS_SOURCE (data)) {
		g_object_unref (data);

		return NULL;
	}

	g_object_unref (data);

	return E_SOURCE (data);
}

/**
 * e_source_selector_set_primary_selection:
 * @selector: an #ESourceSelector widget
 * @source: an #ESource to select
 *
 * Set the primary selected source.
 **/
void
e_source_selector_set_primary_selection (ESourceSelector *selector, ESource *source)
{
	ESourceSelectorPrivate *priv;
	GtkTreeIter parent_iter, source_iter;

	g_return_if_fail (selector != NULL);
	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));
	g_return_if_fail (source != NULL);
	g_return_if_fail (E_IS_SOURCE (source));

	priv = selector->priv;
	source = find_source (selector, source);

	if (!source)
		return;

	if (find_source_iter (selector, source, &parent_iter, &source_iter)) {
		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector));
		GtkTreePath *path;

		/* We block the signal because this all needs to be atomic */
		g_signal_handlers_block_matched (selection, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, selection_changed_callback, NULL);
		gtk_tree_selection_unselect_all (selection);
		g_signal_handlers_unblock_matched (selection, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, selection_changed_callback, NULL);

		clear_saved_primary_selection (selector);

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->tree_store), &parent_iter);

		if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (selector), path)) {
			gtk_tree_selection_select_iter (selection, &source_iter);
		} else {
			GtkTreePath *child_path;

			child_path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->tree_store), &source_iter);
			priv->saved_primary_selection = gtk_tree_row_reference_new (GTK_TREE_MODEL (priv->tree_store), child_path);
			gtk_tree_path_free (child_path);

			/* We do this by hand because we aren't changing the tree selection */
			if (!source_is_selected (selector, source)) {
				select_source (selector, source);
				gtk_tree_model_row_changed (GTK_TREE_MODEL (priv->tree_store), path, &source_iter);
				g_signal_emit (selector, signals[SELECTION_CHANGED], 0);
			}

			g_signal_emit (selector, signals[PRIMARY_SELECTION_CHANGED], 0);
		}

		gtk_tree_path_free (path);
	} else {
		g_warning (G_STRLOC ": Cannot find source %p (%s) in selector %p",
			   (gpointer) source, e_source_peek_name (source),
			   (gpointer) selector);
	}
}

