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

/* Private D-Bus classes. */
#include <e-dbus-address-book.h>

#include <libebook-contacts/libebook-contacts.h>

#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-data-book-view.h"
#include "e-book-backend.h"
#include "e-book-backend-sexp.h"
#include "e-book-backend-factory.h"
#include "e-dbus-localed.h"

#define E_DATA_BOOK_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK, EDataBookPrivate))

struct _EDataBookPrivate {
	GDBusConnection *connection;
	EDBusAddressBook *dbus_interface;
	EModule *direct_module;
	EDataBookDirect *direct_book;

	EBookBackend *backend;
	gchar *object_path;

	GRecMutex pending_ops_lock;
	GHashTable *pending_ops; /* opid -> OperationData */
	GHashTable *direct_ops; /* opid to DirectOperationData for still running operations */

	/* Operations are queued while an
	 * open operation is in progress. */
	GMutex open_lock;
	guint32 open_opid;
	GQueue open_queue;

	guint localed_watch_id;
	EDBusLocale1 *localed_proxy;
	GCancellable *localed_cancel;
};

enum {
	PROP_0,
	PROP_BACKEND,
	PROP_CONNECTION,
	PROP_OBJECT_PATH
};

static EOperationPool *ops_pool = NULL;

typedef enum {
	OP_OPEN,
	OP_REFRESH,
	OP_GET_CONTACT,
	OP_GET_CONTACTS,
	OP_GET_CONTACTS_UIDS,
	OP_ADD_CONTACTS,
	OP_REMOVE_CONTACTS,
	OP_MODIFY_CONTACTS,
	OP_GET_BACKEND_PROPERTY,
	OP_GET_VIEW,
	OP_CLOSE
} OperationID;

typedef struct {
	volatile gint ref_count;

	OperationID op;
	guint32 id; /* operation id */
	EDataBook *book; /* book */
	GCancellable *cancellable;
	GDBusMethodInvocation *invocation;
	guint watcher_id;

	union {
		/* OP_GET_CONTACT */
		gchar *uid;
		/* OP_REMOVE_CONTACTS */
		GSList *ids;
		/* OP_ADD_CONTACTS */
		/* OP_MODIFY_CONTACTS */
		GSList *vcards;
		/* OP_GET_VIEW */
		/* OP_GET_CONTACTS */
		/* OP_GET_CONTACTS_UIDS */
		gchar *query;
		/* OP_GET_BACKEND_PROPERTY */
		const gchar *prop_name;

		/* OP_REFRESH */
		/* OP_CLOSE */
	} d;
} OperationData;

typedef struct {
	GAsyncReadyCallback callback;
	gpointer            user_data;
	GCancellable       *cancellable;
	GSimpleAsyncResult *result;

	gboolean            is_sync_call;
	gboolean            sync_call_complete;
	GMutex              sync_result_mutex;
	GCond               sync_result_condition;
} DirectOperationData;

/* EModule's can never be free'd, however the use count can change
 * Here we ensure that there is only one ever created by way of
 * static variables and locks
 */
static GHashTable *modules_table = NULL;
G_LOCK_DEFINE (modules_table);

/* Forward Declarations */
static void                 e_data_book_initable_init  (GInitableIface *interface);
static DirectOperationData *direct_operation_data_push (EDataBook          *book,
							OperationID         opid,
							GAsyncReadyCallback callback,
							gpointer            user_data,
							GCancellable       *cancellable,
							gpointer            source_tag,
							gboolean            sync_call);
static void                 direct_operation_data_free (DirectOperationData *data);
static void                 direct_operation_complete  (DirectOperationData *data);
static void                 direct_operation_wait      (DirectOperationData *data);
static void                 e_data_book_respond_close  (EDataBook *book,
							guint opid,
							GError *error);

G_DEFINE_TYPE_WITH_CODE (
	EDataBook,
	e_data_book,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_book_initable_init))

static EModule *
load_module (const gchar *module_path)
{
	EModule *module = NULL;

	G_LOCK (modules_table);

	if (!modules_table)
		modules_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	module = g_hash_table_lookup (modules_table, module_path);

	if (!module) {
		module = e_module_new (module_path);
		if (!module)
			g_warning ("Failed to open EModule at path: %s", module_path);
		else
			g_hash_table_insert (modules_table, g_strdup (module_path), module);
	}

	G_UNLOCK (modules_table);

	return module;
}

static DirectOperationData *
direct_operation_data_push (EDataBook *book,
                            OperationID opid,
                            GAsyncReadyCallback callback,
                            gpointer user_data,
                            GCancellable *cancellable,
                            gpointer source_tag,
                            gboolean sync_call)
{
	DirectOperationData *data;

	data = g_slice_new (DirectOperationData);
	data->callback = callback;
	data->user_data = user_data;
	data->cancellable = g_object_ref (cancellable);
	data->result = g_simple_async_result_new (
		G_OBJECT (book),
		data->callback,
		data->user_data,
		source_tag);
	data->is_sync_call = sync_call;
	data->sync_call_complete = FALSE;

	if (data->is_sync_call) {
		g_mutex_init (&data->sync_result_mutex);
		g_cond_init (&data->sync_result_condition);
	}

	g_hash_table_insert (book->priv->direct_ops, GUINT_TO_POINTER (opid), data);

	return data;
}

static void
direct_operation_data_free (DirectOperationData *data)
{
	if (data) {
		if (data->is_sync_call) {
			g_mutex_clear (&data->sync_result_mutex);
			g_cond_clear (&data->sync_result_condition);
		}

		g_object_unref (data->result);
		g_object_unref (data->cancellable);
		g_slice_free (DirectOperationData, data);
	}
}

static void
direct_operation_complete (DirectOperationData *data)
{
	/* If it was a sync call, we have the calling thread
	 * waiting on the sync call condition, it's up to
	 * the sync call to free the data with direct_operation_data_free().
	 *
	 * Otherwise for async calls we need to complete
	 * in the calling thread.
	 */
	if (data->is_sync_call) {
		g_mutex_lock (&data->sync_result_mutex);
		data->sync_call_complete = TRUE;
		g_cond_signal (&data->sync_result_condition);
		g_mutex_unlock (&data->sync_result_mutex);
	} else {
		g_simple_async_result_complete_in_idle (data->result);
		direct_operation_data_free (data);
	}
}

static void
direct_operation_wait (DirectOperationData *data)
{
	g_mutex_lock (&data->sync_result_mutex);
	while (data->sync_call_complete == FALSE)
		g_cond_wait (&data->sync_result_condition, &data->sync_result_mutex);
	g_mutex_unlock (&data->sync_result_mutex);
}

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
op_sender_vanished_cb (GDBusConnection *connection,
                       const gchar *sender,
                       GCancellable *cancellable)
{
	g_cancellable_cancel (cancellable);
}

static OperationData *
op_ref (OperationData *data)
{
	g_return_val_if_fail (data != NULL, data);
	g_return_val_if_fail (data->ref_count > 0, data);

	g_atomic_int_inc (&data->ref_count);

	return data;
}

static OperationData *
op_new (OperationID op,
        EDataBook *book,
        GDBusMethodInvocation *invocation)
{
	OperationData *data;

	data = g_slice_new0 (OperationData);
	data->ref_count = 1;
	data->op = op;
	data->id = e_operation_pool_reserve_opid (ops_pool);
	data->book = g_object_ref (book);
	data->cancellable = g_cancellable_new ();

	/* This is optional so we can fake client requests. */
	if (invocation != NULL) {
		GDBusConnection *connection;
		const gchar *sender;

		data->invocation = g_object_ref (invocation);

		connection = e_data_book_get_connection (book);
		sender = g_dbus_method_invocation_get_sender (invocation);

		data->watcher_id = g_bus_watch_name_on_connection (
			connection, sender,
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			(GBusNameAppearedCallback) NULL,
			(GBusNameVanishedCallback) op_sender_vanished_cb,
			g_object_ref (data->cancellable),
			(GDestroyNotify) g_object_unref);
	}

	g_rec_mutex_lock (&book->priv->pending_ops_lock);
	g_hash_table_insert (
		book->priv->pending_ops,
		GUINT_TO_POINTER (data->id),
		op_ref (data));
	g_rec_mutex_unlock (&book->priv->pending_ops_lock);

	return data;
}

