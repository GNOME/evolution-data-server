/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-entry.c - Single-line text entry widget for EDestinations.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#include <config.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#include <libebook/e-book.h>
#include <libebook/e-contact.h>
#include <libebook/e-destination.h>
#include <libedataserverui/e-book-auth-util.h>
#include "libedataserver/e-sexp.h"

#include "e-name-selector-entry.h"

enum {
	UPDATED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };
static guint COMPLETION_CUE_MIN_LEN = 0;
static gboolean COMPLETION_FORCE_SHOW_ADDRESS = FALSE;
#define ENS_DEBUG(x)

G_DEFINE_TYPE (ENameSelectorEntry, e_name_selector_entry, GTK_TYPE_ENTRY)

typedef struct _ENameSelectorEntryPrivate	ENameSelectorEntryPrivate;
struct _ENameSelectorEntryPrivate
{
	gboolean is_completing;
	GSList *user_query_fields;
};

#define E_NAME_SELECTOR_ENTRY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), E_TYPE_NAME_SELECTOR_ENTRY, ENameSelectorEntryPrivate))

/* 1/3 of the second to wait until invoking autocomplete lookup */
#define AUTOCOMPLETE_TIMEOUT 333

#define re_set_timeout(id,func,ptr)			\
	if (id)						\
		g_source_remove (id);			\
	id = g_timeout_add (AUTOCOMPLETE_TIMEOUT,	\
			    (GSourceFunc) func, ptr);

static void e_name_selector_entry_dispose    (GObject *object);
static void e_name_selector_entry_finalize   (GObject *object);

static void destination_row_inserted (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter);
static void destination_row_changed  (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter);
static void destination_row_deleted  (ENameSelectorEntry *name_selector_entry, GtkTreePath *path);

static void user_insert_text (ENameSelectorEntry *name_selector_entry, gchar *new_text, gint new_text_length, gint *position, gpointer user_data);
static void user_delete_text (ENameSelectorEntry *name_selector_entry, gint start_pos, gint end_pos, gpointer user_data);

static void setup_default_contact_store (ENameSelectorEntry *name_selector_entry);
static void deep_free_list (GList *list);

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

static void
e_name_selector_entry_realize (GtkWidget *widget)
{
	ENameSelectorEntry *name_selector_entry = E_NAME_SELECTOR_ENTRY (widget);

	GTK_WIDGET_CLASS (e_name_selector_entry_parent_class)->realize (widget);

	if (!name_selector_entry->contact_store) {
		setup_default_contact_store (name_selector_entry);
	}
}

/* Partial, repeatable destruction. Release references. */
static void
e_name_selector_entry_dispose (GObject *object)
{
	ENameSelectorEntry *name_selector_entry = E_NAME_SELECTOR_ENTRY (object);
	ENameSelectorEntryPrivate *priv;

	priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

	if (name_selector_entry->entry_completion) {
		g_object_unref (name_selector_entry->entry_completion);
		name_selector_entry->entry_completion = NULL;
	}

	if (name_selector_entry->destination_store) {
		g_object_unref (name_selector_entry->destination_store);
		name_selector_entry->destination_store = NULL;
	}

	if (priv && priv->user_query_fields) {
		g_slist_foreach (priv->user_query_fields, (GFunc)g_free, NULL);
		g_slist_free (priv->user_query_fields);
		priv->user_query_fields = NULL;
	}

	if (G_OBJECT_CLASS (e_name_selector_entry_parent_class)->dispose)
		G_OBJECT_CLASS (e_name_selector_entry_parent_class)->dispose (object);
}

/* Final, one-time destruction. Free all. */
static void
e_name_selector_entry_finalize (GObject *object)
{
	if (G_OBJECT_CLASS (e_name_selector_entry_parent_class)->finalize)
		G_OBJECT_CLASS (e_name_selector_entry_parent_class)->finalize (object);
}

static void
e_name_selector_entry_updated (ENameSelectorEntry *entry, gchar *email)
{
	g_return_if_fail (E_IS_NAME_SELECTOR_ENTRY (entry));
}

static void
e_name_selector_entry_class_init (ENameSelectorEntryClass *name_selector_entry_class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (name_selector_entry_class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (name_selector_entry_class);

	object_class->get_property = e_name_selector_entry_get_property;
	object_class->set_property = e_name_selector_entry_set_property;
	object_class->dispose      = e_name_selector_entry_dispose;
	object_class->finalize     = e_name_selector_entry_finalize;
	name_selector_entry_class->updated	   = e_name_selector_entry_updated;

	widget_class->realize      = e_name_selector_entry_realize;

	/* Install properties */

	/* Install signals */

	signals[UPDATED] = g_signal_new ("updated",
					 E_TYPE_NAME_SELECTOR_ENTRY,
					 G_SIGNAL_RUN_FIRST,
					 G_STRUCT_OFFSET (ENameSelectorEntryClass, updated),
					 NULL,
					 NULL,
					 g_cclosure_marshal_VOID__POINTER,
					 G_TYPE_NONE, 1, G_TYPE_POINTER);

	g_type_class_add_private (object_class, sizeof(ENameSelectorEntryPrivate));
}

/* Remove unquoted commas and control characters from string */
static gchar *
sanitize_string (const gchar *string)
{
	GString     *gstring;
	gboolean     quoted = FALSE;
	const gchar *p;

	gstring = g_string_new ("");

	if (!string)
		return g_string_free (gstring, FALSE);

	for (p = string; *p; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);

		if (c == '"')
			quoted = ~quoted;
		else if (c == ',' && !quoted)
			continue;
		else if (c == '\t' || c == '\n')
			continue;

		g_string_append_unichar (gstring, c);
	}

	return g_string_free (gstring, FALSE);
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
	gboolean     quoted          = FALSE;
	gint         local_start_pos = 0;
	gint         local_end_pos   = 0;
	gint         i;

	if (!string || !*string)
		return FALSE;

	for (p = string, i = 0; *p; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == '"') {
			quoted = ~quoted;
		} else if (c == ',' && !quoted) {
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

static gboolean
is_quoted_at (const gchar *string, gint pos)
{
	const gchar *p;
	gboolean     quoted = FALSE;
	gint         i;

	for (p = string, i = 0; *p && i < pos; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == '"')
			quoted = ~quoted;
	}

	return quoted ? TRUE : FALSE;
}

static gint
get_index_at_position (const gchar *string, gint pos)
{
	const gchar *p;
	gboolean     quoted = FALSE;
	gint         n      = 0;
	gint         i;

	for (p = string, i = 0; *p && i < pos; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == '"')
			quoted = ~quoted;
		else if (c == ',' && !quoted)
			n++;
	}

	return n;
}

