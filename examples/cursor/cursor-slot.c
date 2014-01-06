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

#include "cursor-slot.h"

struct _CursorSlotPrivate {
	/* Screen widgets */
	GtkWidget *area;
	GtkLabel  *name_label;
	GtkLabel  *emails_label;
	GtkLabel  *telephones_label;
};

G_DEFINE_TYPE_WITH_PRIVATE (CursorSlot, cursor_slot, GTK_TYPE_GRID);

/************************************************************************
 *                          GObjectClass                                *
 ************************************************************************/
static void
cursor_slot_class_init (CursorSlotClass *klass)
{
	GtkWidgetClass *widget_class;

	/* Bind to template */
	widget_class = GTK_WIDGET_CLASS (klass);
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/evolution/cursor-example/cursor-slot.ui");
	gtk_widget_class_bind_template_child_private (widget_class, CursorSlot, area);
	gtk_widget_class_bind_template_child_private (widget_class, CursorSlot, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, CursorSlot, emails_label);
	gtk_widget_class_bind_template_child_private (widget_class, CursorSlot, telephones_label);
}

static void
cursor_slot_init (CursorSlot *slot)
{
	slot->priv = cursor_slot_get_instance_private (slot);

	gtk_widget_init_template (GTK_WIDGET (slot));
}

/************************************************************************
 *                                API                                   *
 ************************************************************************/
GtkWidget *
cursor_slot_new (EContact *contact)
{
  CursorSlot *slot;

  slot = g_object_new (CURSOR_TYPE_SLOT, NULL);

  cursor_slot_set_from_contact (slot, contact);

  return (GtkWidget *) slot;
}

static gchar *
make_string_from_list (EContact *contact,
                       EContactField field)
{
	GList *values, *l;
	GString *string;

	string = g_string_new ("<span size=\"x-small\">");
	values = e_contact_get (contact, field);

	for (l = values; l; l = l->next) {
		gchar *value = (gchar *) l->data;

		if (l->prev != NULL)
			g_string_append (string, ", ");

		g_string_append (string, value);
	}

	if (!values)
		g_string_append (string, " - none - ");

	e_contact_attr_list_free (values);
	g_string_append (string, "</span>");

	return g_string_free (string, FALSE);
}

void
cursor_slot_set_from_contact (CursorSlot *slot,
                              EContact *contact)
{
	CursorSlotPrivate *priv;
	const gchar *family_name, *given_name;
	gchar *str;

	g_return_if_fail (CURSOR_IS_SLOT (slot));
	g_return_if_fail (contact == NULL || E_IS_CONTACT (contact));

	priv = slot->priv;

	if (!contact) {
		gtk_widget_hide (priv->area);
		return;
	}

	family_name = (const gchar *) e_contact_get_const (contact, E_CONTACT_FAMILY_NAME);
	given_name = (const gchar *) e_contact_get_const (contact, E_CONTACT_GIVEN_NAME);

	str = g_strdup_printf ("%s, %s", family_name, given_name);
	gtk_label_set_text (priv->name_label, str);
	g_free (str);

	str = make_string_from_list (contact, E_CONTACT_EMAIL);
	gtk_label_set_markup (priv->emails_label, str);
	g_free (str);

	str = make_string_from_list (contact, E_CONTACT_TEL);
	gtk_label_set_markup (priv->telephones_label, str);
	g_free (str);

	gtk_widget_show (priv->area);
}

