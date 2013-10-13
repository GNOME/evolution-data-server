/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */
 
#include <libebook/libebook.h>

#include "cursor-example.h"
#include "cursor-navigator.h"
#include "cursor-search.h"
#include "cursor-data.h"

#define N_SLOTS         10
#define INITIAL_TIMEOUT 600
#define TICK_TIMEOUT    100
#define d(x)

typedef enum _TimeoutActivity TimeoutActivity;

/* GObjectClass */
static void            cursor_example_class_init              (CursorExampleClass *klass);
static void            cursor_example_init                    (CursorExample      *example);
static void            cursor_example_dispose                 (GObject            *object);

/* UI Callbacks */
static gboolean        cursor_example_up_button_press         (CursorExample      *example,
							       GdkEvent           *event,
							       GtkButton          *button);
static gboolean        cursor_example_up_button_release       (CursorExample      *example,
							       GdkEvent           *event,
							       GtkButton          *button);
static gboolean        cursor_example_down_button_press       (CursorExample      *example,
							       GdkEvent           *event,
							       GtkButton          *button);
static gboolean        cursor_example_down_button_release     (CursorExample      *example,
							       GdkEvent           *event,
							       GtkButton          *button);
static void            cursor_example_navigator_changed       (CursorExample      *example,
							       CursorNavigator    *navigator);
static void            cursor_example_sexp_changed            (CursorExample      *example,
							       GParamSpec         *pspec,
							       CursorSearch       *search);

/* EDS Callbacks */
static void            cursor_example_refresh                 (EBookClientCursor  *cursor,
							       CursorExample      *example);
static void            cursor_example_alphabet_changed        (EBookClientCursor  *cursor,
							       GParamSpec         *pspec,
							       CursorExample      *example);
static void            cursor_example_status_changed          (EBookClientCursor  *cursor,
							       GParamSpec         *spec,
							       CursorExample      *example);

/* Utilities */
static void            cursor_example_load_alphabet           (CursorExample      *example);
static gboolean        cursor_example_move_cursor             (CursorExample      *example,
							       EBookCursorOrigin   origin,
							       gint                count,
							       gboolean            load_page,
							       gboolean           *full_results);
static void            cursor_example_update_status           (CursorExample      *example);
static void            cursor_example_update_sensitivity      (CursorExample      *example);
static void            cursor_example_update_current_index    (CursorExample      *example,
							       EContact           *contact);
static void            cursor_example_ensure_timeout          (CursorExample      *example,
							       TimeoutActivity     activity);
static void            cursor_example_cancel_timeout          (CursorExample      *example);

enum _TimeoutActivity {
	TIMEOUT_NONE = 0,
	TIMEOUT_UP_INITIAL,
	TIMEOUT_UP_TICK,
	TIMEOUT_DOWN_INITIAL,
	TIMEOUT_DOWN_TICK,
};

struct _CursorExamplePrivate {
	/* Screen widgets */
	GtkWidget *browse_up_button;
	GtkWidget *browse_down_button;
	GtkWidget *total_label;
	GtkWidget *position_label;
	GtkWidget *alphabet_label;
	GtkWidget *slots[N_SLOTS];
	CursorNavigator *navigator;

	/* EDS Resources */
	EBookClient          *client;
	EBookClientCursor    *cursor;

	/* Manage the automatic scrolling with button pressed */
	guint                 timeout_id;
	TimeoutActivity       activity;
};

G_DEFINE_TYPE_WITH_PRIVATE (CursorExample, cursor_example, GTK_TYPE_WINDOW);

/************************************************************************
 *                          GObjectClass                                *
 ************************************************************************/