static gboolean
get_range_by_index (const gchar *string, gint index, gint *start_pos, gint *end_pos)
{
	const gchar *p;
	gboolean     quoted = FALSE;
	gint         i;
	gint         n = 0;

	for (p = string, i = 0; *p && n < index; p = g_utf8_next_char (p), i++) {
		gunichar c = g_utf8_get_char (p);

		if (c == '"')
			quoted = ~quoted;
		if (c == ',' && !quoted)
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
		/* If we have zero destinations, getting a NULL destination at index 0
		 * is valid. */
		if (index > 0)
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
	gchar   *spaced_str;
	gchar   *comma_str;
	GString *out = g_string_new ("");
	gchar  **strv;
	gchar   *query;

	spaced_str = sanitize_string (value);
	g_strstrip (spaced_str);

	strv = g_strsplit (spaced_str, " ", 0);

	if (strv [0] && strv [1]) {
		g_string_append (out, "(or ");
		comma_str = g_strjoinv (", ", strv);
	} else {
		comma_str = NULL;
	}

	g_string_append (out, " (beginswith ");
	e_sexp_encode_string (out, field);
	e_sexp_encode_string (out, spaced_str);
	g_string_append (out, ")");

	if (comma_str) {
		g_string_append (out, " (beginswith ");

		e_sexp_encode_string (out, field);
		g_strstrip (comma_str);
		e_sexp_encode_string (out, comma_str);
		g_string_append (out, "))");
	}

	query = g_string_free (out, FALSE);

	g_free (spaced_str);
	g_free (comma_str);
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

/**
 * ens_util_populate_user_query_fields:
 *
 * Populates list of user query fields to string usable in query string.
 * Returned pointer is either newly allocated string, supposed to be freed with g_free,
 * or NULL if no fields defined.
 *
 * Since: 2.24
 **/
gchar *
ens_util_populate_user_query_fields (GSList *user_query_fields, const gchar *cue_str, const gchar *encoded_cue_str)
{
	GString *user_fields;
	GSList *s;

	g_return_val_if_fail (cue_str != NULL, NULL);
	g_return_val_if_fail (encoded_cue_str != NULL, NULL);

	user_fields = g_string_new ("");

	for (s = user_query_fields; s; s = s->next) {
		const gchar *field = s->data;

		if (!field || !*field)
			continue;

		if (*field == '$') {
			g_string_append_printf (user_fields, " (beginswith \"%s\" %s) ", field + 1, encoded_cue_str);
		} else if (*field == '@') {
			g_string_append_printf (user_fields, " (is \"%s\" %s) ", field + 1, encoded_cue_str);
		} else {
			gchar *tmp = name_style_query (field, cue_str);

			g_string_append (user_fields, " ");
			g_string_append (user_fields, tmp);
			g_string_append (user_fields, " ");
			g_free (tmp);
		}
	}

	return g_string_free (user_fields, !user_fields->str || !*user_fields->str);
}

static void
set_completion_query (ENameSelectorEntry *name_selector_entry, const gchar *cue_str)
{
	ENameSelectorEntryPrivate *priv;
	EBookQuery *book_query;
	gchar      *query_str;
	gchar      *encoded_cue_str;
	gchar      *full_name_query_str;
	gchar      *file_as_query_str;
	gchar      *user_fields_str;

	priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

	if (!name_selector_entry->contact_store)
		return;

	if (!cue_str) {
		/* Clear the store */
		e_contact_store_set_query (name_selector_entry->contact_store, NULL);
		return;
	}

	encoded_cue_str     = escape_sexp_string (cue_str);
	full_name_query_str = name_style_query ("full_name", cue_str);
	file_as_query_str   = name_style_query ("file_as",   cue_str);
	user_fields_str     = ens_util_populate_user_query_fields (priv->user_query_fields, cue_str, encoded_cue_str);

	query_str = g_strdup_printf ("(or "
				     " (beginswith \"nickname\"  %s) "
				     " (beginswith \"email\"     %s) "
				     " %s "
				     " %s "
				     " %s "
				     ")",
				     encoded_cue_str, encoded_cue_str,
				     full_name_query_str, file_as_query_str,
				     user_fields_str ? user_fields_str : "");

	g_free (user_fields_str);
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
	gint rv;

	rv = g_utf8_collate (s1, s2);

	g_free (s1);
	g_free (s2);

	return rv;
}

static gchar *
build_textrep_for_contact (EContact *contact, EContactField cue_field)
{
	gchar *name  = NULL;
	gchar *email = NULL;
	gchar *textrep;

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
		textrep = g_strdup_printf ("%s <%s>", name, email);
	else
		textrep = g_strdup_printf ("%s", email);

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
		gchar *value_sane;

		/* Don't match e-mail addresses in contact lists */
		if (e_contact_get (contact, E_CONTACT_IS_LIST) &&
		    fields [i] >= E_CONTACT_FIRST_EMAIL_ID &&
		    fields [i] <= E_CONTACT_LAST_EMAIL_ID)
			continue;

		value = e_contact_get (contact, fields [i]);
		if (!value)
			continue;

		value_sane = sanitize_string (value);
		g_free (value);

		ENS_DEBUG (g_print ("Comparing '%s' to '%s'\n", value, cue_str));

		if (!utf8_casefold_collate_len (value_sane, cue_str, cue_len)) {
			if (matched_field)
				*matched_field = fields [i];
			if (matched_field_rank)
				*matched_field_rank = i;

			result = TRUE;
			g_free (value_sane);
			break;
		}
		g_free (value_sane);
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
	EContactField  best_field = 0;
	gint           cue_len;

	g_assert (cue_str);

	if (!name_selector_entry->contact_store)
		return FALSE;

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

		/* Destination will be NULL if we have no entries */
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
	gchar         *temp_str;
	ENameSelectorEntryPrivate *priv;

	priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

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

	temp_str = sanitize_string (textrep);
	g_free (textrep);
	textrep = temp_str;

	textrep_len = g_utf8_strlen (textrep, -1);
	pos         = range_start;

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_changed, name_selector_entry);

	if (textrep_len > range_len) {
		gint i;

		/* keep character's case as user types */
		for (i = 0; textrep [i] && cue_str [i]; i++)
			textrep [i] = cue_str [i];

		gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);
		gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), textrep, -1, &pos);
		gtk_editable_select_region (GTK_EDITABLE (name_selector_entry), range_end,
					    range_start + textrep_len);
		priv->is_completing = TRUE;
	}
	g_free (cue_str);

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
	ENameSelectorEntryPrivate *priv;

	priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

	if (!name_selector_entry->contact_store)
		return;

	e_contact_store_set_query (name_selector_entry->contact_store, NULL);
	priv->is_completing = FALSE;
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
type_ahead_complete_on_timeout_cb (ENameSelectorEntry *name_selector_entry)
{
	type_ahead_complete (name_selector_entry);
	name_selector_entry->type_ahead_complete_cb_id = 0;
	return FALSE;
}

static gboolean
update_completions_on_timeout_cb (ENameSelectorEntry *name_selector_entry)
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
	gboolean      rebuild_attributes = FALSE;

	destination = find_destination_at_position (name_selector_entry, pos);
	if (!destination)
		return;

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

static gchar *
get_destination_textrep (EDestination *destination)
{
	gboolean show_email = COMPLETION_FORCE_SHOW_ADDRESS;
	EContact *contact;

	g_return_val_if_fail (destination != NULL, NULL);

	contact = e_destination_get_contact (destination);

	if (!show_email) {
		if (contact && !e_contact_get (contact, E_CONTACT_IS_LIST)) {
			GList *email_list;

			email_list = e_contact_get (contact, E_CONTACT_EMAIL);
			show_email = g_list_length (email_list) > 1;
			deep_free_list (email_list);
		}
	}

	/* do not show emails for contact lists even user forces it in gconf */
	if (show_email && contact && e_contact_get (contact, E_CONTACT_IS_LIST))
		show_email = FALSE;

	return sanitize_string (e_destination_get_textrep (destination, show_email));
}

