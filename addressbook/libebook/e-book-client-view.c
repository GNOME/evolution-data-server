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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include "e-book-client.h"
#include "e-book-client-view.h"
#include "e-book-client-view-private.h"
#include "e-book-marshal.h"
#include "libedata-book/e-data-book-types.h"
#include "e-gdbus-book-view.h"

G_DEFINE_TYPE (EBookClientView, e_book_client_view, G_TYPE_OBJECT);

struct _EBookClientViewPrivate {
	GDBusProxy *gdbus_bookview;
	EBookClient *client;
	gboolean running;
};

enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	PROGRESS,
	COMPLETE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
objects_added_cb (EGdbusBookView *object, const gchar * const *vcards, EBookClientView *view)
{
	const gchar * const *p;
	GSList *contacts = NULL;

	if (!view->priv->running)
		return;

	for (p = vcards; *p; p++) {
		contacts = g_slist_prepend (contacts, e_contact_new_from_vcard (*p));
	}

	contacts = g_slist_reverse (contacts);

	g_signal_emit (view, signals[OBJECTS_ADDED], 0, contacts);

	g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_slist_free (contacts);
}

static void
objects_modified_cb (EGdbusBookView *object, const gchar * const *vcards, EBookClientView *view)
{
	const gchar * const *p;
	GSList *contacts = NULL;

	if (!view->priv->running)
		return;

	for (p = vcards; *p; p++) {
		contacts = g_slist_prepend (contacts, e_contact_new_from_vcard (*p));
	}
	contacts = g_slist_reverse (contacts);

	g_signal_emit (view, signals[OBJECTS_MODIFIED], 0, contacts);

	g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_slist_free (contacts);
}

static void
objects_removed_cb (EGdbusBookView *object, const gchar * const *ids, EBookClientView *view)
{
	const gchar * const *p;
	GSList *list = NULL;

	if (!view->priv->running)
		return;

	for (p = ids; *p; p++) {
		list = g_slist_prepend (list, (gchar *) *p);
	}
	list = g_slist_reverse (list);

	g_signal_emit (view, signals[OBJECTS_REMOVED], 0, list);

	/* No need to free the values, our caller will */
	g_slist_free (list);
}

static void
progress_cb (EGdbusBookView *object, guint percent, const gchar *message, EBookClientView *view)
{
	if (!view->priv->running)
		return;

	g_signal_emit (view, signals[PROGRESS], 0, percent, message);
}

static void
complete_cb (EGdbusBookView *object, const gchar * const *in_error_strv, EBookClientView *view)
{
	GError *error = NULL;

	if (!view->priv->running)
		return;

	g_return_if_fail (e_gdbus_templates_decode_error (in_error_strv, &error));

	g_signal_emit (view, signals[COMPLETE], 0, error);

	if (error)
		g_error_free (error);
}

/*
 * _e_book_client_view_new:
 * @book_client: an #EBookClient
 * @gdbus_bookview: The #EGdbusBookView to get signals from
 *
 * Creates a new #EBookClientView based on #EBookClient and listening to @gdbus_bookview.
 * This is a private function, applications should call e_book_client_get_view() or
 * e_book_client_get_view_sync().
 *
 * Returns: A new #EBookClientView.
 **/
EBookClientView *
_e_book_client_view_new (EBookClient *book_client, EGdbusBookView *gdbus_bookview)
{
	EBookClientView *view;
	EBookClientViewPrivate *priv;

	view = g_object_new (E_TYPE_BOOK_CLIENT_VIEW, NULL);
	priv = view->priv;

	priv->client = g_object_ref (book_client);

	/* Take ownership of the gdbus_bookview object */
	priv->gdbus_bookview = g_object_ref (G_DBUS_PROXY (gdbus_bookview));

	g_object_add_weak_pointer (G_OBJECT (gdbus_bookview), (gpointer) &priv->gdbus_bookview);
	g_signal_connect (priv->gdbus_bookview, "objects-added", G_CALLBACK (objects_added_cb), view);
	g_signal_connect (priv->gdbus_bookview, "objects-modified", G_CALLBACK (objects_modified_cb), view);
	g_signal_connect (priv->gdbus_bookview, "objects-removed", G_CALLBACK (objects_removed_cb), view);
	g_signal_connect (priv->gdbus_bookview, "progress", G_CALLBACK (progress_cb), view);
	g_signal_connect (priv->gdbus_bookview, "complete", G_CALLBACK (complete_cb), view);

	return view;
}

/**
 * e_book_client_view_get_client:
 * @view: an #EBookClientView
 *
 * Returns the #EBookClient that this book view is monitoring.
 *
 * Returns: an #EBookClient.
 **/
