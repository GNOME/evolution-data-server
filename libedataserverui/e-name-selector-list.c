/*
 * Single-line text entry widget for EDestinations.
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
 *
 * Authors:
 *		Srinivasa Ragavan <sragavan@novell.com>
 *		Devashish Sharma  <sdevashish@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <config.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>

#include <libebook/e-book.h>
#include <libebook/e-contact.h>
#include <libebook/e-destination.h>
#include <libedataserverui/e-book-auth-util.h>
#include "libedataserver/e-sexp.h"
#include <libedataserverui/e-data-server-ui-marshal.h>
#include <libedataserverui/e-name-selector-entry.h>
#include "e-name-selector-list.h"

#define MAX_ROW	10

G_DEFINE_TYPE (ENameSelectorList, e_name_selector_list, E_TYPE_NAME_SELECTOR_ENTRY)

static void e_name_selector_list_dispose    (GObject *object);
static void e_name_selector_list_finalize   (GObject *object);

/* Signals */

static void
enl_popup_size (ENameSelectorList *list)
{
	gint height = 0, count;
	GtkTreeViewColumn *column = NULL;

	column = gtk_tree_view_get_column ( GTK_TREE_VIEW (list->tree_view), 0);
	if (column)
		gtk_tree_view_column_cell_get_size (column, NULL, NULL, NULL, NULL, &height);

	/* Show a maximum of 10 rows in the popup list view */
	count = list->rows;
	if (count > MAX_ROW)
		count = MAX_ROW;
	if (count <= 0)
		count = 1;

	gtk_widget_set_size_request (list->tree_view, ((GtkWidget *)list)->allocation.width - 3 , height * count);
}

static void
enl_popup_position (ENameSelectorList *list)
{
	GdkWindow *window;
	gint x,y;

	enl_popup_size (list);
	window = gtk_widget_get_window (GTK_WIDGET (list));
	gdk_window_get_origin (window, &x, &y);
	y = y +((GtkWidget *)list)->allocation.height;

	gtk_window_move (list->popup, x, y);
}

static void
enl_popup_grab (ENameSelectorList *list)
{
	GdkWindow *window;
	gint len;

	window = gtk_widget_get_window (GTK_WIDGET (list->popup));

	gtk_grab_add (GTK_WIDGET (list->popup));

	gdk_pointer_grab (window, TRUE,
			  GDK_BUTTON_PRESS_MASK |
			  GDK_BUTTON_RELEASE_MASK |
			  GDK_POINTER_MOTION_MASK,
			  NULL, NULL, GDK_CURRENT_TIME);

	gdk_keyboard_grab (window, TRUE, GDK_CURRENT_TIME);
	gtk_widget_grab_focus ((GtkWidget *)list);

	/* Build the listview from the model */
	gtk_tree_view_set_model (GTK_TREE_VIEW (list->tree_view), GTK_TREE_MODEL(((ENameSelectorEntry *)list)->destination_store));

	/* If any selection of text is present, unselect it */
	len = strlen(gtk_entry_get_text(GTK_ENTRY(list)));
	gtk_editable_select_region (GTK_EDITABLE(list), len, -1);
}

static void
enl_popup_ungrab (ENameSelectorList *list)
{
	if (!GTK_WIDGET_HAS_GRAB(list->popup))
		return;

	gdk_pointer_ungrab (GDK_CURRENT_TIME);
	gtk_grab_remove ( GTK_WIDGET (list->popup));
	gdk_keyboard_ungrab (GDK_CURRENT_TIME);
}

static gboolean
enl_entry_focus_in (ENameSelectorList *list, GdkEventFocus *event, gpointer dummy)
{
	gint len;

	/* FIXME: Dont select every thing by default- Code is there but still it does */
	len = strlen(gtk_entry_get_text(GTK_ENTRY(list)));
	gtk_editable_select_region (GTK_EDITABLE(list), len, -1);

	return TRUE;
}

static gboolean
enl_entry_focus_out (ENameSelectorList *list, GdkEventFocus *event, gpointer dummy)
{
	/* When we lose focus and popup is still present hide it. Dont do it, when we click the popup. Look for grab */
	if (GTK_WIDGET_VISIBLE (list->popup) && !GTK_WIDGET_HAS_GRAB(list->popup)) {
		enl_popup_ungrab (list);
		gtk_widget_hide ((GtkWidget *)list->popup);

		return FALSE;
	}

	return FALSE;
}

