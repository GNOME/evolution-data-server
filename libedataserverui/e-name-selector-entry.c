/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-entry.c - Single-line text entry widget for EDestinations.
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#include <config.h>
#include <string.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkentrycompletion.h>
#include <gtk/gtkcelllayout.h>
#include <gtk/gtkcellrenderertext.h>
#include <libgnome/gnome-i18n.h>

#include <libebook/e-book.h>
#include <libebook/e-contact.h>
#include <libebook/e-destination.h>

#include "e-name-selector-entry.h"

#define ENS_DEBUG(x)

#define COMPLETION_CUE_MIN_LEN 2

G_DEFINE_TYPE (ENameSelectorEntry, e_name_selector_entry, GTK_TYPE_ENTRY);

static void e_name_selector_entry_class_init (ENameSelectorEntryClass *name_selector_entry_class);
static void e_name_selector_entry_init       (ENameSelectorEntry *name_selector_entry);
static void e_name_selector_entry_dispose    (GObject *object);
static void e_name_selector_entry_finalize   (GObject *object);

static void destination_row_inserted (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter);
static void destination_row_changed  (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter);
static void destination_row_deleted  (ENameSelectorEntry *name_selector_entry, GtkTreePath *path);

static void user_insert_text (ENameSelectorEntry *name_selector_entry, gchar *new_text, gint new_text_length, gint *position, gpointer user_data);
static void user_delete_text (ENameSelectorEntry *name_selector_entry, gint start_pos, gint end_pos, gpointer user_data);

static void
e_name_selector_entry_get_property (GObject *object, guint prop_id,
				    GValue *value, GParamSpec *pspec)
{
}

static void
e_name_selector_entry_set_property (GObject *object, guint prop_id,
				    const GValue *value, GParamSpec *pspec)
{
}

/* Partial, repeatable destruction. Release references. */
static void
e_name_selector_entry_dispose (GObject *object)
{
	ENameSelectorEntry *name_selector_entry = E_NAME_SELECTOR_ENTRY (object);

	if (G_OBJECT_CLASS (e_name_selector_entry_parent_class)->dispose)
		G_OBJECT_CLASS (e_name_selector_entry_parent_class)->dispose (object);
}

/* Final, one-time destruction. Free all. */
static void
e_name_selector_entry_finalize (GObject *object)
{
	ENameSelectorEntry *name_selector_entry = E_NAME_SELECTOR_ENTRY (object);

	if (G_OBJECT_CLASS (e_name_selector_entry_parent_class)->finalize)
		G_OBJECT_CLASS (e_name_selector_entry_parent_class)->finalize (object);
}

static void
e_name_selector_entry_class_init (ENameSelectorEntryClass *name_selector_entry_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (name_selector_entry_class);

	object_class->get_property = e_name_selector_entry_get_property;
	object_class->set_property = e_name_selector_entry_set_property;
	object_class->dispose      = e_name_selector_entry_dispose;
	object_class->finalize     = e_name_selector_entry_finalize;

	/* Install properties */

	/* Install signals */

}

/* Called for each list store entry whenever the user types (but not on cut/paste) */
static gboolean
completion_match_cb (GtkEntryCompletion *completion, const gchar *key,
		     GtkTreeIter *iter, gpointer user_data)
{
	ENS_DEBUG (g_print ("completion_match_cb, key=%s\n", key));

	return TRUE;
}

/* Gets context of n_unichars total (n_unicars / 2, before and after position)
 * and places them in array. If any positions would be outside the string, the
 * corresponding unichars are set to zero. */
static void
get_utf8_string_context (const gchar *string, gint position, gunichar *unichars, gint n_unichars)
{
	gchar *p = NULL;
	gint   len;
	gint   gap;
	gint   i;

	/* n_unichars must be even */
	g_assert (n_unichars % 2 == 0);

	len = g_utf8_strlen (string, -1);
	gap = n_unichars / 2;

	for (i = 0; i < n_unichars; i++) {
		gint char_pos = position - gap + i;

		if (char_pos < 0 || char_pos >= len) {
			unichars [i] = '\0';
			continue;
		}

		if (p)
			p = g_utf8_next_char (p);
		else
			p = g_utf8_offset_to_pointer (string, char_pos);

		unichars [i] = g_utf8_get_char (p);
	}
}

static gboolean
get_range_at_position (const gchar *string, gint pos, gint *start_pos, gint *end_pos)
{
	const gchar *p;
	gint         local_start_pos = 0;
	gint         local_end_pos   = 0;
	gint         i;

	for (p = string, i = 0; *p; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == ',') {
			if (i < pos) {
				/* Start right after comma */
				local_start_pos = i + 1;
			} else {
				/* Stop right before comma */
				local_end_pos = i;
				break;
			}
		} else if (c == ' ' && local_start_pos == i) {
			/* Adjust start to skip space after first comma */
			local_start_pos++;
		}
	}

	/* If we didn't hit a comma, we must've hit NULL, and ours was the last element. */
	if (!local_end_pos)
		local_end_pos = i;

	if (start_pos)
		*start_pos = local_start_pos;
	if (end_pos)
		*end_pos   = local_end_pos;

	return TRUE;
}

