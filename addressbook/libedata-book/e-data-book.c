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
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libebook/libebook.h>

#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-data-book-view.h"
#include "e-book-backend.h"
#include "e-book-backend-sexp.h"

#include "e-gdbus-book.h"

#define E_DATA_BOOK_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK, EDataBookPrivate))

struct _EDataBookPrivate
{
	EGdbusBook *gdbus_object;

	EBookBackend *backend;

	GStaticRecMutex pending_ops_lock;
	GHashTable *pending_ops; /* opid to GCancellable for still running operations */
};

enum {
	PROP_0,
	PROP_BACKEND
};

static EOperationPool *ops_pool = NULL;

typedef enum {
	OP_OPEN,
	OP_REMOVE,
	OP_REFRESH,
	OP_GET_CONTACT,
	OP_GET_CONTACTS,
	OP_GET_CONTACTS_UIDS,
	OP_ADD_CONTACTS,
	OP_REMOVE_CONTACTS,
	OP_MODIFY_CONTACTS,
	OP_GET_BACKEND_PROPERTY,
	OP_SET_BACKEND_PROPERTY,
	OP_GET_VIEW,
	OP_CANCEL_OPERATION,
	OP_CANCEL_ALL,
	OP_CLOSE
} OperationID;

typedef struct {
	OperationID op;
	guint32 id; /* operation id */
	EDataBook *book; /* book */
	GCancellable *cancellable;

	union {
		/* OP_OPEN */
		gboolean only_if_exists;
		/* OP_GET_CONTACT */
		gchar *uid;
		/* OP_REMOVE_CONTACTS */
		GSList *ids;
		/* OP_ADD_CONTACT */
		/* OP_MODIFY_CONTACTS */
		GSList *vcards;
		/* OP_GET_VIEW */
		/* OP_GET_CONTACTS */
		/* OP_GET_CONTACTS_UIDS */
		gchar *query;
		/* OP_CANCEL_OPERATION */
		guint opid;
		/* OP_GET_BACKEND_PROPERTY */
		gchar *prop_name;
		/* OP_SET_BACKEND_PROPERTY */
		struct _sbp {
			gchar *prop_name;
			gchar *prop_value;
		} sbp;

		/* OP_REMOVE */
		/* OP_REFRESH */
		/* OP_CANCEL_ALL */
		/* OP_CLOSE */
	} d;
} OperationData;

G_DEFINE_TYPE (EDataBook, e_data_book, G_TYPE_OBJECT)

static gchar *
construct_bookview_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/AddressBookView/%d/%d",
		getpid (), counter);
}

static void
cancel_ops_cb (gpointer opid,
               gpointer cancellable,
               gpointer user_data)
{
	g_return_if_fail (cancellable != NULL);

	g_cancellable_cancel (cancellable);
}

