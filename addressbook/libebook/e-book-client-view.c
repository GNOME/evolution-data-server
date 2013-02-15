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

#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>
#include <libedata-book/libedata-book.h>

#include "e-book-client.h"
#include "e-book-client-view.h"
#include "e-book-marshal.h"
#include "e-gdbus-book-view.h"

#define E_BOOK_CLIENT_VIEW_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_CLIENT_VIEW, EBookClientViewPrivate))

struct _EBookClientViewPrivate {
	EBookClient *client;
	GDBusProxy *dbus_proxy;
	GDBusConnection *connection;
	gchar *object_path;
	guint running : 1;
	guint complete : 1;

	EDataBook *direct_book;

	gulong objects_added_handler_id;
	gulong objects_modified_handler_id;
	gulong objects_removed_handler_id;
	gulong progress_handler_id;
	gulong complete_handler_id;
};

enum {
	PROP_0,
	PROP_CLIENT,
	PROP_CONNECTION,
	PROP_OBJECT_PATH,
	PROP_DIRECT_BOOK
};

enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	PROGRESS,
	COMPLETE,
	LAST_SIGNAL
};

/* Forward Declarations */
static void	e_book_client_view_initable_init
						(GInitableIface *interface);

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (
	EBookClientView,
	e_book_client_view,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_book_client_view_initable_init))

typedef struct {
	EBookClientView *view;
	guint signum;
} NotificationData;

static gchar *
direct_contacts_query (const gchar * const *uids)
{
	EBookQuery *query, **qs;
	gchar *sexp;
	gint i, len;

	len = g_strv_length ((gchar **)uids);
	qs = g_new0 (EBookQuery *, len);

	for (i = 0; uids[i] != NULL; i++) {
		const gchar *uid = uids[i];

		qs[i] = e_book_query_field_test (E_CONTACT_UID, E_BOOK_QUERY_IS, uid);
	}

	query = e_book_query_or (len, qs, TRUE);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	return sexp;
}

static void
direct_contacts_ready (GObject *source_object,
		       GAsyncResult *res,
		       gpointer user_data)
{
	NotificationData *data = (NotificationData *)user_data;
	GSList *contacts = NULL;
	GError *error = NULL;

	if (!e_data_book_get_contacts_finish (E_DATA_BOOK (source_object),
					      res, &contacts, &error)) {
		g_warning ("Error fetching contacts directly: %s\n", error->message);
		g_error_free (error);
	} else {
		g_signal_emit (data->view, data->signum, 0, contacts);
	}

	g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
	g_object_unref (data->view);
	g_slice_free (NotificationData, data);
}

static void
direct_contacts_fetch (EBookClientView *view,
		       const gchar * const *uids,
		       guint signum)
{
	NotificationData *data;
	gchar *sexp = direct_contacts_query (uids);

	/* Until the view has completely loaded, we need to make
	 * sync calls to the backend
	 */
	if (!view->priv->complete) {
		GSList *contacts = NULL;
		GError *error = NULL;

		if (!e_data_book_get_contacts_sync (view->priv->direct_book,
						    sexp, &contacts, NULL, &error)) {
			g_warning ("Error fetching contacts directly: %s\n", error->message);
			g_error_free (error);
		} else {
			g_signal_emit (view, signum, 0, contacts);
			g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
		}

	} else {
		/* Make async calls, avoid blocking the thread owning the view
		 * as much as possible
		 */
		data = g_slice_new (NotificationData);
		data->view = g_object_ref (view);
		data->signum = signum;

		e_data_book_get_contacts (view->priv->direct_book,
					  sexp, NULL, direct_contacts_ready, data);
	}

	g_free (sexp);
}