static void
op_unref (OperationData *data)
{
	g_return_if_fail (data != NULL);
	g_return_if_fail (data->ref_count > 0);

	if (g_atomic_int_dec_and_test (&data->ref_count)) {

		switch (data->op) {
			case OP_GET_CONTACT:
				g_free (data->d.uid);
				break;
			case OP_REMOVE_CONTACTS:
				g_slist_free_full (
					data->d.ids,
					(GDestroyNotify) g_free);
				break;
			case OP_ADD_CONTACTS:
			case OP_MODIFY_CONTACTS:
				g_slist_free_full (
					data->d.vcards,
					(GDestroyNotify) g_free);
				break;
			case OP_GET_VIEW:
			case OP_GET_CONTACTS:
			case OP_GET_CONTACTS_UIDS:
				g_free (data->d.query);
				break;
			default:
				break;
		}

		g_object_unref (data->book);
		g_object_unref (data->cancellable);

		if (data->invocation != NULL)
			g_object_unref (data->invocation);

		if (data->watcher_id > 0)
			g_bus_unwatch_name (data->watcher_id);

		g_slice_free (OperationData, data);
	}
}

static void
op_dispatch (EDataBook *book,
             OperationData *data)
{
	g_mutex_lock (&book->priv->open_lock);

	/* If an open operation is currently in progress, queue this
	 * operation to be dispatched when the open operation finishes. */
	if (book->priv->open_opid > 0) {
		g_queue_push_tail (&book->priv->open_queue, data);
	} else {
		if (data->op == OP_OPEN)
			book->priv->open_opid = data->id;
		e_operation_pool_push (ops_pool, data);
	}

	g_mutex_unlock (&book->priv->open_lock);
}

static OperationData *
op_claim (EDataBook *book,
          guint32 opid,
          DirectOperationData **direct)
{
	OperationData *data;
	DirectOperationData *direct_data;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	e_operation_pool_release_opid (ops_pool, opid);

	g_rec_mutex_lock (&book->priv->pending_ops_lock);
	data = g_hash_table_lookup (
		book->priv->pending_ops,
		GUINT_TO_POINTER (opid));
	if (data != NULL) {
		/* Steal the hash table's reference. */
		g_hash_table_steal (
			book->priv->pending_ops,
			GUINT_TO_POINTER (opid));
	}

	direct_data = g_hash_table_lookup (
		book->priv->direct_ops,
		GUINT_TO_POINTER (opid));
	if (direct_data != NULL) {
		g_hash_table_steal (
			book->priv->direct_ops,
			GUINT_TO_POINTER (opid));
	}
	g_rec_mutex_unlock (&book->priv->pending_ops_lock);

	g_warn_if_fail (direct_data == NULL || direct != NULL);
	if (direct)
		*direct = direct_data;

	return data;
}

static DirectOperationData *
op_complete (EDataBook *book,
             guint32 opid)
{
	DirectOperationData *direct_data;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	e_operation_pool_release_opid (ops_pool, opid);

	g_rec_mutex_lock (&book->priv->pending_ops_lock);
	g_hash_table_remove (
		book->priv->pending_ops,
		GUINT_TO_POINTER (opid));

	direct_data = g_hash_table_lookup (
		book->priv->direct_ops,
		GUINT_TO_POINTER (opid));
	if (direct_data != NULL) {
		g_hash_table_steal (
			book->priv->direct_ops,
			GUINT_TO_POINTER (opid));
	}
	g_rec_mutex_unlock (&book->priv->pending_ops_lock);

	return direct_data;
}

static void
data_book_convert_to_client_error (GError *error)
{
	g_return_if_fail (error != NULL);

	if (error->domain != E_DATA_BOOK_ERROR)
		return;

	switch (error->code) {
		case E_DATA_BOOK_STATUS_REPOSITORY_OFFLINE:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_REPOSITORY_OFFLINE;
			break;

		case E_DATA_BOOK_STATUS_PERMISSION_DENIED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_PERMISSION_DENIED;
			break;

		case E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND:
			error->domain = E_BOOK_CLIENT_ERROR;
			error->code = E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND;
			break;

		case E_DATA_BOOK_STATUS_CONTACTID_ALREADY_EXISTS:
			error->domain = E_BOOK_CLIENT_ERROR;
			error->code = E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS;
			break;

		case E_DATA_BOOK_STATUS_AUTHENTICATION_FAILED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_AUTHENTICATION_FAILED;
			break;

		case E_DATA_BOOK_STATUS_UNSUPPORTED_AUTHENTICATION_METHOD:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD;
			break;

		case E_DATA_BOOK_STATUS_TLS_NOT_AVAILABLE:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_TLS_NOT_AVAILABLE;
			break;

		case E_DATA_BOOK_STATUS_NO_SUCH_BOOK:
			error->domain = E_BOOK_CLIENT_ERROR;
			error->code = E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK;
			break;

		case E_DATA_BOOK_STATUS_BOOK_REMOVED:
			error->domain = E_BOOK_CLIENT_ERROR;
			error->code = E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE;
			break;

		case E_DATA_BOOK_STATUS_OFFLINE_UNAVAILABLE:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_OFFLINE_UNAVAILABLE;
			break;

		case E_DATA_BOOK_STATUS_SEARCH_SIZE_LIMIT_EXCEEDED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED;
			break;

		case E_DATA_BOOK_STATUS_SEARCH_TIME_LIMIT_EXCEEDED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED;
			break;

		case E_DATA_BOOK_STATUS_INVALID_QUERY:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_INVALID_QUERY;
			break;

		case E_DATA_BOOK_STATUS_QUERY_REFUSED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_QUERY_REFUSED;
			break;

		case E_DATA_BOOK_STATUS_COULD_NOT_CANCEL:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_COULD_NOT_CANCEL;
			break;

		case E_DATA_BOOK_STATUS_NO_SPACE:
			error->domain = E_BOOK_CLIENT_ERROR;
			error->code = E_BOOK_CLIENT_ERROR_NO_SPACE;
			break;

		case E_DATA_BOOK_STATUS_INVALID_ARG:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_INVALID_ARG;
			break;

		case E_DATA_BOOK_STATUS_NOT_SUPPORTED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_NOT_SUPPORTED;
			break;

		case E_DATA_BOOK_STATUS_NOT_OPENED:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_NOT_OPENED;
			break;

		case E_DATA_BOOK_STATUS_OUT_OF_SYNC:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_OUT_OF_SYNC;
			break;

		case E_DATA_BOOK_STATUS_UNSUPPORTED_FIELD:
		case E_DATA_BOOK_STATUS_OTHER_ERROR:
		case E_DATA_BOOK_STATUS_INVALID_SERVER_VERSION:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_OTHER_ERROR;
			break;

		default:
			g_warn_if_reached ();
	}
}

