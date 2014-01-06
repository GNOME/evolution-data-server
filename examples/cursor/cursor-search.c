/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <libebook/libebook.h>

#include "cursor-search.h"

/* GObjectClass */
static void  cursor_search_finalize       (GObject           *object);
static void  cursor_search_get_property   (GObject           *object,
					   guint              property_id,
					   GValue            *value,
					   GParamSpec        *pspec);

/* UI Callbacks */
static void  cursor_search_option_toggled (CursorSearch         *search,
					   GtkWidget            *item);
static void  cursor_search_entry_changed  (CursorSearch         *search,
					   GtkEditable          *entry);
static void  cursor_search_icon_press     (CursorSearch         *search,
					   GtkEntryIconPosition  icon_pos,
					   GdkEvent             *event,
					   GtkEntry             *entry);

typedef enum {
	SEARCH_NAME,
	SEARCH_PHONE,
	SEARCH_EMAIL
} SearchType;

struct _CursorSearchPrivate {
	GtkWidget   *popup;
	GtkWidget   *name_radio;
	GtkWidget   *phone_radio;
	GtkWidget   *email_radio;

	SearchType   type;
	gchar       *sexp;
};

enum {
	PROP_0,
	PROP_SEXP,
};

G_DEFINE_TYPE_WITH_PRIVATE (CursorSearch, cursor_search, GTK_TYPE_SEARCH_ENTRY);

/************************************************************************
 *                          GObjectClass                                *
 ************************************************************************/
static void
cursor_search_class_init (CursorSearchClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cursor_search_finalize;
	object_class->get_property = cursor_search_get_property;

	g_object_class_install_property (
		object_class,
		PROP_SEXP,
		g_param_spec_string (
			"sexp",
			"Search Expression",
			"The active search expression",
			NULL,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	/* Bind to template */
	widget_class = GTK_WIDGET_CLASS (klass);
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/evolution/cursor-example/cursor-search.ui");
	gtk_widget_class_bind_template_child_private (widget_class, CursorSearch, popup);
	gtk_widget_class_bind_template_child_private (widget_class, CursorSearch, name_radio);
	gtk_widget_class_bind_template_child_private (widget_class, CursorSearch, phone_radio);
	gtk_widget_class_bind_template_child_private (widget_class, CursorSearch, email_radio);
	gtk_widget_class_bind_template_callback (widget_class, cursor_search_option_toggled);
	gtk_widget_class_bind_template_callback (widget_class, cursor_search_entry_changed);
	gtk_widget_class_bind_template_callback (widget_class, cursor_search_icon_press);
}

static void
cursor_search_init (CursorSearch *search)
{
	search->priv = cursor_search_get_instance_private (search);

	gtk_widget_init_template (GTK_WIDGET (search));

	g_object_set (
		search,
		"primary-icon-activatable", TRUE,
		"primary-icon-sensitive", TRUE,
		NULL);
}

static void
cursor_search_finalize (GObject *object)
{
	CursorSearch        *search = CURSOR_SEARCH (object);
	CursorSearchPrivate *priv = search->priv;

	g_free (priv->sexp);

	G_OBJECT_CLASS (cursor_search_parent_class)->finalize (object);
}

static void
cursor_search_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	CursorSearch        *search = CURSOR_SEARCH (object);
	CursorSearchPrivate *priv = search->priv;

	switch (property_id) {
	case PROP_SEXP:
		g_value_set_string (value, priv->sexp);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;

	}
}

/************************************************************************
 *                              UI Callbacks                            *
 ************************************************************************/
static void
cursor_search_option_toggled (CursorSearch *search,
                              GtkWidget *item)
{
	CursorSearchPrivate *priv = search->priv;

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item))) {

		if (item == priv->name_radio)
			priv->type = SEARCH_NAME;
		else if (item == priv->phone_radio)
			priv->type = SEARCH_PHONE;
		else if (item == priv->email_radio)
			priv->type = SEARCH_EMAIL;

		/* Refresh the search */
		cursor_search_entry_changed (search, NULL);
	}
}

static void
cursor_search_entry_changed (CursorSearch *search,
                             GtkEditable *entry)
{
	CursorSearchPrivate *priv = search->priv;
	EBookQuery  *query = NULL;
	const gchar *text;

	text = gtk_entry_get_text (GTK_ENTRY (search));

	if (text && text[0]) {
		switch (priv->type) {
		case SEARCH_NAME:
			query = e_book_query_orv (
				e_book_query_field_test (E_CONTACT_FAMILY_NAME,
				E_BOOK_QUERY_CONTAINS,
				text),
				e_book_query_field_test (E_CONTACT_GIVEN_NAME,
				E_BOOK_QUERY_CONTAINS,
				text),
				NULL);
			break;
		case SEARCH_PHONE:
			query = e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_CONTAINS,
				text);
			break;
		case SEARCH_EMAIL:
			query = e_book_query_field_test (
				E_CONTACT_EMAIL,
				E_BOOK_QUERY_CONTAINS,
				text);
			break;
		}
	}

	g_free (priv->sexp);

	if (query) {
		priv->sexp = e_book_query_to_string (query);
		e_book_query_unref (query);
	} else
		priv->sexp = g_strdup ("");

	g_object_notify (G_OBJECT (search), "sexp");
}

static void
cursor_search_icon_press (CursorSearch *search,
                          GtkEntryIconPosition icon_pos,
                          GdkEvent *event,
                          GtkEntry *entry)
{
	CursorSearchPrivate *priv = search->priv;
	GdkEventButton *button_event = (GdkEventButton *) event;

	if (icon_pos == GTK_ENTRY_ICON_PRIMARY)
		gtk_menu_popup (
			GTK_MENU (priv->popup),
				NULL, NULL, NULL, NULL,
				button_event->button,
				button_event->time);
}

/************************************************************************
 *                                API                                   *
 ************************************************************************/
GtkWidget *
cursor_search_new (void)
{
  return (GtkWidget *) g_object_new (CURSOR_TYPE_SEARCH, NULL);
}

const gchar *
cursor_search_get_sexp (CursorSearch *search)
{
	CursorSearchPrivate *priv;

	g_return_val_if_fail (CURSOR_IS_SEARCH (search), NULL);

	priv = search->priv;

	return priv->sexp;
}