static gint
get_index_at_position (const gchar *string, gint pos)
{
	const gchar *p;
	gint         i;
	gint         n = 0;

	for (p = string, i = 0; *p && i < pos; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == ',')
			n++;
	}

	return n;
}

static gboolean
get_range_by_index (const gchar *string, gint index, gint *start_pos, gint *end_pos)
{
	const gchar *p;
	gint         i;
	gint         n = 0;

	for (p = string, i = 0; *p && n < index; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == ',')
			n++;
	}

	if (n < index)
		return FALSE;

	return get_range_at_position (string, i, start_pos, end_pos);
}

static gchar *
get_address_at_position (const gchar *string, gint pos)
{
	gint         start_pos;
	gint         end_pos;
	const gchar *start_p;
	const gchar *end_p;
	gchar       *address;

	if (!get_range_at_position (string, pos, &start_pos, &end_pos))
		return NULL;

	start_p = g_utf8_offset_to_pointer (string, start_pos);
	end_p   = g_utf8_offset_to_pointer (string, end_pos);

	return g_strndup (start_p, end_p - start_p);
}

/* Finds the destination in model */
static EDestination *
find_destination_by_index (ENameSelectorEntry *name_selector_entry, gint index)
{
	GtkTreePath  *path;
	GtkTreeIter   iter;

	path = gtk_tree_path_new_from_indices (index, -1);
	if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (name_selector_entry->destination_store),
				      &iter, path)) {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		gtk_tree_path_free (path);
		return NULL;
	}
	gtk_tree_path_free (path);

	return e_destination_store_get_destination (name_selector_entry->destination_store, &iter);
}

/* Finds the destination in model */
static EDestination *
find_destination_at_position (ENameSelectorEntry *name_selector_entry, gint pos)
{
	const gchar  *text;
	gint          index;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	index = get_index_at_position (text, pos);

	return find_destination_by_index (name_selector_entry, index);
}

/* Builds destination from our text */
static EDestination *
build_destination_at_position (const gchar *string, gint pos)
{
	EDestination *destination;
	gchar        *address;
	gint          i;

	address = get_address_at_position (string, pos);
	if (!address)
		return NULL;

	destination = e_destination_new ();
	e_destination_set_raw (destination, address);

	g_free (address);
	return destination;
}

static gchar *
name_style_query (const gchar *field, const gchar *value)
{
	gchar   *cpy = g_strdup (value), *c;
	GString *out = g_string_new ("");
	gchar  **strv;
	gchar   *query;
	gint     i;

	for (c = cpy; *c; c++) {
		if (*c == ',')
			*c = ' ';
	}

	strv = g_strsplit (cpy, " ", 0);

	if (strv [0] && strv [1])
		g_string_append (out, "(and ");

	for (i = 0; strv [i]; i++) {
		if (i == 0)
			g_string_append (out, "(beginswith ");
		else
			g_string_append (out, " (beginswith ");

		e_sexp_encode_string (out, field);
		g_strstrip (strv [i]);
		e_sexp_encode_string (out, strv [i]);
		g_string_append (out, ")");
	}

	if (strv [0] && strv [1])
		g_string_append (out, ")");

	query = out->str;
	g_string_free (out, FALSE);

	g_free (cpy);
	g_strfreev (strv);

	return query;
}

static gchar *
escape_sexp_string (const gchar *string)
{
	GString *gstring;
	gchar   *encoded_string;

	gstring = g_string_new ("");
	e_sexp_encode_string (gstring, string);

	encoded_string = gstring->str;
	g_string_free (gstring, FALSE);

	return encoded_string;
}

static void
set_completion_query (ENameSelectorEntry *name_selector_entry, const gchar *cue_str)
{
	EBookQuery *book_query;
	gchar      *query_str;
	gchar      *encoded_cue_str;
	gchar      *full_name_query_str;
	gchar      *file_as_query_str;

	if (!cue_str) {
		/* Clear the store */
		e_contact_store_set_query (name_selector_entry->contact_store, NULL);
		return;
	}

	encoded_cue_str     = escape_sexp_string (cue_str);
	full_name_query_str = name_style_query ("full_name", cue_str);
	file_as_query_str   = name_style_query ("file_as",   cue_str);

	query_str = g_strdup_printf ("(or "
				     " (beginswith \"nickname\"  %s) "
				     " (beginswith \"email\"     %s) "
				     " %s "
				     " %s "
				     ")",
				     encoded_cue_str, encoded_cue_str,
				     full_name_query_str, file_as_query_str);

	g_free (file_as_query_str);
	g_free (full_name_query_str);
	g_free (encoded_cue_str);

	ENS_DEBUG (g_print ("%s\n", query_str));

	book_query = e_book_query_from_string (query_str);
	e_contact_store_set_query (name_selector_entry->contact_store, book_query);
	e_book_query_unref (book_query);

	g_free (query_str);
}

static gchar *
get_entry_substring (ENameSelectorEntry *name_selector_entry, gint range_start, gint range_end)
{
	const gchar *entry_text;
	gchar       *p0, *p1;
	gchar       *substr;

	entry_text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));

	p0 = g_utf8_offset_to_pointer (entry_text, range_start);
	p1 = g_utf8_offset_to_pointer (entry_text, range_end);

	return g_strndup (p0, p1 - p0);
}