static gboolean
enl_popup_button_press (GtkWidget *widget,
			GdkEventButton *event,
			ENameSelectorList *list)
{
	if (!GTK_WIDGET_MAPPED (widget))
		return FALSE;
	/* if we come here, it's usually time to popdown */
	gtk_widget_hide ((GtkWidget *)list->popup);

	return TRUE;
}

static gboolean
enl_popup_focus_out (GtkWidget *w,
		     GdkEventFocus *event,
		     ENameSelectorList *list)
{
	/* Just ungrab. We lose focus on button press event */
	enl_popup_ungrab(list);
	return TRUE;
}

static gboolean
enl_popup_enter_notify (GtkWidget        *widget,
			GdkEventCrossing *event,
			ENameSelectorList *list)
{
  if (event->type == GDK_ENTER_NOTIFY && !GTK_WIDGET_HAS_GRAB (list->popup))
	enl_popup_grab (list);

  return TRUE;
}

static void
enl_tree_select_node (ENameSelectorList *list,
		      gint n)
{
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreePath *path;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list->tree_view));
	iter.stamp = ((ENameSelectorEntry *) list)->destination_store->stamp;
	iter.user_data = GINT_TO_POINTER (n-1);

	gtk_tree_selection_unselect_all (selection);
	gtk_tree_selection_select_iter (selection, &iter);

	path = e_destination_store_get_path (GTK_TREE_MODEL(((ENameSelectorEntry *) list)->destination_store), &iter);
	gtk_tree_view_scroll_to_cell ( GTK_TREE_VIEW (list->tree_view), path, gtk_tree_view_get_column( GTK_TREE_VIEW (list->tree_view), 0), FALSE, 0, 0);
	gtk_tree_view_set_cursor ( GTK_TREE_VIEW (list->tree_view), path, gtk_tree_view_get_column( GTK_TREE_VIEW (list->tree_view), 0), FALSE);
	gtk_widget_grab_focus (list->tree_view);
	/*Fixme: We should grab the focus to the column. How? */

	gtk_tree_path_free (path);
}

static gboolean
enl_entry_key_press_event (ENameSelectorList *list,
			   GdkEventKey *event,
			   gpointer dummy)
{
	if ( (event->state & GDK_CONTROL_MASK)  && (event->keyval == GDK_Down)) {
		enl_popup_position (list);
		gtk_widget_show_all (GTK_WIDGET (list->popup));
		enl_popup_grab (list);
		list->rows = e_destination_store_get_destination_count (((ENameSelectorEntry *) list)->destination_store);
		enl_popup_size (list);
		enl_tree_select_node (list, 1);
		return TRUE;
	}
	return FALSE;
}

static void
delete_row (GtkTreePath *path, ENameSelectorList *list)
{
	GtkTreeIter   iter;
	gint n, len;
	GtkTreeSelection *selection;

	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (E_NAME_SELECTOR_ENTRY (list)->destination_store), &iter, path))
		return;

	selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW (list->tree_view));
	len = e_destination_store_get_destination_count (E_NAME_SELECTOR_ENTRY (list)->destination_store);
	n = GPOINTER_TO_INT (iter.user_data);

	e_destination_store_remove_destination_nth (((ENameSelectorEntry *) list)->destination_store, n);

	/* If the last one is deleted select the last but one or the deleted +1 */
	if (n == len -1)
		n -= 1;

	/* We deleted the last entry */
	if (len == 1) {
		enl_popup_ungrab (list);
		if (list->menu)
			gtk_menu_popdown(GTK_MENU (list->menu));
		gtk_widget_hide ( GTK_WIDGET (list->popup));
		return;
	}

	iter.stamp = ((ENameSelectorEntry *) list)->destination_store->stamp;
	iter.user_data = GINT_TO_POINTER (n);

	gtk_tree_selection_unselect_all (selection);
	gtk_tree_selection_select_iter (selection, &iter);

	gtk_tree_path_free (path);

	list->rows = e_destination_store_get_destination_count (((ENameSelectorEntry *) list)->destination_store);
	enl_popup_size (list);

}