static void
operation_thread (gpointer data,
                  gpointer user_data)
{
	OperationData *op = data;
	EBookBackend *backend;

	backend = e_data_book_get_backend (op->book);

	switch (op->op) {
	case OP_OPEN:
		e_book_backend_open (backend, op->book, op->id, op->cancellable, op->d.only_if_exists);
		break;
	case OP_ADD_CONTACTS:
		e_book_backend_create_contacts (backend, op->book, op->id, op->cancellable, op->d.vcards);
		e_util_free_string_slist (op->d.vcards);
		break;
	case OP_GET_CONTACT:
		e_book_backend_get_contact (backend, op->book, op->id, op->cancellable, op->d.uid);
		g_free (op->d.uid);
		break;
	case OP_GET_CONTACTS:
		e_book_backend_get_contact_list (backend, op->book, op->id, op->cancellable, op->d.query);
		g_free (op->d.query);
		break;
	case OP_GET_CONTACTS_UIDS:
		e_book_backend_get_contact_list_uids (backend, op->book, op->id, op->cancellable, op->d.query);
		g_free (op->d.query);
		break;
	case OP_MODIFY_CONTACTS:
		e_book_backend_modify_contacts (backend, op->book, op->id, op->cancellable, op->d.vcards);
		e_util_free_string_slist (op->d.vcards);
		break;
	case OP_REMOVE_CONTACTS:
		e_book_backend_remove_contacts (backend, op->book, op->id, op->cancellable, op->d.ids);
		e_util_free_string_slist (op->d.ids);
		break;
	case OP_REMOVE:
		e_book_backend_remove (backend, op->book, op->id, op->cancellable);
		break;
	case OP_REFRESH:
		e_book_backend_refresh (backend, op->book, op->id, op->cancellable);
		break;
	case OP_GET_BACKEND_PROPERTY:
		e_book_backend_get_backend_property (backend, op->book, op->id, op->cancellable, op->d.prop_name);
		g_free (op->d.prop_name);
		break;
	case OP_SET_BACKEND_PROPERTY:
		e_book_backend_set_backend_property (backend, op->book, op->id, op->cancellable, op->d.sbp.prop_name, op->d.sbp.prop_value);
		g_free (op->d.sbp.prop_name);
		g_free (op->d.sbp.prop_value);
		break;
	case OP_GET_VIEW:
		if (op->d.query) {
			EBookBackendSExp *card_sexp;
			EDataBookView *book_view;
			gchar *path;
			GError *error = NULL;

			card_sexp = e_book_backend_sexp_new (op->d.query);
			if (!card_sexp) {
				error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
				/* Translators: This is prefix to a detailed error message */
				g_prefix_error (&error, "%s", _("Invalid query: "));
				e_gdbus_book_emit_get_view_done (op->book->priv->gdbus_object, op->id, error, NULL);
				g_error_free (error);
				break;
			}

			path = construct_bookview_path ();

			book_view = e_data_book_view_new (op->book, op->d.query, card_sexp);
			e_data_book_view_register_gdbus_object (book_view, e_gdbus_book_stub_get_connection (op->book->priv->gdbus_object), path, &error);

			if (error) {
				/* Translators: This is prefix to a detailed error message */
				g_prefix_error (&error, "%s", _("Invalid query: "));
				e_gdbus_book_emit_get_view_done (op->book->priv->gdbus_object, op->id, error, NULL);
				g_error_free (error);
				g_object_unref (book_view);
				g_free (path);

				break;
			}

			e_book_backend_add_book_view (backend, book_view);

			e_gdbus_book_emit_get_view_done (op->book->priv->gdbus_object, op->id, NULL, path);

			g_free (path);
		}
		g_free (op->d.query);
		break;
	case OP_CANCEL_OPERATION:
		g_static_rec_mutex_lock (&op->book->priv->pending_ops_lock);

		if (g_hash_table_lookup (op->book->priv->pending_ops, GUINT_TO_POINTER (op->d.opid))) {
			GCancellable *cancellable = g_hash_table_lookup (op->book->priv->pending_ops, GUINT_TO_POINTER (op->d.opid));

			g_cancellable_cancel (cancellable);
		}

		g_static_rec_mutex_unlock (&op->book->priv->pending_ops_lock);
		break;
	case OP_CLOSE:
		/* close just cancels all pending ops and frees data book */
		e_book_backend_remove_client (backend, op->book);
	case OP_CANCEL_ALL:
		g_static_rec_mutex_lock (&op->book->priv->pending_ops_lock);
		g_hash_table_foreach (op->book->priv->pending_ops, cancel_ops_cb, NULL);
		g_static_rec_mutex_unlock (&op->book->priv->pending_ops_lock);
		break;
	}

	g_object_unref (op->book);
	g_object_unref (op->cancellable);
	g_slice_free (OperationData, op);
}

static OperationData *
op_new (OperationID op,
        EDataBook *book)
{
	OperationData *data;

	data = g_slice_new0 (OperationData);
	data->op = op;
	data->book = g_object_ref (book);
	data->id = e_operation_pool_reserve_opid (ops_pool);
	data->cancellable = g_cancellable_new ();

	g_static_rec_mutex_lock (&book->priv->pending_ops_lock);
	g_hash_table_insert (book->priv->pending_ops, GUINT_TO_POINTER (data->id), g_object_ref (data->cancellable));
	g_static_rec_mutex_unlock (&book->priv->pending_ops_lock);

	return data;
}

static void
op_complete (EDataBook *book,
             guint32 opid)
{
	g_return_if_fail (book != NULL);

	e_operation_pool_release_opid (ops_pool, opid);

	g_static_rec_mutex_lock (&book->priv->pending_ops_lock);
	g_hash_table_remove (book->priv->pending_ops, GUINT_TO_POINTER (opid));
	g_static_rec_mutex_unlock (&book->priv->pending_ops_lock);
}

/**
 * e_data_book_status_to_string:
 *
 * Since: 2.32
 **/
