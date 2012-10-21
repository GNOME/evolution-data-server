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

/* e-book deprecated since 3.2, use e-book-client instead */

/**
 * SECTION:e-book
 *
 * The old asynchronous API was deprecated since 3.0 and is replaced with
 * their an equivalent version which has a detailed #GError
 * structure in the asynchronous callback, instead of a status code only.
 *
 * As an example, e_book_async_open() is replaced by e_book_open_async().
 *
 * Deprecated: 3.2: Use #EBookClient instead.
 */

#include <config.h>
#include <unistd.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include "e-book.h"
#include "e-error.h"
#include "e-contact.h"
#include "e-name-western.h"
#include "e-book-view-private.h"
#include "e-book-marshal.h"

#include "e-gdbus-book.h"
#include "e-gdbus-book-factory.h"
#include "e-gdbus-book-view.h"

#define E_BOOK_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK, EBookPrivate))

#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"
#define BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS		"required-fields"
#define BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS		"supported-fields"
#define BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS	"supported-auth-methods"

static gchar ** flatten_stringlist (GList *list);
static GList *array_to_stringlist (gchar **list);
static EList *array_to_elist (gchar **list);
static gboolean unwrap_gerror (GError *error, GError **client_error);

G_DEFINE_TYPE (EBook, e_book, G_TYPE_OBJECT)
enum {
	WRITABLE_STATUS,
	CONNECTION_STATUS,
	AUTH_REQUIRED,
	BACKEND_DIED,
	LAST_SIGNAL
};

static guint e_book_signals[LAST_SIGNAL];

struct _EBookPrivate {
	GDBusProxy *gdbus_book;
	guint gone_signal_id;

	ESource *source;
	gboolean loaded;
	gboolean writable;
	gboolean connected;
	gchar *cap;
	gboolean cap_queried;
};

static guint active_books = 0, book_connection_closed_id = 0;
static EGdbusBookFactory *book_factory_proxy = NULL;
static GStaticRecMutex book_factory_proxy_lock = G_STATIC_REC_MUTEX_INIT;
#define LOCK_FACTORY()   g_static_rec_mutex_lock (&book_factory_proxy_lock)
#define UNLOCK_FACTORY() g_static_rec_mutex_unlock (&book_factory_proxy_lock)

typedef struct {
	EBook *book;
	gpointer callback; /* TODO union */
	gpointer excallback;
	gpointer closure;
	gpointer data;
} AsyncData;

GQuark
e_book_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("e-book-error-quark");

	return q;
}

static void
gdbus_book_disconnect (EBook *book);

/*
 * Called when the addressbook server dies.
 */
static void
gdbus_book_closed_cb (GDBusConnection *connection,
                      gboolean remote_peer_vanished,
                      GError *error,
                      EBook *book)
{
	GError *err = NULL;

	g_assert (E_IS_BOOK (book));

	if (error)
		unwrap_gerror (g_error_copy (error), &err);

	if (err) {
		g_debug (G_STRLOC ": EBook GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else {
		g_debug (G_STRLOC ": EBook GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	gdbus_book_disconnect (book);

	g_signal_emit (G_OBJECT (book), e_book_signals[BACKEND_DIED], 0);
}

static void
gdbus_book_connection_gone_cb (GDBusConnection *connection,
                               const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name,
                               GVariant *parameters,
                               gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	gdbus_book_closed_cb (connection, TRUE, NULL, user_data);
}

static void
gdbus_book_disconnect (EBook *book)
{
	/* Ensure that everything relevant is NULL */
	LOCK_FACTORY ();

	if (book->priv->gdbus_book) {
		GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book->priv->gdbus_book));

		g_signal_handlers_disconnect_by_func (connection, gdbus_book_closed_cb, book);
		g_dbus_connection_signal_unsubscribe (connection, book->priv->gone_signal_id);
		book->priv->gone_signal_id = 0;

		e_gdbus_book_call_close_sync (book->priv->gdbus_book, NULL, NULL);
		g_object_unref (book->priv->gdbus_book);
		book->priv->gdbus_book = NULL;
	}
	UNLOCK_FACTORY ();
}

static void
e_book_dispose (GObject *object)
{
	EBook *book = E_BOOK (object);

	book->priv->loaded = FALSE;

	gdbus_book_disconnect (book);

	if (book->priv->source) {
		g_object_unref (book->priv->source);
		book->priv->source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_parent_class)->dispose (object);
}

static void
e_book_finalize (GObject *object)
{
	EBook *book = E_BOOK (object);

	if (book->priv->cap)
		g_free (book->priv->cap);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_parent_class)->finalize (object);

	LOCK_FACTORY ();
	active_books--;
	UNLOCK_FACTORY ();
}

static void
e_book_class_init (EBookClass *e_book_class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (e_book_class);

	e_book_signals[WRITABLE_STATUS] = g_signal_new (
		"writable_status",
		G_OBJECT_CLASS_TYPE (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClass, writable_status),
		NULL, NULL,
		e_book_marshal_NONE__BOOL,
		G_TYPE_NONE, 1,
		G_TYPE_BOOLEAN);

	e_book_signals[CONNECTION_STATUS] = g_signal_new (
		"connection_status",
		G_OBJECT_CLASS_TYPE (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClass, connection_status),
		NULL, NULL,
		e_book_marshal_NONE__BOOL,
		G_TYPE_NONE, 1,
		G_TYPE_BOOLEAN);

	e_book_signals[BACKEND_DIED] = g_signal_new (
		"backend_died",
		G_OBJECT_CLASS_TYPE (gobject_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EBookClass, backend_died),
		NULL, NULL,
		e_book_marshal_NONE__NONE,
		G_TYPE_NONE, 0);

	gobject_class->dispose = e_book_dispose;
	gobject_class->finalize = e_book_finalize;

	g_type_class_add_private (e_book_class, sizeof (EBookPrivate));
}

static void
e_book_init (EBook *book)
{
	book->priv = E_BOOK_GET_PRIVATE (book);

	LOCK_FACTORY ();
	active_books++;
	UNLOCK_FACTORY ();

	book->priv->gdbus_book = NULL;
	book->priv->source = NULL;
	book->priv->loaded = FALSE;
	book->priv->writable = FALSE;
	book->priv->connected = FALSE;
	book->priv->cap = NULL;
	book->priv->cap_queried = FALSE;
}

static void
book_factory_proxy_closed_cb (GDBusConnection *connection,
                              gboolean remote_peer_vanished,
                              GError *error,
                              gpointer user_data)
{
	GError *err = NULL;

	LOCK_FACTORY ();

	if (book_connection_closed_id) {
		g_dbus_connection_signal_unsubscribe (connection, book_connection_closed_id);
		book_connection_closed_id = 0;
		g_signal_handlers_disconnect_by_func (connection, book_factory_proxy_closed_cb, NULL);
	}

	if (book_factory_proxy) {
		g_object_unref (book_factory_proxy);
		book_factory_proxy = NULL;
	}

	if (error)
		unwrap_gerror (g_error_copy (error), &err);

	if (err) {
		g_debug ("GDBus connection is closed%s: %s", remote_peer_vanished ? ", remote peer vanished" : "", err->message);
		g_error_free (err);
	} else if (active_books) {
		g_debug ("GDBus connection is closed%s", remote_peer_vanished ? ", remote peer vanished" : "");
	}

	UNLOCK_FACTORY ();
}

static void
book_factory_connection_gone_cb (GDBusConnection *connection,
                                 const gchar *sender_name,
                                 const gchar *object_path,
                                 const gchar *interface_name,
                                 const gchar *signal_name,
                                 GVariant *parameters,
                                 gpointer user_data)
{
	/* signal subscription takes care of correct parameters,
	 * thus just do what is to be done here */
	book_factory_proxy_closed_cb (connection, TRUE, NULL, user_data);
}

static gboolean
e_book_activate (GError **error)
{
	GDBusConnection *connection;

	LOCK_FACTORY ();

	if (G_LIKELY (book_factory_proxy)) {
		UNLOCK_FACTORY ();
		return TRUE;
	}

	book_factory_proxy = e_gdbus_book_factory_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/AddressBookFactory",
		NULL,
		error);

	if (!book_factory_proxy) {
		UNLOCK_FACTORY ();
		return FALSE;
	}

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy));
	book_connection_closed_id = g_dbus_connection_signal_subscribe (
		connection,
		NULL,						/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.AddressBook",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		book_factory_connection_gone_cb, NULL, NULL);

	g_signal_connect (connection, "closed", G_CALLBACK (book_factory_proxy_closed_cb), NULL);

	UNLOCK_FACTORY ();

	return TRUE;
}