static void
sync_destination_at_position (ENameSelectorEntry *name_selector_entry, gint range_pos, gint *cursor_pos)
{
	EDestination *destination;
	const gchar  *text;
	gchar        *address;
	gint          address_len;
	gint          range_start, range_end;

	/* Get the destination we're looking at. Note that the entry may be empty, and so
	 * there may not be one. */
	destination = find_destination_at_position (name_selector_entry, range_pos);
	if (!destination)
		return;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!get_range_at_position (text, range_pos, &range_start, &range_end)) {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		return;
	}

	address = get_destination_textrep (destination);
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
	g_free (address);
}

static void
remove_destination_by_index (ENameSelectorEntry *name_selector_entry, gint index)
{
	EDestination *destination;

	destination = find_destination_by_index (name_selector_entry, index);
	if (destination) {
		g_signal_handlers_block_by_func (name_selector_entry->destination_store,
					 destination_row_deleted, name_selector_entry);
		e_destination_store_remove_destination (name_selector_entry->destination_store,
						destination);
		g_signal_handlers_unblock_by_func (name_selector_entry->destination_store,
					   destination_row_deleted, name_selector_entry);
	}
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

	if (c == ',' && !is_quoted_at (text, *pos)) {
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
	len = g_utf8_strlen (text, -1);
	text = g_utf8_next_char (text);

	if (!*text) {
		/* First and only character so far, create initial destination */
		insert_destination_at_position (name_selector_entry, 0);
	} else {
		/* Modified existing destination */
		modify_destination_at_position (name_selector_entry, *pos);
	}

	/* If editing within the string, we need to regenerate attributes */
	if (*pos < len)
		generate_attribute_list (name_selector_entry);

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

	if (chars_inserted >= 1) {
		/* If the user inserted one character, kick off completion */
		re_set_timeout (name_selector_entry->update_completions_cb_id,  update_completions_on_timeout_cb,  name_selector_entry);
		re_set_timeout (name_selector_entry->type_ahead_complete_cb_id, type_ahead_complete_on_timeout_cb, name_selector_entry);
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
	gint	     selection_start, selection_end;
	gunichar     str_context [2], str_b_context [2];
	gint         len;
	gint         i;
	gboolean     already_selected = FALSE, del_space = FALSE, del_comma = FALSE;

	if (start_pos == end_pos)
		return;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	len = g_utf8_strlen (text, -1);

	if (gtk_editable_get_selection_bounds (GTK_EDITABLE (name_selector_entry),
					       &selection_start,
					       &selection_end))
		if ((g_utf8_get_char (g_utf8_offset_to_pointer (text, selection_end)) == 0) ||
		    (g_utf8_get_char (g_utf8_offset_to_pointer (text, selection_end)) == ','))
			already_selected = TRUE;

	get_utf8_string_context (text, start_pos, str_context, 2);
	get_utf8_string_context (text, end_pos, str_b_context, 2);

	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	if (end_pos - start_pos == 1) {
		/* Might be backspace; update completion model so dropdown is accurate */
		re_set_timeout (name_selector_entry->update_completions_cb_id, update_completions_on_timeout_cb, name_selector_entry);
	}

	index_start = get_index_at_position (text, start_pos);
	index_end   = get_index_at_position (text, end_pos);

	g_signal_stop_emission_by_name (name_selector_entry, "delete_text");

	/* If the deletion touches more than one destination, the first one is changed
	 * and the rest are removed. If the last destination wasn't completely deleted,
	 * it becomes part of the first one, since the separator between them was
	 * removed.
	 *
	 * Here, we let the model know about removals. */
	for (i = index_end; i > index_start; i--) {
		EDestination *destination = find_destination_by_index (name_selector_entry, i);
		gint range_start, range_end;
		gchar *ttext;
		const gchar *email=NULL;
		gboolean sel=FALSE;

		if (destination)
			email = e_destination_get_address (destination);

		if (!email || !*email)
			continue;

		if (!get_range_by_index (text, i, &range_start, &range_end)) {
			g_warning ("ENameSelectorEntry is out of sync with model!");
			return;
		}

		if ((selection_start < range_start && selection_end > range_start) ||
		    (selection_end > range_start && selection_end < range_end))
			sel=TRUE;

		if (!sel) {
			g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
			g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

			gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);

			ttext = sanitize_string (email);
			gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ttext, -1, &range_start);
			g_free (ttext);

			g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
			g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

		}

		remove_destination_by_index (name_selector_entry, i);
	}

	/* Do the actual deletion */

	if (end_pos == start_pos +1 &&  index_end == index_start) {
		/* We could be just deleting the empty text */
		gchar *c;

		/* Get the actual deleted text */
		c = gtk_editable_get_chars (GTK_EDITABLE (name_selector_entry), start_pos, start_pos+1);

		if ( c[0] == ' ') {
			/* If we are at the beginning or removing junk space, let us ignore it */
			del_space = TRUE;
		}
	} else	if (end_pos == start_pos +1 &&  index_end == index_start+1) {
		/* We could be just deleting the empty text */
		gchar *c;

		/* Get the actual deleted text */
		c = gtk_editable_get_chars (GTK_EDITABLE (name_selector_entry), start_pos, start_pos+1);

		if ( c[0] == ',' && !is_quoted_at (text, start_pos)) {
			/* If we are at the beginning or removing junk space, let us ignore it */
			del_comma = TRUE;
		}
	}

	if (del_comma) {
		gint range_start=-1, range_end;
		EDestination *dest = find_destination_by_index (name_selector_entry, index_end);
		/* If we have deleted the last comma, let us autocomplete normally
		 */

		if (dest && len - end_pos  != 0) {

			EDestination *destination1  = find_destination_by_index (name_selector_entry, index_start);
			gchar *ttext;
			const gchar *email=NULL;

			if (destination1)
				email = e_destination_get_address (destination1);

			if (email && *email) {

				if (!get_range_by_index (text, i, &range_start, &range_end)) {
					g_warning ("ENameSelectorEntry is out of sync with model!");
					return;
				}

				g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
				g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

				gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);

				ttext = sanitize_string (email);
				gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ttext, -1, &range_start);
				g_free (ttext);

				g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
				g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);
			}

			if (range_start != -1) {
				start_pos = range_start;
				end_pos = start_pos+1;
				gtk_editable_set_position (GTK_EDITABLE (name_selector_entry),start_pos);
			}
		}
	}
	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry),
				  start_pos, end_pos);

	/*If the user is deleting a '"' new destinations have to be created for ',' between the quoted text
	 Like "fd,ty,uy" is a one entity, but if you remove the quotes it has to be broken doan into 3 seperate
	 addresses.
	*/

	if (str_b_context [1] == '"') {
		const gchar *p;
		gint j;
		p = text + end_pos;
		for (p = text + (end_pos-1), j = end_pos - 1; *p && *p != '"' ; p = g_utf8_next_char (p), j++) {
			gunichar c = g_utf8_get_char (p);
			if (c == ',') {
				insert_destination_at_position (name_selector_entry, j+1);
			}
		}

	}

	/* Let model know about changes */
	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!*text || strlen(text) <= 0) {
		/* If the entry was completely cleared, remove the initial destination too */
		remove_destination_by_index (name_selector_entry, 0);
		generate_attribute_list (name_selector_entry);
	} else  if (!del_space) {
		modify_destination_at_position (name_selector_entry, start_pos);
	}

	/* If editing within the string, we need to regenerate attributes */
	if (end_pos < len)
		generate_attribute_list (name_selector_entry);

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
	EContact      *contact;
	EDestination  *destination;
	gint           cursor_pos;
	GtkTreeIter    generator_iter;
	GtkTreeIter    contact_iter;
	gint           email_n;

	if (!name_selector_entry->contact_store)
		return FALSE;

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
							  &generator_iter, iter);
	e_tree_model_generator_convert_iter_to_child_iter (name_selector_entry->email_generator,
							   &contact_iter, &email_n,
							   &generator_iter);

	contact = e_contact_store_get_contact (name_selector_entry->contact_store, &contact_iter);
	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));

	/* Set the contact in the model's destination */

	destination = find_destination_at_position (name_selector_entry, cursor_pos);
	e_destination_set_contact (destination, contact, email_n);
	sync_destination_at_position (name_selector_entry, cursor_pos, &cursor_pos);
	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ",", -1, &cursor_pos);

	/* Place cursor at end of address */

	gtk_editable_set_position (GTK_EDITABLE (name_selector_entry), cursor_pos);
	g_signal_emit (name_selector_entry, signals[UPDATED], 0, destination, NULL);
	return TRUE;
}