const gchar *
e_data_book_status_to_string (EDataBookStatus status)
{
	gint i;
	static struct _statuses {
		EDataBookStatus status;
		const gchar *msg;
	} statuses[] = {
		{ E_DATA_BOOK_STATUS_SUCCESS,				N_("Success") },
		{ E_DATA_BOOK_STATUS_BUSY,				N_("Backend is busy") },
		{ E_DATA_BOOK_STATUS_REPOSITORY_OFFLINE,		N_("Repository offline") },
		{ E_DATA_BOOK_STATUS_PERMISSION_DENIED,			N_("Permission denied") },
		{ E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND,			N_("Contact not found") },
		{ E_DATA_BOOK_STATUS_CONTACTID_ALREADY_EXISTS,		N_("Contact ID already exists") },
		{ E_DATA_BOOK_STATUS_AUTHENTICATION_FAILED,		N_("Authentication Failed") },
		{ E_DATA_BOOK_STATUS_AUTHENTICATION_REQUIRED,		N_("Authentication Required") },
		{ E_DATA_BOOK_STATUS_UNSUPPORTED_FIELD,			N_("Unsupported field") },
		{ E_DATA_BOOK_STATUS_UNSUPPORTED_AUTHENTICATION_METHOD,	N_("Unsupported authentication method") },
		{ E_DATA_BOOK_STATUS_TLS_NOT_AVAILABLE,			N_("TLS not available") },
		{ E_DATA_BOOK_STATUS_NO_SUCH_BOOK,			N_("Address book does not exist") },
		{ E_DATA_BOOK_STATUS_BOOK_REMOVED,			N_("Book removed") },
		{ E_DATA_BOOK_STATUS_OFFLINE_UNAVAILABLE,		N_("Not available in offline mode") },
		{ E_DATA_BOOK_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED,	N_("Search size limit exceeded") },
		{ E_DATA_BOOK_STATUS_SEARCH_TIME_LIMIT_EXCEEDED,	N_("Search time limit exceeded") },
		{ E_DATA_BOOK_STATUS_INVALID_QUERY,			N_("Invalid query") },
		{ E_DATA_BOOK_STATUS_QUERY_REFUSED,			N_("Query refused") },
		{ E_DATA_BOOK_STATUS_COULD_NOT_CANCEL,			N_("Could not cancel") },
		/* { E_DATA_BOOK_STATUS_OTHER_ERROR,			N_("Other error") }, */
		{ E_DATA_BOOK_STATUS_INVALID_SERVER_VERSION,		N_("Invalid server version") },
		{ E_DATA_BOOK_STATUS_NO_SPACE,				N_("No space") },
		{ E_DATA_BOOK_STATUS_INVALID_ARG,			N_("Invalid argument") },
		/* Translators: The string for NOT_SUPPORTED error */
		{ E_DATA_BOOK_STATUS_NOT_SUPPORTED,			N_("Not supported") },
		{ E_DATA_BOOK_STATUS_NOT_OPENED,			N_("Backend is not opened yet") }
	};

	for (i = 0; i < G_N_ELEMENTS (statuses); i++) {
		if (statuses[i].status == status)
			return _(statuses[i].msg);
	}

	return _("Other error");
}

/* Create the EDataBook error quark */
GQuark
e_data_book_error_quark (void)
{
	#define ERR_PREFIX "org.gnome.evolution.dataserver.AddressBook."

	static const GDBusErrorEntry entries[] = {
		{ E_DATA_BOOK_STATUS_SUCCESS,				ERR_PREFIX "Success" },
		{ E_DATA_BOOK_STATUS_BUSY,				ERR_PREFIX "Busy" },
		{ E_DATA_BOOK_STATUS_REPOSITORY_OFFLINE,		ERR_PREFIX "RepositoryOffline" },
		{ E_DATA_BOOK_STATUS_PERMISSION_DENIED,			ERR_PREFIX "PermissionDenied" },
		{ E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND,			ERR_PREFIX "ContactNotFound" },
		{ E_DATA_BOOK_STATUS_CONTACTID_ALREADY_EXISTS,		ERR_PREFIX "ContactIDAlreadyExists" },
		{ E_DATA_BOOK_STATUS_AUTHENTICATION_FAILED,		ERR_PREFIX "AuthenticationFailed" },
		{ E_DATA_BOOK_STATUS_AUTHENTICATION_REQUIRED,		ERR_PREFIX "AuthenticationRequired" },
		{ E_DATA_BOOK_STATUS_UNSUPPORTED_FIELD,			ERR_PREFIX "UnsupportedField" },
		{ E_DATA_BOOK_STATUS_UNSUPPORTED_AUTHENTICATION_METHOD,	ERR_PREFIX "UnsupportedAuthenticationMethod" },
		{ E_DATA_BOOK_STATUS_TLS_NOT_AVAILABLE,			ERR_PREFIX "TLSNotAvailable" },
		{ E_DATA_BOOK_STATUS_NO_SUCH_BOOK,			ERR_PREFIX "NoSuchBook" },
		{ E_DATA_BOOK_STATUS_BOOK_REMOVED,			ERR_PREFIX "BookRemoved" },
		{ E_DATA_BOOK_STATUS_OFFLINE_UNAVAILABLE,		ERR_PREFIX "OfflineUnavailable" },
		{ E_DATA_BOOK_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED,	ERR_PREFIX "SearchSizeLimitExceeded" },
		{ E_DATA_BOOK_STATUS_SEARCH_TIME_LIMIT_EXCEEDED,	ERR_PREFIX "SearchTimeLimitExceeded" },
		{ E_DATA_BOOK_STATUS_INVALID_QUERY,			ERR_PREFIX "InvalidQuery" },
		{ E_DATA_BOOK_STATUS_QUERY_REFUSED,			ERR_PREFIX "QueryRefused" },
		{ E_DATA_BOOK_STATUS_COULD_NOT_CANCEL,			ERR_PREFIX "CouldNotCancel" },
		{ E_DATA_BOOK_STATUS_OTHER_ERROR,			ERR_PREFIX "OtherError" },
		{ E_DATA_BOOK_STATUS_INVALID_SERVER_VERSION,		ERR_PREFIX "InvalidServerVersion" },
		{ E_DATA_BOOK_STATUS_NO_SPACE,				ERR_PREFIX "NoSpace" },
		{ E_DATA_BOOK_STATUS_INVALID_ARG,			ERR_PREFIX "InvalidArg" },
		{ E_DATA_BOOK_STATUS_NOT_SUPPORTED,			ERR_PREFIX "NotSupported" },
		{ E_DATA_BOOK_STATUS_NOT_OPENED,			ERR_PREFIX "NotOpened" }
	};

	#undef ERR_PREFIX

	static volatile gsize quark_volatile = 0;

	g_dbus_error_register_error_domain ("e-data-book-error", &quark_volatile, entries, G_N_ELEMENTS (entries));

	return (GQuark) quark_volatile;
}

