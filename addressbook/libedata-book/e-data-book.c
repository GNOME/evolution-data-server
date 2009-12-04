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

#include <unistd.h>
#include <stdlib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "e-data-book-enumtypes.h"
#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-data-book-view.h"
#include "e-book-backend-sexp.h"
#include "opid.h"

DBusGConnection *connection;

/* DBus glue */
static void impl_AddressBook_Book_open(EDataBook *book, gboolean only_if_exists, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_remove(EDataBook *book, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_getContact(EDataBook *book, const gchar *IN_uid, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_getContactList(EDataBook *book, const gchar *query, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_authenticateUser(EDataBook *book, const gchar *IN_user, const gchar *IN_passwd, const gchar *IN_auth_method, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_addContact(EDataBook *book, const gchar *IN_vcard, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_modifyContact(EDataBook *book, const gchar *IN_vcard, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_removeContacts(EDataBook *book, const gchar **IN_uids, DBusGMethodInvocation *context);
static gboolean impl_AddressBook_Book_getStaticCapabilities(EDataBook *book, gchar **OUT_capabilities, GError **error);
static void impl_AddressBook_Book_getSupportedFields(EDataBook *book, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_getRequiredFields(EDataBook *book, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_getSupportedAuthMethods(EDataBook *book, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_getBookView (EDataBook *book, const gchar *search, const guint max_results, DBusGMethodInvocation *context);
static void impl_AddressBook_Book_getChanges(EDataBook *book, const gchar *IN_change_id, DBusGMethodInvocation *context);
static gboolean impl_AddressBook_Book_cancelOperation(EDataBook *book, GError **error);
static void impl_AddressBook_Book_close(EDataBook *book, DBusGMethodInvocation *context);
#include "e-data-book-glue.h"

static void return_status_and_list (guint32 opid, EDataBookStatus status, GList *list, gboolean free_data);
static void data_book_return_error (DBusGMethodInvocation *context, gint code, const gchar *error_str);

enum {
	WRITABLE,
	CONNECTION,
	AUTH_REQUIRED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static GThreadPool *op_pool;

typedef enum {
	OP_OPEN,
	OP_AUTHENTICATE,
	OP_ADD_CONTACT,
	OP_GET_CONTACT,
	OP_GET_CONTACTS,
	OP_MODIFY_CONTACT,
	OP_REMOVE_CONTACTS,
	OP_GET_CHANGES,
} OperationID;

typedef struct {
	OperationID op;
	guint32 id; /* operation id */
	EDataBook *book; /* book */
	union {
		/* OP_OPEN */
		gboolean only_if_exists;
		/* OP_AUTHENTICATE */
		struct {
			gchar *username;
			gchar *password;
			gchar *method;
		} auth;
		/* OP_ADD_CONTACT */
		/* OP_MODIFY_CONTACT */
		gchar *vcard;
		/* OP_GET_CONTACT */
		gchar *uid;
		/* OP_GET_CONTACTS */
		gchar *query;
		/* OP_MODIFY_CONTACT */
		gchar **vcards;
		/* OP_REMOVE_CONTACTS */
		GList *ids;
		/* OP_GET_CHANGES */
		gchar *change_id;
	} d;
} OperationData;

static void
operation_thread (gpointer data, gpointer user_data)
{
	OperationData *op = data;
	EBookBackend *backend;

	backend = e_data_book_get_backend (op->book);

	switch (op->op) {
	case OP_OPEN:
		e_book_backend_open (backend, op->book, op->id, op->d.only_if_exists);
		break;
	case OP_AUTHENTICATE:
		e_book_backend_authenticate_user (backend, op->book, op->id,
						  op->d.auth.username,
						  op->d.auth.password,
						  op->d.auth.method);
		g_free (op->d.auth.username);
		g_free (op->d.auth.password);
		g_free (op->d.auth.method);
		break;
	case OP_ADD_CONTACT:
		e_book_backend_create_contact (backend, op->book, op->id, op->d.vcard);
		g_free (op->d.vcard);
		break;
	case OP_GET_CONTACT:
		e_book_backend_get_contact (backend, op->book, op->id, op->d.uid);
		g_free (op->d.uid);
		break;
	case OP_GET_CONTACTS:
		e_book_backend_get_contact_list (backend, op->book, op->id, op->d.query);
		g_free (op->d.query);
		break;
	case OP_MODIFY_CONTACT:
		e_book_backend_modify_contact (backend, op->book, op->id, op->d.vcard);
		g_free (op->d.vcard);
		break;
	case OP_REMOVE_CONTACTS:
		e_book_backend_remove_contacts (backend, op->book, op->id, op->d.ids);
		g_list_foreach (op->d.ids, (GFunc)g_free, NULL);
		g_list_free (op->d.ids);
		break;
	case OP_GET_CHANGES:
		e_book_backend_get_changes (backend, op->book, op->id, op->d.change_id);
		g_free (op->d.change_id);
		break;
	}

	g_object_unref (op->book);
	g_slice_free (OperationData, op);
}

static OperationData *
op_new (OperationID op, EDataBook *book, DBusGMethodInvocation *context)
{
	OperationData *data;

	data = g_slice_new0 (OperationData);
	data->op = op;
	data->book = g_object_ref (book);
	data->id = opid_store (context);

	return data;
}

/* Create the EDataBook error quark */
GQuark
e_data_book_error_quark (void)
{
	static GQuark quark = 0;
	if (G_UNLIKELY (quark == 0))
		quark = g_quark_from_static_string ("e-data-book-error");
	return quark;
}

/* Generate the GObject boilerplate */
G_DEFINE_TYPE(EDataBook, e_data_book, G_TYPE_OBJECT)

static void
e_data_book_dispose (GObject *object)
{
	EDataBook *book = E_DATA_BOOK (object);

	if (book->backend) {
		g_object_unref (book->backend);
		book->backend = NULL;
	}

	if (book->source) {
		g_object_unref (book->source);
		book->source = NULL;
	}

	G_OBJECT_CLASS (e_data_book_parent_class)->dispose (object);
}

static void
e_data_book_class_init (EDataBookClass *e_data_book_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (e_data_book_class);

	object_class->dispose = e_data_book_dispose;

	signals[WRITABLE] =
		g_signal_new ("writable",
			      G_OBJECT_CLASS_TYPE (e_data_book_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals[CONNECTION] =
		g_signal_new ("connection",
			      G_OBJECT_CLASS_TYPE (e_data_book_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals[AUTH_REQUIRED] =
		g_signal_new ("auth-required",
			      G_OBJECT_CLASS_TYPE (e_data_book_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (e_data_book_class), &dbus_glib_e_data_book_object_info);

	dbus_g_error_domain_register (E_DATA_BOOK_ERROR, NULL, E_TYPE_DATA_BOOK_STATUS);

	op_pool = g_thread_pool_new (operation_thread, NULL, 10, FALSE, NULL);
	/* Kill threads which don't do anything for 10 seconds */
	g_thread_pool_set_max_idle_time (10 * 1000);
}

/* Instance init */
static void
e_data_book_init (EDataBook *ebook)
{
}

EDataBook *
e_data_book_new (EBookBackend *backend, ESource *source)
{
	EDataBook *book;

	book = g_object_new (E_TYPE_DATA_BOOK, NULL);
	book->backend = g_object_ref (backend);
	book->source = g_object_ref (source);

	return book;
}

ESource*
e_data_book_get_source (EDataBook *book)
{
	g_return_val_if_fail (book != NULL, NULL);

	return book->source;
}

EBookBackend*
e_data_book_get_backend (EDataBook *book)
{
	g_return_val_if_fail (book != NULL, NULL);

	return book->backend;
}

static void
impl_AddressBook_Book_open(EDataBook *book, gboolean only_if_exists, DBusGMethodInvocation *context)
{
	OperationData *op;

	op = op_new (OP_OPEN, book, context);
	op->d.only_if_exists = only_if_exists;
	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_open (EDataBook *book, guint opid, EDataBookStatus status)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot open book"));
	} else {
		dbus_g_method_return (context);
	}
}

static void
impl_AddressBook_Book_remove(EDataBook *book, DBusGMethodInvocation *context)
{
	e_book_backend_remove (book->backend, book, opid_store (context));
}

void
e_data_book_respond_remove (EDataBook *book, guint opid, EDataBookStatus status)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot remove book"));
	} else {
		dbus_g_method_return (context);
	}
}

static void
impl_AddressBook_Book_getContact (EDataBook *book, const gchar *IN_uid, DBusGMethodInvocation *context)
{
	OperationData *op;

	if (IN_uid == NULL) {
		data_book_return_error (context, E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND, _("Cannot get contact"));
		return;
	}

	op = op_new (OP_GET_CONTACT, book, context);
	op->d.uid = g_strdup (IN_uid);
	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_get_contact (EDataBook *book, guint32 opid, EDataBookStatus status, const gchar *vcard)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error  (context, status, _("Cannot get contact"));
	} else {
		dbus_g_method_return (context, vcard);
	}
}

static void
impl_AddressBook_Book_getContactList (EDataBook *book, const gchar *query, DBusGMethodInvocation *context)
{
	OperationData *op;

	if (query == NULL || query[0] == '\0') {
		data_book_return_error (context, E_DATA_BOOK_STATUS_INVALID_QUERY, _("Empty query"));
		return;
	}

	op = op_new (OP_GET_CONTACTS, book, context);
	op->d.query = g_strdup (query);
	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_get_contact_list (EDataBook *book, guint32 opid, EDataBookStatus status, GList *cards)
{
	return_status_and_list (opid, status, cards, TRUE);
}

static void
impl_AddressBook_Book_authenticateUser(EDataBook *book, const gchar *IN_user, const gchar *IN_passwd, const gchar *IN_auth_method, DBusGMethodInvocation *context)
{
	OperationData *op;

	op = op_new (OP_AUTHENTICATE, book, context);
	op->d.auth.username = g_strdup (IN_user);
	op->d.auth.password = g_strdup (IN_passwd);
	op->d.auth.method = g_strdup (IN_auth_method);
	g_thread_pool_push (op_pool, op, NULL);
}

static void
data_book_return_error (DBusGMethodInvocation *context, gint code, const gchar *error_str)
{
	GError *error;

	error = g_error_new (E_DATA_BOOK_ERROR, code, "%s", error_str);
	dbus_g_method_return_error (context, error);

	g_error_free (error);
}

void
e_data_book_respond_authenticate_user (EDataBook *book, guint32 opid, EDataBookStatus status)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot authenticate user"));
	} else {
		dbus_g_method_return (context);
	}
}

static void
impl_AddressBook_Book_addContact (EDataBook *book, const gchar *IN_vcard, DBusGMethodInvocation *context)
{
	OperationData *op;

	if (IN_vcard == NULL || IN_vcard[0] == '\0') {
		data_book_return_error (context, E_DATA_BOOK_STATUS_INVALID_QUERY, _("Cannot add contact"));
		return;
	}

	op = op_new (OP_ADD_CONTACT, book, context);
	op->d.vcard = g_strdup (IN_vcard);
	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_create (EDataBook *book, guint32 opid, EDataBookStatus status, EContact *contact)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot add contact"));
	} else {
		e_book_backend_notify_update (e_data_book_get_backend (book), contact);
		e_book_backend_notify_complete (e_data_book_get_backend (book));

		dbus_g_method_return (context, e_contact_get_const (contact, E_CONTACT_UID));
	}
}

static void
impl_AddressBook_Book_modifyContact (EDataBook *book, const gchar *IN_vcard, DBusGMethodInvocation *context)
{
	OperationData *op;

	if (IN_vcard == NULL) {
		data_book_return_error (context, E_DATA_BOOK_STATUS_INVALID_QUERY, _("Cannot modify contact"));
		return;
	}

	op = op_new (OP_MODIFY_CONTACT, book, context);
	op->d.vcard = g_strdup (IN_vcard);
	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_modify (EDataBook *book, guint32 opid, EDataBookStatus status, EContact *contact)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot modify contact"));
	} else {
		e_book_backend_notify_update (e_data_book_get_backend (book), contact);
		e_book_backend_notify_complete (e_data_book_get_backend (book));

		dbus_g_method_return (context);
	}
}

static void
impl_AddressBook_Book_removeContacts(EDataBook *book, const gchar **IN_uids, DBusGMethodInvocation *context)
{
	OperationData *op;

	/* Allow an empty array to be removed */
	if (IN_uids == NULL) {
		dbus_g_method_return (context);
		return;
	}

	op = op_new (OP_REMOVE_CONTACTS, book, context);

	for (; *IN_uids; IN_uids++) {
		op->d.ids = g_list_prepend (op->d.ids, g_strdup (*IN_uids));
	}

	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_remove_contacts (EDataBook *book, guint32 opid, EDataBookStatus status, GList *ids)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot remove contacts"));
	} else {
		GList *i;

		for (i = ids; i; i = i->next)
			e_book_backend_notify_remove (e_data_book_get_backend (book), i->data);
		e_book_backend_notify_complete (e_data_book_get_backend (book));

		dbus_g_method_return (context);
	}
}

static gboolean
impl_AddressBook_Book_getStaticCapabilities(EDataBook *book, gchar **OUT_capabilities, GError **error)
{
	*OUT_capabilities = e_book_backend_get_static_capabilities (e_data_book_get_backend (book));
	return TRUE;
}

static void
impl_AddressBook_Book_getSupportedFields(EDataBook *book, DBusGMethodInvocation *context)
{
	e_book_backend_get_supported_fields (e_data_book_get_backend (book), book, opid_store (context));
}

void
e_data_book_respond_get_supported_fields (EDataBook *book, guint32 opid, EDataBookStatus status, GList *fields)
{
	return_status_and_list (opid, status, fields, FALSE);
}

static void
impl_AddressBook_Book_getRequiredFields(EDataBook *book, DBusGMethodInvocation *context)
{
	e_book_backend_get_required_fields (e_data_book_get_backend (book), book, opid_store (context));
}

void
e_data_book_respond_get_required_fields (EDataBook *book, guint32 opid, EDataBookStatus status, GList *fields)
{
	return_status_and_list (opid, status, fields, FALSE);
}

static void
impl_AddressBook_Book_getSupportedAuthMethods(EDataBook *book, DBusGMethodInvocation *context)
{
	e_book_backend_get_supported_auth_methods (e_data_book_get_backend (book), book, opid_store (context));
}

void
e_data_book_respond_get_supported_auth_methods (EDataBook *book, guint32 opid, EDataBookStatus status, GList *auth_methods)
{
	return_status_and_list (opid, status, auth_methods, FALSE);
}

static gchar *
construct_bookview_path (void)
{
	static volatile guint counter = 1;

	return g_strdup_printf ("/org/gnome/evolution/dataserver/addressbook/BookView/%d/%d",
				getpid (),
				g_atomic_int_exchange_and_add ((int*)&counter, 1));
}

static void
impl_AddressBook_Book_getBookView (EDataBook *book, const gchar *search, const guint max_results, DBusGMethodInvocation *context)
{
	EBookBackend *backend = e_data_book_get_backend (book);
	EBookBackendSExp *card_sexp;
	EDataBookView *book_view;
	gchar *path;

	card_sexp = e_book_backend_sexp_new (search);
	if (!card_sexp) {
		data_book_return_error (context, E_DATA_BOOK_STATUS_INVALID_QUERY, _("Invalid query"));
		return;
	}

	path = construct_bookview_path ();
	book_view = e_data_book_view_new (book, path, search, card_sexp, max_results);

	e_book_backend_add_book_view (backend, book_view);

	dbus_g_method_return (context, path);
	g_free (path);
}

static void
impl_AddressBook_Book_getChanges(EDataBook *book, const gchar *IN_change_id, DBusGMethodInvocation *context)
{
	OperationData *op;

	op = op_new (OP_GET_CHANGES, book, context);
	op->d.change_id = g_strdup (IN_change_id);
	g_thread_pool_push (op_pool, op, NULL);
}

void
e_data_book_respond_get_changes (EDataBook *book, guint32 opid, EDataBookStatus status, GList *changes)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status != E_DATA_BOOK_STATUS_SUCCESS) {
		data_book_return_error (context, status, _("Cannot get changes"));
	} else {
		/* The DBus interface to this is a(us), an array of structs of unsigned ints
		   and strings.  In dbus-glib this is a GPtrArray of GValueArray of unsigned
		   gint and strings. */
		GPtrArray *array;

		array = g_ptr_array_new ();

		while (changes != NULL) {
			EDataBookChange *change = (EDataBookChange *) changes->data;
			GValueArray *vals;

			vals = g_value_array_new (2);

			g_value_array_append (vals, NULL);
			g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
			g_value_set_uint (g_value_array_get_nth (vals, 0), change->change_type);

			g_value_array_append (vals, NULL);
			g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_STRING);
			g_value_take_string (g_value_array_get_nth (vals, 1), change->vcard);
			/* Now change->vcard is owned by the GValue */

			g_free (change);
			changes = g_list_remove (changes, change);
		}

		dbus_g_method_return (context, array);
		g_ptr_array_foreach (array, (GFunc)g_value_array_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
}

static gboolean
impl_AddressBook_Book_cancelOperation(EDataBook *book, GError **error)
{
	if (e_book_backend_cancel_operation (e_data_book_get_backend (book), book) != GNOME_Evolution_Addressbook_Success) {
		g_set_error (error, E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_COULD_NOT_CANCEL, "Failed to cancel operation");
		return FALSE;
	}

	return TRUE;
}

static void
impl_AddressBook_Book_close(EDataBook *book, DBusGMethodInvocation *context)
{
	e_book_backend_remove_client (e_data_book_get_backend (book), book);
	g_object_unref (book);

	dbus_g_method_return (context);
}

void
e_data_book_report_writable (EDataBook *book, gboolean writable)
{
	g_return_if_fail (book != NULL);

	g_signal_emit (book, signals[WRITABLE], 0, writable);
}

void
e_data_book_report_connection_status (EDataBook *book, gboolean connected)
{
	g_return_if_fail (book != NULL);

	g_signal_emit (book, signals[CONNECTION], 0, connected);
}

void
e_data_book_report_auth_required (EDataBook *book)
{
	g_return_if_fail (book != NULL);

	g_signal_emit (book, signals[AUTH_REQUIRED], 0);
}

static void
return_status_and_list (guint32 opid, EDataBookStatus status, GList *list, gboolean free_data)
{
	DBusGMethodInvocation *context = opid_fetch (opid);

	if (status == E_DATA_BOOK_STATUS_SUCCESS) {
		gchar **array;
		GList *l;
		gint i = 0;

		array = g_new0 (gchar *, g_list_length (list) + 1);
		for (l = list; l != NULL; l = l->next) {
			array[i++] = l->data;
		}

		dbus_g_method_return (context, array);

		if (free_data) {
			g_strfreev (array);
		} else {
			g_free (array);
		}
	} else {
		data_book_return_error (context, status, _("Cannot complete operation"));
	}
}