static gint
utf8_casefold_collate_len (const gchar *str1, const gchar *str2, gint len)
{
	gchar *s1 = g_utf8_casefold(str1, len);
	gchar *s2 = g_utf8_casefold(str2, len);
	int rv;

	rv = g_utf8_collate (s1, s2);

	g_free (s1);
	g_free (s2);

	return rv;
}

static gint
utf8_casefold_collate (const gchar *str1, const gchar *str2)
{
	return utf8_casefold_collate_len (str1, str2, -1);
}

static gchar *
build_textrep_for_contact (EContact *contact, EContactField cue_field)
{
	gchar *name  = NULL;
	gchar *email = NULL;
	gchar *textrep;
	gchar *p0;

	switch (cue_field) {
		case E_CONTACT_FULL_NAME:
		case E_CONTACT_NICKNAME:
		case E_CONTACT_FILE_AS:
			name  = e_contact_get (contact, cue_field);
			email = e_contact_get (contact, E_CONTACT_EMAIL_1);
			break;

		case E_CONTACT_EMAIL_1:
		case E_CONTACT_EMAIL_2:
		case E_CONTACT_EMAIL_3:
		case E_CONTACT_EMAIL_4:
			name = NULL;
			email = e_contact_get (contact, cue_field);
			break;

		default:
			g_assert_not_reached ();
			break;
	}

	g_assert (email);
	g_assert (strlen (email) > 0);

	if (name)
		textrep = g_strdup_printf ("%s", name);
	else
		textrep = g_strdup_printf ("%s", email);

	/* HACK: We can't handle commas in names. Replace commas with spaces for the time being. */
	while ((p0 = g_utf8_strchr (textrep, -1, ',')))
		*p0 = ' ';

	g_free (name);
	g_free (email);
	return textrep;
}

static gboolean
contact_match_cue (EContact *contact, const gchar *cue_str,
		   EContactField *matched_field, gint *matched_field_rank)
{
	EContactField  fields [] = { E_CONTACT_FULL_NAME, E_CONTACT_NICKNAME, E_CONTACT_FILE_AS,
				     E_CONTACT_EMAIL_1, E_CONTACT_EMAIL_2, E_CONTACT_EMAIL_3,
				     E_CONTACT_EMAIL_4 };
	gchar         *email;
	gboolean       result = FALSE;
	gint           cue_len;
	gint           i;

	g_assert (contact);
	g_assert (cue_str);

	if (g_utf8_strlen (cue_str, -1) < COMPLETION_CUE_MIN_LEN)
		return FALSE;

	cue_len = strlen (cue_str);

	/* Make sure contact has an email address */
	email = e_contact_get (contact, E_CONTACT_EMAIL_1);
	if (!email || !*email) {
		g_free (email);
		return FALSE;
	}
	g_free (email);

	for (i = 0; i < G_N_ELEMENTS (fields); i++) {
		gchar *value;

		/* Don't match e-mail addresses in contact lists */
		if (e_contact_get (contact, E_CONTACT_IS_LIST) &&
		    fields [i] >= E_CONTACT_FIRST_EMAIL_ID &&
		    fields [i] <= E_CONTACT_LAST_EMAIL_ID)
			continue;

		value = e_contact_get (contact, fields [i]);
		if (!value)
			continue;

		ENS_DEBUG (g_print ("Comparing '%s' to '%s'\n", value, cue_str));

		if (!utf8_casefold_collate_len (value, cue_str, cue_len)) {
			/* TODO: We have to check if the value needs quoting, and
			 * if we're accepting a quoted match. */
			if (matched_field)
				*matched_field = fields [i];
			if (matched_field_rank)
				*matched_field_rank = i;

			result = TRUE;
			g_free (value);
			break;
		}
		g_free (value);
	}

	return result;
}

static gboolean
find_existing_completion (ENameSelectorEntry *name_selector_entry, const gchar *cue_str,
			  EContact **contact, gchar **text, EContactField *matched_field)
{
	GtkTreeIter    iter;
	EContact      *best_contact    = NULL;
	gint           best_field_rank = G_MAXINT;
	EContactField  best_field;
	gint           cue_len;

	g_assert (cue_str);
	cue_len = strlen (cue_str);

	ENS_DEBUG (g_print ("Completing '%s'\n", cue_str));

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (name_selector_entry->contact_store), &iter))
		return FALSE;

	do {
		EContact      *current_contact;
		gint           current_field_rank;
		EContactField  current_field;
		gboolean       matches;

		current_contact = e_contact_store_get_contact (name_selector_entry->contact_store, &iter);
		if (!current_contact)
			continue;

		matches = contact_match_cue (current_contact, cue_str, &current_field, &current_field_rank);
		if (matches && current_field_rank < best_field_rank) {
			best_contact    = current_contact;
			best_field_rank = current_field_rank;
			best_field      = current_field;
		}
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (name_selector_entry->contact_store), &iter));

	if (!best_contact)
		return FALSE;

	if (contact)
		*contact = best_contact;
	if (text)
		*text = build_textrep_for_contact (best_contact, best_field);
	if (matched_field)
		*matched_field = best_field;

	return TRUE;
}