static void
book_client_view_objects_added_cb (EGdbusBookView *object,
                                   const gchar * const *vcards,
                                   EBookClientView *view)
{
	const gchar * const *p;
	GSList *list = NULL;

	if (!view->priv->running)
		return;

	/* array contains UIDs only */
	if (view->priv->direct_book) {
		direct_contacts_fetch (view, vcards, signals[OBJECTS_ADDED]);
		return;
	}

	/* array contains both UID and vcard */
	for (p = vcards; p[0] && p[1]; p += 2) {
		EContact *contact;
		const gchar *vcard = p[0];
		const gchar *uid = p[1];

		contact = e_contact_new_from_vcard_with_uid (vcard, uid);
		list = g_slist_prepend (list, contact);
	}

	list = g_slist_reverse (list);

	g_signal_emit (view, signals[OBJECTS_ADDED], 0, list);

	g_slist_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
book_client_view_objects_modified_cb (EGdbusBookView *object,
                                      const gchar * const *vcards,
                                      EBookClientView *view)
{
	const gchar * const *p;
	GSList *list = NULL;

	if (!view->priv->running)
		return;

	/* array contains UIDs only */
	if (view->priv->direct_book) {
		direct_contacts_fetch (view, vcards, signals[OBJECTS_MODIFIED]);
		return;
	}

	/* array contains both UID and vcard */
	for (p = vcards; p[0] && p[1]; p += 2) {
		EContact *contact;
		const gchar *vcard = p[0];
		const gchar *uid = p[1];

		contact = e_contact_new_from_vcard_with_uid (vcard, uid);
		list = g_slist_prepend (list, contact);
	}

	list = g_slist_reverse (list);

	g_signal_emit (view, signals[OBJECTS_MODIFIED], 0, list);

	g_slist_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
book_client_view_objects_removed_cb (EGdbusBookView *object,
                                     const gchar * const *ids,
                                     EBookClientView *view)
{
	const gchar * const *p;
	GSList *list = NULL;

	if (!view->priv->running)
		return;

	for (p = ids; *p; p++)
		list = g_slist_prepend (list, (gchar *) *p);

	list = g_slist_reverse (list);

	g_signal_emit (view, signals[OBJECTS_REMOVED], 0, list);

	/* No need to free the values, our caller will */
	g_slist_free (list);
}

static void
book_client_view_progress_cb (EGdbusBookView *object,
                              guint percent,
                              const gchar *message,
                              EBookClientView *view)
{
	if (!view->priv->running)
		return;

	g_signal_emit (view, signals[PROGRESS], 0, percent, message);
}

static void
book_client_view_complete_cb (EGdbusBookView *object,
                              const gchar * const *in_error_strv,
                              EBookClientView *view)
{
	GError *error = NULL;

	if (!view->priv->running)
		return;

	view->priv->complete = TRUE;

	g_return_if_fail (e_gdbus_templates_decode_error (in_error_strv, &error));

	g_signal_emit (view, signals[COMPLETE], 0, error);

	if (error != NULL)
		g_error_free (error);
}

static void
book_client_view_set_client (EBookClientView *view,
                             EBookClient *client)
{
	g_return_if_fail (E_IS_BOOK_CLIENT (client));
	g_return_if_fail (view->priv->client == NULL);

	view->priv->client = g_object_ref (client);
}

static void
book_client_view_set_connection (EBookClientView *view,
                                 GDBusConnection *connection)
{
	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (view->priv->connection == NULL);

	view->priv->connection = g_object_ref (connection);
}

static void
book_client_view_set_object_path (EBookClientView *view,
                                  const gchar *object_path)
{
	g_return_if_fail (object_path != NULL);
	g_return_if_fail (view->priv->object_path == NULL);

	view->priv->object_path = g_strdup (object_path);
}

static void
book_client_view_set_direct_book (EBookClientView *view,
				  EDataBook       *book)
{
	g_return_if_fail (book == NULL ||
			  E_IS_DATA_BOOK (book));
	g_return_if_fail (view->priv->direct_book == NULL);

	if (book)
		view->priv->direct_book = g_object_ref (book);
}

static void
book_client_view_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CLIENT:
			book_client_view_set_client (
				E_BOOK_CLIENT_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_CONNECTION:
			book_client_view_set_connection (
				E_BOOK_CLIENT_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			book_client_view_set_object_path (
				E_BOOK_CLIENT_VIEW (object),
				g_value_get_string (value));
			return;

		case PROP_DIRECT_BOOK:
			book_client_view_set_direct_book (
				E_BOOK_CLIENT_VIEW (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_client_view_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CLIENT:
			g_value_set_object (
				value,
				e_book_client_view_get_client (
				E_BOOK_CLIENT_VIEW (object)));
			return;

		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_book_client_view_get_connection (
				E_BOOK_CLIENT_VIEW (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_book_client_view_get_object_path (
				E_BOOK_CLIENT_VIEW (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
book_client_view_dispose (GObject *object)
{
	EBookClientViewPrivate *priv;

	priv = E_BOOK_CLIENT_VIEW_GET_PRIVATE (object);

	if (priv->client != NULL) {
		g_object_unref (priv->client);
		priv->client = NULL;
	}

	if (priv->connection != NULL) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (priv->direct_book != NULL) {
		g_object_unref (priv->direct_book);
		priv->direct_book = NULL;
	}

	if (priv->dbus_proxy != NULL) {
		GError *error = NULL;

		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->objects_added_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->objects_modified_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->objects_removed_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->progress_handler_id);
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->complete_handler_id);

		e_gdbus_book_view_call_dispose_sync (
			priv->dbus_proxy, NULL, &error);

		if (error != NULL) {
			g_dbus_error_strip_remote_error (error);
			g_warning (
				"Failed to dispose book view: %s",
				error->message);
			g_error_free (error);
		}

		g_object_unref (priv->dbus_proxy);
		priv->dbus_proxy = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_client_view_parent_class)->dispose (object);
}

static void
book_client_view_finalize (GObject *object)
{
	EBookClientViewPrivate *priv;

	priv = E_BOOK_CLIENT_VIEW_GET_PRIVATE (object);

	g_free (priv->object_path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_client_view_parent_class)->finalize (object);
}

static gboolean
book_client_view_initable_init (GInitable *initable,
                                GCancellable *cancellable,
                                GError **error)
{
	EBookClientViewPrivate *priv;
	EGdbusBookView *gdbus_bookview;
	gulong handler_id;

	priv = E_BOOK_CLIENT_VIEW_GET_PRIVATE (initable);

	gdbus_bookview = e_gdbus_book_view_proxy_new_sync (
		priv->connection,
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		priv->object_path,
		cancellable, error);

	if (gdbus_bookview == NULL)
		return FALSE;

	priv->dbus_proxy = G_DBUS_PROXY (gdbus_bookview);

	handler_id = g_signal_connect (
		priv->dbus_proxy, "objects-added",
		G_CALLBACK (book_client_view_objects_added_cb), initable);
	priv->objects_added_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->dbus_proxy, "objects-modified",
		G_CALLBACK (book_client_view_objects_modified_cb), initable);
	priv->objects_modified_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->dbus_proxy, "objects-removed",
		G_CALLBACK (book_client_view_objects_removed_cb), initable);
	priv->objects_removed_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->dbus_proxy, "progress",
		G_CALLBACK (book_client_view_progress_cb), initable);
	priv->progress_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->dbus_proxy, "complete",
		G_CALLBACK (book_client_view_complete_cb), initable);
	priv->complete_handler_id = handler_id;

	/* When in direct read access mode, we add a special field
	 * to fields-of-interest indicating we only want uids sent
	 */
	if (priv->direct_book)
		e_book_client_view_set_fields_of_interest (E_BOOK_CLIENT_VIEW (initable), NULL, NULL);

	return TRUE;
}

static void
e_book_client_view_class_init (EBookClientViewClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EBookClientViewPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = book_client_view_set_property;
	object_class->get_property = book_client_view_get_property;
	object_class->dispose = book_client_view_dispose;
	object_class->finalize = book_client_view_finalize;

	g_object_class_install_property (
		object_class,
		PROP_CLIENT,
		g_param_spec_object (
			"client",
			"The EBookClient for the view",
			NULL,
			E_TYPE_BOOK_CLIENT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"The GDBusConnection used "
			"to create the D-Bus proxy",
			G_TYPE_DBUS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OBJECT_PATH,
		g_param_spec_string (
			"object-path",
			"Object Path",
			"The object path used "
			"to create the D-Bus proxy",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_DIRECT_BOOK,
		g_param_spec_object (
			"direct-book",
			"Direct Book",
			"The EDataBook to fetch contact "
			"data from, if direct read access "
			"is enabled",
			E_TYPE_DATA_BOOK,
			G_PARAM_WRITABLE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	signals[OBJECTS_ADDED] = g_signal_new (
		"objects-added",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClientViewClass, objects_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[OBJECTS_MODIFIED] = g_signal_new (
		"objects-modified",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClientViewClass, objects_modified),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[OBJECTS_REMOVED] = g_signal_new (
		"objects-removed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClientViewClass, objects_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[PROGRESS] = g_signal_new (
		"progress",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClientViewClass, progress),
		NULL, NULL,
		e_gdbus_marshallers_VOID__UINT_STRING,
		G_TYPE_NONE, 2,
		G_TYPE_UINT,
		G_TYPE_STRING);

	signals[COMPLETE] = g_signal_new (
		"complete",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClientViewClass, complete),
		NULL, NULL,
		g_cclosure_marshal_VOID__BOXED,
		G_TYPE_NONE, 1,
		G_TYPE_ERROR);
}

static void
e_book_client_view_initable_init (GInitableIface *interface)
{
	interface->init = book_client_view_initable_init;
}

static void
e_book_client_view_init (EBookClientView *view)
{
	view->priv = E_BOOK_CLIENT_VIEW_GET_PRIVATE (view);
}

/**
 * e_book_client_view_get_client:
 * @view: an #EBookClientView
 *
 * Returns the #EBookClient associated with @view.
 *
 * Returns: (transfer none): an #EBookClient
 **/
EBookClient *
e_book_client_view_get_client (EBookClientView *view)
{
	g_return_val_if_fail (E_IS_BOOK_CLIENT_VIEW (view), NULL);

	return view->priv->client;
}

/**
 * e_book_client_view_get_connection:
 * @view: an #EBookClientView
 *
 * Returns the #GDBusConnection used to create the D-Bus proxy.
 *
 * Returns: (transfer none): the #GDBusConnection
 *
 * Since: 3.8
 **/
GDBusConnection *
e_book_client_view_get_connection (EBookClientView *view)
{
	g_return_val_if_fail (E_IS_BOOK_CLIENT_VIEW (view), NULL);

	return view->priv->connection;
}

/**
 * e_book_client_view_get_object_path:
 * @view: an #EBookClientView
 *
 * Returns the object path used to create the D-Bus proxy.
 *
 * Returns: the object path
 *
 * Since: 3.8
 **/
const gchar *
e_book_client_view_get_object_path (EBookClientView *view)
{
	g_return_val_if_fail (E_IS_BOOK_CLIENT_VIEW (view), NULL);

	return view->priv->object_path;
}

/**
 * e_book_client_view_start:
 * @view: an #EBookClientView
 * @error: return location for a #GError, or %NULL
 *
 * Tells @view to start processing events.
 */
void
e_book_client_view_start (EBookClientView *view,
                          GError **error)
{
	gboolean success;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	view->priv->running = TRUE;

	success = e_gdbus_book_view_call_start_sync (
		view->priv->dbus_proxy, NULL, &local_error);
	if (!success)
		view->priv->running = FALSE;

	e_client_unwrap_dbus_error (
		E_CLIENT (view->priv->client), local_error, error);
}

/**
 * e_book_client_view_stop:
 * @view: an #EBookClientView
 * @error: return location for a #GError, or %NULL
 *
 * Tells @view to stop processing events.
 **/
void
e_book_client_view_stop (EBookClientView *view,
                         GError **error)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	view->priv->running = FALSE;

	e_gdbus_book_view_call_stop_sync (
		view->priv->dbus_proxy, NULL, &local_error);

	e_client_unwrap_dbus_error (
		E_CLIENT (view->priv->client), local_error, error);
}

/**
 * e_book_client_view_set_flags:
 * @view: an #EBookClientView
 * @flags: the #EBookClientViewFlags for @view
 * @error: return location for a #GError, or %NULL
 *
 * Sets the @flags which control the behaviour of @view.
 *
 * Since: 3.4
 */
void
e_book_client_view_set_flags (EBookClientView *view,
                              EBookClientViewFlags flags,
                              GError **error)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	e_gdbus_book_view_call_set_flags_sync (
		view->priv->dbus_proxy, flags, NULL, &local_error);

	e_client_unwrap_dbus_error (
		E_CLIENT (view->priv->client), local_error, error);
}

/**
 * e_book_client_view_set_fields_of_interest:
 * @view: An #EBookClientView object
 * @fields_of_interest: (element-type utf8): List of field names in which
 *                      the client is interested
 * @error: return location for a #GError, or %NULL
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
e_book_client_view_set_fields_of_interest (EBookClientView *view,
                                           const GSList *fields_of_interest,
                                           GError **error)
{
	gchar **strv;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_BOOK_CLIENT_VIEW (view));

	/* When in direct read access mode, ensure that the
	 * backend is configured to only send us UIDs for everything,
	 *
	 * Just ignore the fields_of_interest and use them locally
	 * when filtering cards to be returned in direct reads.
	 */
	if (view->priv->direct_book) {
		GSList uid_field = { 0, };

		uid_field.data = (gpointer)"x-evolution-uids-only";
		strv = e_client_util_slist_to_strv (&uid_field);
	} else
		strv = e_client_util_slist_to_strv (fields_of_interest);

	e_gdbus_book_view_call_set_fields_of_interest_sync (
		view->priv->dbus_proxy,
		(const gchar * const *) strv,
		NULL, &local_error);
	g_strfreev (strv);

	e_client_unwrap_dbus_error (
		E_CLIENT (view->priv->client), local_error, error);
}

