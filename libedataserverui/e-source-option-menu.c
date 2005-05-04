/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-option-menu.c
 *
 * Copyright (C) 2003  Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>

#include "e-data-server-ui-marshal.h"
#include "e-source-option-menu.h"

/* We set data on each menu item specifying the corresponding ESource using this key.  */
#define MENU_ITEM_SOURCE_DATA_ID	"ESourceOptionMenu:Source"


struct _ESourceOptionMenuPrivate {
	ESourceList *source_list;

	ESource *selected_source;
};

typedef struct {
	ESourceOptionMenu *option_menu;
	ESource *source;
	ESource *found_source;
	int i;
} ForeachMenuItemData;

enum {
	SOURCE_SELECTED,
	NUM_SIGNALS
};

static uint signals[NUM_SIGNALS] = { 0 };

G_DEFINE_TYPE (ESourceOptionMenu, e_source_option_menu, GTK_TYPE_OPTION_MENU)

/* Selecting a source.  */
static void
select_source_foreach_menu_item (GtkWidget *menu_item,
				 ForeachMenuItemData *data)
{
	ESource *source = gtk_object_get_data (GTK_OBJECT (menu_item), MENU_ITEM_SOURCE_DATA_ID);

	if (data->found_source)
		return;

	if (source && e_source_equal (source, data->source)) {
		data->found_source = source;
		gtk_option_menu_set_history (GTK_OPTION_MENU (data->option_menu), data->i);
	}

	data->i ++;
}

static void
select_source (ESourceOptionMenu *menu,
	       ESource *source)
{
	ForeachMenuItemData *foreach_data;

	foreach_data = g_new0 (ForeachMenuItemData, 1);
	foreach_data->option_menu = menu;
	foreach_data->source = source;

	gtk_container_foreach (GTK_CONTAINER (GTK_OPTION_MENU (menu)->menu),
			       (GtkCallback) select_source_foreach_menu_item, foreach_data);

	if (foreach_data->found_source) {
		menu->priv->selected_source = foreach_data->found_source;
		g_signal_emit (menu, signals[SOURCE_SELECTED], 0, foreach_data->found_source);
	}

	g_free (foreach_data);
}


/* Menu callback.  */

static void
menu_item_activate_callback (GtkMenuItem *menu_item,
			     ESourceOptionMenu *option_menu)
{
	ESource *source = gtk_object_get_data (GTK_OBJECT (menu_item), MENU_ITEM_SOURCE_DATA_ID);

	if (source != NULL)
		select_source (option_menu, source);
}


/* Functions to keep the menu in sync with the ESourceList.  */

static void
populate (ESourceOptionMenu *option_menu)
{
	GtkWidget *menu = gtk_menu_new ();
	GSList *groups = e_source_list_peek_groups (option_menu->priv->source_list);
	GSList *p;
	ESource *first_source = NULL;
	int first_source_item = -1;
	int selected_item = -1;
	int i;

	i = 0;
	for (p = groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GtkWidget *item = gtk_menu_item_new_with_label (e_source_group_peek_name (group));
		GSList *q;

		gtk_widget_set_sensitive (item, FALSE);
		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);

		i ++;

		for (q = e_source_group_peek_sources (group); q != NULL; q = q->next) {
			ESource *source = E_SOURCE (q->data);
			char *label = g_strconcat ("    ", e_source_peek_name (source), NULL);
			GtkWidget *item = gtk_menu_item_new_with_label (label);

			gtk_object_set_data_full (GTK_OBJECT (item), MENU_ITEM_SOURCE_DATA_ID, source,
						  (GtkDestroyNotify) g_object_unref);
			g_object_ref (source);

			g_signal_connect (item, "activate", G_CALLBACK (menu_item_activate_callback), option_menu);

			gtk_widget_show (item);
			gtk_menu_append (GTK_MENU (menu), item);

			if (first_source_item == -1) {
				first_source_item = i;
				first_source = source;
			}

			if (source == option_menu->priv->selected_source)
				selected_item = i;

			i ++;
		}
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);

	if (selected_item != -1) {
		gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), selected_item);
	} else {
		if (option_menu->priv->selected_source != NULL)
			g_object_unref (option_menu->priv->selected_source);
		option_menu->priv->selected_source = first_source;
		if (option_menu->priv->selected_source != NULL)
			g_object_ref (option_menu->priv->selected_source);

		gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), first_source_item);
	}
}


static void
source_list_changed_callback (ESourceList *list,
			      ESourceOptionMenu *menu)
{
	populate (menu);
}

static void
connect_signals (ESourceOptionMenu *menu)
{
	g_signal_connect_object (menu->priv->source_list, "changed",
				 G_CALLBACK (source_list_changed_callback), G_OBJECT (menu), 0);
}


/* GObject methods.  */

static void
e_source_option_menu_dispose (GObject *object)
{
	ESourceOptionMenuPrivate *priv = E_SOURCE_OPTION_MENU (object)->priv;

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	if (priv->selected_source != NULL) {
		g_object_unref (priv->selected_source);
		priv->selected_source = NULL;
	}

	(* G_OBJECT_CLASS (e_source_option_menu_parent_class)->dispose) (object);
}

static void
e_source_option_menu_finalize (GObject *object)
{
	ESourceOptionMenuPrivate *priv = E_SOURCE_OPTION_MENU (object)->priv;

	g_free (priv);

	(* G_OBJECT_CLASS (e_source_option_menu_parent_class)->finalize) (object);
}


/* Initialization.  */

static void
e_source_option_menu_class_init (ESourceOptionMenuClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = e_source_option_menu_dispose;
	object_class->finalize = e_source_option_menu_finalize;

	signals[SOURCE_SELECTED] = 
		g_signal_new ("source_selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceOptionMenuClass, source_selected),
			      NULL, NULL,
			      e_data_server_ui_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
e_source_option_menu_init (ESourceOptionMenu *source_option_menu)
{
	ESourceOptionMenuPrivate *priv;

	priv = g_new0 (ESourceOptionMenuPrivate, 1);

	source_option_menu->priv = priv;
}


/* Public methods.  */

/**
 * e_source_option_menu_new:
 * @source_list: an #ESourceList to choose from
 *
 * Creates a new #ESourceOptionMenu widget that lets the user pick
 * an #ESource from the provided #ESourceList.
 *
 * Return value: A new #ESourceOptionMenu.
 **/
GtkWidget *
e_source_option_menu_new (ESourceList *source_list)
{
	ESourceOptionMenu *menu;

	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	menu = g_object_new (e_source_option_menu_get_type (), NULL);

	menu->priv->source_list = source_list;
	g_object_ref (source_list);

	connect_signals (menu);
	populate (menu);

	return GTK_WIDGET (menu);
}

/**
 * e_source_option_menu_peek_selected:
 * @menu: an #ESourceOptionMenu
 *
 * Return value: The selected #ESource, or %NULL if none was selected.
 **/
ESource *
e_source_option_menu_peek_selected  (ESourceOptionMenu *menu)
{
	g_return_val_if_fail (E_IS_SOURCE_OPTION_MENU (menu), NULL);

	return menu->priv->selected_source;
}

/**
 * e_source_option_menu_select:
 * @menu: an #ESourceOptionMenu
 * @source: an #ESource to select
 *
 * Programmatically selects a source in @menu. @source must be present
 * in @menu's #ESourceList.
 **/
void
e_source_option_menu_select (ESourceOptionMenu *menu,
			     ESource *source)
{
	g_return_if_fail (E_IS_SOURCE_OPTION_MENU (menu));
	g_return_if_fail (E_IS_SOURCE (source));

	select_source (menu, source);
}
