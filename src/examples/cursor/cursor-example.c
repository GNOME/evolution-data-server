/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <libebook/libebook.h>

#include "cursor-example.h"
#include "cursor-navigator.h"
#include "cursor-search.h"
#include "cursor-slot.h"
#include "cursor-data.h"
#include "cursor-slot.h"

#define N_SLOTS         10

#define INITIAL_TIMEOUT 600
#define TICK_TIMEOUT    100

#define d(x)

typedef enum _TimeoutActivity TimeoutActivity;

/* GObjectClass */
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
							       gint                count);
static gboolean        cursor_example_load_page               (CursorExample      *example,
							       gboolean           *full_results);
static void            cursor_example_update_status           (CursorExample      *example);
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
	GtkWidget *progressbar;
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
	gtk_widget_class_bind_template_child_private (widget_class, CursorExample, progressbar);

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

	g_type_ensure (CURSOR_TYPE_NAVIGATOR);
	g_type_ensure (CURSOR_TYPE_SEARCH);

	gtk_widget_init_template (GTK_WIDGET (example));

	for (i = 0; i < N_SLOTS; i++) {

		gchar *name = g_strdup_printf ("contact_slot_%d", i + 1);
		priv->slots[i] = (GtkWidget *) gtk_widget_get_template_child (GTK_WIDGET (example),
									     CURSOR_TYPE_EXAMPLE,
									     name);
		g_free (name);
	}
}

static void
cursor_example_dispose (GObject *object)
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
                                GdkEvent *event,
                                GtkButton *button)
{
	d (g_print ("Browse up press\n"));

	/* Move the cursor backwards by 1 and then refresh the page */
	if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 0 - 1))
		cursor_example_load_page (example, NULL);

	cursor_example_ensure_timeout (example, TIMEOUT_UP_INITIAL);

	return FALSE;
}

static gboolean
cursor_example_up_button_release (CursorExample *example,
                                  GdkEvent *event,
                                  GtkButton *button)
{
	d (g_print ("Browse up release\n"));

	cursor_example_cancel_timeout (example);

	return FALSE;
}

static gboolean
cursor_example_down_button_press (CursorExample *example,
                                  GdkEvent *event,
                                  GtkButton *button)
{
	d (g_print ("Browse down press\n"));

	/* Move the cursor forward by 1 and then refresh the page */
	if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 1))
		cursor_example_load_page (example, NULL);

	cursor_example_ensure_timeout (example, TIMEOUT_DOWN_INITIAL);

	return FALSE;
}

static gboolean
cursor_example_down_button_release (CursorExample *example,
                                    GdkEvent *event,
                                    GtkButton *button)
{
	d (g_print ("Browse down released\n"));

	cursor_example_cancel_timeout (example);

	return FALSE;
}

static void
cursor_example_navigator_changed (CursorExample *example,
                                  CursorNavigator *navigator)
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
	if (!cursor_example_load_page (example, &full_results))
		return;

	/* If we hit the end of the results (less than a full page) then load the last page of results */
	if (!full_results) {
		if (cursor_example_move_cursor (example,
						E_BOOK_CURSOR_ORIGIN_END,
						0 - (N_SLOTS + 1))) {
			cursor_example_load_page (example, NULL);
		}
	}
}

static void
cursor_example_sexp_changed (CursorExample *example,
                             GParamSpec *pspec,
                             CursorSearch *search)
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

	/* And load one page full of results */
	if (!cursor_example_load_page (example, &full_results))
		return;

	/* If we hit the end of the results (less than a full page) then load the last page of results */
	if (!full_results)
		if (cursor_example_move_cursor (example,
						E_BOOK_CURSOR_ORIGIN_END,
						0 - (N_SLOTS + 1))) {
			cursor_example_load_page (example, NULL);
	}
}

/************************************************************************
 *                           EDS Callbacks                              *
 ************************************************************************/
static void
cursor_example_refresh (EBookClientCursor *cursor,
                        CursorExample *example)
{
	d (g_print ("Cursor refreshed\n"));

	/* Refresh the page */
	if (cursor_example_load_page (example, NULL))
		cursor_example_update_status (example);
}

static void
cursor_example_alphabet_changed (EBookClientCursor *cursor,
                                 GParamSpec *spec,
                                 CursorExample *example)
{
	d (g_print ("Alphabet Changed\n"));

	cursor_example_load_alphabet (example);

	/* Get the first page of contacts in the addressbook */
	if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_BEGIN, 0))
		cursor_example_load_page (example, NULL);
}

static void
cursor_example_status_changed (EBookClientCursor *cursor,
                               GParamSpec *spec,
                               CursorExample *example)
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

static gboolean
cursor_example_move_cursor (CursorExample *example,
                            EBookCursorOrigin origin,
                            gint count)
{
	CursorExamplePrivate *priv = example->priv;
	GError               *error = NULL;
	gint                  n_results;

	n_results = e_book_client_cursor_step_sync (
		priv->cursor,
		E_BOOK_CURSOR_STEP_MOVE,
		origin,
		count,
		NULL, /* Result list */
		NULL, /* GCancellable */
		&error);

	if (n_results < 0) {

		if (g_error_matches (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC)) {

			/* The addressbook has very recently been modified,
			 * very soon we will receive a "refresh" signal and
			 * automatically reload the current page position.
			 */
			d (g_print ("Cursor was temporarily out of sync while moving\n"));

		} else if (g_error_matches (error,
					    E_CLIENT_ERROR,
					    E_CLIENT_ERROR_QUERY_REFUSED)) {

			d (g_print ("End of list was reached\n"));

		} else
			g_warning ("Failed to move the cursor: %s", error->message);

		g_clear_error (&error);

	}

	return n_results >= 0;
}