static void
entry_activate (ENameSelectorEntry *name_selector_entry)
{
	gint         cursor_pos;
	gint         range_start, range_end;
	ENameSelectorEntryPrivate *priv;
	EDestination  *destination;
	gint           range_len;
	const gchar   *text;
	gchar         *cue_str;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));
	if (cursor_pos < 0)
		return;

	priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!get_range_at_position (text, cursor_pos, &range_start, &range_end))
		return;

	range_len = range_end - range_start;
	if (range_len < COMPLETION_CUE_MIN_LEN)
		return;

	destination = find_destination_at_position (name_selector_entry, cursor_pos);
	if (!destination)
		return;

	cue_str = get_entry_substring (name_selector_entry, range_start, range_end);
#if 0
	if (!find_existing_completion (name_selector_entry, cue_str, &contact,
				       &textrep, &matched_field)) {
		g_free (cue_str);
		return;
	}
#endif
	g_free (cue_str);
	sync_destination_at_position (name_selector_entry, cursor_pos, &cursor_pos);

	/* Place cursor at end of address */
	get_range_at_position (text, cursor_pos, &range_start, &range_end);

	if (priv->is_completing) {
		gchar *str_context=NULL;

		str_context = gtk_editable_get_chars (GTK_EDITABLE (name_selector_entry), range_end, range_end+1);

		if (str_context[0] != ',') {
			/* At the end*/
			gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ", ", -1, &range_end);
		} else {
			/* In the middle */
			gint newpos = strlen (text);

                        /* Doing this we can make sure that It wont ask for completion again. */
			gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ", ", -1, &newpos);
			g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);
			gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), newpos-2, newpos);
			g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);

			/* Move it close to next destination*/
			range_end = range_end+2;

		}
	}

	gtk_editable_set_position (GTK_EDITABLE (name_selector_entry), range_end);
	g_signal_emit (name_selector_entry, signals[UPDATED], 0, destination, NULL);

	if (priv->is_completing)
		clear_completion_model (name_selector_entry);
}

static void
update_text (ENameSelectorEntry *name_selector_entry, const gchar *text)
{
	gint start = 0, end = 0;
	gboolean has_selection;

	has_selection = gtk_editable_get_selection_bounds (GTK_EDITABLE (name_selector_entry), &start, &end);

	gtk_entry_set_text (GTK_ENTRY (name_selector_entry), text);

	if (has_selection)
		gtk_editable_select_region (GTK_EDITABLE (name_selector_entry), start, end);
}

static void
sanitize_entry (ENameSelectorEntry *name_selector_entry)
{
	gint n;
	GList *l, *known, *del = NULL;
	GString *str = g_string_new ("");

	g_signal_handlers_block_matched (name_selector_entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);
	g_signal_handlers_block_matched (name_selector_entry->destination_store, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);

	known = e_destination_store_list_destinations (name_selector_entry->destination_store);
	for (l = known, n = 0; l != NULL; l = l->next, n++) {
		EDestination *dest = l->data;

		if (!dest || !e_destination_get_address (dest))
			del = g_list_prepend (del, GINT_TO_POINTER (n));
		else {
			gchar *text;

			text = get_destination_textrep (dest);
			if (text) {
				if (str->str && str->str[0])
					g_string_append (str, ", ");

				g_string_append (str, text);
			}
			g_free (text);
		}
	}
	g_list_free (known);

	for (l = del; l != NULL; l = l->next) {
		e_destination_store_remove_destination_nth (name_selector_entry->destination_store, GPOINTER_TO_INT (l->data));
	}
	g_list_free (del);

	update_text (name_selector_entry, str->str);

	g_string_free (str, TRUE);

	g_signal_handlers_unblock_matched (name_selector_entry->destination_store, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);
	g_signal_handlers_unblock_matched (name_selector_entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);

	generate_attribute_list (name_selector_entry);
}

static gboolean
user_focus_in (ENameSelectorEntry *name_selector_entry, GdkEventFocus *event_focus)
{
	gint n;
	GList *l, *known;
	GString *str = g_string_new ("");
	EDestination *dest_dummy = e_destination_new ();

	g_signal_handlers_block_matched (name_selector_entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);
	g_signal_handlers_block_matched (name_selector_entry->destination_store, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);

	known = e_destination_store_list_destinations (name_selector_entry->destination_store);
	for (l = known, n = 0; l != NULL; l = l->next, n++) {
		EDestination *dest = l->data;

		if (dest) {
			gchar *text;

			text = get_destination_textrep (dest);
			if (text) {
				if (str->str && str->str[0])
					g_string_append (str, ", ");

				g_string_append (str, text);
			}
			g_free (text);
		}
	}
	g_list_free (known);

	/* Add a blank destination */
	e_destination_store_append_destination (name_selector_entry->destination_store, dest_dummy);
	if (str->str && str->str[0])
		g_string_append (str, ", ");

	gtk_entry_set_text (GTK_ENTRY (name_selector_entry), str->str);

	g_string_free (str, TRUE);

	g_signal_handlers_unblock_matched (name_selector_entry->destination_store, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);
	g_signal_handlers_unblock_matched (name_selector_entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, name_selector_entry);

	generate_attribute_list (name_selector_entry);

	return FALSE;
}

static gboolean
user_focus_out (ENameSelectorEntry *name_selector_entry, GdkEventFocus *event_focus)
{
	ENameSelectorEntryPrivate *priv;

	priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

	if (!event_focus->in && priv->is_completing) {
		entry_activate (name_selector_entry);
	}

	if (name_selector_entry->type_ahead_complete_cb_id) {
		g_source_remove (name_selector_entry->type_ahead_complete_cb_id);
		name_selector_entry->type_ahead_complete_cb_id = 0;
	}

	if (name_selector_entry->update_completions_cb_id) {
		g_source_remove (name_selector_entry->update_completions_cb_id);
		name_selector_entry->update_completions_cb_id = 0;
	}

	clear_completion_model (name_selector_entry);

	if (!event_focus->in) {
		sanitize_entry (name_selector_entry);
	}

	return FALSE;
}