static void
popup_activate_email (ENameSelectorEntry *name_selector_entry, GtkWidget *menu_item)
{
	EDestination *destination;
	EContact     *contact;
	gint          email_num;

	destination = name_selector_entry->popup_destination;
	if (!destination)
		return;

	contact = e_destination_get_contact (destination);
	if (!contact)
		return;

	email_num = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu_item), "order"));
	e_destination_set_contact (destination, contact, email_num);
}

static void
popup_activate_list (EDestination *destination, GtkWidget *item)
{
	gboolean status = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));

	e_destination_set_ignored (destination, !status);
}

static void
destination_set_list (GtkWidget *item, EDestination *destination)
{
	EContact *contact;
	gboolean status = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));

	contact = e_destination_get_contact (destination);
	if (!contact)
		return;

	e_destination_set_ignored (destination, !status);
}

static void
destination_set_email (GtkWidget *item, EDestination *destination)
{
	gint email_num;
	EContact *contact;

	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
		return;
	contact = e_destination_get_contact (destination);
	if (!contact)
		return;

	email_num = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "order"));
	e_destination_set_contact (destination, contact, email_num);
}

typedef struct {
	ENameSelectorList *list;
	GtkTreePath *path;
}PopupDeleteRowInfo;

static void
popup_delete_row(GtkWidget *w, PopupDeleteRowInfo *row_info)
{
	delete_row(row_info->path, row_info->list);
	g_free(row_info);
}

static void
menu_deactivate (GtkMenuShell *junk, ENameSelectorList *list)
{
	enl_popup_grab (list);
}

static gboolean
enl_tree_button_press_event (GtkWidget *widget,
		       GdkEventButton *event,
		       ENameSelectorList *list)
{
	GtkWidget *menu;
	EDestination *destination;
	ENameSelectorEntry *name_selector_entry;
	EContact     *contact;
	GtkWidget    *menu_item;
	GList        *email_list = NULL, *l;
	gint          i;
	gint	      email_num, len;
	gchar         *delete_label;
	GSList	     *group = NULL;
	gboolean      is_list;
	gboolean      show_menu = FALSE;
	GtkTreeSelection *selection;
	GtkTreePath  *path;
	PopupDeleteRowInfo *row_info;
	GtkTreeIter   iter;

	if ( !GTK_WIDGET_HAS_GRAB (list->popup))
		enl_popup_grab (list);

	gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW (list->tree_view), event->x, event->y, &path, NULL);
	selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW (list->tree_view));
	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (E_NAME_SELECTOR_ENTRY (list)->destination_store), &iter, path))
		return FALSE;

	gtk_tree_selection_unselect_all (selection);
	gtk_tree_selection_select_iter (selection, &iter);

	if (event->button != 3) {
		return FALSE;
	}

	name_selector_entry = E_NAME_SELECTOR_ENTRY (list);

	destination = e_destination_store_get_destination ( ((ENameSelectorEntry *)list)->destination_store, &iter);

	if (!destination)
		return FALSE;

	contact = e_destination_get_contact (destination);
	if (!contact)
		return FALSE;

	if (list->menu) {
		gtk_menu_popdown (GTK_MENU (list->menu));
	}
	menu = gtk_menu_new ();
	g_signal_connect (menu, "deactivate", G_CALLBACK(menu_deactivate), list);
	list->menu = menu;
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, event->button, gtk_get_current_event_time());

	email_num = e_destination_get_email_num (destination);

	/* Addresses */
	is_list = e_contact_get (contact, E_CONTACT_IS_LIST) ? TRUE : FALSE;
	if (is_list) {
		const GList *dests = e_destination_list_get_dests (destination);
		GList *iters;
		gint length = g_list_length ((GList *)dests);

		for (iters = (GList *)dests; iters; iters = iters->next) {
			EDestination *dest = (EDestination *) iters->data;
			const gchar *email = e_destination_get_email (dest);

			if (!email || *email == '\0')
				continue;

			if (length > 1) {
				menu_item = gtk_check_menu_item_new_with_label (email);
				g_signal_connect (menu_item, "toggled", G_CALLBACK (destination_set_list), dest);
			} else {
				menu_item = gtk_menu_item_new_with_label (email);
			}

			gtk_widget_show (menu_item);
			gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
			show_menu = TRUE;

			if (length > 1) {
				gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), !e_destination_is_ignored(dest));
				g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (popup_activate_list),
							  dest);
			}
		}

	} else {
		email_list = e_contact_get (contact, E_CONTACT_EMAIL);
		len = g_list_length (email_list);

		for (l = email_list, i = 0; l; l = g_list_next (l), i++) {
			gchar *email = l->data;

			if (!email || *email == '\0')
				continue;

			if (len > 1) {
				menu_item = gtk_radio_menu_item_new_with_label (group, email);
				group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menu_item));
				g_signal_connect (menu_item, "toggled", G_CALLBACK (destination_set_email), destination);
			} else {
				menu_item = gtk_menu_item_new_with_label (email);
			}

			gtk_widget_show (menu_item);
			gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
			show_menu = TRUE;
			g_object_set_data (G_OBJECT (menu_item), "order", GINT_TO_POINTER (i));

			if (i == email_num && len > 1) {
				gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);
				g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (popup_activate_email),
							  name_selector_entry);
			}
		}
		g_list_foreach (email_list, (GFunc) g_free, NULL);
		g_list_free (email_list);
	}

	/* Separator */

	if (show_menu) {
		menu_item = gtk_separator_menu_item_new ();
		gtk_widget_show (menu_item);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	}

	delete_label = g_strdup_printf(_("_Delete %s"), (gchar *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
	menu_item = gtk_menu_item_new_with_mnemonic (delete_label);
	g_free (delete_label);
	gtk_widget_show (menu_item);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);

	row_info = g_new (PopupDeleteRowInfo, 1);
	row_info->list = list;
	row_info->path = path;

	g_signal_connect (menu_item, "activate", G_CALLBACK (popup_delete_row),
				  row_info);

	return TRUE;

}