/* Loads a page at the current cursor position, returns
 * FALSE if there was an error.
 */
static gboolean
cursor_example_load_page (CursorExample *example,
                          gboolean *full_results)
{
	CursorExamplePrivate *priv = example->priv;
	GError               *error = NULL;
	GSList               *results = NULL;
	gint                  n_results;

	/* Fetch N_SLOTS contacts after the current cursor position,
	 * without modifying the current cursor position
	 */
	n_results = e_book_client_cursor_step_sync (
		priv->cursor,
		E_BOOK_CURSOR_STEP_FETCH,
		E_BOOK_CURSOR_ORIGIN_CURRENT,
		N_SLOTS,
		&results,
		NULL, /* GCancellable */
		&error);

	if (n_results < 0) {
		if (g_error_matches (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC)) {

			/* The addressbook has very recently been modified,
			 * very soon we will receive a "refresh" signal and
			 * automatically reload the current page position.
			 */
			d (g_print ("Cursor was temporarily out of sync while loading page\n"));

		} else if (g_error_matches (error,
					    E_CLIENT_ERROR,
					    E_CLIENT_ERROR_QUERY_REFUSED)) {

			d (g_print ("End of list was reached\n"));

		} else
			g_warning ("Failed to move the cursor: %s", error->message);

		g_clear_error (&error);

	} else {
		/* Display the results */
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

			cursor_slot_set_from_contact (CURSOR_SLOT (priv->slots[i]), contact);
		}
	}

	if (full_results)
		*full_results = (n_results == N_SLOTS);

	g_slist_free_full (results, (GDestroyNotify) g_object_unref);

	return n_results >= 0;
}

static void
cursor_example_update_status (CursorExample *example)
{
	CursorExamplePrivate *priv = example->priv;
	gint                  total, position;
	gchar                *txt;
	gboolean              up_sensitive;
	gboolean              down_sensitive;
	gdouble               fraction;

	total = e_book_client_cursor_get_total (priv->cursor);
	position = e_book_client_cursor_get_position (priv->cursor);

	/* Set the label showing the cursor position and total contacts */
	txt = g_strdup_printf ("Position %d / Total %d", position, total);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (priv->progressbar), txt);
	g_free (txt);

	/* Give visual feedback on how far we are into the contact list */
	fraction = position * 1.0F / (total - N_SLOTS);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progressbar), fraction);

	/* Update sensitivity of buttons */
	if (total <= N_SLOTS) {
		/* If the amount of contacts is less than the amount of visual slots,
		 * then we cannot browse up and down
		 */
		up_sensitive = FALSE;
		down_sensitive = FALSE;
	} else {
		/* The cursor is always pointing directly before
		 * the first contact visible in the view, so if the
		 * cursor is passed the first contact we can rewind.
		 */
		up_sensitive = position > 0;

		/* If more than N_SLOTS contacts remain, then
		 * we can still scroll down */
		down_sensitive = position < total - N_SLOTS;
	}

	gtk_widget_set_sensitive (priv->browse_up_button, up_sensitive);
	gtk_widget_set_sensitive (priv->browse_down_button, down_sensitive);
}

/* This is called when refreshing the window contents with
 * the first contact shown in the window.
 */
static void
cursor_example_update_current_index (CursorExample *example,
                                     EContact *contact)
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
	gboolean can_move;

	switch (priv->activity) {
	case TIMEOUT_NONE:
		break;

	case TIMEOUT_UP_INITIAL:
	case TIMEOUT_UP_TICK:

		/* Move the cursor backwards by 1 and then refresh the page */
		if (cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 0 - 1)) {
			cursor_example_load_page (example, NULL);
			cursor_example_ensure_timeout (example, TIMEOUT_UP_TICK);
		} else
			cursor_example_cancel_timeout (example);

		break;

	case TIMEOUT_DOWN_INITIAL:
	case TIMEOUT_DOWN_TICK:

		/* Avoid scrolling past the end of the list - N_SLOTS */
		can_move = (e_book_client_cursor_get_position (priv->cursor) <
			    e_book_client_cursor_get_total (priv->cursor) - N_SLOTS);

		/* Move the cursor forwards by 1 and then refresh the page */
		if (can_move &&
		    cursor_example_move_cursor (example, E_BOOK_CURSOR_ORIGIN_CURRENT, 1)) {
			cursor_example_load_page (example, NULL);
			cursor_example_ensure_timeout (example, TIMEOUT_DOWN_TICK);
		} else
			cursor_example_cancel_timeout (example);

		break;
	}

	return FALSE;
}

static void
cursor_example_ensure_timeout (CursorExample *example,
                               TimeoutActivity activity)
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
		g_timeout_add (
			timeout,
			(GSourceFunc) cursor_example_timeout,
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
  priv = example->priv;

  priv->client = cursor_load_data (vcard_path, &priv->cursor);

  cursor_example_load_alphabet (example);

  /* Load the first page of results */
  cursor_example_load_page (example, NULL);
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

  return (GtkWidget *) example;
}