static void
generate_attribute_list (ENameSelectorEntry *name_selector_entry)
{
	PangoLayout    *layout;
	PangoAttrList  *attr_list;
	const gchar    *text;
	gint            i;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	layout = gtk_entry_get_layout (GTK_ENTRY (name_selector_entry));

	/* Set up the attribute list */

	attr_list = pango_attr_list_new ();

	if (name_selector_entry->attr_list)
		pango_attr_list_unref (name_selector_entry->attr_list);

	name_selector_entry->attr_list = attr_list;

	/* Parse the entry's text and apply attributes to real contacts */

	for (i = 0; ; i++) {
		EDestination   *destination;
		PangoAttribute *attr;
		gint            start_pos;
		gint            end_pos;

		if (!get_range_by_index (text, i, &start_pos, &end_pos))
			break;

		destination = find_destination_at_position (name_selector_entry, start_pos);
		/* g_assert (destination);
		 *
		 * The above assertion is correct, but I'm disabling it temporarily as
		 * users have reported crashes. The cause is somewhere else.
		 */

		if (!destination || !e_destination_get_contact (destination))
			continue;

		attr = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);
		attr->start_index = g_utf8_offset_to_pointer (text, start_pos) - text;
		attr->end_index = g_utf8_offset_to_pointer (text, end_pos) - text;
		pango_attr_list_insert (attr_list, attr);
	}

	pango_layout_set_attributes (layout, attr_list);
}

static gboolean
expose_event (ENameSelectorEntry *name_selector_entry)
{
	PangoLayout *layout;

	layout = gtk_entry_get_layout (GTK_ENTRY (name_selector_entry));
	pango_layout_set_attributes (layout, name_selector_entry->attr_list);

	return FALSE;
}

static void
type_ahead_complete (ENameSelectorEntry *name_selector_entry)
{
	EContact      *contact;
	EContactField  matched_field;
	EDestination  *destination;
	gint           cursor_pos;
	gint           range_start = 0;
	gint           range_end   = 0;
	gint           pos         = 0;
	gchar         *textrep;
	gint           textrep_len;
	gint           range_len;
	const gchar   *text;
	gchar         *cue_str;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));
	if (cursor_pos < 0)
		return;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	get_range_at_position (text, cursor_pos, &range_start, &range_end);
	range_len = range_end - range_start;
	if (range_len < COMPLETION_CUE_MIN_LEN)
		return;

	destination = find_destination_at_position (name_selector_entry, cursor_pos);

	cue_str = get_entry_substring (name_selector_entry, range_start, range_end);
	if (!find_existing_completion (name_selector_entry, cue_str, &contact,
				       &textrep, &matched_field)) {
		g_free (cue_str);
		return;
	}
	g_free (cue_str);

	textrep_len = g_utf8_strlen (textrep, -1);
	pos         = range_start;

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_changed, name_selector_entry);

	if (textrep_len > range_len) {
		gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);
		gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), textrep, -1, &pos);
		gtk_editable_select_region (GTK_EDITABLE (name_selector_entry), range_end,
					    range_start + textrep_len);
	}

	if (contact && destination) {
		gint email_n = 0;

		if (matched_field >= E_CONTACT_FIRST_EMAIL_ID && matched_field <= E_CONTACT_LAST_EMAIL_ID)
			email_n = matched_field - E_CONTACT_FIRST_EMAIL_ID;

		e_destination_set_contact (destination, contact, email_n);
		generate_attribute_list (name_selector_entry);
	}

	g_signal_handlers_unblock_by_func (name_selector_entry->destination_store,
					   destination_row_changed, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	g_free (textrep);
}

static void
clear_completion_model (ENameSelectorEntry *name_selector_entry)
{
	e_contact_store_set_query (name_selector_entry->contact_store, NULL);
}

static void
update_completion_model (ENameSelectorEntry *name_selector_entry)
{
	const gchar *text;
	gint         cursor_pos;
	gint         range_start = 0;
	gint         range_end   = 0;

	text       = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));

	if (cursor_pos >= 0)
		get_range_at_position (text, cursor_pos, &range_start, &range_end);

	if (range_end - range_start >= COMPLETION_CUE_MIN_LEN && cursor_pos == range_end) {
		gchar *cue_str;

		cue_str = get_entry_substring (name_selector_entry, range_start, range_end);
		set_completion_query (name_selector_entry, cue_str);
		g_free (cue_str);
	} else {
		/* N/A; Clear completion model */
		clear_completion_model (name_selector_entry);
	}
}

static gboolean
type_ahead_complete_on_idle_cb (ENameSelectorEntry *name_selector_entry)
{
	type_ahead_complete (name_selector_entry);
	name_selector_entry->type_ahead_complete_cb_id = 0;
	return FALSE;
}

static gboolean
update_completions_on_idle_cb (ENameSelectorEntry *name_selector_entry)
{
	update_completion_model (name_selector_entry);
	name_selector_entry->update_completions_cb_id = 0;
	return FALSE;
}