static gboolean
enl_tree_key_press_event (GtkWidget *w,
			   GdkEventKey *event,
			   ENameSelectorList *list)
{
	if (event->keyval == GDK_Escape) {
		enl_popup_ungrab (list);
		gtk_widget_hide ( GTK_WIDGET (list->popup));
		return TRUE;
	} else if (event->keyval == GDK_Delete) {
		GtkTreeSelection *selection;
		GList *paths;

		selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW (list->tree_view));
		paths = gtk_tree_selection_get_selected_rows (selection, (GtkTreeModel **)&(E_NAME_SELECTOR_ENTRY (list)->destination_store));
		paths = g_list_reverse (paths);
		g_list_foreach (paths, (GFunc) delete_row, list);
		g_list_free (paths);
	} else if (event->keyval != GDK_Up && event->keyval != GDK_Down
		   && event->keyval != GDK_Shift_R && event->keyval != GDK_Shift_L
		   && event->keyval != GDK_Control_R && event->keyval != GDK_Control_L) {

		enl_popup_ungrab (list);
		gtk_widget_hide ( GTK_WIDGET (list->popup));
		gtk_widget_event (GTK_WIDGET (list), (GdkEvent *)event);
		return TRUE;
	}

	return FALSE;
}

void
e_name_selector_list_expand_clicked(ENameSelectorList *list)
{

	if (!GTK_WIDGET_VISIBLE (list->popup)) {
		enl_popup_position (list);
		gtk_widget_show_all (GTK_WIDGET (list->popup));
		enl_popup_grab (list);
		list->rows = e_destination_store_get_destination_count (((ENameSelectorEntry *) list)->destination_store);
		enl_popup_size (list);
		enl_tree_select_node (list, 1);
	}
	else {
		enl_popup_ungrab (list);
		if (list->menu)
			gtk_menu_popdown(GTK_MENU (list->menu));
		gtk_widget_hide (GTK_WIDGET (list->popup));
	}
}
/* Object Methods */
static void
e_name_selector_list_dispose (GObject *object)
{
	if (G_OBJECT_CLASS (e_name_selector_list_parent_class)->dispose)
		G_OBJECT_CLASS (e_name_selector_list_parent_class)->dispose (object);
}