/**
 * e_data_book_create_error:
 *
 * Since: 2.32
 **/
GError *
e_data_book_create_error (EDataBookStatus status,
                          const gchar *custom_msg)
{
	if (status == E_DATA_BOOK_STATUS_SUCCESS)
		return NULL;

	return g_error_new_literal (E_DATA_BOOK_ERROR, status, custom_msg ? custom_msg : e_data_book_status_to_string (status));
}

/**
 * e_data_book_create_error_fmt:
 *
 * Since: 2.32
 **/
GError *
e_data_book_create_error_fmt (EDataBookStatus status,
                              const gchar *custom_msg_fmt,
                              ...)
{
	GError *error;
	gchar *custom_msg;
	va_list ap;

	if (!custom_msg_fmt)
		return e_data_book_create_error (status, NULL);

	va_start (ap, custom_msg_fmt);
	custom_msg = g_strdup_vprintf (custom_msg_fmt, ap);
	va_end (ap);

	error = e_data_book_create_error (status, custom_msg);

	g_free (custom_msg);

	return error;
}

static void
data_book_return_error (GDBusMethodInvocation *invocation,
                        const GError *perror,
                        const gchar *error_prefix)
{
	GError *error;

	if (perror == NULL)
		error = g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_OTHER_ERROR, "%s", _("Unknown error"));
	else
		error = g_error_new (E_DATA_BOOK_ERROR, perror->code, "%s", perror->message);

	g_prefix_error (&error, "%s", error_prefix);

	g_dbus_method_invocation_return_gerror (invocation, error);

	g_error_free (error);
}

/**
 * e_data_book_string_slist_to_comma_string:
 *
 * Takes a list of strings and converts it to a comma-separated string of
 * values; free returned pointer with g_free()
 *
 * Since: 3.2
 **/
gchar *
e_data_book_string_slist_to_comma_string (const GSList *strings)
{
	GString *tmp;
	gchar *res;
	const GSList *l;

	tmp = g_string_new ("");
	for (l = strings; l != NULL; l = l->next) {
		const gchar *str = l->data;

		if (!str)
			continue;

		if (strchr (str, ',')) {
			g_warning ("%s: String cannot contain comma; skipping value '%s'\n", G_STRFUNC, str);
			continue;
		}

		if (tmp->len)
			g_string_append_c (tmp, ',');
		g_string_append (tmp, str);
	}

	res = e_util_utf8_make_valid (tmp->str);

	g_string_free (tmp, TRUE);

	return res;
}