static void
deep_free_list (GList *list)
{
	GList *l;

	for (l = list; l; l = g_list_next (l))
		g_free (l->data);

	g_list_free (list);
}

/* Given a widget, determines the height that text will normally be drawn. */
static guint
entry_height (GtkWidget *widget)
{
	PangoLayout *layout;
	gint bound;

	g_return_val_if_fail (widget != NULL, 0);

	layout = gtk_widget_create_pango_layout (widget, NULL);

	pango_layout_get_pixel_size (layout, NULL, &bound);

	return bound;
}

static void
contact_layout_pixbuffer (GtkCellLayout *cell_layout, GtkCellRenderer *cell, GtkTreeModel *model,
			  GtkTreeIter *iter, ENameSelectorEntry *name_selector_entry)
{
	EContact      *contact;
	GtkTreeIter    generator_iter;
	GtkTreeIter    contact_store_iter;
	gint           email_n;
	EContactPhoto *photo;
	GdkPixbuf *pixbuf = NULL;

	if (!name_selector_entry->contact_store)
		return;

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
							  &generator_iter, iter);
	e_tree_model_generator_convert_iter_to_child_iter (name_selector_entry->email_generator,
							   &contact_store_iter, &email_n,
							   &generator_iter);

	contact = e_contact_store_get_contact (name_selector_entry->contact_store, &contact_store_iter);
	if (!contact) {
		g_object_set (cell, "pixbuf", pixbuf, NULL);
		return;
	}

	photo =  e_contact_get (contact, E_CONTACT_PHOTO);
	if (photo && photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		guint max_height = entry_height (GTK_WIDGET (name_selector_entry));
		GdkPixbufLoader *loader;

		loader = gdk_pixbuf_loader_new ();
		if (gdk_pixbuf_loader_write (loader, (guchar *)photo->data.inlined.data, photo->data.inlined.length, NULL) &&
		    gdk_pixbuf_loader_close (loader, NULL)) {
			pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
			if (pixbuf)
				g_object_ref (pixbuf);
		}
		g_object_unref (loader);

		if (pixbuf) {
			gint w, h;
			gdouble scale = 1.0;

			w = gdk_pixbuf_get_width (pixbuf);
			h = gdk_pixbuf_get_height (pixbuf);

			if (h > w)
				scale = max_height / (double) h;
			else
				scale = max_height / (double) w;

			if (scale < 1.0) {
				GdkPixbuf *tmp;

				tmp = gdk_pixbuf_scale_simple (pixbuf, w * scale, h * scale, GDK_INTERP_BILINEAR);
				g_object_unref (pixbuf);
				pixbuf = tmp;
			}

		}
	}

	e_contact_photo_free (photo);

	g_object_set (cell, "pixbuf", pixbuf, NULL);

	if (pixbuf)
		g_object_unref (pixbuf);
}

static void
contact_layout_formatter (GtkCellLayout *cell_layout, GtkCellRenderer *cell, GtkTreeModel *model,
			  GtkTreeIter *iter, ENameSelectorEntry *name_selector_entry)
{
	EContact      *contact;
	GtkTreeIter    generator_iter;
	GtkTreeIter    contact_store_iter;
	GList         *email_list;
	gchar         *string;
	gchar         *file_as_str;
	gchar         *email_str;
	gint           email_n;

	if (!name_selector_entry->contact_store)
		return;

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
							  &generator_iter, iter);
	e_tree_model_generator_convert_iter_to_child_iter (name_selector_entry->email_generator,
							   &contact_store_iter, &email_n,
							   &generator_iter);

	contact = e_contact_store_get_contact (name_selector_entry->contact_store, &contact_store_iter);
	email_list = e_contact_get (contact, E_CONTACT_EMAIL);
	email_str = g_list_nth_data (email_list, email_n);
	file_as_str = e_contact_get (contact, E_CONTACT_FILE_AS);

	if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
		string = g_strdup_printf ("%s", file_as_str ? file_as_str : "?");
	} else {
		string = g_strdup_printf ("%s%s<%s>", file_as_str ? file_as_str : "",
					  file_as_str ? " " : "",
					  email_str ? email_str : "");
	}

	g_free (file_as_str);
	deep_free_list (email_list);

	g_object_set (cell, "text", string, NULL);
	g_free (string);
}

static gint
generate_contact_rows (EContactStore *contact_store, GtkTreeIter *iter,
		       ENameSelectorEntry *name_selector_entry)
{
	EContact    *contact;
	const gchar *contact_uid;
	GList       *email_list;
	gint         n_rows;

	contact = e_contact_store_get_contact (contact_store, iter);
	g_assert (contact != NULL);

	contact_uid = e_contact_get_const (contact, E_CONTACT_UID);
	if (!contact_uid)
		return 0;  /* Can happen with broken databases */

	if (e_contact_get (contact, E_CONTACT_IS_LIST))
		return 1;

	email_list = e_contact_get (contact, E_CONTACT_EMAIL);
	n_rows = g_list_length (email_list);
	deep_free_list (email_list);

	return n_rows;
}

static void
ensure_type_ahead_complete_on_timeout (ENameSelectorEntry *name_selector_entry)
{
	re_set_timeout (name_selector_entry->type_ahead_complete_cb_id, type_ahead_complete_on_timeout_cb, name_selector_entry);
}

static void
setup_contact_store (ENameSelectorEntry *name_selector_entry)
{
	if (name_selector_entry->email_generator) {
		g_object_unref (name_selector_entry->email_generator);
		name_selector_entry->email_generator = NULL;
	}

	if (name_selector_entry->contact_store) {
		name_selector_entry->email_generator =
			e_tree_model_generator_new (GTK_TREE_MODEL (name_selector_entry->contact_store));

		e_tree_model_generator_set_generate_func (name_selector_entry->email_generator,
							  (ETreeModelGeneratorGenerateFunc) generate_contact_rows,
							  name_selector_entry, NULL);

		/* Assign the store to the entry completion */

		gtk_entry_completion_set_model (name_selector_entry->entry_completion,
						GTK_TREE_MODEL (name_selector_entry->email_generator));

		/* Set up callback for incoming matches */
		g_signal_connect_swapped (name_selector_entry->contact_store, "row-inserted",
					  G_CALLBACK (ensure_type_ahead_complete_on_timeout), name_selector_entry);
	} else {
		/* Remove the store from the entry completion */

		gtk_entry_completion_set_model (name_selector_entry->entry_completion, NULL);
	}
}

static void
setup_default_contact_store (ENameSelectorEntry *name_selector_entry)
{
	GSList *groups;
	GSList *l;

	g_return_if_fail (name_selector_entry->contact_store == NULL);

	/* Create a book for each completion source, and assign them to the contact store */

	name_selector_entry->contact_store = e_contact_store_new ();
	groups = e_source_list_peek_groups (name_selector_entry->source_list);

	for (l = groups; l; l = g_slist_next (l)) {
		ESourceGroup *group   = l->data;
		GSList       *sources = e_source_group_peek_sources (group);
		GSList       *m;

		for (m = sources; m; m = g_slist_next (m)) {
			ESource     *source = m->data;
			EBook       *book;
			const gchar *completion;

			/* Skip non-completion sources */
			completion = e_source_get_property (source, "completion");
			if (!completion || g_ascii_strcasecmp (completion, "true"))
				continue;

			book = e_load_book_source (source, NULL, NULL);
			if (!book)
				continue;

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
	const gchar  *entry_text;
	gchar        *text;
	gint          range_start, range_end;
	gint          n;

	n = gtk_tree_path_get_indices (path) [0];
	destination = e_destination_store_get_destination (name_selector_entry->destination_store, iter);

	if (!destination)
		return;

	g_assert (n >= 0);

	entry_text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	if (!get_range_by_index (entry_text, n, &range_start, &range_end)) {
		g_warning ("ENameSelectorEntry is out of sync with model!");
		return;
	}

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), range_start, range_end);

	text = get_destination_textrep (destination);
	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), text, -1, &range_start);
	g_free (text);

	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);

	clear_completion_model (name_selector_entry);
	generate_attribute_list (name_selector_entry);
}