EBookClient *
e_book_client_view_get_client (EBookClientView *view)
{
	g_return_val_if_fail (E_IS_BOOK_CLIENT_VIEW (view), NULL);

	return view->priv->client;
}

/**
 * e_book_client_view_start:
 * @error: A #GError
 * @view: an #EBookClientView
 *
 * Tells @view to start processing events.
 */
void
e_book_client_view_start (EBookClientView *view, GError **error)
{
	EBookClientViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	priv = view->priv;

	if (priv->gdbus_bookview) {
		GError *local_error = NULL;

		if (e_gdbus_book_view_call_start_sync (priv->gdbus_bookview, NULL, &local_error))
			priv->running = TRUE;

		e_client_util_unwrap_dbus_error (local_error, error, NULL, 0, 0, FALSE);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR, _("Cannot start view, D-Bus proxy gone"));
	}
}

/**
 * e_book_client_view_stop:
 * @view: an #EBookClientView
 * @error: A #GError
 *
 * Tells @view to stop processing events.
 **/
void
e_book_client_view_stop (EBookClientView *view, GError **error)
{
	EBookClientViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	priv = view->priv;
	priv->running = FALSE;

	if (priv->gdbus_bookview) {
		GError *local_error = NULL;

		e_gdbus_book_view_call_stop_sync (priv->gdbus_bookview, NULL, &local_error);

		e_client_util_unwrap_dbus_error (local_error, error, NULL, 0, 0, FALSE);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR, _("Cannot stop view, D-Bus proxy gone"));
	}
}

/**
 * e_book_client_view_set_fields_of_interest:
 * @view: An #EBookClientView object
 * @fields_of_interest: List of field names in which the client is interested
 * @error: A #GError
 *
 * Client can instruct server to which fields it is interested in only, thus
 * the server can return less data over the wire. The server can still return
 * complete objects, this is just a hint to it that the listed fields will
 * be used only. The UID field is returned always. Initial views has no fields
 * of interest and using %NULL for @fields_of_interest will unset any previous
 * changes.
 *
 * Some backends can use summary information of its cache to create artifical
 * objects, which will omit stored object parsing. If this cannot be done then
 * it will simply return object as is stored in the cache.
 **/
void
e_book_client_view_set_fields_of_interest (EBookClientView *view, const GSList *fields_of_interest, GError **error)
{
	EBookClientViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	priv = view->priv;

	if (priv->gdbus_bookview) {
		GError *local_error = NULL;
		gchar **strv;

		strv = e_client_util_slist_to_strv (fields_of_interest);
		e_gdbus_book_view_call_set_fields_of_interest_sync (priv->gdbus_bookview, (const gchar * const *) strv, NULL, &local_error);
		g_strfreev (strv);

		e_client_util_unwrap_dbus_error (local_error, error, NULL, 0, 0, FALSE);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR, _("Cannot set fields of interest, D-Bus proxy gone"));
	}
}

static void
e_book_client_view_init (EBookClientView *view)
{
	view->priv = G_TYPE_INSTANCE_GET_PRIVATE (view, E_TYPE_BOOK_CLIENT_VIEW, EBookClientViewPrivate);
	view->priv->gdbus_bookview = NULL;

	view->priv->client = NULL;
	view->priv->running = FALSE;
}

static void
book_client_view_dispose (GObject *object)
{
	EBookClientView *view = E_BOOK_CLIENT_VIEW (object);

	if (view->priv->gdbus_bookview) {
		GError *error = NULL;

		g_signal_handlers_disconnect_matched (view->priv->gdbus_bookview, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, view);
		e_gdbus_book_view_call_dispose_sync (G_DBUS_PROXY (view->priv->gdbus_bookview), NULL, &error);
		g_object_unref (view->priv->gdbus_bookview);
		view->priv->gdbus_bookview = NULL;

		if (error) {
			g_warning ("Failed to dispose book view: %s", error->message);
			g_error_free (error);
		}
	}

	if (view->priv->client) {
		g_object_unref (view->priv->client);
		view->priv->client = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_client_view_parent_class)->dispose (object);
}

static void
e_book_client_view_class_init (EBookClientViewClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EBookClientViewPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = book_client_view_dispose;

	signals [OBJECTS_ADDED] =
		g_signal_new ("objects-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClientViewClass, objects_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals [OBJECTS_MODIFIED] =
		g_signal_new ("objects-modified",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClientViewClass, objects_modified),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals [OBJECTS_REMOVED] =
		g_signal_new ("objects-removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClientViewClass, objects_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals [PROGRESS] =
		g_signal_new ("progress",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClientViewClass, progress),
			      NULL, NULL,
			      e_gdbus_marshallers_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

	signals [COMPLETE] =
		g_signal_new ("complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EBookClientViewClass, complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_ERROR);
}