static void
cursor_example_class_init (CursorExampleClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;
	gint i;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = cursor_example_dispose;

	/* Bind to template */
	widget_class = GTK_WIDGET_CLASS (klass);
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/evolution/cursor-example/cursor-example.ui");
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, navigator);
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, browse_up_button);
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, browse_down_button);
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, alphabet_label);
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, total_label);
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, position_label);

	for (i = 0; i < N_SLOTS; i++) {
		gchar *name = g_strdup_printf ("contact_slot_%d", i + 1);

		gtk_widget_class_bind_template_child_full (widget_class, name, FALSE, 0);
		g_free (name);
	}

	gtk_widget_class_bind_template_callback (widget_class, cursor_example_navigator_changed);
	gtk_widget_class_bind_template_callback (widget_class, cursor_example_up_button_press);
	gtk_widget_class_bind_template_callback (widget_class, cursor_example_up_button_release);
	gtk_widget_class_bind_template_callback (widget_class, cursor_example_down_button_press);
	gtk_widget_class_bind_template_callback (widget_class, cursor_example_down_button_release);
	gtk_widget_class_bind_template_callback (widget_class, cursor_example_sexp_changed);
}

static void
cursor_example_init (CursorExample *example)
{
	CursorExamplePrivate *priv;
	gint i;

	example->priv = priv =
		cursor_example_get_instance_private (example);

	/* Initialize the template, we use 2 private types in
	 * our GtkBuilder xml, the CursorNavigator is used to
	 * display the locale specific alphabet navigator and
	 * the CursorSearch object controls the query expression.
	 */
	g_type_ensure (CURSOR_TYPE_NAVIGATOR);
	g_type_ensure (CURSOR_TYPE_SEARCH);
	gtk_widget_init_template (GTK_WIDGET (example));

	/* Pick up our dynamic 'slot' widgets, the CursorSlot widgets
	 * are used to display results (contacts).
	 */
	for (i = 0; i < N_SLOTS; i++) {

		gchar *name = g_strdup_printf ("contact_slot_%d", i + 1);
		priv->slots[i] = (GtkWidget *)gtk_widget_get_template_child (GTK_WIDGET (example),
									     CURSOR_TYPE_EXAMPLE,
									     name);
		g_free (name);
	}
}

static void
cursor_example_dispose (GObject  *object)
{
	CursorExample        *example = CURSOR_EXAMPLE (object);
	CursorExamplePrivate *priv = example->priv;

	cursor_example_cancel_timeout (example);

	if (priv->client) {
		g_object_unref (priv->client);
		priv->client = NULL;
	}

	if (priv->cursor) {
		g_object_unref (priv->cursor);
		priv->cursor = NULL;
	}

	G_OBJECT_CLASS (cursor_example_parent_class)->dispose (object);
}

/************************************************************************
 *                           UI Callbacks                               *
 ************************************************************************/
static gboolean
cursor_example_up_button_press (CursorExample *example,
				GdkEvent      *event,
				GtkButton     *button)
{
	d (g_print ("Browse up press\n"));

	/* Move the cursor backwards by 'N_SLOTS + 1' and then refresh the page */
	if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 0 - (N_SLOTS + 1), FALSE, NULL))
		cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, N_SLOTS, TRUE, NULL);

	cursor_example_ensure_timeout (example, TIMEOUT_UP_INITIAL);

	return FALSE;
}

static gboolean
cursor_example_up_button_release (CursorExample *example,
				  GdkEvent      *event,
				  GtkButton     *button)
{
	d (g_print ("Browse up release\n"));

	cursor_example_cancel_timeout (example);

	return FALSE;
}

static gboolean
cursor_example_down_button_press (CursorExample *example,
				  GdkEvent      *event,
				  GtkButton     *button)
{
	d (g_print ("Browse down press\n"));

	/* Move the cursor backwards by 'N_SLOTS - 1' and then refresh the page */
	if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 0 - (N_SLOTS - 1), FALSE, NULL))
		cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, N_SLOTS, TRUE, NULL);

	cursor_example_ensure_timeout (example, TIMEOUT_DOWN_INITIAL);

	return FALSE;
}

static gboolean
cursor_example_down_button_release (CursorExample *example,
				    GdkEvent      *event,
				    GtkButton     *button)
{
	d (g_print ("Browse down released\n"));

	cursor_example_cancel_timeout (example);

	return FALSE;
}