static void
insert_destination_at_position (ENameSelectorEntry *name_selector_entry, gint pos)
{
	EDestination *destination;
	const gchar  *text;
	gint          index;
	gint          range_start, range_end;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	index = get_index_at_position (text, pos);

	destination = build_destination_at_position (text, pos);
	g_assert (destination);

	g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_inserted, name_selector_entry);
	e_destination_store_insert_destination (name_selector_entry->destination_store,
						index, destination);
	g_signal_handlers_unblock_by_func (name_selector_entry->destination_store,
					   destination_row_inserted, name_selector_entry);
	g_object_unref (destination);
}

static void
modify_destination_at_position (ENameSelectorEntry *name_selector_entry, gint pos)
{
	EDestination *destination;
	const gchar  *text;
	gchar        *raw_address;
	gint          index;
	gint          range_start, range_end;
	gboolean      rebuild_attributes = FALSE;

	destination = find_destination_at_position (name_selector_entry, pos);
	g_assert (destination);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	raw_address = get_address_at_position (text, pos);
	g_assert (raw_address);

	if (e_destination_get_contact (destination))
		rebuild_attributes = TRUE;

	g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_changed, name_selector_entry);
	e_destination_set_raw (destination, raw_address);
	g_signal_handlers_unblock_by_func (name_selector_entry->destination_store,
					   destination_row_changed, name_selector_entry);

	g_free (raw_address);

	if (rebuild_attributes)
		generate_attribute_list (name_selector_entry);
}

static void
remove_destination_at_position (ENameSelectorEntry *name_selector_entry, gint pos)
{
	EDestination *destination;

	destination = find_destination_at_position (name_selector_entry, pos);
	g_assert (destination);

	g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_deleted, name_selector_entry);
	e_destination_store_remove_destination (name_selector_entry->destination_store,
						destination);
	g_signal_handlers_unblock_by_func (name_selector_entry->destination_store,
					   destination_row_deleted, name_selector_entry);
}

static void
sync_destination_at_position (ENameSelectorEntry *name_selector_entry, gint range_pos, gint *cursor_pos)
{
	EDestination *destination;
	const gchar  *text;
	const gchar  *address;
	gint          address_len;
	gint          range_start, range_end;

	destination = find_destination_at_position (name_selector_entry, range_pos);
	g_assert (destination);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!get_range_at_position (text, range_pos, &range_start, &range_end)) {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		return;
	}

	address = e_destination_get_textrep (destination, FALSE);
	address_len = g_utf8_strlen (address, -1);

	if (cursor_pos) {
		/* Update cursor placement */
		if (*cursor_pos >= range_end)
			*cursor_pos += address_len - (range_end - range_start);
		else if (*cursor_pos > range_start)
			*cursor_pos = range_start + address_len;
	}

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);
	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), address, -1, &range_start);

	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	generate_attribute_list (name_selector_entry);
}

static void
remove_destination_by_index (ENameSelectorEntry *name_selector_entry, gint index)
{
	EDestination *destination;

	destination = find_destination_by_index (name_selector_entry, index);
	g_assert (destination);

	g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_deleted, name_selector_entry);
	e_destination_store_remove_destination (name_selector_entry->destination_store,
						destination);
	g_signal_handlers_unblock_by_func (name_selector_entry->destination_store,
					   destination_row_deleted, name_selector_entry);
}

/* Returns the number of characters inserted */
static gint
insert_unichar (ENameSelectorEntry *name_selector_entry, gint *pos, gunichar c)
{
	const gchar *text;
	gunichar     str_context [4];
	gchar        buf [7];
	gint         len;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	get_utf8_string_context (text, *pos, str_context, 4);

	/* Space is not allowed:
	 * - Before or after another space.
	 * - At start of string. */

	if (c == ' ' && (str_context [1] == ' ' || str_context [1] == '\0' || str_context [2] == ' '))
		return 0;

	/* Comma is not allowed:
	 * - After another comma.
	 * - At start of string. */

	if (c == ',') {
		const gchar *p0;
		gint         start_pos;
		gint         end_pos;
		gboolean     at_start = FALSE;
		gboolean     at_end   = FALSE;

		if (str_context [1] == ',' || str_context [1] == '\0')
			return 0;

		/* We do this so we can avoid disturbing destinations with completed contacts
		 * either before or after the destination being inserted. */
		get_range_at_position (text, *pos, &start_pos, &end_pos);
		if (*pos <= start_pos)
			at_start = TRUE;
		if (*pos >= end_pos)
			at_end = TRUE;

		/* Must insert comma first, so modify_destination_at_position can do its job
		 * correctly, splitting up the contact if necessary. */
		gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ", ", -1, pos);

		/* Update model */
		g_assert (*pos >= 2);

		/* If we inserted the comma at the end of, or in the middle of, an existing
		 * address, add a new destination for what appears after comma. Else, we
		 * have to add a destination for what appears before comma (a blank one). */
		if (at_end) {
			/* End: Add last, sync first */
			insert_destination_at_position (name_selector_entry, *pos);
			sync_destination_at_position (name_selector_entry, *pos - 2, pos);
			/* Sync generates the attributes list */
		} else if (at_start) {
			/* Start: Add first */
			insert_destination_at_position (name_selector_entry, *pos - 2);
			generate_attribute_list (name_selector_entry);
		} else {
			/* Middle: */
			insert_destination_at_position (name_selector_entry, *pos);
			modify_destination_at_position (name_selector_entry, *pos - 2);
			generate_attribute_list (name_selector_entry);
		}

		return 2;
	}

	/* Generic case. Allowed spaces also end up here. */

	len = g_unichar_to_utf8 (c, buf);
	buf [len] = '\0';

	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), buf, -1, pos);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	text = g_utf8_next_char (text);
	if (!*text) {
		/* First and only character so far, create initial destination */
		insert_destination_at_position (name_selector_entry, 0);
	} else {
		/* Modified existing destination */
		modify_destination_at_position (name_selector_entry, *pos);
	}

	return 1;
}

