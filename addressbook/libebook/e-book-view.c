/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#include <glib-object.h>
#include "e-book.h"
#include "e-book-view.h"
#include "e-book-view-private.h"
#include "e-book-marshal.h"
#include "libedata-book/e-data-book-types.h"
#include "e-gdbus-egdbusbookview.h"

G_DEFINE_TYPE(EBookView, e_book_view, G_TYPE_OBJECT);

#define E_BOOK_VIEW_GET_PRIVATE(o)					\
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_BOOK_VIEW, EBookViewPrivate))

struct _EBookViewPrivate {
	EGdbusBookView *gdbus_bookview;
	EBook *book;
	gboolean running;
};

enum {
	CONTACTS_CHANGED,
	CONTACTS_REMOVED,
	CONTACTS_ADDED,
	#ifndef E_BOOK_DISABLE_DEPRECATED
	SEQUENCE_COMPLETE,
	#endif
	VIEW_COMPLETE,
	STATUS_MESSAGE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
contacts_added_cb (EGdbusBookView *object, const gchar * const *vcards, EBookView *book_view)
{
	const gchar * const *p;
	GList *contacts = NULL;

	if (!book_view->priv->running)
		return;

	for (p = vcards; *p; p++) {
		contacts = g_list_prepend (contacts, e_contact_new_from_vcard (*p));
	}

	contacts = g_list_reverse (contacts);

	g_signal_emit (book_view, signals[CONTACTS_ADDED], 0, contacts);

	g_list_foreach (contacts, (GFunc)g_object_unref, NULL);
	g_list_free (contacts);
}

static void
contacts_changed_cb (EGdbusBookView *object, const gchar * const *vcards, EBookView *book_view)
{
	const gchar * const *p;
	GList *contacts = NULL;

	if (!book_view->priv->running)
		return;

	for (p = vcards; *p; p++) {
		contacts = g_list_prepend (contacts, e_contact_new_from_vcard (*p));
	}
	contacts = g_list_reverse (contacts);

	g_signal_emit (book_view, signals[CONTACTS_CHANGED], 0, contacts);

	g_list_foreach (contacts, (GFunc)g_object_unref, NULL);
	g_list_free (contacts);
}

static void
contacts_removed_cb (EGdbusBookView *object, const gchar * const *ids, EBookView *book_view)
{
	const gchar * const *p;
	GList *list = NULL;

	if (!book_view->priv->running)
		return;

	for (p = ids; *p; p++) {
		list = g_list_prepend (list, (gchar *)*p);
	}
	list = g_list_reverse (list);

	g_signal_emit (book_view, signals[CONTACTS_REMOVED], 0, list);

	/* No need to free the values, our caller will */
	g_list_free (list);
}

static void
status_message_cb (EGdbusBookView *object, const gchar *message, EBookView *book_view)
{
	if (!book_view->priv->running)
		return;

	g_signal_emit (book_view, signals[STATUS_MESSAGE], 0, message);
}

static void
complete_cb (EGdbusBookView *object, /* EDataBookStatus */ guint status, const gchar *message, EBookView *book_view)
{
	EBookViewStatus bv_status = E_BOOK_VIEW_ERROR_OTHER_ERROR;

	if (!book_view->priv->running)
		return;

	switch (status) {
	case E_DATA_BOOK_STATUS_SUCCESS:
		bv_status = E_BOOK_VIEW_STATUS_OK;
		break;
	case E_DATA_BOOK_STATUS_SEARCH_TIME_LIMIT_EXCEEDED:
		bv_status = E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED;
		break;
	case E_DATA_BOOK_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED:
		bv_status = E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED;
		break;
	case E_DATA_BOOK_STATUS_INVALID_QUERY:
		bv_status = E_BOOK_VIEW_ERROR_INVALID_QUERY;
		break;
	case E_DATA_BOOK_STATUS_QUERY_REFUSED:
		bv_status = E_BOOK_VIEW_ERROR_QUERY_REFUSED;
		break;
	default:
		break;
	}

	#ifndef E_BOOK_DISABLE_DEPRECATED
	g_signal_emit (book_view, signals[SEQUENCE_COMPLETE], 0, bv_status);
	#endif
	g_signal_emit (book_view, signals[VIEW_COMPLETE], 0, bv_status, message);
}

/*
 * e_book_view_new:
 * @book: an #EBook
 * @gdbus_bookview: The #EGdbusBookView to get signals from
 *
 * Creates a new #EBookView based on #EBook and listening to @gdbus_bookview.  This
 * is a private function, applications should call #e_book_get_book_view or
 * #e_book_async_get_book_view.
 *
 * Returns: A new #EBookView.
 **/
EBookView *
_e_book_view_new (EBook *book, EGdbusBookView *gdbus_bookview)
{
	EBookView *view;
	EBookViewPrivate *priv;

	view = g_object_new (E_TYPE_BOOK_VIEW, NULL);
	priv = view->priv;

	priv->book = g_object_ref (book);

	/* Take ownership of the gdbus_bookview object */
	priv->gdbus_bookview = gdbus_bookview;

	g_object_add_weak_pointer (G_OBJECT (gdbus_bookview), (gpointer) &priv->gdbus_bookview);
	g_signal_connect (priv->gdbus_bookview, "contacts-added", G_CALLBACK (contacts_added_cb), view);
	g_signal_connect (priv->gdbus_bookview, "contacts-changed", G_CALLBACK (contacts_changed_cb), view);
	g_signal_connect (priv->gdbus_bookview, "contacts-removed", G_CALLBACK (contacts_removed_cb), view);
	g_signal_connect (priv->gdbus_bookview, "status-message", G_CALLBACK (status_message_cb), view);
	g_signal_connect (priv->gdbus_bookview, "complete", G_CALLBACK (complete_cb), view);

	return view;
}

/**
 * e_book_view_get_book:
 * @book_view: an #EBookView
 *
 * Returns the #EBook that this book view is monitoring.
 *
 * Returns: an #EBook.
 *
 * Since: 2.22
 **/
EBook *
e_book_view_get_book (EBookView *book_view)
{
	g_return_val_if_fail (E_IS_BOOK_VIEW (book_view), NULL);

	return book_view->priv->book;
}

/**
 * e_book_view_start:
 * @book_view: an #EBookView
 *
 * Tells @book_view to start processing events.
 */
void
e_book_view_start (EBookView *book_view)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_VIEW (book_view));

	book_view->priv->running = TRUE;

	if (book_view->priv->gdbus_bookview) {
		e_gdbus_book_view_call_start_sync (book_view->priv->gdbus_bookview, NULL, &error);
		if (error) {
			g_warning ("Cannot start book view: %s\n", error->message);

			/* Fake a sequence-complete so that the application knows this failed */
			/* TODO: use get_status_from_error */
			#ifndef E_BOOK_DISABLE_DEPRECATED
			g_signal_emit (book_view, signals[SEQUENCE_COMPLETE], 0, E_BOOK_VIEW_ERROR_OTHER_ERROR);
			#endif
			g_signal_emit (book_view, signals[VIEW_COMPLETE], 0, E_BOOK_VIEW_ERROR_OTHER_ERROR, error->message);

			g_error_free (error);
		}
	}
}