static void
cursor_example_navigator_changed (CursorExample      *example,
				  CursorNavigator    *navigator)
{
	CursorExamplePrivate *priv = example->priv;
	GError               *error = NULL;
	gint                  index;
	gboolean              full_results = FALSE;

	index = cursor_navigator_get_index (priv->navigator);

	d (g_print ("Alphabet index changed to: %d\n", index));

	/* Move to this index */
	if (!e_book_client_cursor_set_alphabetic_index_sync (priv->cursor, index, NULL, &error)) {

		if (g_error_matches (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC)) {

			/* Just ignore the error.
			 *
			 * The addressbook locale has recently changed, very
			 * soon we will receive an alphabet change notification
			 * where we will reset the cursor position and reload
			 * the alphabet.
			 */
			d (g_print ("Cursor was temporarily out of sync while setting the alphabetic target\n"));
		} else
			g_warning ("Failed to move the cursor: %s", error->message);

		g_clear_error (&error);
	}

	/* And load one page full of results starting with this index */
	if (!cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT,
					 N_SLOTS, TRUE, &full_results))
		return;

	/* If we hit the end of the results (less than a full page) then load the last page of results */
	if (!full_results) {

		if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_RESET,
						0 - (N_SLOTS + 1), FALSE, NULL)) {
			cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT,
						    N_SLOTS, TRUE, NULL);
		}
	}
}

static void
cursor_example_sexp_changed (CursorExample      *example,
			     GParamSpec         *pspec,
			     CursorSearch       *search)
{
	CursorExamplePrivate *priv = example->priv;
	gboolean              full_results = FALSE;
	GError               *error = NULL;
	const gchar          *sexp;

	sexp = cursor_search_get_sexp (search);

	d (g_print ("Search expression changed to: '%s'\n", sexp));

	/* Set the search expression */
	if (!e_book_client_cursor_set_sexp_sync (priv->cursor, sexp, NULL, &error)) {
		g_warning ("Failed to move the cursor: %s", error->message);
		g_clear_error (&error);
	}

	/* And re-load one page full of results, load from the previous index */
	cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_PREVIOUS, N_SLOTS, TRUE, &full_results);

	/* If we hit the end of the results (less than a full page) then load the last page of results */
	if (!full_results)
		if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_RESET,
						0 - (N_SLOTS + 1), FALSE, NULL)) {
			cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT,
						    N_SLOTS, TRUE, NULL);
	}
}

/************************************************************************
 *                           EDS Callbacks                              *
 ************************************************************************/
static void
cursor_example_refresh (EBookClientCursor  *cursor,
			CursorExample      *example)
{
	d (g_print ("Cursor refreshed\n"));

	/* Repeat our last query, by requesting that we move from the previous origin */
	cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_PREVIOUS, N_SLOTS, TRUE, NULL);

	cursor_example_update_status (example);
}

static void
cursor_example_alphabet_changed (EBookClientCursor *cursor,
				 GParamSpec        *spec,
				 CursorExample     *example)
{
	d (g_print ("Alphabet Changed\n"));

	cursor_example_load_alphabet (example);

	/* Get the first page of contacts in the addressbook */
	cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_RESET, N_SLOTS, TRUE, NULL);
}

static void
cursor_example_status_changed (EBookClientCursor *cursor,
			       GParamSpec        *spec,
			       CursorExample     *example)
{
	d (g_print ("Status changed\n"));

	cursor_example_update_status (example);
}

/************************************************************************
 *                             Utilities                                *
 ************************************************************************/
static void
cursor_example_load_alphabet (CursorExample *example)
{
	CursorExamplePrivate *priv = example->priv;
	const gchar *const   *alphabet;

	/* Update the alphabet on the navigator */
	alphabet = e_book_client_cursor_get_alphabet (priv->cursor, NULL, NULL, NULL, NULL);
	cursor_navigator_set_alphabet (priv->navigator, alphabet);

	/* Reset navigator to the beginning */
	g_signal_handlers_block_by_func (priv->navigator, cursor_example_navigator_changed, example);
	cursor_navigator_set_index (priv->navigator, 0);
	g_signal_handlers_unblock_by_func (priv->navigator, cursor_example_navigator_changed, example);
}