static gboolean
impl_Book_open (EGdbusBook *object,
                GDBusMethodInvocation *invocation,
                gboolean only_if_exists,
                EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_OPEN, book);
	op->d.only_if_exists = only_if_exists;

	e_gdbus_book_complete_open (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_remove (EGdbusBook *object,
                  GDBusMethodInvocation *invocation,
                  EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_REMOVE, book);

	e_gdbus_book_complete_remove (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_refresh (EGdbusBook *object,
                   GDBusMethodInvocation *invocation,
                   EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_REFRESH, book);

	e_gdbus_book_complete_refresh (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_get_contact (EGdbusBook *object,
                       GDBusMethodInvocation *invocation,
                       const gchar *in_uid,
                       EDataBook *book)
{
	OperationData *op;

	if (in_uid == NULL) {
		GError *error;

		error = e_data_book_create_error (E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND, NULL);
		/* Translators: This is prefix to a detailed error message */
		data_book_return_error (invocation, error, _("Cannot get contact: "));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_GET_CONTACT, book);
	op->d.uid = g_strdup (in_uid);

	e_gdbus_book_complete_get_contact (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_get_contact_list (EGdbusBook *object,
                            GDBusMethodInvocation *invocation,
                            const gchar *in_query,
                            EDataBook *book)
{
	OperationData *op;

	if (in_query == NULL || !*in_query) {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: This is prefix to a detailed error message */
		data_book_return_error (invocation, error, _("Empty query: "));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_GET_CONTACTS, book);
	op->d.query = g_strdup (in_query);

	e_gdbus_book_complete_get_contact_list (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_get_contact_list_uids (EGdbusBook *object,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *in_query,
                                 EDataBook *book)
{
	OperationData *op;

	if (in_query == NULL || !*in_query) {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: This is prefix to a detailed error message */
		data_book_return_error (invocation, error, _("Empty query: "));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_GET_CONTACTS_UIDS, book);
	op->d.query = g_strdup (in_query);

	e_gdbus_book_complete_get_contact_list_uids (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_add_contacts (EGdbusBook *object,
                       GDBusMethodInvocation *invocation,
                       const gchar * const *in_vcards,
                       EDataBook *book)
{
	OperationData *op;

	if (in_vcards == NULL || !*in_vcards) {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: This is prefix to a detailed error message */
		data_book_return_error (invocation, error, _("Cannot add contact: "));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_ADD_CONTACTS, book);
	op->d.vcards = e_util_strv_to_slist (in_vcards);

	e_gdbus_book_complete_add_contacts (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_modify_contacts (EGdbusBook *object,
                           GDBusMethodInvocation *invocation,
                           const gchar * const *in_vcards,
                           EDataBook *book)
{
	OperationData *op;

	if (in_vcards == NULL || !*in_vcards) {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: This is prefix to a detailed error message */
		data_book_return_error (invocation, error, _("Cannot modify contacts: "));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_MODIFY_CONTACTS, book);
	op->d.vcards = e_util_strv_to_slist (in_vcards);

	e_gdbus_book_complete_modify_contacts (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_remove_contacts (EGdbusBook *object,
                           GDBusMethodInvocation *invocation,
                           const gchar * const *in_uids,
                           EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_REMOVE_CONTACTS, book);

	/* Allow an empty array to be removed */
	for (; in_uids && *in_uids; in_uids++) {
		op->d.ids = g_slist_prepend (op->d.ids, g_strdup (*in_uids));
	}

	e_gdbus_book_complete_remove_contacts (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_get_backend_property (EGdbusBook *object,
                                GDBusMethodInvocation *invocation,
                                const gchar *in_prop_name,
                                EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_GET_BACKEND_PROPERTY, book);
	op->d.prop_name = g_strdup (in_prop_name);

	e_gdbus_book_complete_get_backend_property (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_set_backend_property (EGdbusBook *object,
                                GDBusMethodInvocation *invocation,
                                const gchar * const *in_prop_name_value,
                                EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_SET_BACKEND_PROPERTY, book);
	g_return_val_if_fail (e_gdbus_book_decode_set_backend_property (in_prop_name_value, &op->d.sbp.prop_name, &op->d.sbp.prop_value), FALSE);

	e_gdbus_book_complete_set_backend_property (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_get_view (EGdbusBook *object,
                    GDBusMethodInvocation *invocation,
                    const gchar *in_query,
                    EDataBook *book)
{
	OperationData *op;

	if (!in_query || !*in_query) {
		GError *error;

		error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: This is prefix to a detailed error message */
		data_book_return_error (invocation, error, _("Invalid query: "));
		g_error_free (error);

		return TRUE;
	}

	op = op_new (OP_GET_VIEW, book);
	op->d.query = g_strdup (in_query);

	e_gdbus_book_complete_get_view (book->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_cancel_operation (EGdbusBook *object,
                            GDBusMethodInvocation *invocation,
                            guint in_opid,
                            EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_CANCEL_OPERATION, book);
	op->d.opid = in_opid;

	e_gdbus_book_complete_cancel_operation (book->priv->gdbus_object, invocation, NULL);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_cancel_all (EGdbusBook *object,
                      GDBusMethodInvocation *invocation,
                      EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_CANCEL_ALL, book);

	e_gdbus_book_complete_cancel_all (book->priv->gdbus_object, invocation, NULL);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Book_close (EGdbusBook *object,
                 GDBusMethodInvocation *invocation,
                 EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_CLOSE, book);
	/* unref here makes sure the book is freed in a separate thread */
	g_object_unref (book);

	e_gdbus_book_complete_close (book->priv->gdbus_object, invocation, NULL);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

void
e_data_book_respond_open (EDataBook *book,
                          guint opid,
                          GError *error)
{
	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot open book: "));

	e_gdbus_book_emit_open_done (book->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
}

void
e_data_book_respond_remove (EDataBook *book,
                            guint opid,
                            GError *error)
{
	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove book: "));

	e_gdbus_book_emit_remove_done (book->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
	else
		e_book_backend_set_is_removed (book->priv->backend, TRUE);
}

/**
 * e_data_book_respond_refresh:
 * @book: An addressbook client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the refresh method call.
 *
 * Since: 3.2
 */
void
e_data_book_respond_refresh (EDataBook *book,
                             guint32 opid,
                             GError *error)
{
	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot refresh address book: "));

	e_gdbus_book_emit_refresh_done (book->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
}

/**
 * e_data_book_respond_get_backend_property:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_respond_get_backend_property (EDataBook *book,
                                          guint32 opid,
                                          GError *error,
                                          const gchar *prop_value)
{
	gchar *gdbus_prop_value = NULL;

	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot get backend property: "));

	e_gdbus_book_emit_get_backend_property_done (book->priv->gdbus_object, opid, error, e_util_ensure_gdbus_string (prop_value, &gdbus_prop_value));

	if (error)
		g_error_free (error);

	g_free (gdbus_prop_value);
}

/**
 * e_data_book_respond_set_backend_property:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_respond_set_backend_property (EDataBook *book,
                                          guint32 opid,
                                          GError *error)
{
	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot set backend property: "));

	e_gdbus_book_emit_set_backend_property_done (book->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
}

void
e_data_book_respond_get_contact (EDataBook *book,
                                 guint32 opid,
                                 GError *error,
                                 const gchar *vcard)
{
	gchar *gdbus_vcard = NULL;

	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot get contact: "));

	e_gdbus_book_emit_get_contact_done (book->priv->gdbus_object, opid, error, e_util_ensure_gdbus_string (vcard, &gdbus_vcard));

	if (error)
		g_error_free (error);

	g_free (gdbus_vcard);
}

void
e_data_book_respond_get_contact_list (EDataBook *book,
                                      guint32 opid,
                                      GError *error,
                                      const GSList *cards)
{
	if (error) {
		/* Translators: This is prefix to a detailed error message */
		g_prefix_error (&error, "%s", _("Cannot get contact list: "));
		e_gdbus_book_emit_get_contact_list_done (book->priv->gdbus_object, opid, error, NULL);
		g_error_free (error);
	} else {
		gchar **array;
		const GSList *l;
		gint i = 0;

		array = g_new0 (gchar *, g_slist_length ((GSList *) cards) + 1);
		for (l = cards; l != NULL; l = l->next) {
			array[i++] = e_util_utf8_make_valid (l->data);
		}

		e_gdbus_book_emit_get_contact_list_done (book->priv->gdbus_object, opid, NULL, (const gchar * const *) array);

		g_strfreev (array);
	}
}

/**
 * e_data_book_respond_get_contact_list_uids:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_respond_get_contact_list_uids (EDataBook *book,
                                           guint32 opid,
                                           GError *error,
                                           const GSList *uids)
{
	if (error) {
		/* Translators: This is prefix to a detailed error message */
		g_prefix_error (&error, "%s", _("Cannot get contact list uids: "));
		e_gdbus_book_emit_get_contact_list_uids_done (book->priv->gdbus_object, opid, error, NULL);
		g_error_free (error);
	} else {
		gchar **array;
		const GSList *l;
		gint i = 0;

		array = g_new0 (gchar *, g_slist_length ((GSList *) uids) + 1);
		for (l = uids; l != NULL; l = l->next) {
			array[i++] = e_util_utf8_make_valid (l->data);
		}

		e_gdbus_book_emit_get_contact_list_uids_done (book->priv->gdbus_object, opid, NULL, (const gchar * const *) array);

		g_strfreev (array);
	}
}

/**
 * e_data_book_respond_create_contacts:
 *
 * FIXME: Document me!
 *
 * Since: 3.4
 **/
void
e_data_book_respond_create_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *contacts)
{
	gchar **array = NULL;
	const GSList *l;
	gint i = 0;

	op_complete (book, opid);

	array = g_new0 (gchar *, g_slist_length ((GSList *) contacts) + 1);
	for (l = contacts; l != NULL; l = l->next) {
		EContact *contact = E_CONTACT (l->data);

		array[i++] = e_util_utf8_make_valid (e_contact_get_const (contact, E_CONTACT_UID));
	}

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot add contact: "));

	e_gdbus_book_emit_add_contacts_done (book->priv->gdbus_object, opid, error, (const gchar * const *) array);

	g_strfreev (array);
	if (error) {
		g_error_free (error);
	} else {
		for (l = contacts; l != NULL; l = l->next) {
			EContact *contact = E_CONTACT (l->data);

			e_book_backend_notify_update (e_data_book_get_backend (book), contact);
		}

		e_book_backend_notify_complete (e_data_book_get_backend (book));
	}
}

/**
 * e_data_book_respond_modify_contacts:
 *
 * FIXME: Document me!
 *
 * Since: 3.4
 **/
void
e_data_book_respond_modify_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *contacts)
{
	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot modify contacts: "));

	e_gdbus_book_emit_modify_contacts_done (book->priv->gdbus_object, opid, error);

	if (error) {
		g_error_free (error);
	} else {
		const GSList *l;

		for (l = contacts; l != NULL; l = l->next)
			e_book_backend_notify_update (e_data_book_get_backend (book), l->data);

		e_book_backend_notify_complete (e_data_book_get_backend (book));
	}
}

void
e_data_book_respond_remove_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *ids)
{
	op_complete (book, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove contacts: "));

	e_gdbus_book_emit_remove_contacts_done (book->priv->gdbus_object, opid, error);

	if (error) {
		g_error_free (error);
	} else {
		const GSList *ii;

		for (ii = ids; ii; ii = ii->next)
			e_book_backend_notify_remove (e_data_book_get_backend (book), ii->data);

		e_book_backend_notify_complete (e_data_book_get_backend (book));
	}

}

/**
 * e_data_book_report_error:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_report_error (EDataBook *book,
                          const gchar *message)
{
	g_return_if_fail (book != NULL);
	g_return_if_fail (message != NULL);

	e_gdbus_book_emit_backend_error (book->priv->gdbus_object, message);
}

/**
 * e_data_book_report_readonly:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_report_readonly (EDataBook *book,
                             gboolean readonly)
{
	g_return_if_fail (book != NULL);

	e_gdbus_book_emit_readonly (book->priv->gdbus_object, readonly);
}

/**
 * e_data_book_report_online:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_report_online (EDataBook *book,
                           gboolean is_online)
{
	g_return_if_fail (book != NULL);

	e_gdbus_book_emit_online (book->priv->gdbus_object, is_online);
}

/**
 * e_data_book_report_opened:
 *
 * Reports to associated client that opening phase of the book is finished.
 * error being NULL means successfully, otherwise reports an error which
 * happened during opening phase. By opening phase is meant a process
 * including successfull authentication to the server/storage.
 *
 * Since: 3.2
 **/
void
e_data_book_report_opened (EDataBook *book,
                           const GError *error)
{
	gchar **strv_error;

	strv_error = e_gdbus_templates_encode_error (error);

	e_gdbus_book_emit_opened (book->priv->gdbus_object, (const gchar * const *) strv_error);

	g_strfreev (strv_error);
}

/**
 * e_data_book_report_backend_property_changed:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_book_report_backend_property_changed (EDataBook *book,
                                             const gchar *prop_name,
                                             const gchar *prop_value)
{
	gchar **strv;

	/* Notifies client about certain property value change */

	g_return_if_fail (book != NULL);
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name != '\0');
	g_return_if_fail (prop_value != NULL);

	strv = e_gdbus_templates_encode_two_strings (prop_name, prop_value);
	g_return_if_fail (strv != NULL);

	e_gdbus_book_emit_backend_property_changed (book->priv->gdbus_object, (const gchar * const *) strv);

	g_strfreev (strv);
}

/**
 * e_data_book_register_gdbus_object:
 *
 * Registers GDBus object of this EDataBook.
 *
 * Since: 2.32
 **/
guint
e_data_book_register_gdbus_object (EDataBook *book,
                                   GDBusConnection *connection,
                                   const gchar *object_path,
                                   GError **error)
{
	g_return_val_if_fail (book != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_book_register_object (book->priv->gdbus_object, connection, object_path, error);
}

static void
data_book_set_backend (EDataBook *book,
                       EBookBackend *backend)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));
	g_return_if_fail (book->priv->backend == NULL);

	book->priv->backend = g_object_ref (backend);
}

static void
data_book_set_property (GObject *object,
                        guint property_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			data_book_set_backend (
				E_DATA_BOOK (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_get_property (GObject *object,
                        guint property_id,
                        GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			g_value_set_object (
				value,
				e_data_book_get_backend (
				E_DATA_BOOK (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_dispose (GObject *object)
{
	EDataBookPrivate *priv;

	priv = E_DATA_BOOK_GET_PRIVATE (object);

	if (priv->backend) {
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	/* Chain up to parent's dispose() metnod. */
	G_OBJECT_CLASS (e_data_book_parent_class)->dispose (object);
}

static void
data_book_finalize (GObject *object)
{
	EDataBookPrivate *priv;

	priv = E_DATA_BOOK_GET_PRIVATE (object);

	if (priv->pending_ops) {
		g_hash_table_destroy (priv->pending_ops);
		priv->pending_ops = NULL;
	}

	g_static_rec_mutex_free (&priv->pending_ops_lock);

	if (priv->gdbus_object) {
		g_object_unref (priv->gdbus_object);
		priv->gdbus_object = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_parent_class)->finalize (object);
}

static void
e_data_book_class_init (EDataBookClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EDataBookPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = data_book_set_property;
	object_class->get_property = data_book_get_property;
	object_class->dispose = data_book_dispose;
	object_class->finalize = data_book_finalize;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			"Backend",
			"The backend driving this connection",
			E_TYPE_BOOK_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	if (!ops_pool)
		ops_pool = e_operation_pool_new (10, operation_thread, NULL);
}

static void
e_data_book_init (EDataBook *ebook)
{
	EGdbusBook *gdbus_object;

	ebook->priv = E_DATA_BOOK_GET_PRIVATE (ebook);

	ebook->priv->gdbus_object = e_gdbus_book_stub_new ();
	ebook->priv->pending_ops = g_hash_table_new_full (
		g_direct_hash, g_direct_equal, NULL, g_object_unref);
	g_static_rec_mutex_init (&ebook->priv->pending_ops_lock);

	gdbus_object = ebook->priv->gdbus_object;
	g_signal_connect (
		gdbus_object, "handle-open",
		G_CALLBACK (impl_Book_open), ebook);
	g_signal_connect (
		gdbus_object, "handle-remove",
		G_CALLBACK (impl_Book_remove), ebook);
	g_signal_connect (
		gdbus_object, "handle-refresh",
		G_CALLBACK (impl_Book_refresh), ebook);
	g_signal_connect (
		gdbus_object, "handle-get-contact",
		G_CALLBACK (impl_Book_get_contact), ebook);
	g_signal_connect (
		gdbus_object, "handle-get-contact-list",
		G_CALLBACK (impl_Book_get_contact_list), ebook);
	g_signal_connect (
		gdbus_object, "handle-get-contact-list-uids",
		G_CALLBACK (impl_Book_get_contact_list_uids), ebook);
	g_signal_connect (
		gdbus_object, "handle-add-contacts",
		G_CALLBACK (impl_Book_add_contacts), ebook);
	g_signal_connect (
		gdbus_object, "handle-remove-contacts",
		G_CALLBACK (impl_Book_remove_contacts), ebook);
	g_signal_connect (
		gdbus_object, "handle-modify-contacts",
		G_CALLBACK (impl_Book_modify_contacts), ebook);
	g_signal_connect (
		gdbus_object, "handle-get-backend-property",
		G_CALLBACK (impl_Book_get_backend_property), ebook);
	g_signal_connect (
		gdbus_object, "handle-set-backend-property",
		G_CALLBACK (impl_Book_set_backend_property), ebook);
	g_signal_connect (
		gdbus_object, "handle-get-view",
		G_CALLBACK (impl_Book_get_view), ebook);
	g_signal_connect (
		gdbus_object, "handle-cancel-operation",
		G_CALLBACK (impl_Book_cancel_operation), ebook);
	g_signal_connect (
		gdbus_object, "handle-cancel-all",
		G_CALLBACK (impl_Book_cancel_all), ebook);
	g_signal_connect (
		gdbus_object, "handle-close",
		G_CALLBACK (impl_Book_close), ebook);
}

EDataBook *
e_data_book_new (EBookBackend *backend)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);

	return g_object_new (E_TYPE_DATA_BOOK, "backend", backend, NULL);
}

EBookBackend *
e_data_book_get_backend (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->backend;
}