static void
destination_row_inserted (ENameSelectorEntry *name_selector_entry, GtkTreePath *path, GtkTreeIter *iter)
{
	EDestination *destination;
	const gchar  *entry_text;
	gchar        *text;
	gboolean      comma_before = FALSE;
	gboolean      comma_after  = FALSE;
	gint          range_start, range_end;
	gint          insert_pos;
	gint          n;

	n = gtk_tree_path_get_indices (path) [0];
	destination = e_destination_store_get_destination (name_selector_entry->destination_store, iter);

	g_assert (n >= 0);
	g_assert (destination != NULL);

	entry_text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));

	if (get_range_by_index (entry_text, n, &range_start, &range_end) && range_start != range_end) {
		/* Another destination comes after us */
		insert_pos = range_start;
		comma_after = TRUE;
	} else if (n > 0 && get_range_by_index (entry_text, n - 1, &range_start, &range_end)) {
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

	if (comma_before)
		gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), ", ", -1, &insert_pos);

	text = get_destination_textrep (destination);
	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), text, -1, &insert_pos);
	g_free (text);

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

static gboolean
prepare_popup_destination (ENameSelectorEntry *name_selector_entry, GdkEventButton *event_button)
{
	EDestination *destination;
	PangoLayout  *layout;
	gint          layout_offset_x;
	gint          layout_offset_y;
	gint          x, y;
	gint          index;

	if (event_button->type != GDK_BUTTON_PRESS)
		return FALSE;

	if (event_button->button != 3)
		return FALSE;

	if (name_selector_entry->popup_destination) {
		g_object_unref (name_selector_entry->popup_destination);
		name_selector_entry->popup_destination = NULL;
	}

	gtk_entry_get_layout_offsets (GTK_ENTRY (name_selector_entry),
				      &layout_offset_x, &layout_offset_y);
	x = (event_button->x + 0.5) - layout_offset_x;
	y = (event_button->y + 0.5) - layout_offset_y;

	if (x < 0 || y < 0)
		return FALSE;

	layout = gtk_entry_get_layout (GTK_ENTRY (name_selector_entry));
	if (!pango_layout_xy_to_index (layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, NULL))
		return FALSE;

	index = gtk_entry_layout_index_to_text_index (GTK_ENTRY (name_selector_entry), index);
	destination = find_destination_at_position (name_selector_entry, index);
	/* FIXME: Add this to a private variable, in ENameSelectorEntry Class*/
	g_object_set_data ((GObject *)name_selector_entry, "index", GINT_TO_POINTER(index));

	if (!destination || !e_destination_get_contact (destination))
		return FALSE;

	/* TODO: Unref destination when we finalize */
	name_selector_entry->popup_destination = g_object_ref (destination);
	return FALSE;
}

static EBook *
find_book_by_contact (GList *books, const gchar *contact_uid)
{
	GList *l;

	for (l = books; l; l = g_list_next (l)) {
		EBook    *book = l->data;
		EContact *contact;
		gboolean  result;

		result = e_book_get_contact (book, contact_uid, &contact, NULL);
		if (contact)
			g_object_unref (contact);

		if (result)
			return book;
	}

	return NULL;
}

static void
editor_closed_cb (GtkObject *editor, gpointer data)
{
	EContact *contact;
	gchar *contact_uid;
	EDestination *destination;
	GList *books;
	EBook *book;
	gboolean result;
	gint email_num;
	ENameSelectorEntry *name_selector_entry = E_NAME_SELECTOR_ENTRY (data);

	destination = name_selector_entry->popup_destination;
	contact = e_destination_get_contact (destination);
	if (!contact)
		return;
	contact_uid = e_contact_get (contact, E_CONTACT_UID);
	if (!contact_uid)
		return;

	if (name_selector_entry->contact_store) {
		books = e_contact_store_get_books (name_selector_entry->contact_store);
		book = find_book_by_contact (books, contact_uid);
		g_list_free (books);
	} else {
		book = NULL;
	}
	if (!book)
		return;

	result = e_book_get_contact(book, contact_uid, &contact, NULL);
	email_num = e_destination_get_email_num(destination);
	e_destination_set_contact (destination, contact, email_num);

	g_free (contact_uid);
	g_object_unref (contact);
	g_object_unref (editor);
	g_object_unref (name_selector_entry);
}

static void
popup_activate_inline_expand (ENameSelectorEntry *name_selector_entry, GtkWidget *menu_item)
{
	const gchar *email_list, *text;
	gchar *sanitized_text;
	EDestination *destination = name_selector_entry->popup_destination;
	gint position, start, end;

	position = GPOINTER_TO_INT(g_object_get_data ((GObject *)name_selector_entry, "index"));

	email_list = e_destination_get_address(destination);
	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));
	get_range_at_position (text, position, &start, &end);

	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), start, end);

	sanitized_text = sanitize_string (email_list);
	gtk_editable_insert_text (GTK_EDITABLE (name_selector_entry), sanitized_text, -1, &start);
	g_free (sanitized_text);

	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	clear_completion_model (name_selector_entry);
	generate_attribute_list (name_selector_entry);

}

static void
popup_activate_contact (ENameSelectorEntry *name_selector_entry, GtkWidget *menu_item)
{
	EBook        *book;
	GList        *books;
	EDestination *destination;
	EContact     *contact;
	gchar        *contact_uid;

	destination = name_selector_entry->popup_destination;
	if (!destination)
		return;

	contact = e_destination_get_contact (destination);
	if (!contact)
		return;

	contact_uid = e_contact_get (contact, E_CONTACT_UID);
	if (!contact_uid)
		return;
	if (name_selector_entry->contact_store) {
		books = e_contact_store_get_books (name_selector_entry->contact_store);
		/*FIXME: read URI from contact and get the book ?*/
		book = find_book_by_contact (books, contact_uid);
		g_list_free (books);
		g_free (contact_uid);
	} else {
		book = NULL;
	}

	if (!book)
		return;

	if (e_destination_is_evolution_list (destination)) {
		GtkWidget *contact_list_editor;

		if (!name_selector_entry->contact_list_editor_func)
			return;

		contact_list_editor = (*name_selector_entry->contact_list_editor_func) (book, contact, FALSE, TRUE);
		g_object_ref (name_selector_entry);
		g_signal_connect (contact_list_editor, "editor_closed",
				  G_CALLBACK (editor_closed_cb), name_selector_entry);
	} else {
		GtkWidget *contact_editor;

		if (!name_selector_entry->contact_editor_func)
			return;

		contact_editor = (*name_selector_entry->contact_editor_func) (book, contact, FALSE, TRUE);
		g_object_ref (name_selector_entry);
		g_signal_connect (contact_editor, "editor_closed",
				  G_CALLBACK (editor_closed_cb), name_selector_entry);
	}
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
popup_activate_cut (ENameSelectorEntry *name_selector_entry, GtkWidget *menu_item)
{
	EDestination *destination;
	const gchar *contact_email;
	gchar *pemail = NULL;
	GtkClipboard *clipboard;

	destination = name_selector_entry->popup_destination;
	contact_email =e_destination_get_address(destination);

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
	pemail = g_strconcat (contact_email, ",", NULL);
	gtk_clipboard_set_text (clipboard, pemail, strlen (pemail));

	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, pemail, strlen (pemail));

	gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), 0, 0);
	e_destination_store_remove_destination (name_selector_entry->destination_store, destination);

	g_free (pemail);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);
}