static void
cursor_example_update_view (CursorExample     *example,
			    GSList            *results)
{
	CursorExamplePrivate *priv = example->priv;
	EContact             *contact;
	gint                  i;

	/* Fill the page with results for the current cursor position
	 */
	for (i = 0; i < N_SLOTS; i++) {
		contact = g_slist_nth_data (results, i);

		/* For the first contact, give some visual feedback about where we
		 * are in the list, which alphabet character we're browsing right now.
		 */
		if (i == 0 && contact)
			cursor_example_update_current_index (example, contact);

		cursor_slot_set_from_contact (priv->slots[i], contact);
	}
}

/* Returns whether a full page was loaded or if we reached 
 * the end of the results
 */
static gboolean
cursor_example_move_cursor (CursorExample     *example,
			    EBookCursorOrigin  origin,
			    gint               count,
			    gboolean           load_page,
			    gboolean          *full_results)
{
	CursorExamplePrivate *priv = example->priv;
	GError               *error = NULL;
	GSList               *results = NULL, **results_arg = NULL;
	gint                  n_results;

	/* We don't ask EDS for results if we're not going to load the view */
	g_assert (load_page == TRUE || full_results == NULL);

	/* Only fetch results if we will actually display them */
	if (load_page)
		results_arg = &results;

	n_results = e_book_client_cursor_move_by_sync (priv->cursor,
						       origin,
						       count,
						       results_arg,
						       NULL, &error);

	if (n_results < 0) {
		if (g_error_matches (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC)) {

			/* Just ignore this error.
			 *
			 * The addressbook has very recently been modified,
			 * very soon we will receive a "refresh" signal and
			 * automatically reload the current page position.
			 */
			d (g_print ("Cursor was temporarily out of sync while moving\n"));

		} else
			g_warning ("Failed to move the cursor: %s", error->message);

		g_clear_error (&error);

	} else if (load_page) {

		/* Display the results */
		cursor_example_update_view (example, results);
	}

	if (full_results)
		*full_results = (n_results == ABS (count));

	g_slist_free_full (results, (GDestroyNotify)g_object_unref);

	return n_results >= 0;
}

static void
cursor_example_update_status (CursorExample *example)
{
	CursorExamplePrivate *priv = example->priv;
	GError               *error = NULL;
	gint                  total, position;
	gchar                *txt;
	gboolean              up_sensitive;
	gboolean              down_sensitive;

	total = e_book_client_cursor_get_total (priv->cursor);
	position = e_book_client_cursor_get_position (priv->cursor);

	/* Update total indicator label */
	txt = g_strdup_printf ("%d", total);
	gtk_label_set_text (GTK_LABEL (priv->total_label), txt);
	g_free (txt);

	/* Update position indicator label */
	txt = g_strdup_printf ("%d", position);
	gtk_label_set_text (GTK_LABEL (priv->position_label), txt);
	g_free (txt);

	/* Update sensitivity of buttons */
	if (total <= N_SLOTS) {
		/* If the amount of contacts is less than the amount of visual slots,
		 * then we cannot browse up and down
		 */
		up_sensitive = FALSE;
		down_sensitive = FALSE;
	} else {
		/* The cursor is always pointing to the last contact
		 * visible in the view, so if the cursor is further
		 * than N_SLOTS we can rewind.
		 */
		up_sensitive = (position > N_SLOTS);

		/* So long as we have not reached the last contact, we can move
		 * the cursor down through the list */
		down_sensitive = (position < total);
	}

	gtk_widget_set_sensitive (priv->browse_up_button, up_sensitive);
	gtk_widget_set_sensitive (priv->browse_down_button, down_sensitive);
}

/* This is called when refreshing the window contents with
 * the first contact shown in the window.
 */
static void
cursor_example_update_current_index (CursorExample *example,
				     EContact      *contact)
{
	CursorExamplePrivate *priv = example->priv;
	const gchar *const   *labels;
	gint                  index;

	/* Fetch the alphabetic index for this contact */
	index = e_book_client_cursor_get_contact_alphabetic_index (priv->cursor, contact);

	/* Refresh the current alphabet index indicator.
	 *
	 * The index returned by e_book_client_cursor_get_contact_alphabetic_index() is
	 * a valid position into the array returned by e_book_client_cursor_get_alphabet().
	 */
	labels = e_book_client_cursor_get_alphabet (priv->cursor, NULL, NULL, NULL, NULL);
	gtk_label_set_text (GTK_LABEL (priv->alphabet_label), labels[index]);

	/* Update the current scroll position (and avoid reacting to the value change)
	 */
	if (contact) {
		g_signal_handlers_block_by_func (priv->navigator, cursor_example_navigator_changed, example);
		cursor_navigator_set_index (priv->navigator, index);
		g_signal_handlers_unblock_by_func (priv->navigator, cursor_example_navigator_changed, example);
	}
}