static void
e_name_selector_list_finalize (GObject *object)
{
	if (G_OBJECT_CLASS (e_name_selector_list_parent_class)->finalize)
		G_OBJECT_CLASS (e_name_selector_list_parent_class)->finalize (object);
}

static void
e_name_selector_list_realize (GtkWidget *widget)
{
	ENameSelectorList *list = (ENameSelectorList *)widget;
	GTK_WIDGET_CLASS (e_name_selector_list_parent_class)->realize (widget);

	gtk_tree_view_set_model ( GTK_TREE_VIEW (list->tree_view), GTK_TREE_MODEL(((ENameSelectorEntry *)list)->destination_store));
}

static void
e_name_selector_list_class_init (ENameSelectorListClass *name_selector_list_class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (name_selector_list_class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (name_selector_list_class);

	object_class->dispose      = e_name_selector_list_dispose;
	object_class->finalize     = e_name_selector_list_finalize;

	widget_class->realize      = e_name_selector_list_realize;

	/* Install properties */

	/* Install signals */

}

static void
e_name_selector_list_init (ENameSelectorList *list)
{
	GtkCellRenderer *renderer;
	GtkWidget *scroll, *popup_frame, *vbox;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	ENameSelectorEntry *entry = E_NAME_SELECTOR_ENTRY (list);
	GtkEntryCompletion *completion;

	list->store = e_destination_store_new ();
	list->menu = NULL;

	list->tree_view = GTK_WIDGET (gtk_tree_view_new_with_model (GTK_TREE_MODEL(entry->destination_store)));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list->tree_view), FALSE);
	gtk_tree_view_set_hover_selection (GTK_TREE_VIEW (list->tree_view), FALSE);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list->tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_unselect_all (selection);
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (list->tree_view), FALSE);

	completion = gtk_entry_get_completion (GTK_ENTRY(list));
	gtk_entry_completion_set_inline_completion (completion, TRUE);
	gtk_entry_completion_set_popup_completion (completion, TRUE);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer, "text", E_DESTINATION_STORE_COLUMN_ADDRESS, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list->tree_view), column);
	gtk_tree_view_column_set_clickable (column, TRUE);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
					     GTK_SHADOW_NONE);
	gtk_widget_set_size_request (
		gtk_scrolled_window_get_vscrollbar (
		GTK_SCROLLED_WINDOW (scroll)), -1, 0);

	list->popup =  GTK_WINDOW (gtk_window_new (GTK_WINDOW_POPUP));
	gtk_window_set_resizable (GTK_WINDOW (list->popup), FALSE);

	popup_frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (popup_frame),
				   GTK_SHADOW_ETCHED_IN);

	gtk_container_add (GTK_CONTAINER (list->popup), popup_frame);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (popup_frame), vbox);

	gtk_container_add (GTK_CONTAINER (scroll), list->tree_view);
	gtk_box_pack_start (GTK_BOX (vbox), scroll,
			    TRUE, TRUE, 0);

	g_signal_connect_after (GTK_WIDGET (list), "focus-in-event", G_CALLBACK(enl_entry_focus_in), NULL);
	g_signal_connect (GTK_WIDGET (list), "focus-out-event", G_CALLBACK(enl_entry_focus_out), NULL);
	g_signal_connect (GTK_WIDGET (list), "key-press-event", G_CALLBACK(enl_entry_key_press_event), NULL);

	g_signal_connect_after (list->tree_view, "key-press-event", G_CALLBACK(enl_tree_key_press_event), list);
	g_signal_connect (list->tree_view, "button-press-event", G_CALLBACK (enl_tree_button_press_event), list);

	g_signal_connect (GTK_WIDGET (list->popup), "button-press-event", G_CALLBACK(enl_popup_button_press), list);
	g_signal_connect (GTK_WIDGET (list->popup), "focus-out-event", G_CALLBACK(enl_popup_focus_out), list);
	g_signal_connect (GTK_WIDGET (list->popup), "enter-notify-event", G_CALLBACK (enl_popup_enter_notify), list);

}

ENameSelectorList *
e_name_selector_list_new (void)
{
	return g_object_new (e_name_selector_list_get_type (), NULL);
}