static void
popup_activate_copy (ENameSelectorEntry *name_selector_entry, GtkWidget *menu_item)
{
	EDestination *destination;
	const gchar *contact_email;
	gchar *pemail;
	GtkClipboard *clipboard;

	destination = name_selector_entry->popup_destination;
	contact_email = e_destination_get_address(destination);

	g_signal_handlers_block_by_func (name_selector_entry, user_insert_text, name_selector_entry);
	g_signal_handlers_block_by_func (name_selector_entry, user_delete_text, name_selector_entry);

	clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
	pemail = g_strconcat (contact_email, ",", NULL);
	gtk_clipboard_set_text (clipboard, pemail, strlen (pemail));

	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, pemail, strlen (pemail));
	g_free (pemail);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_delete_text, name_selector_entry);
	g_signal_handlers_unblock_by_func (name_selector_entry, user_insert_text, name_selector_entry);
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

static void
populate_popup (ENameSelectorEntry *name_selector_entry, GtkMenu *menu)
{
	EDestination *destination;
	EContact     *contact;
	GtkWidget    *menu_item;
	GList        *email_list=NULL;
	GList        *l;
	gint          i;
	gchar	     *edit_label;
	gchar	     *cut_label;
	gchar         *copy_label;
	gint	      email_num, len;
	GSList	     *group = NULL;
	gboolean      is_list;
	gboolean      show_menu = FALSE;

	destination = name_selector_entry->popup_destination;
	if (!destination)
		return;

	contact = e_destination_get_contact (destination);
	if (!contact)
		return;

	/* Prepend the menu items, backwards */

	/* Separator */

	menu_item = gtk_separator_menu_item_new ();
	gtk_widget_show (menu_item);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	email_num = e_destination_get_email_num (destination);

	/* Addresses */
	is_list = e_contact_get (contact, E_CONTACT_IS_LIST) ? TRUE : FALSE;
	if (is_list) {
		const GList *dests = e_destination_list_get_dests (destination);
		GList *iter;
		gint length = g_list_length ((GList *)dests);

		for (iter = (GList *)dests; iter; iter = iter->next) {
			EDestination *dest = (EDestination *) iter->data;
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
	}

	/* Separator */

	if (show_menu) {
		menu_item = gtk_separator_menu_item_new ();
		gtk_widget_show (menu_item);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	}

	/* Expand a list inline */
	if (is_list) {
		/* To Translators: This would be similiar to "Expand MyList Inline" where MyList is a Contact List*/
		edit_label = g_strdup_printf (_("E_xpand %s Inline"), (gchar *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
		menu_item = gtk_menu_item_new_with_mnemonic (edit_label);
		g_free (edit_label);
		gtk_widget_show (menu_item);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
		g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (popup_activate_inline_expand),
					  name_selector_entry);

		/* Separator */
		menu_item = gtk_separator_menu_item_new ();
		gtk_widget_show (menu_item);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	}

	/* Copy Contact Item */
	copy_label = g_strdup_printf (_("Cop_y %s"), (gchar *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
	menu_item = gtk_menu_item_new_with_mnemonic (copy_label);
	g_free (copy_label);
	gtk_widget_show (menu_item);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);

	g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (popup_activate_copy),
				  name_selector_entry);

	/* Cut Contact Item */
	cut_label = g_strdup_printf (_("C_ut %s"), (gchar *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
	menu_item = gtk_menu_item_new_with_mnemonic (cut_label);
	g_free (cut_label);
	gtk_widget_show (menu_item);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);

	g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (popup_activate_cut),
				  name_selector_entry);

	if (show_menu) {
		menu_item = gtk_separator_menu_item_new ();
		gtk_widget_show (menu_item);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	}

	/* Edit Contact item */

	edit_label = g_strdup_printf (_("_Edit %s"), (gchar *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
	menu_item = gtk_menu_item_new_with_mnemonic (edit_label);
	g_free (edit_label);
	gtk_widget_show (menu_item);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);

	g_signal_connect_swapped (menu_item, "activate", G_CALLBACK (popup_activate_contact),
				  name_selector_entry);

	deep_free_list (email_list);
}

static void
copy_or_cut_clipboard (ENameSelectorEntry *name_selector_entry, gboolean is_cut)
{
	gint i, start = 0, end = 0;
	const gchar *text;
	GHashTable *hash;
	GHashTableIter iter;
	gpointer key, value;
	GString *addresses;

	text = gtk_entry_get_text (GTK_ENTRY (name_selector_entry));

	if (!gtk_editable_get_selection_bounds (GTK_EDITABLE (name_selector_entry), &start, &end)) {
		start = gtk_editable_get_position (GTK_EDITABLE (name_selector_entry));
		end = start;
	}

	/* do nothing when there is nothing selected */
	if (start == end)
		return;

	hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (i = start; i <= end; i++) {
		gint index = get_index_at_position (text, i);

		g_hash_table_insert (hash, GINT_TO_POINTER (index), NULL);
	}

	addresses = g_string_new ("");

	g_hash_table_iter_init (&iter, hash);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		gint index = GPOINTER_TO_INT (key);
		EDestination *dest;
		gint rstart, rend;

		if (!get_range_by_index (text, index, &rstart, &rend))
			continue;

		if (rstart < start) {
			if (addresses->str && *addresses->str)
				g_string_append (addresses, ", ");

			g_string_append_len (addresses, text + start, rend - start);
		} else if (rend > end) {
			if (addresses->str && *addresses->str)
				g_string_append (addresses, ", ");

			g_string_append_len (addresses, text + rstart, end - rstart);
		} else {
			/* the contact is whole selected */
			dest = find_destination_by_index (name_selector_entry, index);
			if (dest && e_destination_get_address (dest)) {
				if (addresses->str && *addresses->str)
					g_string_append (addresses, ", ");

				g_string_append (addresses, e_destination_get_address (dest));

				/* store the 'dest' as a value for the index */
				g_hash_table_insert (hash, GINT_TO_POINTER (index), dest);
			} else
				g_string_append_len (addresses, text + rstart, rend - rstart);
		}
	}

	if (is_cut)
		gtk_editable_delete_text (GTK_EDITABLE (name_selector_entry), start, end);

	g_hash_table_unref (hash);

	gtk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (name_selector_entry), GDK_SELECTION_CLIPBOARD), addresses->str, -1);

	g_string_free (addresses, TRUE);
}