static void
user_insert_text (ENameSelectorEntry *name_selector_entry, gchar *new_text,
		  gint new_text_length, gint *position, gpointer user_data)
{
	gchar *p;
	gint   chars_inserted = 0;

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	/* Apply some rules as to where spaces and commas can be inserted, and insert
	 * a trailing space after comma. */

	for (p = new_text; *p; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);
		insert_unichar (name_selector_entry, position, c);
		chars_inserted++;
	}

	if (chars_inserted == 1) {
		/* If the user inserted one character, kick off completion */
		if (!name_selector_entry->update_completions_cb_id) {
			name_selector_entry->update_completions_cb_id =
				g_idle_add ((GSourceFunc) update_completions_on_idle_cb,
					    name_selector_entry);
		}

		if (!name_selector_entry->type_ahead_complete_cb_id) {
			name_selector_entry->type_ahead_complete_cb_id =
				g_idle_add ((GSourceFunc) type_ahead_complete_on_idle_cb,
					    name_selector_entry);
		}
	} else if (chars_inserted > 1 && name_selector_entry->type_ahead_complete_cb_id) {
		/* If the user inserted more than one character, prevent completion */
		g_source_remove (name_selector_entry->type_ahead_complete_cb_id);
		name_selector_entry->type_ahead_complete_cb_id = 0;
	}

	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	g_signal_stop_emission_by_name (name_selector_entry, "insert_text");
}

static void
user_delete_text (ENameSelectorEntry *name_selector_entry, gint start_pos, gint end_pos,
		  gpointer user_data)
{
	const gchar *text;
	gint         index_start, index_end;
	gunichar     str_context [2];
	gchar        buf [7];
	gint         len;
	gint         i;

	if (start_pos == end_pos)
		return;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	get_utf8_string_context (text, start_pos, str_context, 2);

	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	if (end_pos - start_pos == 1) {
		/* Might be backspace; update completion model so dropdown is accurate */
		if (!name_selector_entry->update_completions_cb_id) {
			name_selector_entry->update_completions_cb_id =
				g_idle_add ((GSourceFunc) update_completions_on_idle_cb,
					    name_selector_entry);
		}
	}

	if (str_context [0] == ',' && str_context [1] == ' ') {
		/* If we're deleting the trailing space in ", ", delete the whole ", " sequence. */
		start_pos--;
	}

	index_start = get_index_at_position (text, start_pos);
	index_end   = get_index_at_position (text, end_pos);

	/* If the deletion touches more than one destination, the first one is changed
	 * and the rest are removed. If the last destination wasn't completely deleted,
	 * it becomes part of the first one, since the separator between them was
	 * removed.
	 *
	 * Here, we let the model know about removals. */
	for (i = index_end; i > index_start; i--)
		remove_destination_by_index (name_selector_entry, i);

	/* Do the actual deletion */
	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry),
				  start_pos, end_pos);
	g_signal_stop_emission_by_name (name_selector_entry, "delete_text");

	/* Let model know about changes */
	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!*text) {
		/* If the entry was completely cleared, remove the initial destination too */
		remove_destination_by_index (name_selector_entry, 0);
	} else {
		modify_destination_at_position (name_selector_entry, start_pos);
	}

	/* Prevent type-ahead completion */
	if (name_selector_entry->type_ahead_complete_cb_id) {
		g_source_remove (name_selector_entry->type_ahead_complete_cb_id);
		name_selector_entry->type_ahead_complete_cb_id = 0;
	}

	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
}