static gboolean
cursor_example_timeout (CursorExample *example)
{
	CursorExamplePrivate *priv = example->priv;
	gboolean reschedule = FALSE;

	switch (priv->activity) {
	case TIMEOUT_NONE:
		break;

	case TIMEOUT_UP_INITIAL:
		cursor_example_ensure_timeout (example, TIMEOUT_UP_TICK);
		break;
	case TIMEOUT_DOWN_INITIAL:
		cursor_example_ensure_timeout (example, TIMEOUT_DOWN_TICK);
		break;

	case TIMEOUT_UP_TICK:

		/* Move the cursor backwards by 'N_SLOTS + 1' and then refresh the page */
		if (gtk_widget_get_sensitive (priv->browse_up_button) &&
		    cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 0 - (N_SLOTS + 1), FALSE, NULL)) {

			if (gtk_widget_get_sensitive (priv->browse_up_button) &&
			    cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, N_SLOTS, TRUE, NULL))
				reschedule = TRUE;
		}

		if (!reschedule)
			cursor_example_cancel_timeout (example);

		break;

	case TIMEOUT_DOWN_TICK:

		/* Move the cursor backwards by 'N_SLOTS - 1' and then refresh the page */
		if (gtk_widget_get_sensitive (priv->browse_down_button) &&
		    cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 0 - (N_SLOTS - 1), FALSE, NULL)) {

			if (gtk_widget_get_sensitive (priv->browse_down_button) &&
			    cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, N_SLOTS, TRUE, NULL))
				reschedule = TRUE;
		}

		if (!reschedule)
			cursor_example_cancel_timeout (example);

		break;
	}

	return reschedule;
}

static void
cursor_example_ensure_timeout (CursorExample      *example,
			       TimeoutActivity     activity)
{
	CursorExamplePrivate *priv = example->priv;
	guint                 timeout = 0;

	cursor_example_cancel_timeout (example);

	if (activity == TIMEOUT_UP_INITIAL ||
	    activity == TIMEOUT_DOWN_INITIAL)
		timeout = INITIAL_TIMEOUT;
	else
		timeout = TICK_TIMEOUT;

	priv->activity = activity;

	priv->timeout_id =
		g_timeout_add (timeout,
			       (GSourceFunc)cursor_example_timeout,
			       example);
}

static void
cursor_example_cancel_timeout (CursorExample *example)
{
	CursorExamplePrivate *priv = example->priv;

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}
}

/************************************************************************
 *                                API                                   *
 ************************************************************************/
GtkWidget *
cursor_example_new (const gchar *vcard_path)
{
  CursorExample *example;
  CursorExamplePrivate *priv;

  example = g_object_new (CURSOR_TYPE_EXAMPLE, NULL);
  priv    = example->priv;

  priv->client = cursor_load_data (vcard_path, &priv->cursor);

  cursor_example_load_alphabet (example);

  cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_RESET, N_SLOTS, TRUE, NULL);
  cursor_example_update_status (example);

  g_signal_connect (priv->cursor, "refresh",
		    G_CALLBACK (cursor_example_refresh), example);
  g_signal_connect (priv->cursor, "notify::alphabet",
		    G_CALLBACK (cursor_example_alphabet_changed), example);
  g_signal_connect (priv->cursor, "notify::total",
		    G_CALLBACK (cursor_example_status_changed), example);
  g_signal_connect (priv->cursor, "notify::position",
		    G_CALLBACK (cursor_example_status_changed), example);

  g_message ("Cursor example started in locale: %s",
	     e_book_client_get_locale (priv->client));

  return (GtkWidget *)example;
}