/**
 * e_book_view_stop:
 * @book_view: an #EBookView
 *
 * Tells @book_view to stop processing events.
 **/
void
e_book_view_stop (EBookView *book_view)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_VIEW (book_view));

	book_view->priv->running = FALSE;

	if (book_view->priv->gdbus_bookview) {
		e_gdbus_book_view_call_stop_sync (book_view->priv->gdbus_bookview, NULL, &error);
		if (error) {
			g_warning ("Cannot stop book view: %s\n", error->message);
			g_error_free (error);
		}
	}
}

static void
e_book_view_init (EBookView *book_view)
{
	EBookViewPrivate *priv = E_BOOK_VIEW_GET_PRIVATE (book_view);

	priv->gdbus_bookview = NULL;
	priv->book = NULL;
	priv->running = FALSE;

	book_view->priv = priv;
}

static void
e_book_view_dispose (GObject *object)
{
	EBookView *book_view = E_BOOK_VIEW (object);

	if (book_view->priv->gdbus_bookview) {
		GError *error = NULL;

		e_gdbus_book_view_call_dispose_sync (book_view->priv->gdbus_bookview, NULL, &error);
		g_object_unref (book_view->priv->gdbus_bookview);
		book_view->priv->gdbus_bookview = NULL;

		if (error) {
			g_warning ("Failed to dispose book view: %s", error->message);
			g_error_free (error);
		}
	}

	if (book_view->priv->book) {
		g_object_unref (book_view->priv->book);
		book_view->priv->book = NULL;
	}
}

static void
e_book_view_class_init (EBookViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EBookViewPrivate));

	signals [CONTACTS_CHANGED] = g_signal_new ("contacts_changed",
						   G_OBJECT_CLASS_TYPE (object_class),
						   G_SIGNAL_RUN_LAST,
						   G_STRUCT_OFFSET (EBookViewClass, contacts_changed),
						   NULL, NULL,
						   e_book_marshal_NONE__POINTER,
						   G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CONTACTS_REMOVED] = g_signal_new ("contacts_removed",
						   G_OBJECT_CLASS_TYPE (object_class),
						   G_SIGNAL_RUN_LAST,
						   G_STRUCT_OFFSET (EBookViewClass, contacts_removed),
						   NULL, NULL,
						   e_book_marshal_NONE__POINTER,
						   G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CONTACTS_ADDED] = g_signal_new ("contacts_added",
						 G_OBJECT_CLASS_TYPE (object_class),
						 G_SIGNAL_RUN_LAST,
						 G_STRUCT_OFFSET (EBookViewClass, contacts_added),
						 NULL, NULL,
						 e_book_marshal_NONE__POINTER,
						 G_TYPE_NONE, 1, G_TYPE_POINTER);
	#ifndef E_BOOK_DISABLE_DEPRECATED
	signals [SEQUENCE_COMPLETE] = g_signal_new ("sequence_complete",
						    G_OBJECT_CLASS_TYPE (object_class),
						    G_SIGNAL_RUN_LAST,
						    G_STRUCT_OFFSET (EBookViewClass, sequence_complete),
						    NULL, NULL,
						    e_book_marshal_NONE__INT,
						    G_TYPE_NONE, 1, G_TYPE_UINT);
	#endif
	signals [VIEW_COMPLETE] = g_signal_new ("view_complete",
						    G_OBJECT_CLASS_TYPE (object_class),
						    G_SIGNAL_RUN_LAST,
						    G_STRUCT_OFFSET (EBookViewClass, view_complete),
						    NULL, NULL,
						    e_book_marshal_NONE__UINT_STRING,
						    G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [STATUS_MESSAGE] = g_signal_new ("status_message",
						 G_OBJECT_CLASS_TYPE (object_class),
						 G_SIGNAL_RUN_LAST,
						 G_STRUCT_OFFSET (EBookViewClass, status_message),
						 NULL, NULL,
						 e_book_marshal_NONE__STRING,
						 G_TYPE_NONE, 1, G_TYPE_STRING);

	object_class->dispose = e_book_view_dispose;
}