static void
operation_thread (gpointer data,
                  gpointer user_data)
{
	OperationData *op = data;
	EBookBackend *backend;
	GHashTableIter iter;
	gpointer value;

	backend = e_data_book_get_backend (op->book);

	switch (op->op) {
	case OP_OPEN:
		e_book_backend_open (
			backend, op->book, op->id,
			op->cancellable, FALSE);
		break;

	case OP_ADD_CONTACTS:
		e_book_backend_create_contacts (
			backend, op->book, op->id,
			op->cancellable, op->d.vcards);
		break;

	case OP_GET_CONTACT:
		e_book_backend_get_contact (
			backend, op->book, op->id,
			op->cancellable, op->d.uid);
		break;

	case OP_GET_CONTACTS:
		e_book_backend_get_contact_list (
			backend, op->book, op->id,
			op->cancellable, op->d.query);
		break;

	case OP_GET_CONTACTS_UIDS:
		e_book_backend_get_contact_list_uids (
			backend, op->book, op->id,
			op->cancellable, op->d.query);
		break;

	case OP_MODIFY_CONTACTS:
		e_book_backend_modify_contacts (
			backend, op->book, op->id,
			op->cancellable, op->d.vcards);
		break;

	case OP_REMOVE_CONTACTS:
		e_book_backend_remove_contacts (
			backend, op->book, op->id,
			op->cancellable, op->d.ids);
		break;

	case OP_REFRESH:
		e_book_backend_refresh (
			backend, op->book, op->id, op->cancellable);
		break;

	case OP_GET_BACKEND_PROPERTY:
		e_book_backend_get_backend_property (
			backend, op->book, op->id,
			op->cancellable, op->d.prop_name);
		break;

	case OP_GET_VIEW:
		if (op->d.query) {
			EDataBookView *view;
			EBookBackendSExp *card_sexp;
			GDBusConnection *connection;
			gchar *object_path;
			GError *error = NULL;

			card_sexp = e_book_backend_sexp_new (op->d.query);
			if (!card_sexp) {
				g_dbus_method_invocation_return_error_literal (
					op->invocation,
					E_CLIENT_ERROR,
					E_CLIENT_ERROR_INVALID_QUERY,
					_("Invalid query"));

				op_complete (op->book, op->id);
				break;
			}

			object_path = construct_bookview_path ();
			connection = e_data_book_get_connection (op->book);

			view = e_data_book_view_new (
				op->book, card_sexp,
				connection, object_path, &error);

			g_object_unref (card_sexp);

			/* Sanity check. */
			g_return_if_fail (
				((view != NULL) && (error == NULL)) ||
				((view == NULL) && (error != NULL)));

			if (error != NULL) {
				/* Translators: This is prefix to a detailed error message */
				g_prefix_error (&error, "%s", _("Invalid query: "));
				data_book_convert_to_client_error (error);
				g_dbus_method_invocation_take_error (
					op->invocation, error);

				op_complete (op->book, op->id);
				g_free (object_path);
				break;
			}

			e_book_backend_add_view (backend, view);

			e_dbus_address_book_complete_get_view (
				op->book->priv->dbus_interface,
				op->invocation,
				object_path);

			op_complete (op->book, op->id);
			g_free (object_path);
		}
		break;

	case OP_CLOSE:
		/* close just cancels all pending ops and frees data book */
		e_book_backend_remove_client (backend, op->book);

		g_rec_mutex_lock (&op->book->priv->pending_ops_lock);

		g_hash_table_iter_init (&iter, op->book->priv->pending_ops);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			OperationData *cancel_op = value;
			g_cancellable_cancel (cancel_op->cancellable);
		}

		g_rec_mutex_unlock (&op->book->priv->pending_ops_lock);

		if (op->book->priv->dbus_interface)
			e_dbus_address_book_complete_close (
				op->book->priv->dbus_interface,
				op->invocation);

		/* Let direct calls return, notify the direct callers that it's closed */
		e_data_book_respond_close (op->book, op->id, NULL);
		break;
	}

	op_unref (op);
}

static OperationData *
op_direct_new (OperationID op,
               EDataBook *book,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data,
               gpointer source_tag,
               gboolean sync_call,
               DirectOperationData **ret_data)
{
	OperationData *data;
	DirectOperationData *direct_data;

	data = g_slice_new0 (OperationData);
	data->ref_count = 1;
	data->op = op;
	data->book = g_object_ref (book);
	data->id = e_operation_pool_reserve_opid (ops_pool);

	if (cancellable)
		data->cancellable = g_object_ref (cancellable);
	else
		data->cancellable = g_cancellable_new ();

	g_rec_mutex_lock (&book->priv->pending_ops_lock);
	g_hash_table_insert (
		book->priv->pending_ops,
		GUINT_TO_POINTER (data->id),
		op_ref (data));
	direct_data = direct_operation_data_push (
		book, data->id, callback, user_data,
		data->cancellable, source_tag, sync_call);
	g_rec_mutex_unlock (&book->priv->pending_ops_lock);

	if (ret_data)
		*ret_data = direct_data;

	return data;
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
		{ E_DATA_BOOK_STATUS_NOT_OPENED,			N_("Backend is not opened yet") },
		{ E_DATA_BOOK_STATUS_OUT_OF_SYNC,                       N_("Object is out of sync") }
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
		{ E_DATA_BOOK_STATUS_NOT_OPENED,			ERR_PREFIX "NotOpened" },
		{ E_DATA_BOOK_STATUS_OUT_OF_SYNC,                       ERR_PREFIX "OutOfSync" }
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
data_book_handle_open_cb (EDBusAddressBook *interface,
                          GDBusMethodInvocation *invocation,
                          EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_OPEN, book, invocation);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_refresh_cb (EDBusAddressBook *interface,
                             GDBusMethodInvocation *invocation,
                             EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_REFRESH, book, invocation);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_get_contact_cb (EDBusAddressBook *interface,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *in_uid,
                                 EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_GET_CONTACT, book, invocation);
	op->d.uid = g_strdup (in_uid);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_get_contact_list_cb (EDBusAddressBook *interface,
                                      GDBusMethodInvocation *invocation,
                                      const gchar *in_query,
                                      EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_GET_CONTACTS, book, invocation);
	op->d.query = g_strdup (in_query);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_get_contact_list_uids_cb (EDBusAddressBook *interface,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *in_query,
                                           EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_GET_CONTACTS_UIDS, book, invocation);
	op->d.query = g_strdup (in_query);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_create_contacts_cb (EDBusAddressBook *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar * const *in_vcards,
                                     EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_ADD_CONTACTS, book, invocation);
	op->d.vcards = e_util_strv_to_slist (in_vcards);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_modify_contacts_cb (EDBusAddressBook *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar * const *in_vcards,
                                     EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_MODIFY_CONTACTS, book, invocation);
	op->d.vcards = e_util_strv_to_slist (in_vcards);

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_remove_contacts_cb (EDBusAddressBook *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar * const *in_uids,
                                     EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_REMOVE_CONTACTS, book, invocation);

	/* Allow an empty array to be removed */
	for (; in_uids && *in_uids; in_uids++) {
		op->d.ids = g_slist_prepend (op->d.ids, g_strdup (*in_uids));
	}

	op_dispatch (book, op);

	return TRUE;
}

static gboolean
data_book_handle_get_view_cb (EDBusAddressBook *interface,
                              GDBusMethodInvocation *invocation,
                              const gchar *in_query,
                              EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_GET_VIEW, book, invocation);
	op->d.query = g_strdup (in_query);

	/* This operation is never queued. */
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
data_book_handle_close_cb (EDBusAddressBook *interface,
                           GDBusMethodInvocation *invocation,
                           EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_CLOSE, book, invocation);
	/* unref here makes sure the book is freed in a separate thread */
	g_object_unref (book);

	/* This operation is never queued. */
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