static gboolean
completion_match_selected (ENameSelectorEntry *name_selector_entry, GtkTreeModel *model,
			   GtkTreeIter *iter)
{
	EContactStore *contact_store;
	EContact      *contact;
	EDestination  *destination;
	EContactField  matched_field = E_CONTACT_EMAIL_1;
	gint           cursor_pos;
	const gchar   *text;
	gchar         *raw_address;
	GtkTreeIter    contact_iter;

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
							  &contact_iter, iter);

	contact = e_contact_store_get_contact (name_selector_entry->contact_store, &contact_iter);

	/* Find the matching field, in case the user entered a specific e-mail address.
	 * The default is to use the first e-mail. */

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));
	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	raw_address = get_address_at_position (text, cursor_pos);

	if (raw_address)
		contact_match_cue (contact, raw_address, &matched_field, NULL);

	if (matched_field < E_CONTACT_FIRST_EMAIL_ID || matched_field > E_CONTACT_LAST_EMAIL_ID)
		matched_field = E_CONTACT_EMAIL_1;

	/* Set the contact in the model's destination */

	destination = find_destination_at_position (name_selector_entry, cursor_pos);
	e_destination_set_contact (destination, contact, matched_field - E_CONTACT_FIRST_EMAIL_ID);
	sync_destination_at_position (name_selector_entry, cursor_pos, &cursor_pos);

	/* Place cursor at end of address */

	gtk_editable_set_position (GTK_EDITABLE (name_selector_entry), cursor_pos);
	return TRUE;
}

static void
entry_activate (ENameSelectorEntry *name_selector_entry)
{
	gint         cursor_pos;
	gint         range_start, range_end;
	const gchar *text;

	/* Show us what's really there */

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));
	sync_destination_at_position (name_selector_entry, cursor_pos, &cursor_pos);

	/* Place cursor at end of address */

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	get_range_at_position (text, cursor_pos, &range_start, &range_end);
	gtk_editable_set_position (GTK_EDITABLE (name_selector_entry), range_end);
}

static void
setup_contact_store (ENameSelectorEntry *name_selector_entry)
{
	/* Assign the store to the entry completion */

	gtk_entry_completion_set_model (name_selector_entry->entry_completion,
					GTK_TREE_MODEL (name_selector_entry->contact_store));
}

static void
setup_default_contact_store (ENameSelectorEntry *name_selector_entry)
{
	GSList *groups;
	GSList *l;

	/* Create a book for each completion source, and assign them to the contact store */

	groups = e_source_list_peek_groups (name_selector_entry->source_list);

	for (l = groups; l; l = g_slist_next (l)) {
		ESourceGroup *group   = l->data;
		GSList       *sources = e_source_group_peek_sources (group);
		GSList       *m;

		for (m = sources; m; m = g_slist_next (m)) {
			ESource *source = m->data;
			EBook   *book;

			/* TODO: Exclude non-completion sources */

			book = e_book_new (source, NULL);
			e_book_async_open (book, TRUE, NULL, NULL);
			e_contact_store_add_book (name_selector_entry->contact_store, book);
			g_object_unref (book);
		}
	}

	setup_contact_store (name_selector_entry);
}

static void
destination_row_changed (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter)
{
	EDestination *destination;
	const gchar  *text;
	gint          range_start, range_end;
	gint          n;

	n = gtk_tree_path_get_indices (path) [0];
	destination = e_destination_store_get_destination (name_selector_entry->destination_store, iter);

	g_assert (n >= 0);
	g_assert (destination != NULL);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!get_range_by_index (text, n, &range_start, &range_end)) {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		return;
	}

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);
	text = e_destination_get_textrep (destination, FALSE);
	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), text, -1, &range_start);

	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	clear_completion_model (name_selector_entry);
	generate_attribute_list (name_selector_entry);
}

static void
destination_row_inserted (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter)
{
	EDestination *destination;
	const gchar  *text;
	gboolean      comma_before = FALSE;
	gboolean      comma_after  = FALSE;
	gint          range_start, range_end;
	gint          insert_pos;
	gint          n;

	n = gtk_tree_path_get_indices (path) [0];
	destination = e_destination_store_get_destination (name_selector_entry->destination_store, iter);

	g_assert (n >= 0);
	g_assert (destination != NULL);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));

	if (get_range_by_index (text, n, &range_start, &range_end) && range_start != range_end) {
		/* Another destination comes after us */
		insert_pos = range_start;
		comma_after = TRUE;
	} else if (n > 0 && get_range_by_index (text, n - 1, &range_start, &range_end)) {
		/* Another destination comes before us */
		insert_pos = range_end;
		comma_before = TRUE;
	} else if (n == 0) {
		/* We're the sole destination */
		insert_pos = 0;
	} else {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		return;
	}

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	text = e_destination_get_textrep (destination, FALSE);

	if (comma_before)
		gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ", ", -1, &insert_pos);

	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), text, -1, &insert_pos);

	if (comma_after)
		gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ", ", -1, &insert_pos);

	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	clear_completion_model (name_selector_entry);
	generate_attribute_list (name_selector_entry);
}

