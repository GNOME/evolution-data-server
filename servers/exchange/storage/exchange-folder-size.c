/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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
 */

/* ExchangeFolderSize: Display the folder tree with the folder sizes */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <gtk/gtk.h>

#include "exchange-hierarchy-webdav.h"
#include "e-folder-exchange.h"
#include "exchange-folder-size.h"

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (ExchangeFolderSize, exchange_folder_size, G_TYPE_OBJECT)

typedef struct {
        gchar *folder_name;
        gdouble folder_size;
} folder_info;

struct _ExchangeFolderSizePrivate {

	GHashTable *table;
	GtkListStore *model;
	GHashTable *row_refs;
};

enum {
        COLUMN_NAME,
        COLUMN_SIZE,
        NUM_COLUMNS
};

static gboolean
free_fgsizeable (gpointer key, gpointer value, gpointer data)
{
	folder_info *f_info = (folder_info *) value;

	g_free (key);
	g_free (f_info->folder_name);
	g_free (f_info);
	return TRUE;
}

static gboolean
free_row_refs (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	gtk_tree_row_reference_free (value);
	return TRUE;
}

static void
finalize (GObject *object)
{
	ExchangeFolderSize *fsize = EXCHANGE_FOLDER_SIZE (object);

	g_hash_table_foreach_remove (fsize->priv->table, free_fgsizeable, NULL);
	g_hash_table_destroy (fsize->priv->table);
	g_hash_table_foreach_remove (fsize->priv->row_refs, free_row_refs, NULL);
	g_hash_table_destroy (fsize->priv->row_refs);
	if (fsize->priv->model)
		g_object_unref (fsize->priv->model);
	g_free (fsize->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
exchange_folder_size_class_init (ExchangeFolderSizeClass *class)
{
	GObjectClass *object_class;
	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class = G_OBJECT_CLASS (class);

	/* override virtual methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

}

static void
exchange_folder_size_init (ExchangeFolderSize *fsize)
{
	fsize->priv = g_new0 (ExchangeFolderSizePrivate, 1);
	fsize->priv->table = g_hash_table_new (g_str_hash, g_str_equal);
        fsize->priv->model = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_DOUBLE);
	fsize->priv->row_refs = g_hash_table_new (g_str_hash, g_str_equal);
}

/**
 * exchange_folder_size_new:
 *
 * Return value: a foldersize object with the table initialized
 **/
ExchangeFolderSize *
exchange_folder_size_new (void)
{
	ExchangeFolderSize *fsize;

	fsize = g_object_new (EXCHANGE_TYPE_FOLDER_SIZE, NULL);

	return fsize;
}

void
exchange_folder_size_update (ExchangeFolderSize *fsize,
				const gchar *folder_name,
				gdouble folder_size)
{
	folder_info *f_info, *cached_info;
	ExchangeFolderSizePrivate *priv;
	GHashTable *folder_gsizeable;
	GtkTreeRowReference *row;
	GtkTreeIter iter;
	GtkTreePath *path;

	g_return_if_fail (EXCHANGE_IS_FOLDER_SIZE (fsize));

	priv = fsize->priv;
	folder_gsizeable = priv->table;

	cached_info = g_hash_table_lookup (folder_gsizeable, folder_name);
	if (cached_info) {
		if (cached_info->folder_size == folder_size) {
			return;
		} else {
			cached_info->folder_size = folder_size;
			row = g_hash_table_lookup (priv->row_refs, folder_name);
			path = gtk_tree_row_reference_get_path (row);
			if (gtk_tree_model_get_iter (GTK_TREE_MODEL (fsize->priv->model), &iter, path)) {
				gtk_list_store_set (fsize->priv->model, &iter,
						      COLUMN_NAME, cached_info->folder_name,
						      COLUMN_SIZE, cached_info->folder_size,
						      -1);
			}
			gtk_tree_path_free (path);
			return;
		}
	} else {
		f_info = g_new0(folder_info, 1);
		f_info->folder_name = g_strdup (folder_name);
		f_info->folder_size = folder_size;
		g_hash_table_insert (folder_gsizeable, f_info->folder_name, f_info);

		gtk_list_store_append (fsize->priv->model, &iter);
		gtk_list_store_set (fsize->priv->model, &iter,
				      COLUMN_NAME, f_info->folder_name,
				      COLUMN_SIZE, f_info->folder_size,
				      -1);

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (fsize->priv->model), &iter);
		row = gtk_tree_row_reference_new (GTK_TREE_MODEL (fsize->priv->model), path);
		gtk_tree_path_free (path);

		g_hash_table_insert (fsize->priv->row_refs, g_strdup (folder_name), row);
	}
}

void
exchange_folder_size_remove (ExchangeFolderSize *fsize,
				const gchar *folder_name)
{
	ExchangeFolderSizePrivate *priv;
	GHashTable *folder_gsizeable;
	folder_info *cached_info;
	GtkTreeRowReference *row;
	GtkTreeIter iter;
	GtkTreePath *path;

	g_return_if_fail (EXCHANGE_IS_FOLDER_SIZE (fsize));
	g_return_if_fail (folder_name != NULL);

	priv = fsize->priv;
	folder_gsizeable = priv->table;

	cached_info = g_hash_table_lookup (folder_gsizeable, folder_name);
	if (cached_info)  {
		row = g_hash_table_lookup (priv->row_refs, folder_name);
		path = gtk_tree_row_reference_get_path (row);
		g_hash_table_remove (folder_gsizeable, folder_name);
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (fsize->priv->model), &iter, path)) {
			gtk_list_store_remove (fsize->priv->model, &iter);
		}
		g_hash_table_remove (priv->row_refs, row);
		gtk_tree_path_free (path);
	}
}

gdouble
exchange_folder_size_get (ExchangeFolderSize *fsize,
			  const gchar *folder_name)
{
	ExchangeFolderSizePrivate *priv;
	GHashTable *folder_gsizeable;
	folder_info *cached_info;

	g_return_val_if_fail (EXCHANGE_IS_FOLDER_SIZE (fsize), -1);

	priv = fsize->priv;
	folder_gsizeable = priv->table;

	cached_info = g_hash_table_lookup (folder_gsizeable, folder_name);
	if (cached_info)  {
		return cached_info->folder_size;
	}
	return -1;
}

GtkListStore *
exchange_folder_size_get_model (ExchangeFolderSize *fsize)
{
        ExchangeFolderSizePrivate *priv;

	priv = fsize->priv;

	if (!g_hash_table_size (priv->table))
		return NULL;

	return priv->model;
}