void
e_data_book_respond_open (EDataBook *book,
                          guint opid,
                          GError *error)
{
	DirectOperationData *direct = NULL;
	OperationData *data;
	GError *copy = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, &direct);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot open book: "));

	/* This function is deprecated, but it's the only way to
	 * set EBookBackend's internal 'opened' flag.  We should
	 * be the only ones calling this. */
	if (error != NULL) {
		data_book_convert_to_client_error (error);
		copy = g_error_copy (error);
	}
	e_book_backend_notify_opened (book->priv->backend, copy);

	if (direct) {
		gboolean result = FALSE;

		if (error) {
			g_simple_async_result_set_error (
				direct->result,
				error->domain,
				error->code,
				"%s", error->message);
			g_error_free (error);
		} else {
			g_simple_async_result_set_check_cancellable (
				direct->result,
				direct->cancellable);

			if (!g_cancellable_is_cancelled (direct->cancellable))
				result = TRUE;
		}

		g_simple_async_result_set_op_res_gboolean (direct->result, result);

		/* Deliver the result to the caller */
		direct_operation_complete (direct);
	} else if (error == NULL) {
		e_dbus_address_book_complete_open (
			book->priv->dbus_interface,
			data->invocation);
	} else {
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);

	/* Dispatch any pending operations. */
	g_mutex_lock (&book->priv->open_lock);

	if (opid == book->priv->open_opid) {
		OperationData *op;

		book->priv->open_opid = 0;

		while (!g_queue_is_empty (&book->priv->open_queue)) {
			op = g_queue_pop_head (&book->priv->open_queue);
			e_operation_pool_push (ops_pool, op);
		}
	}

	g_mutex_unlock (&book->priv->open_lock);
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
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, NULL);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot refresh address book: "));

	if (error == NULL) {
		e_dbus_address_book_complete_refresh (
			book->priv->dbus_interface,
			data->invocation);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
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
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, NULL);
	g_return_if_fail (data != NULL);

	if (error == NULL) {
		e_data_book_report_backend_property_changed (
			book, data->d.prop_name, prop_value);
	} else {
		/* This should never happen, since all backend property
		 * requests now originate from our constructed() method. */
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}

	op_unref (data);
}

/**
 * e_data_book_respond_set_backend_property:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: This function no longer does anything.
 **/
void
e_data_book_respond_set_backend_property (EDataBook *book,
                                          guint32 opid,
                                          GError *error)
{
	/* Do nothing. */
}