static void
copy_clipboard (GtkEntry *entry, ENameSelectorEntry *name_selector_entry)
{
	copy_or_cut_clipboard (name_selector_entry, FALSE);
	g_signal_stop_emission_by_name (entry, "copy-clipboard");
}

static void
cut_clipboard (GtkEntry *entry, ENameSelectorEntry *name_selector_entry)
{
	copy_or_cut_clipboard (name_selector_entry, TRUE);
	g_signal_stop_emission_by_name (entry, "cut-clipboard");
}

static void
e_name_selector_entry_init (ENameSelectorEntry *name_selector_entry)
{
  GtkCellRenderer *renderer;
  ENameSelectorEntryPrivate *priv;
  GConfClient *gconf;

  priv = E_NAME_SELECTOR_ENTRY_GET_PRIVATE (name_selector_entry);

  /* Source list */

  if (!e_book_get_addressbooks (&name_selector_entry->source_list, NULL)) {
	  g_warning ("ENameSelectorEntry can't find any addressbooks!");
	  return;
  }

  /* read minimum_query_length from gconf*/
  gconf = gconf_client_get_default();
  if (COMPLETION_CUE_MIN_LEN == 0) {
	  if ((COMPLETION_CUE_MIN_LEN = gconf_client_get_int (gconf, MINIMUM_QUERY_LENGTH, NULL)))
		;
	  else COMPLETION_CUE_MIN_LEN = 3;
  }
  COMPLETION_FORCE_SHOW_ADDRESS = gconf_client_get_bool (gconf, FORCE_SHOW_ADDRESS, NULL);
	priv->user_query_fields = gconf_client_get_list (gconf, USER_QUERY_FIELDS, GCONF_VALUE_STRING, NULL);
  g_object_unref (G_OBJECT (gconf));

  /* Edit signals */

  g_signal_connect (name_selector_entry, "insert-text", G_CALLBACK (user_insert_text), name_selector_entry);
  g_signal_connect (name_selector_entry, "delete-text", G_CALLBACK (user_delete_text), name_selector_entry);
  g_signal_connect (name_selector_entry, "focus-out-event", G_CALLBACK (user_focus_out), name_selector_entry);
  g_signal_connect_after (name_selector_entry, "focus-in-event", G_CALLBACK (user_focus_in), name_selector_entry);

  /* Exposition */

  g_signal_connect (name_selector_entry, "expose-event", G_CALLBACK (expose_event), name_selector_entry);

  /* Activation: Complete current entry if possible */

  g_signal_connect (name_selector_entry, "activate", G_CALLBACK (entry_activate), name_selector_entry);

  /* Pop-up menu */

  g_signal_connect (name_selector_entry, "button-press-event", G_CALLBACK (prepare_popup_destination), name_selector_entry);
  g_signal_connect (name_selector_entry, "populate-popup", G_CALLBACK (populate_popup), name_selector_entry);

	/* Clipboard signals */
	g_signal_connect (name_selector_entry, "copy-clipboard", G_CALLBACK (copy_clipboard), name_selector_entry);
	g_signal_connect (name_selector_entry, "cut-clipboard", G_CALLBACK (cut_clipboard), name_selector_entry);

  /* Completion */

  name_selector_entry->email_generator = NULL;

  name_selector_entry->entry_completion = gtk_entry_completion_new ();
  gtk_entry_completion_set_match_func (name_selector_entry->entry_completion,
				       (GtkEntryCompletionMatchFunc) completion_match_cb, NULL, NULL);
  g_signal_connect_swapped (name_selector_entry->entry_completion, "match-selected",
			    G_CALLBACK (completion_match_selected), name_selector_entry);

  gtk_entry_set_completion (GTK_ENTRY (name_selector_entry), name_selector_entry->entry_completion);

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (name_selector_entry->entry_completion), renderer, FALSE);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (name_selector_entry->entry_completion),
		GTK_CELL_RENDERER (renderer),
		(GtkCellLayoutDataFunc) contact_layout_pixbuffer,
		name_selector_entry, NULL);

  /* Completion list name renderer */
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (name_selector_entry->entry_completion),
			      renderer, TRUE);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (name_selector_entry->entry_completion),
				      GTK_CELL_RENDERER (renderer),
				      (GtkCellLayoutDataFunc) contact_layout_formatter,
				      name_selector_entry, NULL);

  /* Destination store */

  name_selector_entry->destination_store = e_destination_store_new ();
  setup_destination_store (name_selector_entry);
  priv->is_completing = FALSE;
}

/**
 * e_name_selector_entry_new:
 *
 * Creates a new #ENameSelectorEntry.
 *
 * Returns: A new #ENameSelectorEntry.
 **/
ENameSelectorEntry *
e_name_selector_entry_new (void)
{
	  return g_object_new (e_name_selector_entry_get_type (), NULL);
}

/**
 * e_name_selector_entry_peek_contact_store:
 * @name_selector_entry: an #ENameSelectorEntry
 *
 * Gets the #EContactStore being used by @name_selector_entry.
 *
 * Returns: An #EContactStore.
 **/
EContactStore *
e_name_selector_entry_peek_contact_store (ENameSelectorEntry *name_selector_entry)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry), NULL);

	return name_selector_entry->contact_store;
}

/**
 * e_name_selector_entry_set_contact_store:
 * @name_selector_entry: an #ENameSelectorEntry
 * @contact_store: an #EContactStore to use
 *
 * Sets the #EContactStore being used by @name_selector_entry to @contact_store.
 **/
void
e_name_selector_entry_set_contact_store (ENameSelectorEntry *name_selector_entry,
					 EContactStore *contact_store)
{
	g_return_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry));
	g_return_if_fail (contact_store == NULL || E_IS_CONTACT_STORE (contact_store));

	if (contact_store == name_selector_entry->contact_store)
		return;

	if (name_selector_entry->contact_store)
		g_object_unref (name_selector_entry->contact_store);
	name_selector_entry->contact_store = contact_store;
	if (name_selector_entry->contact_store)
		g_object_ref (name_selector_entry->contact_store);

	setup_contact_store (name_selector_entry);
}

/**
 * e_name_selector_entry_peek_destination_store:
 * @name_selector_entry: an #ENameSelectorEntry
 *
 * Gets the #EDestinationStore being used to store @name_selector_entry's destinations.
 *
 * Returns: An #EDestinationStore.
 **/
EDestinationStore *
e_name_selector_entry_peek_destination_store (ENameSelectorEntry *name_selector_entry)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_ENTRY (name_selector_entry), NULL);

	return name_selector_entry->destination_store;
}

/**
 * e_name_selector_entry_set_destination_store:
 * @name_selector_entry: an #ENameSelectorEntry
 * @destination_store: an #EDestinationStore to use
 *
 * Sets @destination_store as the #EDestinationStore to be used to store
 * destinations for @name_selector_entry.
 **/
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

/**
 * e_name_selector_entry_set_contact_editor_func:
 *
 * DO NOT USE.
 **/
void
e_name_selector_entry_set_contact_editor_func (ENameSelectorEntry *name_selector_entry, gpointer func)
{
	name_selector_entry->contact_editor_func = func;
}

/**
 * e_name_selector_entry_set_contact_list_editor_func:
 *
 * DO NOT USE.
 **/
void
e_name_selector_entry_set_contact_list_editor_func (ENameSelectorEntry *name_selector_entry, gpointer func)
{
	name_selector_entry->contact_list_editor_func = func;
}