static void
destination_row_deleted (ENameSelectorEntry *name_selector_entry, GtkTreePath *path)
{
	const gchar *text;
	gboolean     deleted_comma = FALSE;
	gint         range_start, range_end;
	gchar       *p0;
	gint         n;

	n = gtk_tree_path_get_indices (path) [0];
	g_assert (n >= 0);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));

	if (!get_range_by_index (text, n, &range_start, &range_end)) {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		return;
	}

	/* Expand range for deletion forwards */
	for (p0 = g_utf8_offset_to_pointer (text, range_end); *p0;
	     p0 = g_utf8_next_char (p0), range_end++) {
		gunichar c = g_utf8_get_char (p0);

		/* Gobble spaces directly after comma */
		if (c != ' ' && deleted_comma) {
			range_end--;
			break;
		}

		if (c == ',') {
			deleted_comma = TRUE;
			range_end++;
		}
	}

	/* Expand range for deletion backwards */
	for (p0 = g_utf8_offset_to_pointer (text, range_start); range_start > 0;
	     p0 = g_utf8_prev_char (p0), range_start--) {
		gunichar c = g_utf8_get_char (p0);

		if (c == ',') {
			if (!deleted_comma) {
				deleted_comma = TRUE;
				break;
			}

			range_start++;

			/* Leave a space in front; we deleted the comma and spaces before the
			 * following destination */
			p0 = g_utf8_next_char (p0);
			c = g_utf8_get_char (p0);
			if (c == ' ')
				range_start++;

			break;
		}
	}

	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	clear_completion_model (name_selector_entry);
	generate_attribute_list (name_selector_entry);
}

static void
setup_destination_store (ENameSelectorEntry *name_selector_entry)
{
	GtkTreeIter  iter;
	GtkTreePath *path;

	g_signal_connect_swapped (name_selector_entry->destination_store, "row-changed",
				  G_CALLBACK (destination_row_changed), name_selector_entry);
	g_signal_connect_swapped (name_selector_entry->destination_store, "row-deleted",
				  G_CALLBACK (destination_row_deleted), name_selector_entry);
	g_signal_connect_swapped (name_selector_entry->destination_store, "row-inserted",
				  G_CALLBACK (destination_row_inserted), name_selector_entry);

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (name_selector_entry->destination_store), &iter))
		return;

	do {
		GtkTreePath *path;

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (name_selector_entry->destination_store), &iter);
		g_assert (path);

		destination_row_inserted (name_selector_entry, path, &iter);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (name_selector_entry->destination_store), &iter));
}

static void
e_name_selector_entry_init (ENameSelectorEntry *name_selector_entry)
{
  GtkCellRenderer *renderer;

  /* Source list */

  if (!e_book_get_addressbooks (&name_selector_entry->source_list, NULL)) {
	  g_warning ("ENameSelectorEntry can't find any addressbooks!");
	  return;
  }

  /* Edit signals */

  g_signal_connect (name_selector_entry, "insert-text", G_CALLBACK (user_insert_text), name_selector_entry);
  g_signal_connect (name_selector_entry, "delete-text", G_CALLBACK (user_delete_text), name_selector_entry);

  /* Exposition */

  g_signal_connect (name_selector_entry, "expose-event", G_CALLBACK (expose_event), name_selector_entry);

  /* Activation: Complete current entry if possible */

  g_signal_connect (name_selector_entry, "activate", G_CALLBACK (entry_activate), name_selector_entry);

  /* Completion */

  name_selector_entry->entry_completion = gtk_entry_completion_new ();
  gtk_entry_completion_set_match_func (name_selector_entry->entry_completion,
				       (GtkEntryCompletionMatchFunc) completion_match_cb, NULL, NULL);
  g_signal_connect_swapped (name_selector_entry->entry_completion, "match-selected",
			    G_CALLBACK (completion_match_selected), name_selector_entry);

  gtk_entry_set_completion (GTK_ENTRY (name_selector_entry), name_selector_entry->entry_completion);

  /* Completion list name renderer */

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (name_selector_entry->entry_completion),
			      renderer, TRUE);
  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (name_selector_entry->entry_completion),
				 renderer, "text", E_CONTACT_FILE_AS);

  /* Completion store */

  name_selector_entry->contact_store = e_contact_store_new ();
  setup_default_contact_store (name_selector_entry);

  /* Destination store */

  name_selector_entry->destination_store = e_destination_store_new ();
  setup_destination_store (name_selector_entry);
}

ENameSelectorEntry *
e_name_selector_entry_new (void)
{
	  return g_object_new (e_name_selector_entry_get_type (), NULL);
}

EContactStore *
e_name_selector_entry_peek_contact_store (ENameSelectorEntry *name_selector_entry)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry), NULL);

	return name_selector_entry->contact_store;
}

void
e_name_selector_entry_set_contact_store (ENameSelectorEntry *name_selector_entry,
					 EContactStore *contact_store)
{
	g_return_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry));
	g_return_if_fail (E_IS_CONTACT_STORE (contact_store));

	if (contact_store == name_selector_entry->contact_store)
		return;

	g_object_unref (name_selector_entry->contact_store);
	name_selector_entry->contact_store = g_object_ref (contact_store);

	setup_contact_store (name_selector_entry);
}

EDestinationStore *
e_name_selector_entry_peek_destination_store (ENameSelectorEntry *name_selector_entry)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry), NULL);

	return name_selector_entry->destination_store;
}

void
e_name_selector_entry_set_destination_store  (ENameSelectorEntry *name_selector_entry,
					      EDestinationStore *destination_store)
{
	g_return_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry));
	g_return_if_fail (E_IS_DESTINATION_STORE (destination_store));

	if (destination_store == name_selector_entry->destination_store)
		return;

	g_object_unref (name_selector_entry->destination_store);
	name_selector_entry->destination_store = g_object_ref (destination_store);

	setup_destination_store (name_selector_entry);
}