void
e_data_book_respond_get_contact (EDataBook *book,
                                 guint32 opid,
                                 GError *error,
                                 const gchar *vcard)
{
	DirectOperationData *direct = NULL;
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, &direct);
	g_return_if_fail (data != NULL);

	if (error) {
		/* Translators: This is prefix to a detailed error message */
		g_prefix_error (&error, "%s", _("Cannot get contact: "));
		data_book_convert_to_client_error (error);
	}

	if (direct) {

		if (error) {
			g_simple_async_result_set_error (
				direct->result,
				error->domain,
				error->code,
				"%s", error->message);
			g_error_free (error);
		} else {
			g_simple_async_result_set_check_cancellable (
				direct->result,
				direct->cancellable);

			if (!g_cancellable_is_cancelled (direct->cancellable)) {
				EContact *contact;

				contact = e_contact_new_from_vcard (vcard);

				/* Give it an EContact for the return value */
				g_simple_async_result_set_op_res_gpointer (direct->result, contact, g_object_unref);
			}
		}

		/* Deliver the result to the caller */
		direct_operation_complete (direct);

	} else if (error == NULL) {
		gchar *utf8_vcard;

		utf8_vcard = e_util_utf8_make_valid (vcard);

		e_dbus_address_book_complete_get_contact (
			book->priv->dbus_interface,
			data->invocation,
			utf8_vcard);

		g_free (utf8_vcard);
	} else {
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

void
e_data_book_respond_get_contact_list (EDataBook *book,
                                      guint32 opid,
                                      GError *error,
                                      const GSList *cards)
{
	DirectOperationData *direct = NULL;
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, &direct);
	g_return_if_fail (data != NULL);

	if (error) {
		/* Translators: This is prefix to a detailed error message */
		g_prefix_error (&error, "%s", _("Cannot get contact list: "));
		data_book_convert_to_client_error (error);
	}

	if (direct) {

		if (error) {
			g_simple_async_result_set_error (
				direct->result,
				error->domain,
				error->code,
				"%s", error->message);
			g_error_free (error);
		} else {
			g_simple_async_result_set_check_cancellable (
				direct->result,
				direct->cancellable);

			if (!g_cancellable_is_cancelled (direct->cancellable)) {
				EContact *contact;
				const GSList *l;
				GSList *contacts = NULL;

				for (l = cards; l; l = l->next) {
					const gchar *vcard = l->data;

					contact = e_contact_new_from_vcard (vcard);
					contacts = g_slist_prepend (contacts, contact);
				}

				contacts = g_slist_reverse (contacts);

				/* Give it an EContact for the return value */
				g_simple_async_result_set_op_res_gpointer (
					direct->result, contacts,
					(GDestroyNotify) e_util_free_object_slist);
			}
		}

		/* Deliver the result to the caller */
		direct_operation_complete (direct);

	} else if (error == NULL) {
		gchar **strv;
		guint length;
		gint ii = 0;

		length = g_slist_length ((GSList *) cards);
		strv = g_new0 (gchar *, length + 1);

		while (cards != NULL) {
			strv[ii++] = e_util_utf8_make_valid (cards->data);
			cards = g_slist_next ((GSList *) cards);
		}

		e_dbus_address_book_complete_get_contact_list (
			book->priv->dbus_interface,
			data->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
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
	DirectOperationData *direct = NULL;
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, &direct);
	g_return_if_fail (data != NULL);

	if (error) {
		/* Translators: This is prefix to a detailed error message */
		g_prefix_error (&error, "%s", _("Cannot get contact list uids: "));
		data_book_convert_to_client_error (error);
	}

	if (direct) {

		if (error) {
			g_simple_async_result_set_error (
				direct->result,
				error->domain,
				error->code,
				"%s", error->message);
			g_error_free (error);
		} else {
			g_simple_async_result_set_check_cancellable (
				direct->result,
				direct->cancellable);

			if (!g_cancellable_is_cancelled (direct->cancellable)) {
				GSList *ret_uids = NULL;

				ret_uids = e_util_copy_string_slist (NULL, uids);

				g_simple_async_result_set_op_res_gpointer (
					direct->result, ret_uids,
					(GDestroyNotify) e_util_free_string_slist);
			}
		}

		/* Deliver the result to the caller */
		direct_operation_complete (direct);

	} else if (error == NULL) {
		gchar **strv;
		guint length;
		gint ii = 0;

		length = g_slist_length ((GSList *) uids);
		strv = g_new0 (gchar *, length + 1);

		while (uids != NULL) {
			strv[ii++] = e_util_utf8_make_valid (uids->data);
			uids = g_slist_next ((GSList *) uids);
		}

		e_dbus_address_book_complete_get_contact_list_uids (
			book->priv->dbus_interface,
			data->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
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
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, NULL);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot add contact: "));

	if (error == NULL) {
		EBookBackend *backend;
		gchar **strv;
		guint length;
		gint ii = 0;

		backend = e_data_book_get_backend (book);

		length = g_slist_length ((GSList *) contacts);
		strv = g_new0 (gchar *, length + 1);

		while (contacts != NULL) {
			EContact *contact = E_CONTACT (contacts->data);
			const gchar *uid;

			uid = e_contact_get_const (contact, E_CONTACT_UID);
			strv[ii++] = e_util_utf8_make_valid (uid);

			e_book_backend_notify_update (backend, contact);

			contacts = g_slist_next ((GSList *) contacts);
		}

		e_dbus_address_book_complete_create_contacts (
			book->priv->dbus_interface,
			data->invocation,
			(const gchar * const *) strv);

		e_book_backend_notify_complete (backend);

		g_strfreev (strv);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
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
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, NULL);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot modify contacts: "));

	if (error == NULL) {
		EBookBackend *backend;

		backend = e_data_book_get_backend (book);

		e_dbus_address_book_complete_modify_contacts (
			book->priv->dbus_interface,
			data->invocation);

		while (contacts != NULL) {
			EContact *contact = E_CONTACT (contacts->data);
			e_book_backend_notify_update (backend, contact);
			contacts = g_slist_next ((GSList *) contacts);
		}

		e_book_backend_notify_complete (backend);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

void
e_data_book_respond_remove_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *ids)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	data = op_claim (book, opid, NULL);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove contacts: "));

	if (error == NULL) {
		EBookBackend *backend;

		backend = e_data_book_get_backend (book);

		e_dbus_address_book_complete_remove_contacts (
			book->priv->dbus_interface,
			data->invocation);

		while (ids != NULL) {
			e_book_backend_notify_remove (backend, ids->data);
			ids = g_slist_next ((GSList *) ids);
		}

		e_book_backend_notify_complete (backend);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
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
	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (message != NULL);

	e_dbus_address_book_emit_error (book->priv->dbus_interface, message);
}

/**
 * e_data_book_report_readonly:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: Use e_book_backend_set_writable() instead.
 **/
void
e_data_book_report_readonly (EDataBook *book,
                             gboolean readonly)
{
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_book_backend_set_writable (book->priv->backend, !readonly);
}

/**
 * e_data_book_report_online:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: Use e_backend_set_online() instead.
 **/
void
e_data_book_report_online (EDataBook *book,
                           gboolean is_online)
{
	g_return_if_fail (E_IS_DATA_BOOK (book));

	e_backend_set_online (E_BACKEND (book->priv->backend), is_online);
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
 *
 * Deprecated: 3.8: This function no longer does anything.
 **/
void
e_data_book_report_opened (EDataBook *book,
                           const GError *error)
{
	/* Do nothing. */
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
	EDBusAddressBook *dbus_interface;
	gchar **strv;

	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (prop_name != NULL);

	if (prop_value == NULL)
		prop_value = "";

	dbus_interface = book->priv->dbus_interface;

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_address_book_set_capabilities (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_REVISION))
		e_dbus_address_book_set_revision (dbus_interface, prop_value);

	if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_address_book_set_required_fields (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_address_book_set_supported_fields (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	/* Disregard anything else. */
}

static gchar *
data_book_interpret_locale_value (const gchar *value)
{
	gchar *interpreted_value = NULL;
	gchar **split;

	split = g_strsplit (value, "=", 2);

	if (split && split[0] && split[1])
		interpreted_value = g_strdup (split[1]);

	g_strfreev (split);

	if (!interpreted_value)
		g_warning ("Failed to interpret locale value: %s", value);

	return interpreted_value;
}

static gchar *
data_book_interpret_locale (const gchar * const * locale)
{
	gint i;
	gchar *interpreted_locale = NULL;

	/* Prioritize LC_COLLATE and then LANG values
	 * in the 'locale' specified by localed.
	 *
	 * If localed explicitly specifies no locale, then
	 * default to checking system locale.
	 */
	if (locale) {

		for (i = 0; locale[i] != NULL && interpreted_locale == NULL; i++) {

			if (strncmp (locale[i], "LC_COLLATE", 10))
				interpreted_locale = data_book_interpret_locale_value (locale[i]);
		}

		for (i = 0; locale[i] != NULL && interpreted_locale == NULL; i++) {

			if (strncmp (locale[i], "LANG", 4))
				interpreted_locale = data_book_interpret_locale_value (locale[i]);
		}
	}

	if (!interpreted_locale) {
		const gchar *system_locale = setlocale (LC_COLLATE, NULL);

		interpreted_locale = g_strdup (system_locale);
	}

	return interpreted_locale;
}

static void
data_book_locale_changed (GObject *object,
			  GParamSpec *pspec,
			  gpointer user_data)
{
	EDBusLocale1 *locale_proxy = E_DBUS_LOCALE1 (object);
	EDataBook *book = (EDataBook *)user_data;
	EBookBackend *backend;

	backend = book->priv->backend;

	if (backend) {
		const gchar * const *locale;
		gchar *interpreted_locale;

		locale = e_dbus_locale1_get_locale (locale_proxy);
		interpreted_locale = data_book_interpret_locale (locale);

		e_book_backend_set_locale (backend, interpreted_locale);

		e_dbus_address_book_set_locale (book->priv->dbus_interface, interpreted_locale);

		g_free (interpreted_locale);
	}
}

static void
data_book_localed_ready (GObject *source_object,
			 GAsyncResult *res,
			 gpointer user_data)
{
	EDataBook *book = (EDataBook *)user_data;
	GError *error = NULL;

	book->priv->localed_proxy = e_dbus_locale1_proxy_new_finish (res, &error);

	if (book->priv->localed_proxy == NULL) {
		g_warning ("Error fetching localed proxy: %s", error->message);
		g_error_free (error);
	}

	if (book->priv->localed_cancel) {
		g_object_unref (book->priv->localed_cancel);
		book->priv->localed_cancel = NULL;
	}

	if (book->priv->localed_proxy) {
		g_signal_connect (book->priv->localed_proxy, "notify::locale",
				  G_CALLBACK (data_book_locale_changed), book);

		/* Initial refresh the locale */
		data_book_locale_changed (G_OBJECT (book->priv->localed_proxy), NULL, book);
	}
}

static void
data_book_localed_appeared (GDBusConnection *connection,
			    const gchar *name,
			    const gchar *name_owner,
			    gpointer user_data)
{
	EDataBook *book = (EDataBook *)user_data;

	book->priv->localed_cancel = g_cancellable_new ();

	e_dbus_locale1_proxy_new (connection,
				  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
				  "org.freedesktop.locale1",
				  "/org/freedesktop/locale1",
				  book->priv->localed_cancel,
				  data_book_localed_ready,
				  book);
}

static void
data_book_localed_vanished (GDBusConnection *connection,
			    const gchar *name,
			    gpointer user_data)
{
	EDataBook *book = (EDataBook *)user_data;

	if (book->priv->localed_cancel) {
		g_cancellable_cancel (book->priv->localed_cancel);
		g_object_unref (book->priv->localed_cancel);
		book->priv->localed_cancel = NULL;
	}

	if (book->priv->localed_proxy) {
		g_object_unref (book->priv->localed_proxy);
		book->priv->localed_proxy = NULL;
	}
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
data_book_set_connection (EDataBook *book,
                          GDBusConnection *connection)
{
	g_return_if_fail (connection == NULL ||
			  G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (book->priv->connection == NULL);

	if (connection)
		book->priv->connection = g_object_ref (connection);
}

static void
data_book_set_object_path (EDataBook *book,
                           const gchar *object_path)
{
	g_return_if_fail (book->priv->object_path == NULL);

	book->priv->object_path = g_strdup (object_path);
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

		case PROP_CONNECTION:
			data_book_set_connection (
				E_DATA_BOOK (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			data_book_set_object_path (
				E_DATA_BOOK (object),
				g_value_get_string (value));
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

		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_data_book_get_connection (
				E_DATA_BOOK (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_data_book_get_object_path (
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

	if (priv->connection != NULL) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (priv->backend != NULL) {
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	if (priv->direct_book) {
		g_object_unref (priv->direct_book);
		priv->direct_book = NULL;
	}

	if (priv->direct_module) {
		g_type_module_unuse (G_TYPE_MODULE (priv->direct_module));
		priv->direct_module = NULL;
	}

	if (priv->localed_cancel) {
		g_cancellable_cancel (priv->localed_cancel);
		g_object_unref (priv->localed_cancel);
		priv->localed_cancel = NULL;
	}

	if (priv->localed_proxy) {
		g_object_unref (priv->localed_proxy);
		priv->localed_proxy = NULL;
	}

	/* Chain up to parent's dispose() metnod. */
	G_OBJECT_CLASS (e_data_book_parent_class)->dispose (object);
}

static void
data_book_finalize (GObject *object)
{
	EDataBookPrivate *priv;

	priv = E_DATA_BOOK_GET_PRIVATE (object);

	g_free (priv->object_path);

	if (priv->pending_ops) {
		g_hash_table_destroy (priv->pending_ops);
		priv->pending_ops = NULL;
	}

	g_rec_mutex_clear (&priv->pending_ops_lock);
	if (priv->direct_ops) {
		g_hash_table_destroy (priv->direct_ops);
		priv->direct_ops = NULL;
	}

	if (priv->dbus_interface) {
		g_object_unref (priv->dbus_interface);
		priv->dbus_interface = NULL;
	}

	g_mutex_clear (&priv->open_lock);

	/* This should be empty now, else we leak memory. */
	g_warn_if_fail (g_queue_is_empty (&priv->open_queue));

	if (priv->localed_watch_id > 0)
		g_bus_unwatch_name (priv->localed_watch_id);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_parent_class)->finalize (object);
}

static gboolean
data_book_initable_init (GInitable *initable,
                         GCancellable *cancellable,
                         GError **error)
{
	EDataBook *book;
	OperationData *op;
	const gchar *locale;
	GBusType bus_type = G_BUS_TYPE_SYSTEM;

	book = E_DATA_BOOK (initable);

	if (book->priv->connection != NULL && book->priv->object_path != NULL) {
		book->priv->dbus_interface =
			e_dbus_address_book_skeleton_new ();

		g_signal_connect (
			book->priv->dbus_interface, "handle-open",
			G_CALLBACK (data_book_handle_open_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-refresh",
			G_CALLBACK (data_book_handle_refresh_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-get-contact",
			G_CALLBACK (data_book_handle_get_contact_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-get-contact-list",
			G_CALLBACK (data_book_handle_get_contact_list_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-get-contact-list-uids",
			G_CALLBACK (data_book_handle_get_contact_list_uids_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-create-contacts",
			G_CALLBACK (data_book_handle_create_contacts_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-remove-contacts",
			G_CALLBACK (data_book_handle_remove_contacts_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-modify-contacts",
			G_CALLBACK (data_book_handle_modify_contacts_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-get-view",
			G_CALLBACK (data_book_handle_get_view_cb), book);
		g_signal_connect (
			book->priv->dbus_interface, "handle-close",
			G_CALLBACK (data_book_handle_close_cb), book);

		/* This will be NULL for a backend that does not support
		 * direct read access. */
		book->priv->direct_book =
			e_book_backend_get_direct_book (book->priv->backend);

		if (book->priv->direct_book != NULL) {
			gboolean success;

			success = e_data_book_direct_register_gdbus_object (
				book->priv->direct_book,
				book->priv->connection,
				book->priv->object_path,
				error);
			if (!success)
				return FALSE;
		}

		g_object_bind_property (
			book->priv->backend, "cache-dir",
			book->priv->dbus_interface, "cache-dir",
			G_BINDING_SYNC_CREATE);

		g_object_bind_property (
			book->priv->backend, "online",
			book->priv->dbus_interface, "online",
			G_BINDING_SYNC_CREATE);

		g_object_bind_property (
			book->priv->backend, "writable",
			book->priv->dbus_interface, "writable",
			G_BINDING_SYNC_CREATE);

		/* XXX Initialize the rest of the properties by faking client
		 *     requests.  At present it's the only way to fish values
		 *     from EBookBackend's antiquated API. */

		op = op_new (OP_GET_BACKEND_PROPERTY, book, NULL);
		op->d.prop_name = CLIENT_BACKEND_PROPERTY_CAPABILITIES;
		e_book_backend_get_backend_property (
			book->priv->backend, book, op->id,
			op->cancellable, op->d.prop_name);
		op_unref (op);

		op = op_new (OP_GET_BACKEND_PROPERTY, book, NULL);
		op->d.prop_name = CLIENT_BACKEND_PROPERTY_REVISION;
		e_book_backend_get_backend_property (
			book->priv->backend, book, op->id,
			op->cancellable, op->d.prop_name);
		op_unref (op);

		op = op_new (OP_GET_BACKEND_PROPERTY, book, NULL);
		op->d.prop_name = BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS;
		e_book_backend_get_backend_property (
			book->priv->backend, book, op->id,
			op->cancellable, op->d.prop_name);
		op_unref (op);

		op = op_new (OP_GET_BACKEND_PROPERTY, book, NULL);
		op->d.prop_name = BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS;
		e_book_backend_get_backend_property (
			book->priv->backend, book, op->id,
			op->cancellable, op->d.prop_name);
		op_unref (op);

		/* Fetch backend configured locale and set that as the initial
		 * value on the dbus object
		 */
		locale = e_book_backend_get_locale (book->priv->backend);
		e_dbus_address_book_set_locale (book->priv->dbus_interface, locale);

		/* When running tests, we pretend to be the "org.freedesktop.locale1" service
		 * on the session bus instead of the real location on the system bus.
		 */
		if (g_getenv ("EDS_TESTING") != NULL)
			bus_type = G_BUS_TYPE_SESSION;

		/* Watch system bus for locale change notifications */
		book->priv->localed_watch_id =
			g_bus_watch_name (bus_type,
					  "org.freedesktop.locale1",
					  G_BUS_NAME_WATCHER_FLAGS_NONE,
					  data_book_localed_appeared,
					  data_book_localed_vanished,
					  book,
					  NULL);

		return g_dbus_interface_skeleton_export (
			G_DBUS_INTERFACE_SKELETON (book->priv->dbus_interface),
			book->priv->connection,
			book->priv->object_path,
			error);
	}

	return TRUE;
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

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"The GDBusConnection on which to "
			"export the address book interface",
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
			"The object path at which to "
			"export the address book interface",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	if (!ops_pool)
		ops_pool = e_operation_pool_new (10, operation_thread, NULL);
}

static void
e_data_book_initable_init (GInitableIface *interface)
{
	interface->init = data_book_initable_init;
}

static void
e_data_book_init (EDataBook *ebook)
{
	ebook->priv = E_DATA_BOOK_GET_PRIVATE (ebook);

	ebook->priv->pending_ops = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) op_unref);
	ebook->priv->direct_ops = g_hash_table_new_full (
		g_direct_hash, g_direct_equal, NULL,
		(GDestroyNotify) direct_operation_data_free);
	g_rec_mutex_init (&ebook->priv->pending_ops_lock);
	g_mutex_init (&ebook->priv->open_lock);
}

/**
 * e_data_book_new:
 * @backend: an #EBookBackend
 * @connection: a #GDBusConnection
 * @object_path: object path for the D-Bus interface
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EDataBook and exports the AddressBook D-Bus interface
 * on @connection at @object_path.  The #EDataBook handles incoming remote
 * method invocations and forwards them to the @backend.  If the AddressBook
 * interface fails to export, the function sets @error and returns %NULL.
 *
 * Returns: an #EDataBook, or %NULL on error
 **/
EDataBook *
e_data_book_new (EBookBackend *backend,
                 GDBusConnection *connection,
                 const gchar *object_path,
                 GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	g_return_val_if_fail (object_path != NULL, NULL);

	return g_initable_new (
		E_TYPE_DATA_BOOK, NULL, error,
		"backend", backend,
		"connection", connection,
		"object-path", object_path,
		NULL);
}

/**
 * e_data_book_new_direct:
 * @registry: The #ESourceRegistry
 * @source: The #ESource to create a book for
 * @backend_path: The full path to the backend module to use
 * @backend_name: The #EDataFactory type name to use from the module specified by @backend_path
 * @config: The backend specific configuration string specified by the #EDataBookDirect
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Creates an #EDataBook for Direct Read Access, the @backend_path, @backend_name and @config
 * parameters are fetched from an #EDataBookDirect reported over D-Bus from the server side
 * counter part of a given backend.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Returns: (transfer full): A newly created #EDataBook for direct read access,
 *          otherwise %NULL is returned and @error is set.
 *
 * Since: 3.8
 */
EDataBook *
e_data_book_new_direct (ESourceRegistry *registry,
                        ESource *source,
                        const gchar *backend_path,
                        const gchar *backend_name,
                        const gchar *config,
                        GError **error)
{
	EDataBook *book = NULL;
	EModule *module;
	EBookBackend *backend;
	GType backend_type;
	GType factory_type;
	GTypeClass *factory_class;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (backend_path && backend_path[0], NULL);
	g_return_val_if_fail (backend_name && backend_name[0], NULL);

	module = load_module (backend_path);
	if (!module) {
		g_set_error (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			"Failed to load module at specified path: %s", backend_path);
		goto new_direct_finish;
	}

	if (!g_type_module_use (G_TYPE_MODULE (module))) {
		g_set_error (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			"Failed to use EModule at path: %s", backend_path);
		goto new_direct_finish;
	}

	factory_type = g_type_from_name (backend_name);
	if (factory_type == 0) {
		g_set_error (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			"Failed to get backend factory '%s' from EModule at path: %s",
			backend_name, backend_path);
		g_type_module_unuse (G_TYPE_MODULE (module));
		goto new_direct_finish;
	}

	factory_class = g_type_class_ref (factory_type);
	backend_type  = E_BOOK_BACKEND_FACTORY_CLASS (factory_class)->backend_type;
	g_type_class_unref (factory_class);

	backend = g_object_new (
		backend_type,
		"registry", registry,
		"source", source, NULL);

	/* The backend must be configured for direct access
	 * before calling g_initable_init() because backends
	 * now can open thier content at initable_init() time. */
	e_book_backend_configure_direct (backend, config);

	if (!g_initable_init (G_INITABLE (backend), NULL, error)) {
		g_object_unref (backend);
		g_type_module_unuse (G_TYPE_MODULE (module));
		goto new_direct_finish;
	}

	book = g_initable_new (
		E_TYPE_DATA_BOOK, NULL, error,
		"backend", backend, NULL);
	g_object_unref (backend);

	if (!book) {
		g_type_module_unuse (G_TYPE_MODULE (module));
		goto new_direct_finish;
	}

	book->priv->direct_module = module;

 new_direct_finish:

	return book;
}

/**
 * e_data_book_get_backend:
 * @book: an #EDataBook
 *
 * Returns the #EBookBackend to which incoming remote method invocations
 * are being forwarded.
 *
 * Returns: the #EBookBackend
 **/
EBookBackend *
e_data_book_get_backend (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->backend;
}

/**
 * e_data_book_get_connection:
 * @book: an #EDataBook
 *
 * Returns the #GDBusConnection on which the AddressBook D-Bus interface
 * is exported.
 *
 * Returns: the #GDBusConnection
 *
 * Since: 3.8
 **/
GDBusConnection *
e_data_book_get_connection (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->connection;
}

/**
 * e_data_book_get_object_path:
 * @book: an #EDataBook
 *
 * Returns the object path at which the AddressBook D-Bus interface is
 * exported.
 *
 * Returns: the object path
 *
 * Since: 3.8
 **/
const gchar *
e_data_book_get_object_path (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->object_path;
}

/*************************************************************************
 *                        Direct Read Access APIs                        *
 *************************************************************************/

static gboolean
e_data_book_open_finish (EDataBook *book,
                         GAsyncResult *result,
                         GError **error)
{
	gboolean res;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	res = g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (result));
	g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);

	return res;
}

/**
 * e_data_book_open_sync:
 * @book: an #EDataBook
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Opens the #EDataBook and it's backend.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Returns: %TRUE on success. If %FALSE is returned, @error will be set
 *
 * Since: 3.8
 */
gboolean
e_data_book_open_sync (EDataBook *book,
                       GCancellable *cancellable,
                       GError **error)
{
	DirectOperationData *data = NULL;
	OperationData *op;
	gboolean result = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	op = op_direct_new (OP_OPEN, book, cancellable, NULL, NULL, e_data_book_open_sync, TRUE, &data);

	op_dispatch (book, op);

	direct_operation_wait (data);
	result = e_data_book_open_finish (book, G_ASYNC_RESULT (data->result), error);
	direct_operation_data_free (data);

	return result;
}

static void
e_data_book_respond_close (EDataBook *book,
                           guint opid,
                           GError *error)
{
	DirectOperationData *data;

	data = op_complete (book, opid);

	if (data) {
		gboolean result = FALSE;

		if (error)
			g_simple_async_result_set_error (
				data->result,
				error->domain,
				error->code,
				"%s", error->message);

		else {
			if (!g_cancellable_is_cancelled (data->cancellable))
				result = TRUE;

			g_simple_async_result_set_check_cancellable (
				data->result,
				data->cancellable);
		}

		g_simple_async_result_set_op_res_gboolean (data->result, result);

		/* Deliver the result to the caller */
		direct_operation_complete (data);
	}

	if (error)
		g_error_free (error);
}

/**
 * e_data_book_close:
 * @book: an #EDataBook
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to @callback
 *
 * Closes the @book and its backend.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Since: 3.8
 */
void
e_data_book_close (EDataBook *book,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
	OperationData *op;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	op = op_direct_new (OP_CLOSE, book, cancellable, callback, user_data, e_data_book_close, FALSE, NULL);

	/* This operation is never queued. */
	e_operation_pool_push (ops_pool, op);
}

/**
 * e_data_book_close_finish:
 * @book: an #EDataBook
 * @result: a #GAsyncResult
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_data_book_close().  If an
 * error occurs, then this function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success
 *
 * Since: 3.8
 */
gboolean
e_data_book_close_finish (EDataBook *book,
                          GAsyncResult *result,
                          GError **error)
{
	gboolean res;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	res = g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (result));
	g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);

	return res;
}

/**
 * e_data_book_close_sync:
 * @book: an #EDataBook
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Closes the @book and its backend.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Returns: %TRUE on success. If %FALSE is returned, @error will be set
 *
 * Since: 3.8
 */
gboolean
e_data_book_close_sync (EDataBook *book,
                        GCancellable *cancellable,
                        GError **error)
{
	DirectOperationData *data = NULL;
	OperationData *op;
	gboolean result = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	op = op_direct_new (OP_CLOSE, book, cancellable, NULL, NULL, e_data_book_close_sync, TRUE, &data);

	/* This operation is never queued. */
	e_operation_pool_push (ops_pool, op);

	direct_operation_wait (data);
	result = e_data_book_close_finish (book, G_ASYNC_RESULT (data->result), error);
	direct_operation_data_free (data);

	return result;
}

/**
 * e_data_book_get_contact:
 * @book: an #EDataBook
 * @uid: a unique string ID specifying the contact
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to @callback
 *
 * Retrieves #EContact from the @book for the gived @uid.
 * The call is finished by e_data_book_get_contact_finish()
 * from the @callback.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Since: 3.8
 */
void
e_data_book_get_contact (EDataBook *book,
                         const gchar *uid,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	OperationData *op;

	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (uid && uid[0]);

	op = op_direct_new (OP_GET_CONTACT, book, cancellable, callback, user_data, e_data_book_get_contact, FALSE, NULL);
	op->d.uid = g_strdup (uid);

	op_dispatch (book, op);
}

/**
 * e_data_book_get_contact_finish:
 * @book: an #EDataBook
 * @result: a #GAsyncResult
 * @contact: return location for the fetched #EContact
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_data_book_get_contact().  If an
 * error occurs, then this function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success
 *
 * Since: 3.8
 */
gboolean
e_data_book_get_contact_finish (EDataBook *book,
                                GAsyncResult *result,
                                EContact **contact,
                                GError **error)
{
	EContact *ret_contact;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	ret_contact = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);

	if (contact) {
		if (ret_contact)
			*contact = g_object_ref (ret_contact);
		else
			*contact = NULL;
	}

	return ret_contact != NULL;
}

/**
 * e_data_book_get_contact_sync:
 * @book: an #EDataBook
 * @uid: a unique string ID specifying the contact
 * @contact: return location for the fetched #EContact
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Retrieves an #EContact from the @book for the gived @uid.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Returns: %TRUE on success. If %FALSE is returned, @error will be set
 *
 * Since: 3.8
 */
gboolean
e_data_book_get_contact_sync (EDataBook *book,
                              const gchar *uid,
                              EContact **contact,
                              GCancellable *cancellable,
                              GError **error)
{
	DirectOperationData *data = NULL;
	OperationData *op;
	gboolean result = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);
	g_return_val_if_fail (uid && uid[0], FALSE);

	op = op_direct_new (OP_GET_CONTACT, book, cancellable, NULL, NULL, e_data_book_get_contact_sync, TRUE, &data);
	op->d.uid = g_strdup (uid);

	op_dispatch (book, op);

	direct_operation_wait (data);
	result = e_data_book_get_contact_finish (book, G_ASYNC_RESULT (data->result), contact, error);
	direct_operation_data_free (data);

	return result;
}

/**
 * e_data_book_get_contacts:
 * @book: an #EDataBook
 * @sexp: an S-expression representing the query
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to @callback
 *
 * Query @book with @sexp, receiving a list of contacts which
 * matched. The call is finished by e_data_book_get_contacts_finish()
 * from the @callback.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Since: 3.8
 */
void
e_data_book_get_contacts (EDataBook *book,
                          const gchar *sexp,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	OperationData *op;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	op = op_direct_new (OP_GET_CONTACTS, book, cancellable, callback, user_data, e_data_book_get_contacts, FALSE, NULL);
	op->d.query = g_strdup (sexp);

	op_dispatch (book, op);
}

/**
 * e_data_book_get_contacts_finish:
 * @book: an #EDataBook
 * @result: a #GAsyncResult
 * @contacts: (element-type EContact) (out): a #GSList of matched #EContacts
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_data_book_get_contacts(). If an
 * error occurs, then this function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success
 *
 * Since: 3.8
 */
gboolean
e_data_book_get_contacts_finish (EDataBook *book,
                                 GAsyncResult *result,
                                 GSList **contacts,
                                 GError **error)
{
	GSList *ret_contacts;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	ret_contacts = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

	if (contacts) {
		if (ret_contacts)
			*contacts = e_util_copy_object_slist (NULL, ret_contacts);
		else
			*contacts = NULL;
	}

	/* If there was an error, the return is FALSE, otherwise
	 * the call was successfull even if no results were found
	 */
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

/**
 * e_data_book_get_contacts_sync:
 * @book: an #EDataBook
 * @sexp: an S-expression representing the query
 * @contacts: (element-type EContact) (out): a #GSList of matched #EContacts
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Query @book with @sexp, receiving a list of contacts which
 * matched.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Returns: %TRUE on success. If %FALSE is returned, @error will be set
 *
 * Since: 3.8
 */
gboolean
e_data_book_get_contacts_sync (EDataBook *book,
                               const gchar *sexp,
                               GSList **contacts,
                               GCancellable *cancellable,
                               GError **error)
{
	DirectOperationData *data = NULL;
	OperationData *op;
	gboolean result = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	op = op_direct_new (OP_GET_CONTACTS, book, cancellable, NULL, NULL, e_data_book_get_contacts_sync, TRUE, &data);
	op->d.query = g_strdup (sexp);

	op_dispatch (book, op);

	direct_operation_wait (data);
	result = e_data_book_get_contacts_finish (book, G_ASYNC_RESULT (data->result), contacts, error);
	direct_operation_data_free (data);

	return result;
}

/**
 * e_data_book_get_contacts_uids:
 * @book: an #EDataBook
 * @sexp: an S-expression representing the query
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to @callback
 *
 * Query @book with @sexp, receiving a list of contacts UIDs which
 * matched. The call is finished by e_data_book_get_contacts_uids_finish()
 * from the @callback.
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Since: 3.8
 */
void
e_data_book_get_contacts_uids (EDataBook *book,
                               const gchar *sexp,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	OperationData *op;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	op = op_direct_new (OP_GET_CONTACTS_UIDS, book, cancellable, callback, user_data, e_data_book_get_contacts_uids, FALSE, NULL);
	op->d.query = g_strdup (sexp);

	op_dispatch (book, op);
}

/**
 * e_data_book_get_contacts_uids_finish:
 * @book: an #EDataBook
 * @result: a #GAsyncResult
 * @contacts_uids: (element-type utf8) (out): a #GSList of matched contact UIDs stored as strings
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_data_book_get_contacts_uids(). If an
 * error occurs, then this function sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success
 *
 * Since: 3.8
 */
gboolean
e_data_book_get_contacts_uids_finish (EDataBook *book,
                                      GAsyncResult *result,
                                      GSList **contacts_uids,
                                      GError **error)
{
	GSList *ret_uids;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	ret_uids = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

	if (contacts_uids) {
		if (ret_uids)
			*contacts_uids = e_util_copy_string_slist (NULL, ret_uids);
		else
			*contacts_uids = NULL;
	}

	/* If there was an error, the return is FALSE, otherwise
	 * the call was successfull even if no results were found
	 */
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

/**
 * e_data_book_get_contacts_uids_sync:
 * @book: an #EDataBook
 * @sexp: an S-expression representing the query
 * @contacts_uids: (element-type utf8) (out): a #GSList of matched contact UIDs stored as strings
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Query @book with @sexp, receiving a list of contacts UIDs which
 * matched. 
 *
 * <note><para>This API is intended for internal use only, if you want client side
 * direct read access then use e_book_client_connect_direct_sync() instead</para></note>
 *
 * Returns: %TRUE on success. If %FALSE is returned, @error will be set
 *
 * Since: 3.8
 */
gboolean
e_data_book_get_contacts_uids_sync (EDataBook *book,
                                    const gchar *sexp,
                                    GSList **contacts_uids,
                                    GCancellable *cancellable,
                                    GError **error)
{
	DirectOperationData *data = NULL;
	OperationData *op;
	gboolean result = FALSE;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	op = op_direct_new (OP_GET_CONTACTS_UIDS, book, cancellable, NULL, NULL, e_data_book_get_contacts_uids_sync, TRUE, &data);
	op->d.query = g_strdup (sexp);

	op_dispatch (book, op);

	direct_operation_wait (data);
	result = e_data_book_get_contacts_uids_finish (book, G_ASYNC_RESULT (data->result), contacts_uids, error);
	direct_operation_data_free (data);

	return result;
}