static void
readonly_cb (EGdbusBook *object,
             gboolean readonly,
             EBook *book)
{
	g_return_if_fail (E_IS_BOOK (book));

	book->priv->writable = !readonly;

	g_signal_emit (G_OBJECT (book), e_book_signals[WRITABLE_STATUS], 0, book->priv->writable);
}

static void
online_cb (EGdbusBook *object,
           gboolean is_online,
           EBook *book)
{
	g_return_if_fail (E_IS_BOOK (book));

	book->priv->connected = is_online;

	g_signal_emit (G_OBJECT (book), e_book_signals[CONNECTION_STATUS], 0, is_online);
}

/**
 * e_book_add_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Adds @contact to @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_book_client_add_contact_sync() instead.
 **/
gboolean
e_book_add_contact (EBook *book,
                    EContact *contact,
                    GError **error)
{
	GError *err = NULL;
	gchar *vcard, **uids = NULL, *gdbus_vcard = NULL;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	e_gdbus_book_call_add_contacts_sync (book->priv->gdbus_book, strv, &uids, NULL, &err);
	g_free (vcard);
	g_free (gdbus_vcard);

	if (uids) {
		e_contact_set (contact, E_CONTACT_UID, uids[0]);
		g_strfreev (uids);
	}

	return unwrap_gerror (err, error);
}

static void
add_contact_reply (GObject *gdbus_book,
                   GAsyncResult *res,
                   gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	gchar *uid = NULL, **uids = NULL;
	AsyncData *data = user_data;
	EBookIdAsyncCallback excb = data->excallback;
	EBookIdCallback cb = data->callback;

	e_gdbus_book_call_add_contacts_finish (G_DBUS_PROXY (gdbus_book), res, &uids, &error);

	unwrap_gerror (error, &err);

	/* If there is an error returned the GLib bindings currently return garbage
	 * for the OUT values. This is bad. */
	if (error)
		uid = NULL;
	else
		uid = uids[0];

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, uid, data->closure);
	if (excb)
		excb (data->book, err, uid, data->closure);

	if (err)
		g_error_free (err);

	if (uids)
		g_strfreev (uids);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_add_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Adds @contact to @book without blocking.
 *
 * Returns: %TRUE if the operation was started, %FALSE otherwise.
 *
 * Deprecated: 3.0: Use e_book_add_contact_async() instead.
 **/
gboolean
e_book_async_add_contact (EBook *book,
                          EContact *contact,
                          EBookIdCallback cb,
                          gpointer closure)
{
	gchar *vcard, *gdbus_vcard = NULL;
	AsyncData *data;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_add_contacts (book->priv->gdbus_book, strv, NULL, add_contact_reply, data);

	g_free (vcard);
	g_free (gdbus_vcard);

	return TRUE;
}

/**
 * e_book_add_contact_async:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Adds @contact to @book without blocking.
 *
 * Returns: %TRUE if the operation was started, %FALSE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_add_contact() and e_book_client_add_contact_finish() instead.
 **/
gboolean
e_book_add_contact_async (EBook *book,
                          EContact *contact,
                          EBookIdAsyncCallback cb,
                          gpointer closure)
{
	gchar *vcard, *gdbus_vcard = NULL;
	AsyncData *data;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_add_contacts (book->priv->gdbus_book, strv, NULL, add_contact_reply, data);

	g_free (vcard);
	g_free (gdbus_vcard);

	return TRUE;
}

/**
 * e_book_commit_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Applies the changes made to @contact to the stored version in
 * @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_book_client_modify_contact_sync() instead.
 **/
gboolean
e_book_commit_contact (EBook *book,
                       EContact *contact,
                       GError **error)
{
	GError *err = NULL;
	gchar *vcard, *gdbus_vcard = NULL;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	e_gdbus_book_call_modify_contacts_sync (book->priv->gdbus_book, strv, NULL, &err);
	g_free (vcard);
	g_free (gdbus_vcard);

	return unwrap_gerror (err, error);
}

static void
modify_contacts_reply (GObject *gdbus_book,
                      GAsyncResult *res,
                      gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookAsyncCallback excb = data->excallback;
	EBookCallback cb = data->callback;

	e_gdbus_book_call_modify_contacts_finish (G_DBUS_PROXY (gdbus_book), res, &error);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, data->closure);

	if (excb)
		excb (data->book, err, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_commit_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Applies the changes made to @contact to the stored version in
 * @book without blocking.
 *
 * Returns: %TRUE if the operation was started, %FALSE otherwise.
 *
 * Deprecated: 3.0: Use e_book_commit_contact_async() instead.
 **/
gboolean
e_book_async_commit_contact (EBook *book,
                             EContact *contact,
                             EBookCallback cb,
                             gpointer closure)
{
	gchar *vcard, *gdbus_vcard = NULL;
	AsyncData *data;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;
	e_gdbus_book_call_modify_contacts (book->priv->gdbus_book, strv, NULL, modify_contacts_reply, data);

	g_free (vcard);
	g_free (gdbus_vcard);

	return TRUE;
}

/**
 * e_book_commit_contact_async:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Applies the changes made to @contact to the stored version in
 * @book without blocking.
 *
 * Returns: %TRUE if the operation was started, %FALSE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_modify_contact() and e_book_client_modify_contact_finish() instead.
 **/
gboolean
e_book_commit_contact_async (EBook *book,
                             EContact *contact,
                             EBookAsyncCallback cb,
                             gpointer closure)
{
	gchar *vcard, *gdbus_vcard = NULL;
	AsyncData *data;
	const gchar *strv[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_ex_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	strv[0] = e_util_ensure_gdbus_string (vcard, &gdbus_vcard);
	strv[1] = NULL;

	e_gdbus_book_call_modify_contacts (book->priv->gdbus_book, strv, NULL, modify_contacts_reply, data);

	g_free (vcard);
	g_free (gdbus_vcard);

	return TRUE;
}

/**
 * e_book_get_required_fields:
 * @book: an #EBook
 * @fields: (out) (transfer full) (element-type utf8): a #GList of fields to set on success
 * @error: a #GError to set on failure
 *
 * Gets a list of fields that are required to be filled in for
 * all contacts in this @book. The list will contain pointers
 * to allocated strings, and both the #GList and the strings
 * must be freed by the caller.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_client_get_backend_property_sync() on
 * an #EBookClient object with #BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS instead.
 **/
gboolean
e_book_get_required_fields (EBook *book,
                            GList **fields,
                            GError **error)
{
	GError *err = NULL;
	gchar **list = NULL, *list_str = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	e_gdbus_book_call_get_backend_property_sync (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS, &list_str, NULL, &err);

	list = g_strsplit (list_str, ",", -1);
	g_free (list_str);

	if (list) {
		*fields = array_to_stringlist (list);
		g_strfreev (list);
		return TRUE;
	} else {
		return unwrap_gerror (err, error);
	}
}

static void
get_required_fields_reply (GObject *gdbus_book,
                           GAsyncResult *res,
                           gpointer user_data)
{
	gchar **fields = NULL, *fields_str = NULL;
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookEListAsyncCallback excb = data->excallback;
	EBookEListCallback cb = data->callback;
	EList *efields = NULL;

	e_gdbus_book_call_get_backend_property_finish (G_DBUS_PROXY (gdbus_book), res, &fields_str, &error);

	fields = g_strsplit (fields_str, ",", -1);
	g_free (fields_str);

	efields = array_to_elist (fields);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, efields, data->closure);
	if (excb)
		excb (data->book, err, efields, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (efields);
	g_strfreev (fields);
	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_get_required_fields:
 * @book: an #EBook
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Gets a list of fields that are required to be filled in for
 * all contacts in this @book. This function does not block.
 *
 * Returns: %TRUE if the operation was started, %FALSE otherwise.
 *
 * Deprecated: 3.0: Use e_book_get_required_fields_async() instead.
 **/
gboolean
e_book_async_get_required_fields (EBook *book,
                                  EBookEListCallback cb,
                                  gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_backend_property (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS, NULL, get_required_fields_reply, data);

	return TRUE;
}

/**
 * e_book_get_required_fields_async:
 * @book: an #EBook
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Gets a list of fields that are required to be filled in for
 * all contacts in this @book. This function does not block.
 *
 * Returns: %TRUE if the operation was started, %FALSE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_get_backend_property() and e_client_get_backend_property_finish()
 * on an #EBookClient object with #BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS instead.
 **/
gboolean
e_book_get_required_fields_async (EBook *book,
                                  EBookEListAsyncCallback cb,
                                  gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_backend_property (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS, NULL, get_required_fields_reply, data);

	return TRUE;
}

/**
 * e_book_get_supported_fields:
 * @book: an #EBook
 * @fields: (out) (transfer full) (element-type utf8): a #GList of fields to set on success
 * @error: a #GError to set on failure
 *
 * Gets a list of fields that can be stored for contacts
 * in this @book. Other fields may be discarded. The list
 * will contain pointers to allocated strings, and both the
 * #GList and the strings must be freed by the caller.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_client_get_backend_property_sync() on
 * an #EBookClient object with #BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS instead.
 **/
gboolean
e_book_get_supported_fields (EBook *book,
                             GList **fields,
                             GError **error)
{
	GError *err = NULL;
	gchar **list = NULL, *list_str = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	e_gdbus_book_call_get_backend_property_sync (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS, &list_str, NULL, &err);

	list = g_strsplit (list_str, ",", -1);
	g_free (list_str);
	if (list) {
		*fields = array_to_stringlist (list);
		g_strfreev (list);
		return TRUE;
	} else {
		return unwrap_gerror (err, error);
	}
}

static void
get_supported_fields_reply (GObject *gdbus_book,
                            GAsyncResult *res,
                            gpointer user_data)
{
	gchar **fields = NULL, *fields_str = NULL;
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookEListAsyncCallback excb = data->excallback;
	EBookEListCallback cb = data->callback;
	EList *efields;

	e_gdbus_book_call_get_backend_property_finish (G_DBUS_PROXY (gdbus_book), res, &fields_str, &error);

	fields = g_strsplit (fields_str, ",", -1);
	g_free (fields_str);

	efields = array_to_elist (fields);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, efields, data->closure);
	if (excb)
		excb (data->book, err, efields, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (efields);
	g_strfreev (fields);
	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_get_supported_fields:
 * @book: an #EBook
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Gets a list of fields that can be stored for contacts
 * in this @book. Other fields may be discarded. This
 * function does not block.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.0: Use e_book_get_supported_fields_async() instead.
 **/
gboolean
e_book_async_get_supported_fields (EBook *book,
                                   EBookEListCallback cb,
                                   gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_backend_property (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS, NULL, get_supported_fields_reply, data);

	return TRUE;
}

/**
 * e_book_get_supported_fields_async:
 * @book: an #EBook
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Gets a list of fields that can be stored for contacts
 * in this @book. Other fields may be discarded. This
 * function does not block.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_get_backend_property() and e_client_get_backend_property_finish()
 * on an #EBookClient object with #BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS instead.
 **/
gboolean
e_book_get_supported_fields_async (EBook *book,
                                   EBookEListAsyncCallback cb,
                                   gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_backend_property (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS, NULL, get_supported_fields_reply, data);

	return TRUE;
}

/**
 * e_book_get_supported_auth_methods:
 * @book: an #EBook
 * @auth_methods: (out) (transfer full) (element-type utf8): a #GList of auth methods to set on success
 * @error: a #GError to set on failure
 *
 * Queries @book for the list of authentication methods it supports.
 * The list will contain pointers to allocated strings, and both the
 * #GList and the strings must be freed by the caller.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_client_get_backend_property_sync() on
 * an #EBookClient object with #BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS instead.
 **/
gboolean
e_book_get_supported_auth_methods (EBook *book,
                                   GList **auth_methods,
                                   GError **error)
{
	GError *err = NULL;
	gchar *list_str = NULL;
	gchar **list = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	e_gdbus_book_call_get_backend_property_sync (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS, &list_str, NULL, &err);

	list = g_strsplit (list_str, ",", -1);
	g_free (list_str);

	if (list) {
		*auth_methods = array_to_stringlist (list);
		g_strfreev (list);
		return TRUE;
	} else {
		return unwrap_gerror (err, error);
	}
}

static void
get_supported_auth_methods_reply (GObject *gdbus_book,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
	gchar **methods = NULL, *methods_str = NULL;
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookEListAsyncCallback excb = data->excallback;
	EBookEListCallback cb = data->callback;
	EList *emethods;

	e_gdbus_book_call_get_backend_property_finish (G_DBUS_PROXY (gdbus_book), res, &methods_str, &error);

	methods = g_strsplit (methods_str, ",", -1);
	g_free (methods_str);

	emethods = array_to_elist (methods);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, emethods, data->closure);
	if (excb)
		excb (data->book, err, emethods, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (emethods);
	g_strfreev (methods);
	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_get_supported_auth_methods:
 * @book: an #EBook
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Queries @book for the list of authentication methods it supports.
 * This function does not block.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.0: Use e_book_get_supported_auth_methods_async() instead.
 **/
gboolean
e_book_async_get_supported_auth_methods (EBook *book,
                                         EBookEListCallback cb,
                                         gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_backend_property (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS, NULL, get_supported_auth_methods_reply, data);

	return TRUE;
}

/**
 * e_book_get_supported_auth_methods_async:
 * @book: an #EBook
 * @cb: (scope async): function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Queries @book for the list of authentication methods it supports.
 * This function does not block.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_get_backend_property() and e_client_get_backend_property_finish()
 * on an #EBookClient object with #BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS instead.
 **/
gboolean
e_book_get_supported_auth_methods_async (EBook *book,
                                         EBookEListAsyncCallback cb,
                                         gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_backend_property (book->priv->gdbus_book, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS, NULL, get_supported_auth_methods_reply, data);

	return TRUE;
}

/**
 * e_book_get_contact:
 * @book: an #EBook
 * @id: a unique string ID specifying the contact
 * @contact: (out) (transfer full): an #EContact
 * @error: a #GError to set on failure
 *
 * Fills in @contact with the contents of the vcard in @book
 * corresponding to @id.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_book_client_get_contact_sync() instead.
 **/
gboolean
e_book_get_contact (EBook *book,
                    const gchar *id,
                    EContact **contact,
                    GError **error)
{
	GError *err = NULL;
	gchar *vcard = NULL, *gdbus_id = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	e_gdbus_book_call_get_contact_sync (book->priv->gdbus_book, e_util_ensure_gdbus_string (id, &gdbus_id), &vcard, NULL, &err);

	g_free (gdbus_id);

	if (vcard) {
		*contact = e_contact_new_from_vcard_with_uid (vcard, id);
		g_free (vcard);
	}

	return unwrap_gerror (err, error);
}

static void
get_contact_reply (GObject *gdbus_book,
                   GAsyncResult *res,
                   gpointer user_data)
{
	gchar *vcard = NULL;
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookContactAsyncCallback excb = data->excallback;
	EBookContactCallback cb = data->callback;

	e_gdbus_book_call_get_contact_finish (G_DBUS_PROXY (gdbus_book), res, &vcard, &error);

	unwrap_gerror (error, &err);

	/* Protect against garbage return values on error */
	if (error)
		vcard = NULL;

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, err ? NULL : e_contact_new_from_vcard (vcard), data->closure);
	if (excb)
		excb (data->book, err, err ? NULL : e_contact_new_from_vcard (vcard), data->closure);

	if (err)
		g_error_free (err);

	g_free (vcard);
	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_get_contact:
 * @book: an #EBook
 * @id: a unique string ID specifying the contact
 * @cb: (scope async): function to call when operation finishes
 * @closure: data to pass to callback function
 *
 * Retrieves a contact specified by @id from @book.
 *
 * Returns: %FALSE if successful, %TRUE otherwise
 *
 * Deprecated: 3.0: Use e_book_get_contact_async() instead.
 **/
gboolean
e_book_async_get_contact (EBook *book,
                          const gchar *id,
                          EBookContactCallback cb,
                          gpointer closure)
{
	AsyncData *data;
	gchar *gdbus_id = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_contact (book->priv->gdbus_book, e_util_ensure_gdbus_string (id, &gdbus_id), NULL, get_contact_reply, data);

	g_free (gdbus_id);

	return TRUE;
}

/**
 * e_book_get_contact_async:
 * @book: an #EBook
 * @id: a unique string ID specifying the contact
 * @cb: (scope async): function to call when operation finishes
 * @closure: data to pass to callback function
 *
 * Retrieves a contact specified by @id from @book.
 *
 * Returns: %FALSE if successful, %TRUE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_get_contact() and e_book_client_get_contact_finish() instead.
 **/
gboolean
e_book_get_contact_async (EBook *book,
                          const gchar *id,
                          EBookContactAsyncCallback cb,
                          gpointer closure)
{
	AsyncData *data;
	gchar *gdbus_id = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_contact (book->priv->gdbus_book, e_util_ensure_gdbus_string (id, &gdbus_id), NULL, get_contact_reply, data);

	g_free (gdbus_id);

	return TRUE;
}

/**
 * e_book_remove_contact:
 * @book: an #EBook
 * @id: a string
 * @error: a #GError to set on failure
 *
 * Removes the contact with id @id from @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_book_client_remove_contact_by_uid_sync() or e_book_client_remove_contact_sync() instead.
 **/
gboolean
e_book_remove_contact (EBook *book,
                       const gchar *id,
                       GError **error)
{
	GError *err = NULL;
	const gchar *l[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	l[0] = e_util_utf8_make_valid (id);
	l[1] = NULL;

	e_gdbus_book_call_remove_contacts_sync (book->priv->gdbus_book, (const gchar * const *) l, NULL, &err);

	g_free ((gchar *) l[0]);

	return unwrap_gerror (err, error);
}

static void
remove_contact_reply (GObject *gdbus_book,
                      GAsyncResult *res,
                      gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookAsyncCallback excb = data->excallback;
	EBookCallback cb = data->callback;

	e_gdbus_book_call_remove_contacts_finish (G_DBUS_PROXY (gdbus_book), res, &error);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, data->closure);
	if (excb)
		excb (data->book, err, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_remove_contacts:
 * @book: an #EBook
 * @ids: (element-type utf8): an #GList of const gchar *id's
 * @error: a #GError to set on failure
 *
 * Removes the contacts with ids from the list @ids from @book.  This is
 * always more efficient than calling e_book_remove_contact() if you
 * have more than one id to remove, as some backends can implement it
 * as a batch request.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_book_client_remove_contacts_sync() instead.
 **/
gboolean
e_book_remove_contacts (EBook *book,
                        GList *ids,
                        GError **error)
{
	GError *err = NULL;
	gchar **l;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (ids != NULL, FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	l = flatten_stringlist (ids);

	e_gdbus_book_call_remove_contacts_sync (book->priv->gdbus_book, (const gchar * const *) l, NULL, &err);

	g_strfreev (l);

	return unwrap_gerror (err, error);
}

/**
 * e_book_async_remove_contact:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes @contact from @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.0: Use e_book_remove_contact_async() instead.
 **/
gboolean
e_book_async_remove_contact (EBook *book,
                             EContact *contact,
                             EBookCallback cb,
                             gpointer closure)
{
	AsyncData *data;
	const gchar *l[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	l[0] = e_util_utf8_make_valid (e_contact_get_const (contact, E_CONTACT_UID));
	l[1] = NULL;

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove_contacts (book->priv->gdbus_book, (const gchar * const *) l, NULL, remove_contact_reply, data);

	g_free ((gchar *) l[0]);

	return TRUE;
}

/**
 * e_book_remove_contact_async:
 * @book: an #EBook
 * @contact: an #EContact
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes @contact from @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_remove_contact() and e_book_client_remove_contact_finish() instead.
 **/
gboolean
e_book_remove_contact_async (EBook *book,
                             EContact *contact,
                             EBookAsyncCallback cb,
                             gpointer closure)
{
	AsyncData *data;
	const gchar *l[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	e_return_ex_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	l[0] = e_util_utf8_make_valid (e_contact_get_const (contact, E_CONTACT_UID));
	l[1] = NULL;

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove_contacts (book->priv->gdbus_book, (const gchar * const *) l, NULL, remove_contact_reply, data);

	g_free ((gchar *) l[0]);

	return TRUE;
}

static void
remove_contact_by_id_reply (GObject *gdbus_book,
                            GAsyncResult *res,
                            gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookAsyncCallback excb = data->excallback;
	EBookCallback cb = data->callback;

	e_gdbus_book_call_remove_contacts_finish (G_DBUS_PROXY (gdbus_book), res, &error);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, data->closure);
	if (excb)
		excb (data->book, err, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_remove_contact_by_id:
 * @book: an #EBook
 * @id: a unique ID string specifying the contact
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes the contact with id @id from @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.0: Use e_book_remove_contact_by_id_async() instead.
 **/
gboolean
e_book_async_remove_contact_by_id (EBook *book,
                                   const gchar *id,
                                   EBookCallback cb,
                                   gpointer closure)
{
	AsyncData *data;
	const gchar *l[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	e_return_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	l[0] = e_util_utf8_make_valid (id);
	l[1] = NULL;

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove_contacts (book->priv->gdbus_book, (const gchar * const *) l, NULL, remove_contact_by_id_reply, data);

	g_free ((gchar *) l[0]);

	return TRUE;
}

/**
 * e_book_remove_contact_by_id_async:
 * @book: an #EBook
 * @id: a unique ID string specifying the contact
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes the contact with id @id from @book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_remove_contact_by_uid() and e_book_client_remove_contact_by_uid_finish() instead.
 **/
gboolean
e_book_remove_contact_by_id_async (EBook *book,
                                   const gchar *id,
                                   EBookAsyncCallback cb,
                                   gpointer closure)
{
	AsyncData *data;
	const gchar *l[2];

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	e_return_ex_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	l[0] = e_util_utf8_make_valid (id);
	l[1] = NULL;

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove_contacts (book->priv->gdbus_book, (const gchar * const *) l, NULL, remove_contact_by_id_reply, data);

	g_free ((gchar *) l[0]);

	return TRUE;
}

static void
remove_contacts_reply (GObject *gdbus_book,
                       GAsyncResult *res,
                       gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookAsyncCallback excb = data->excallback;
	EBookCallback cb = data->callback;

	e_gdbus_book_call_remove_contacts_finish (G_DBUS_PROXY (gdbus_book), res, &error);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, data->closure);
	if (excb)
		excb (data->book, err, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_remove_contacts:
 * @book: an #EBook
 * @ids: (element-type utf8): a #GList of const gchar *id's
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes the contacts with ids from the list @ids from @book.  This is
 * always more efficient than calling e_book_remove_contact() if you
 * have more than one id to remove, as some backends can implement it
 * as a batch request.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.0: Use e_book_remove_contacts_async() instead.
 **/
gboolean
e_book_async_remove_contacts (EBook *book,
                              GList *ids,
                              EBookCallback cb,
                              gpointer closure)
{
	AsyncData *data;
	gchar **l;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	if (ids == NULL) {
		if (cb)
			cb (book, E_BOOK_ERROR_OK, closure);
		return TRUE;
	}

	l = flatten_stringlist (ids);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove_contacts (book->priv->gdbus_book, (const gchar * const *) l, NULL, remove_contacts_reply, data);

	g_strfreev (l);

	return TRUE;
}

/**
 * e_book_remove_contacts_async:
 * @book: an #EBook
 * @ids: (element-type utf8): a #GList of const gchar *id's
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Removes the contacts with ids from the list @ids from @book.  This is
 * always more efficient than calling e_book_remove_contact() if you
 * have more than one id to remove, as some backends can implement it
 * as a batch request.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_remove_contacts() and e_book_client_remove_contacts_finish() instead.
 **/
gboolean
e_book_remove_contacts_async (EBook *book,
                              GList *ids,
                              EBookAsyncCallback cb,
                              gpointer closure)
{
	AsyncData *data;
	gchar **l;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_ex_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	if (ids == NULL) {
		if (cb)
			cb (book, E_BOOK_ERROR_OK, closure);
		return TRUE;
	}

	l = flatten_stringlist (ids);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove_contacts (book->priv->gdbus_book, (const gchar *const *) l, NULL, remove_contacts_reply, data);

	g_strfreev (l);

	return TRUE;
}

/**
 * e_book_get_book_view:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @requested_fields: (allow-none) (element-type utf8): a #GList containing the names of fields to
 * return, or NULL for all
 * @max_results: the maximum number of contacts to show (or 0 for all)
 * @book_view: (out): A #EBookView pointer, will be set to the view
 * @error: a #GError to set on failure
 *
 * Query @book with @query, creating a #EBookView in @book_view with the fields
 * specified by @requested_fields and limited at @max_results records. On an
 * error, @error is set and %FALSE returned.
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_book_client_get_view_sync() instead.
 **/
gboolean
e_book_get_book_view (EBook *book,
                      EBookQuery *query,
                      GList *requested_fields,
                      gint max_results,
                      EBookView **book_view,
                      GError **error)
{
	GError *err = NULL;
	EGdbusBookView *gdbus_bookview;
	gchar *sexp, *view_path = NULL, *gdbus_sexp = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	sexp = e_book_query_to_string (query);

	if (!e_gdbus_book_call_get_view_sync (book->priv->gdbus_book, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &view_path, NULL, &err)) {
		*book_view = NULL;
		g_free (sexp);
		g_free (gdbus_sexp);

		return unwrap_gerror (err, error);
	}

	gdbus_bookview = e_gdbus_book_view_proxy_new_sync (
		g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		view_path,
		NULL,
		error);

	if (gdbus_bookview) {
		*book_view = _e_book_view_new (book, gdbus_bookview);
	} else {
		*book_view = NULL;
		g_set_error_literal (
			error, E_BOOK_ERROR, E_BOOK_ERROR_DBUS_EXCEPTION,
			"Cannot get connection to view");
		ret = FALSE;
	}

	g_free (view_path);
	g_free (sexp);
	g_free (gdbus_sexp);

	return ret;
}

static void
get_book_view_reply (GObject *gdbus_book,
                     GAsyncResult *res,
                     gpointer user_data)
{
	gchar *view_path = NULL;
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookView *view = NULL;
	EBookBookViewAsyncCallback excb = data->excallback;
	EBookBookViewCallback cb = data->callback;
	EGdbusBookView *gdbus_bookview;

	e_gdbus_book_call_get_view_finish (G_DBUS_PROXY (gdbus_book), res, &view_path, &error);

	if (view_path) {
		gdbus_bookview = e_gdbus_book_view_proxy_new_sync (
			g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
			G_DBUS_PROXY_FLAGS_NONE,
			ADDRESS_BOOK_DBUS_SERVICE_NAME,
			view_path,
			NULL,
			&error);
		if (gdbus_bookview) {
			view = _e_book_view_new (data->book, gdbus_bookview);
		}
	}

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, view, data->closure);
	if (excb)
		excb (data->book, err, view, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_get_book_view:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @requested_fields: (element-type utf8): a #GList containing the names of fields to return, or NULL for all
 * @max_results: the maximum number of contacts to show (or 0 for all)
 * @cb: (scope call): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Query @book with @query, creating a #EBookView with the fields
 * specified by @requested_fields and limited at @max_results records.
 *
 * Returns: %FALSE if successful, %TRUE otherwise
 *
 * Deprecated: 3.0: Use e_book_get_book_view_async() instead.
 **/
gboolean
e_book_async_get_book_view (EBook *book,
                            EBookQuery *query,
                            GList *requested_fields,
                            gint max_results,
                            EBookBookViewCallback cb,
                            gpointer closure)
{
	AsyncData *data;
	gchar *sexp, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	sexp = e_book_query_to_string (query);

	e_gdbus_book_call_get_view (book->priv->gdbus_book, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), NULL, get_book_view_reply, data);

	g_free (sexp);
	g_free (gdbus_sexp);

	return TRUE;
}

/**
 * e_book_get_book_view_async:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @requested_fields: (allow-none) (element-type utf8): a #GList containing the names of fields to
 * return, or NULL for all
 * @max_results: the maximum number of contacts to show (or 0 for all)
 * @cb: (scope call): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Query @book with @query, creating a #EBookView with the fields
 * specified by @requested_fields and limited at @max_results records.
 *
 * Returns: %FALSE if successful, %TRUE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_get_view() and e_book_client_get_view_finish() instead.
 **/
gboolean
e_book_get_book_view_async (EBook *book,
                            EBookQuery *query,
                            GList *requested_fields,
                            gint max_results,
                            EBookBookViewAsyncCallback cb,
                            gpointer closure)
{
	AsyncData *data;
	gchar *sexp, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	sexp = e_book_query_to_string (query);

	e_gdbus_book_call_get_view (book->priv->gdbus_book, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), NULL, get_book_view_reply, data);

	g_free (sexp);
	g_free (gdbus_sexp);

	return TRUE;
}

/**
 * e_book_get_contacts:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @contacts: (element-type utf8): a #GList pointer, will be set to the list of contacts
 * @error: a #GError to set on failure
 *
 * Query @book with @query, setting @contacts to the list of contacts which
 * matched. On failed, @error will be set and %FALSE returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_book_client_get_contacts_sync() instead.
 **/
gboolean
e_book_get_contacts (EBook *book,
                     EBookQuery *query,
                     GList **contacts,
                     GError **error)
{
	GError *err = NULL;
	gchar **list = NULL;
	gchar *sexp, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	sexp = e_book_query_to_string (query);

	e_gdbus_book_call_get_contact_list_sync (book->priv->gdbus_book, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), &list, NULL, &err);

	g_free (sexp);
	g_free (gdbus_sexp);

	if (!err) {
		GList *l = NULL;
		gchar **i = list;
		while (*i != NULL) {
			l = g_list_prepend (l, e_contact_new_from_vcard (*i++));
		}
		*contacts = g_list_reverse (l);
		g_strfreev (list);
		return TRUE;
	} else {
		return unwrap_gerror (err, error);
	}
}

static void
get_contacts_reply (GObject *gdbus_book,
                    GAsyncResult *res,
                    gpointer user_data)
{
	gchar **vcards = NULL;
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	GList *list = NULL;
	EBookListAsyncCallback excb = data->excallback;
	EBookListCallback cb = data->callback;

	e_gdbus_book_call_get_contact_list_finish (G_DBUS_PROXY (gdbus_book), res, &vcards, &error);

	unwrap_gerror (error, &err);

	if (!error && vcards) {
		gchar **i = vcards;

		while (*i != NULL) {
			list = g_list_prepend (list, e_contact_new_from_vcard (*i++));
		}

		g_strfreev (vcards);

		list = g_list_reverse (list);
	}

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, list, data->closure);

	if (excb)
		excb (data->book, err, list, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_get_contacts:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Query @book with @query.
 *
 * Returns: %FALSE on success, %TRUE otherwise
 *
 * Deprecated: 3.0: Use e_book_get_contacts_async() instead.
 **/
gboolean
e_book_async_get_contacts (EBook *book,
                           EBookQuery *query,
                           EBookListCallback cb,
                           gpointer closure)
{
	AsyncData *data;
	gchar *sexp, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	sexp = e_book_query_to_string (query);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_contact_list (book->priv->gdbus_book, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), NULL, get_contacts_reply, data);

	g_free (sexp);
	g_free (gdbus_sexp);

	return TRUE;
}

/**
 * e_book_get_contacts_async:
 * @book: an #EBook
 * @query: an #EBookQuery
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Query @book with @query.
 *
 * Returns: %FALSE on success, %TRUE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_book_client_get_contacts() and e_book_client_get_contacts_finish() instead.
 **/
gboolean
e_book_get_contacts_async (EBook *book,
                           EBookQuery *query,
                           EBookListAsyncCallback cb,
                           gpointer closure)
{
	AsyncData *data;
	gchar *sexp, *gdbus_sexp = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	sexp = e_book_query_to_string (query);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_get_contact_list (book->priv->gdbus_book, e_util_ensure_gdbus_string (sexp, &gdbus_sexp), NULL, get_contacts_reply, data);

	g_free (sexp);
	g_free (gdbus_sexp);

	return TRUE;
}

/**
 * e_book_get_changes: (skip)
 * @book: an #EBook
 * @changeid:  the change ID
 * @changes: (out) (transfer full): return location for a #GList of #EBookChange items
 * @error: a #GError to set on failure.
 *
 * Get the set of changes since the previous call to e_book_get_changes()
 * for a given change ID.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Deprecated: 3.2: This function has been dropped completely.
 */
gboolean
e_book_get_changes (EBook *book,
                    const gchar *changeid,
                    GList **changes,
                    GError **error)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	if (error) {
		*error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_NOT_SUPPORTED, "Not supported");
	}

	return FALSE;
}

/**
 * e_book_async_get_changes:
 * @book: an #EBook
 * @changeid:  the change ID
 * @cb: (scope async): function to call when operation finishes
 * @closure: data to pass to callback function
 *
 * Get the set of changes since the previous call to
 * e_book_async_get_changes() for a given change ID.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Deprecated: 3.0: Use e_book_get_changes_async() instead.
 */
gboolean
e_book_async_get_changes (EBook *book,
                          const gchar *changeid,
                          EBookListCallback cb,
                          gpointer closure)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	cb (book, E_BOOK_ERROR_NOT_SUPPORTED, NULL, closure);

	return TRUE;
}

/**
 * e_book_get_changes_async:
 * @book: an #EBook
 * @changeid:  the change ID
 * @cb: (scope async): function to call when operation finishes
 * @closure: data to pass to callback function
 *
 * Get the set of changes since the previous call to
 * e_book_async_get_changes() for a given change ID.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: This function has been dropped completely.
 */
gboolean
e_book_get_changes_async (EBook *book,
                          const gchar *changeid,
                          EBookListAsyncCallback cb,
                          gpointer closure)
{
	GError *error;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_ex_async_error_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	error = g_error_new (E_BOOK_ERROR, E_BOOK_ERROR_NOT_SUPPORTED, "Not supported");
	cb (book, error, NULL, closure);
	g_error_free (error);

	return TRUE;
}

/**
 * e_book_free_change_list:
 * @change_list: (element-type EBookChange): a #GList of #EBookChange items
 *
 * Free the contents of #change_list, and the list itself.
 *
 * Deprecated: 3.2: Related function has been dropped completely.
 */
void
e_book_free_change_list (GList *change_list)
{
	GList *l;
	for (l = change_list; l; l = l->next) {
		EBookChange *change = l->data;

		g_object_unref (change->contact);
		g_slice_free (EBookChange, change);
	}

	g_list_free (change_list);
}

/**
 * e_book_cancel:
 * @book: an #EBook
 * @error: a #GError to set on failure
 *
 * Used to cancel an already running operation on @book.  This
 * function makes a synchronous CORBA to the backend telling it to
 * cancel the operation.  If the operation wasn't cancellable (either
 * transiently or permanently) or had already comopleted on the server
 * side, this function will return E_BOOK_STATUS_COULD_NOT_CANCEL, and
 * the operation will continue uncancelled.  If the operation could be
 * cancelled, this function will return E_BOOK_ERROR_OK, and the
 * blocked e_book function corresponding to current operation will
 * return with a status of E_BOOK_STATUS_CANCELLED.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Deprecated: 3.2: Use e_client_cancel_all() or e_client_cancel_op() on an #EBookClient object instead.
 **/
gboolean
e_book_cancel (EBook *book,
               GError **error)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	return e_gdbus_book_call_cancel_all_sync (book->priv->gdbus_book, NULL, error);
}

/**
 * e_book_cancel_async_op:
 *
 * Similar to above e_book_cancel function, only cancels last, still running,
 * asynchronous operation.
 *
 * Since: 2.24
 *
 * Deprecated: 3.2: Use e_client_cancel_all() or e_client_cancel_op() on an #EBookClient object instead.
 **/
gboolean
e_book_cancel_async_op (EBook *book,
                        GError **error)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	return e_gdbus_book_call_cancel_all_sync (book->priv->gdbus_book, NULL, error);
}

/**
 * e_book_open:
 * @book: an #EBook
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @error: a #GError to set on failure
 *
 * Opens the addressbook, making it ready for queries and other operations.
 *
 * Returns: %TRUE if the book was successfully opened, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_client_open_sync() on an #EBookClient object instead.
 */
gboolean
e_book_open (EBook *book,
             gboolean only_if_exists,
             GError **error)
{
	GError *err = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	if (!e_gdbus_book_call_open_sync (book->priv->gdbus_book, only_if_exists, NULL, &err)) {

		unwrap_gerror (err, error);

		return FALSE;
	}

	if (!err)
		book->priv->loaded = TRUE;

	return unwrap_gerror (err, error);
}

static void
open_reply (GObject *gdbus_book,
            GAsyncResult *res,
            gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookAsyncCallback excb = data->excallback;
	EBookCallback cb = data->callback;

	e_gdbus_book_call_open_finish (G_DBUS_PROXY (gdbus_book), res, &error);

	unwrap_gerror (error, &err);

	data->book->priv->loaded = !error;

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, data->closure);
	if (excb)
		excb (data->book, err, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_open:
 * @book: an #EBook
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @open_response: (scope call) (closure closure): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Opens the addressbook, making it ready for queries and other operations.
 * This function does not block.
 *
 * Returns: %FALSE if successful, %TRUE otherwise.
 *
 * Deprecated: 3.0: Use e_book_open_async() instead.
 **/
gboolean
e_book_async_open (EBook *book,
                   gboolean only_if_exists,
                   EBookCallback cb,
                   gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_open (book->priv->gdbus_book, only_if_exists, NULL, open_reply, data);

	return TRUE;
}

/**
 * e_book_open_async:
 * @book: an #EBook
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @open_response: (scope call): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Opens the addressbook, making it ready for queries and other operations.
 * This function does not block.
 *
 * Returns: %FALSE if successful, %TRUE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_open() and e_client_open_finish() on an #EBookClient object instead.
 **/
gboolean
e_book_open_async (EBook *book,
                   gboolean only_if_exists,
                   EBookAsyncCallback cb,
                   gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	g_return_val_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_open (book->priv->gdbus_book, only_if_exists, NULL, open_reply, data);

	return TRUE;
}

/**
 * e_book_remove:
 * @book: an #EBook
 * @error: a #GError to set on failure
 *
 * Removes the backing data for this #EBook. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Returns: %TRUE on success, %FALSE on failure.
 *
 * Deprecated: 3.2: Use e_client_remove_sync() on an #EBookClient object instead.
 */
gboolean
e_book_remove (EBook *book,
               GError **error)
{
	GError *err = NULL;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	e_gdbus_book_call_remove_sync (book->priv->gdbus_book, NULL, &err);

	return unwrap_gerror (err, error);
}

static void
remove_reply (GObject *gdbus_book,
              GAsyncResult *res,
              gpointer user_data)
{
	GError *err = NULL, *error = NULL;
	AsyncData *data = user_data;
	EBookAsyncCallback excb = data->excallback;
	EBookCallback cb = data->callback;

	e_gdbus_book_call_remove_finish (G_DBUS_PROXY (gdbus_book), res, &error);

	unwrap_gerror (error, &err);

	if (cb)
		cb (data->book, err ? err->code : E_BOOK_ERROR_OK, data->closure);
	if (excb)
		excb (data->book, err, data->closure);

	if (err)
		g_error_free (err);

	g_object_unref (data->book);
	g_slice_free (AsyncData, data);
}

/**
 * e_book_async_remove:
 * @book: an #EBook
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Remove the backing data for this #EBook. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Returns: %FALSE if successful, %TRUE otherwise.
 *
 * Deprecated: 3.0: Use e_book_remove_async() instead.
 **/
gboolean
e_book_async_remove (EBook *book,
                     EBookCallback cb,
                     gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->callback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove (book->priv->gdbus_book, NULL, remove_reply, data);

	return TRUE;
}

/**
 * e_book_remove_async:
 * @book: an #EBook
 * @cb: (scope async): a function to call when the operation finishes
 * @closure: data to pass to callback function
 *
 * Remove the backing data for this #EBook. For example, with the file backend this
 * deletes the database file. You cannot get it back!
 *
 * Returns: %FALSE if successful, %TRUE otherwise.
 *
 * Since: 2.32
 *
 * Deprecated: 3.2: Use e_client_remove() and e_client_remove_finish() on an #EBookClient object instead.
 **/
gboolean
e_book_remove_async (EBook *book,
                     EBookAsyncCallback cb,
                     gpointer closure)
{
	AsyncData *data;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	e_return_ex_async_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	data = g_slice_new0 (AsyncData);
	data->book = g_object_ref (book);
	data->excallback = cb;
	data->closure = closure;

	e_gdbus_book_call_remove (book->priv->gdbus_book, NULL, remove_reply, data);

	return TRUE;
}

/**
 * e_book_get_source:
 * @book: an #EBook
 *
 * Get the #ESource that this book has loaded.
 *
 * Returns: (transfer none): The source.
 *
 * Deprecated: 3.2: Use e_client_get_source() on an #EBookClient object instead.
 */
ESource *
e_book_get_source (EBook *book)
{
	g_return_val_if_fail (E_IS_BOOK (book), NULL);

	return book->priv->source;
}

/**
 * e_book_get_static_capabilities:
 * @book: an #EBook
 * @error: an #GError to set on failure
 *
 * Get the list of capabilities which the backend for this address book
 * supports. This string should not be freed.
 *
 * Returns: The capabilities list
 *
 * Deprecated: 3.2: Use e_client_get_capabilities() on an #EBookClient object.
 */
const gchar *
e_book_get_static_capabilities (EBook *book,
                                GError **error)
{
	g_return_val_if_fail (E_IS_BOOK (book), NULL);

	e_return_error_if_fail (
		book->priv->gdbus_book, E_BOOK_ERROR_REPOSITORY_OFFLINE);

	if (!book->priv->cap_queried) {
		gchar *cap = NULL;

		if (!e_gdbus_book_call_get_backend_property_sync (book->priv->gdbus_book, CLIENT_BACKEND_PROPERTY_CAPABILITIES, &cap, NULL, error)) {
			return NULL;
		}

		book->priv->cap = cap;
		book->priv->cap_queried = TRUE;
	}

	return book->priv->cap;
}

/**
 * e_book_check_static_capability:
 * @book: an #EBook
 * @cap: A capability string
 *
 * Check to see if the backend for this address book supports the capability
 * @cap.
 *
 * Returns: %TRUE if the backend supports @cap, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_client_check_capability() on an #EBookClient object instead.
 */
gboolean
e_book_check_static_capability (EBook *book,
                                const gchar *cap)
{
	const gchar *caps;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	caps = e_book_get_static_capabilities (book, NULL);

	/* XXX this is an inexact test but it works for our use */
	if (caps && strstr (caps, cap))
		return TRUE;

	return FALSE;
}

/**
 * e_book_is_opened:
 * @book: and #EBook
 *
 * Check if this book has been opened.
 *
 * Returns: %TRUE if this book has been opened, otherwise %FALSE.
 *
 * Deprecated: 3.2: Use e_client_is_opened() on an #EBookClient object instead.
 */
gboolean
e_book_is_opened (EBook *book)
{
	g_return_val_if_fail (E_IS_BOOK (book), FALSE);

	return book->priv->loaded;
}

/**
 * e_book_is_writable:
 * @book: an #EBook
 *
 * Check if this book is writable.
 *
 * Returns: %TRUE if this book is writable, otherwise %FALSE.
 *
 * Deprecated: 3.2: Use e_client_is_readonly() on an #EBookClient object instead.
 */
gboolean
e_book_is_writable (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	return book->priv->writable;
}

/**
 * e_book_is_online:
 * @book: an #EBook
 *
 * Check if this book is connected.
 *
 * Returns: %TRUE if this book is connected, otherwise %FALSE.
 *
 * Deprecated: 3.2: Use e_client_is_online() on an #EBookClient object instead.
 **/
gboolean
e_book_is_online (EBook *book)
{
	g_return_val_if_fail (book && E_IS_BOOK (book), FALSE);

	return book->priv->connected;
}

#define SELF_UID_PATH_ID "org.gnome.evolution-data-server.addressbook"
#define SELF_UID_KEY "self-contact-uid"

static EContact *
make_me_card (void)
{
	GString *vcard;
	const gchar *s;
	EContact *contact;

	vcard = g_string_new ("BEGIN:VCARD\nVERSION:3.0\n");

	s = g_get_user_name ();
	if (s)
		g_string_append_printf (vcard, "NICKNAME:%s\n", s);

	s = g_get_real_name ();
	if (s && strcmp (s, "Unknown") != 0) {
		ENameWestern *western;

		g_string_append_printf (vcard, "FN:%s\n", s);

		western = e_name_western_parse (s);
		g_string_append_printf (
			vcard, "N:%s;%s;%s;%s;%s\n",
			western->last ? western->last : "",
			western->first ? western->first : "",
			western->middle ? western->middle : "",
			western->prefix ? western->prefix : "",
			western->suffix ? western->suffix : "");
		e_name_western_free (western);
	}
	g_string_append (vcard, "END:VCARD");

	contact = e_contact_new_from_vcard (vcard->str);

	g_string_free (vcard, TRUE);

	return contact;
}

/**
 * e_book_get_self:
 * @registry: an #ESourceRegistry
 * @contact: (out): an #EContact pointer to set
 * @book: (out): an #EBook pointer to set
 * @error: a #GError to set on failure
 *
 * Get the #EContact referring to the user of the address book
 * and set it in @contact and @book.
 *
 * Returns: %TRUE if successful, otherwise %FALSE.
 *
 * Deprecated: 3.2: Use e_book_client_get_self() instead.
 **/
gboolean
e_book_get_self (ESourceRegistry *registry,
                 EContact **contact,
                 EBook **book,
                 GError **error)
{
	ESource *source;
	GError *e = NULL;
	GSettings *settings;
	gboolean status;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);

	source = e_source_registry_ref_builtin_address_book (registry);
	*book = e_book_new (source, &e);
	g_object_unref (source);

	if (!*book) {
		if (error)
			g_propagate_error (error, e);
		return FALSE;
	}

	status = e_book_open (*book, FALSE, &e);
	if (status == FALSE) {
		g_object_unref (*book);
		*book = NULL;
		if (error)
			g_propagate_error (error, e);
		return FALSE;
	}

	settings = g_settings_new (SELF_UID_PATH_ID);
	uid = g_settings_get_string (settings, SELF_UID_KEY);
	g_object_unref (settings);

	if (uid) {
		gboolean got;

		/* Don't care about errors because we'll create a new card on failure */
		got = e_book_get_contact (*book, uid, contact, NULL);
		g_free (uid);
		if (got)
			return TRUE;
	}

	*contact = make_me_card ();
	if (!e_book_add_contact (*book, *contact, &e)) {
		/* TODO: return NULL or the contact anyway? */
		g_object_unref (*book);
		*book = NULL;
		g_object_unref (*contact);
		*contact = NULL;
		if (error)
			g_propagate_error (error, e);
		return FALSE;
	}

	e_book_set_self (*book, *contact, NULL);

	return TRUE;
}

/**
 * e_book_set_self:
 * @book: an #EBook
 * @contact: an #EContact
 * @error: a #GError to set on failure
 *
 * Specify that @contact residing in @book is the #EContact that
 * refers to the user of the address book.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_book_client_set_self() instead.
 **/
gboolean
e_book_set_self (EBook *book,
                 EContact *contact,
                 GError **error)
{
	GSettings *settings;

	g_return_val_if_fail (E_IS_BOOK (book), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	settings = g_settings_new (SELF_UID_PATH_ID);
	g_settings_set_string (settings, SELF_UID_KEY, e_contact_get_const (contact, E_CONTACT_UID));
	g_object_unref (settings);

	return TRUE;
}

/**
 * e_book_is_self:
 * @contact: an #EContact
 *
 * Check if @contact is the user of the address book.
 *
 * Returns: %TRUE if @contact is the user, %FALSE otherwise.
 *
 * Deprecated: 3.2: Use e_book_client_is_self() instead.
 **/
gboolean
e_book_is_self (EContact *contact)
{
	GSettings *settings;
	gchar *uid;
	gboolean rv;

	/* XXX this should probably be e_return_error_if_fail, but we
	 * need a GError arg for that */
	g_return_val_if_fail (contact && E_IS_CONTACT (contact), FALSE);

	settings = g_settings_new (SELF_UID_PATH_ID);
	uid = g_settings_get_string (settings, SELF_UID_KEY);
	g_object_unref (settings);

	rv = (uid && !strcmp (uid, e_contact_get_const (contact, E_CONTACT_UID)));

	g_free (uid);

	return rv;
}

/**
 * e_book_new:
 * @source: an #ESource
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EBook corresponding to the given @source.  There are
 * only two operations that are valid on this book at this point:
 * e_book_open(), and e_book_remove().
 *
 * Returns: a new but unopened #EBook.
 *
 * Deprecated: 3.2: Use e_book_client_new() instead.
 */
EBook *
e_book_new (ESource *source,
            GError **error)
{
	GError *err = NULL;
	EBook *book;
	gchar *path = NULL;
	const gchar *uid;
	GDBusConnection *connection;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	if (!e_book_activate (&err)) {
		unwrap_gerror (err, &err);
		g_warning (G_STRLOC ": cannot activate book: %s", err->message);
		g_propagate_error (error, err);

		return NULL;
	}

	book = g_object_new (E_TYPE_BOOK, NULL);

	book->priv->source = g_object_ref (source);

	uid = e_source_get_uid (source);

	if (!e_gdbus_book_factory_call_get_book_sync (G_DBUS_PROXY (book_factory_proxy), uid, &path, NULL, &err)) {
		unwrap_gerror (err, &err);
		g_warning (G_STRLOC ": cannot get book from factory: %s", err ? err->message : "[no error]");
		if (err)
			g_propagate_error (error, err);
		g_object_unref (book);

		return NULL;
	}

	book->priv->gdbus_book = G_DBUS_PROXY (
		e_gdbus_book_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (book_factory_proxy)),
		G_DBUS_PROXY_FLAGS_NONE,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		path,
		NULL,
		&err));

	if (!book->priv->gdbus_book) {
		g_free (path);
		unwrap_gerror (err, &err);
		g_warning ("Cannot create cal proxy: %s", err ? err->message : "Unknown error");
		if (err)
			g_error_free (err);
		g_object_unref (book);
		return NULL;
	}

	g_free (path);

	connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (book->priv->gdbus_book));
	book->priv->gone_signal_id = g_dbus_connection_signal_subscribe (
		connection,
		"org.freedesktop.DBus",				/* sender */
		"org.freedesktop.DBus",				/* interface */
		"NameOwnerChanged",				/* member */
		"/org/freedesktop/DBus",			/* object_path */
		"org.gnome.evolution.dataserver.AddressBook",	/* arg0 */
		G_DBUS_SIGNAL_FLAGS_NONE,
		gdbus_book_connection_gone_cb, book, NULL);
	g_signal_connect (connection, "closed", G_CALLBACK (gdbus_book_closed_cb), book);

	g_signal_connect (book->priv->gdbus_book, "readonly", G_CALLBACK (readonly_cb), book);
	g_signal_connect (book->priv->gdbus_book, "online", G_CALLBACK (online_cb), book);

	return book;
}

/*
 * If the GError is a remote error, extract the EBookStatus embedded inside.
 * Otherwise return DBUS_EXCEPTION (I know this is DBus...).
 */
static EBookStatus
get_status_from_error (GError *error)
{
	#define err(a,b) "org.gnome.evolution.dataserver.AddressBook." a, b
	static struct {
		const gchar *name;
		EBookStatus err_code;
	} errors[] = {
		{ err ("Success",				E_BOOK_ERROR_OK) },
		{ err ("Busy",					E_BOOK_ERROR_BUSY) },
		{ err ("RepositoryOffline",			E_BOOK_ERROR_REPOSITORY_OFFLINE) },
		{ err ("PermissionDenied",			E_BOOK_ERROR_PERMISSION_DENIED) },
		{ err ("ContactNotFound",			E_BOOK_ERROR_CONTACT_NOT_FOUND) },
		{ err ("ContactIDAlreadyExists",		E_BOOK_ERROR_CONTACT_ID_ALREADY_EXISTS) },
		{ err ("AuthenticationFailed",			E_BOOK_ERROR_AUTHENTICATION_FAILED) },
		{ err ("AuthenticationRequired",		E_BOOK_ERROR_AUTHENTICATION_REQUIRED) },
		{ err ("UnsupportedField",			E_BOOK_ERROR_OTHER_ERROR) },
		{ err ("UnsupportedAuthenticationMethod",	E_BOOK_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD) },
		{ err ("TLSNotAvailable",			E_BOOK_ERROR_TLS_NOT_AVAILABLE) },
		{ err ("NoSuchBook",				E_BOOK_ERROR_NO_SUCH_BOOK) },
		{ err ("BookRemoved",				E_BOOK_ERROR_NO_SUCH_SOURCE) },
		{ err ("OfflineUnavailable",			E_BOOK_ERROR_OFFLINE_UNAVAILABLE) },
		{ err ("SearchSizeLimitExceeded",		E_BOOK_ERROR_OTHER_ERROR) },
		{ err ("SearchTimeLimitExceeded",		E_BOOK_ERROR_OTHER_ERROR) },
		{ err ("InvalidQuery",				E_BOOK_ERROR_OTHER_ERROR) },
		{ err ("QueryRefused",				E_BOOK_ERROR_OTHER_ERROR) },
		{ err ("CouldNotCancel",			E_BOOK_ERROR_COULD_NOT_CANCEL) },
		{ err ("OtherError",				E_BOOK_ERROR_OTHER_ERROR) },
		{ err ("InvalidServerVersion",			E_BOOK_ERROR_INVALID_SERVER_VERSION) },
		{ err ("NoSpace",				E_BOOK_ERROR_NO_SPACE) },
		{ err ("InvalidArg",				E_BOOK_ERROR_INVALID_ARG) },
		{ err ("NotSupported",				E_BOOK_ERROR_NOT_SUPPORTED) }
	};
	#undef err

	if G_LIKELY (error == NULL)
			    return E_BOOK_ERROR_OK;

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
		gchar *name;
		gint i;

		name = g_dbus_error_get_remote_error (error);

		for (i = 0; i < G_N_ELEMENTS (errors); i++) {
			if (g_ascii_strcasecmp (errors[i].name, name) == 0) {
				g_free (name);
				return errors[i].err_code;
			}
		}

		g_warning (G_STRLOC ": unmatched error name %s", name);
		g_free (name);

		return E_BOOK_ERROR_OTHER_ERROR;
	} else if (error->domain == E_BOOK_ERROR) {
		return error->code;
	} else {
		/* In this case the error was caused by DBus. Dump the message to the
		 * console as otherwise we have no idea what the problem is. */
		g_warning ("DBus error: %s", error->message);
		return E_BOOK_ERROR_DBUS_EXCEPTION;
	}
}

/*
 * If the specified GError is a remote error, then create a new error
 * representing the remote error.  If the error is anything else, then leave it
 * alone.
 */
static gboolean
unwrap_gerror (GError *error,
               GError **client_error)
{
	if (error == NULL)
		return TRUE;

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
		if (client_error) {
			gint code;

			code = get_status_from_error (error);
			g_dbus_error_strip_remote_error (error);

			*client_error = g_error_new_literal (E_BOOK_ERROR, code, error->message);
		}

		g_error_free (error);
	} else {
		if (client_error)
			*client_error = error;
	}

	return FALSE;
}

/*
 * Turn a GList of strings into an array of strings. Free with g_strfreev().
 */
static gchar **
flatten_stringlist (GList *list)
{
	gchar **array = g_new0 (gchar *, g_list_length (list) + 1);
	GList *l = list;
	gint i = 0;
	while (l != NULL) {
		array[i++] = e_util_utf8_make_valid (l->data);
		l = l->next;
	}
	return array;
}

/*
 * Turn an array of strings into a GList.
 */
static GList *
array_to_stringlist (gchar **list)
{
	GList *l = NULL;
	gchar **i = list;
	while (*i != NULL) {
		l = g_list_prepend (l, e_util_utf8_make_valid (*i++));
	}
	return g_list_reverse (l);
}

static EList *
array_to_elist (gchar **strv)
{
	EList *elst = NULL;
	gchar **i = strv;

	elst = e_list_new (NULL, (EListFreeFunc) g_free, NULL);
	if (!strv)
		return elst;

	while (*i != NULL) {
		e_list_append (elst, e_util_utf8_make_valid (*i++));
	}

	return elst;
}

