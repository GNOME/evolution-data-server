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
#include <glib-object.h>
#include <gio/gio.h>

#include "e-data-book-enumtypes.h"
#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-data-book-view.h"
#include "e-book-backend-sexp.h"
#include "opid.h"

#include "e-gdbus-egdbusbook.h"

G_DEFINE_TYPE (EDataBook, e_data_book, G_TYPE_OBJECT)

struct _EDataBookPrivate
{
	EGdbusBook *gdbus_object;

	EBookBackend *backend;
	ESource *source;
};

static void return_error_and_list (EGdbusBook *gdbus_object, void (* complete_func) (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar * const *out_array), guint32 opid, GError *error, const gchar *error_fmt, GList *list, gboolean free_data);
static void data_book_return_error (GDBusMethodInvocation *invocation, const GError *error, const gchar *error_fmt);

static GThreadPool *op_pool = NULL;

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
op_new (OperationID op, EDataBook *book, GDBusMethodInvocation *invocation)
{
	OperationData *data;

	data = g_slice_new0 (OperationData);
	data->op = op;
	data->book = g_object_ref (book);
	data->id = opid_store (invocation);

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
		{ E_DATA_BOOK_STATUS_NOT_SUPPORTED,			N_("Not supported") }
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
	#define ERR_PREFIX "org.gnome.evolution.dataserver.addressbook.Book."

	static const GDBusErrorEntry entries[] = {
		{ E_DATA_BOOK_STATUS_SUCCESS,				ERR_PREFIX "Success" },
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
		{ E_DATA_BOOK_STATUS_NOT_SUPPORTED,			ERR_PREFIX "NotSupported" }
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
e_data_book_create_error (EDataBookStatus status, const gchar *custom_msg)
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
e_data_book_create_error_fmt (EDataBookStatus status, const gchar *custom_msg_fmt, ...)
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

ESource*
e_data_book_get_source (EDataBook *book)
{
	g_return_val_if_fail (book != NULL, NULL);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->source;
}

EBookBackend*
e_data_book_get_backend (EDataBook *book)
{
	g_return_val_if_fail (book != NULL, NULL);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->backend;
}

static gboolean
impl_Book_open (EGdbusBook *object, GDBusMethodInvocation *invocation, gboolean only_if_exists, EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_OPEN, book, invocation);
	op->d.only_if_exists = only_if_exists;
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_open (EDataBook *book, guint opid, GError *error)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot open book: %s"));
		g_error_free (error);
	} else {
		e_gdbus_book_complete_open (book->priv->gdbus_object, invocation);
	}
}

static gboolean
impl_Book_remove (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	e_book_backend_remove (book->priv->backend, book, opid_store (invocation));

	return TRUE;
}

void
e_data_book_respond_remove (EDataBook *book, guint opid, GError *error)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot remove book: %s"));
		g_error_free (error);
	} else {
		e_gdbus_book_complete_remove (book->priv->gdbus_object, invocation);
	}
}

static gboolean
impl_Book_getContact (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *IN_uid, EDataBook *book)
{
	OperationData *op;

	if (IN_uid == NULL) {
		GError *error;

		error = e_data_book_create_error (E_DATA_BOOK_STATUS_CONTACT_NOT_FOUND, NULL);
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot get contact: %s"));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_GET_CONTACT, book, invocation);
	op->d.uid = g_strdup (IN_uid);
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_get_contact (EDataBook *book, guint32 opid, GError *error, const gchar *vcard)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error  (invocation, error, _("Cannot get contact: %s"));
		g_error_free (error);
	} else {
		e_gdbus_book_complete_get_contact (book->priv->gdbus_object, invocation, vcard);
	}
}

static gboolean
impl_Book_getContactList (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *query, EDataBook *book)
{
	OperationData *op;

	if (query == NULL || query[0] == '\0') {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Empty query: %s"));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_GET_CONTACTS, book, invocation);
	op->d.query = g_strdup (query);
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_get_contact_list (EDataBook *book, guint32 opid, GError *error, GList *cards)
{
	/* Translators: The '%s' is replaced with a detailed error message */
	return_error_and_list (book->priv->gdbus_object, e_gdbus_book_complete_get_contact_list, opid, error, _("Cannot get contact list: %s"), cards, TRUE);
}

static gboolean
impl_Book_authenticateUser (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *IN_user, const gchar *IN_passwd, const gchar *IN_auth_method, EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_AUTHENTICATE, book, invocation);
	op->d.auth.username = g_strdup (IN_user);
	op->d.auth.password = g_strdup (IN_passwd);
	op->d.auth.method = g_strdup (IN_auth_method);
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

static void
data_book_return_error (GDBusMethodInvocation *invocation, const GError *perror, const gchar *error_fmt)
{
	GError *error;

	g_return_if_fail (perror != NULL);

	error = g_error_new (E_DATA_BOOK_ERROR, perror->code, error_fmt, perror->message);
	g_dbus_method_invocation_return_gerror (invocation, error);

	g_error_free (error);
}

void
e_data_book_respond_authenticate_user (EDataBook *book, guint32 opid, GError *error)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot authenticate user: %s"));
		g_error_free (error);
	} else {
		e_gdbus_book_complete_authenticate_user (book->priv->gdbus_object, invocation);
	}
}

static gboolean
impl_Book_addContact (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *IN_vcard, EDataBook *book)
{
	OperationData *op;

	if (IN_vcard == NULL || IN_vcard[0] == '\0') {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot add contact: %s"));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_ADD_CONTACT, book, invocation);
	op->d.vcard = g_strdup (IN_vcard);
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_create (EDataBook *book, guint32 opid, GError *error, EContact *contact)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot add contact: %s"));
		g_error_free (error);
	} else {
		e_book_backend_notify_update (e_data_book_get_backend (book), contact);
		e_book_backend_notify_complete (e_data_book_get_backend (book));

		e_gdbus_book_complete_add_contact (book->priv->gdbus_object, invocation, e_contact_get_const (contact, E_CONTACT_UID));
	}
}

static gboolean
impl_Book_modifyContact (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *IN_vcard, EDataBook *book)
{
	OperationData *op;

	if (IN_vcard == NULL) {
		GError *error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot modify contact: %s"));
		g_error_free (error);
		return TRUE;
	}

	op = op_new (OP_MODIFY_CONTACT, book, invocation);
	op->d.vcard = g_strdup (IN_vcard);
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_modify (EDataBook *book, guint32 opid, GError *error, EContact *contact)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot modify contact: %s"));
		g_error_free (error);
	} else {
		e_book_backend_notify_update (e_data_book_get_backend (book), contact);
		e_book_backend_notify_complete (e_data_book_get_backend (book));

		e_gdbus_book_complete_modify_contact (book->priv->gdbus_object, invocation);
	}
}

static gboolean
impl_Book_removeContacts (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar * const *IN_uids, EDataBook *book)
{
	OperationData *op;

	/* Allow an empty array to be removed */
	if (IN_uids == NULL) {
		e_gdbus_book_complete_remove_contacts (object, invocation);
		return TRUE;
	}

	op = op_new (OP_REMOVE_CONTACTS, book, invocation);

	for (; *IN_uids; IN_uids++) {
		op->d.ids = g_list_prepend (op->d.ids, g_strdup (*IN_uids));
	}

	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_remove_contacts (EDataBook *book, guint32 opid, GError *error, GList *ids)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot remove contacts: %s"));
		g_error_free (error);
	} else {
		GList *i;

		for (i = ids; i; i = i->next)
			e_book_backend_notify_remove (e_data_book_get_backend (book), i->data);
		e_book_backend_notify_complete (e_data_book_get_backend (book));

		e_gdbus_book_complete_remove_contacts (book->priv->gdbus_object, invocation);
	}
}

static gboolean
impl_Book_getStaticCapabilities (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	gchar *capabilities = e_book_backend_get_static_capabilities (e_data_book_get_backend (book));

	e_gdbus_book_complete_get_static_capabilities (object, invocation, capabilities ? capabilities : "");

	g_free (capabilities);

	return TRUE;
}

static gboolean
impl_Book_getSupportedFields (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	e_book_backend_get_supported_fields (e_data_book_get_backend (book), book, opid_store (invocation));

	return TRUE;
}

void
e_data_book_respond_get_supported_fields (EDataBook *book, guint32 opid, GError *error, GList *fields)
{
	/* Translators: The '%s' is replaced with a detailed error message */
	return_error_and_list (book->priv->gdbus_object, e_gdbus_book_complete_get_supported_fields, opid, error, _("Cannot get supported fields: %s"), fields, FALSE);
}

static gboolean
impl_Book_getRequiredFields (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	e_book_backend_get_required_fields (e_data_book_get_backend (book), book, opid_store (invocation));

	return TRUE;
}

void
e_data_book_respond_get_required_fields (EDataBook *book, guint32 opid, GError *error, GList *fields)
{
	/* Translators: The '%s' is replaced with a detailed error message */
	return_error_and_list (book->priv->gdbus_object, e_gdbus_book_complete_get_required_fields, opid, error, _("Cannot get required fields: %s"), fields, FALSE);
}

static gboolean
impl_Book_getSupportedAuthMethods (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	e_book_backend_get_supported_auth_methods (e_data_book_get_backend (book), book, opid_store (invocation));

	return TRUE;
}

void
e_data_book_respond_get_supported_auth_methods (EDataBook *book, guint32 opid, GError *error, GList *auth_methods)
{
	/* Translators: The '%s' is replaced with a detailed error message */
	return_error_and_list (book->priv->gdbus_object, e_gdbus_book_complete_get_supported_auth_methods, opid, error, _("Cannot get supported authentication methods: %s"), auth_methods, FALSE);
}

static gchar *
construct_bookview_path (void)
{
	static volatile guint counter = 1;

	return g_strdup_printf ("/org/gnome/evolution/dataserver/addressbook/BookView/%d/%d",
				getpid (),
				g_atomic_int_exchange_and_add ((int*)&counter, 1));
}

static gboolean
impl_Book_getBookView (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *search, const guint max_results, EDataBook *book)
{
	EBookBackend *backend = e_data_book_get_backend (book);
	EBookBackendSExp *card_sexp;
	EDataBookView *book_view;
	gchar *path;
	GError *error = NULL;

	card_sexp = e_book_backend_sexp_new (search);
	if (!card_sexp) {
		error = e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, NULL);
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Invalid query: %s"));
		g_error_free (error);
		return TRUE;
	}

	path = construct_bookview_path ();
	book_view = e_data_book_view_new (book, search, card_sexp, max_results);
	e_data_book_view_register_gdbus_object (book_view, g_dbus_method_invocation_get_connection (invocation), path, &error);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Invalid query: %s"));
		g_error_free (error);
		g_object_unref (book_view);
		g_free (path);

		return TRUE;
	}

	e_book_backend_add_book_view (backend, book_view);

	e_gdbus_book_complete_get_book_view (object, invocation, path);

	g_free (path);

	return TRUE;
}

static gboolean
impl_Book_getChanges (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar *IN_change_id, EDataBook *book)
{
	OperationData *op;

	op = op_new (OP_GET_CHANGES, book, invocation);
	op->d.change_id = g_strdup (IN_change_id);
	g_thread_pool_push (op_pool, op, NULL);

	return TRUE;
}

void
e_data_book_respond_get_changes (EDataBook *book, guint32 opid, GError *error, GList *changes)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cannot get changes: %s"));
		g_error_free (error);
	} else {
		GVariantBuilder *builder;
		GVariant *variant;

		builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);

		while (changes != NULL) {
			EDataBookChange *change = (EDataBookChange *) changes->data;

			g_variant_builder_add (builder, "(us)", change->change_type, change->vcard ? change->vcard : "");

			g_free (change->vcard);
			g_free (change);

			changes = g_list_remove (changes, change);
		}

		/* always add one empty value */
		g_variant_builder_add (builder, "(us)", -1, "");

		variant = g_variant_builder_end (builder);
		g_variant_builder_unref (builder);

		e_gdbus_book_complete_get_changes (book->priv->gdbus_object, invocation, variant);

		g_variant_unref (variant);
	}
}

static gboolean
impl_Book_cancelOperation (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	GError *error = NULL;

	e_book_backend_cancel_operation (e_data_book_get_backend (book), book, &error);

	if (error) {
		/* Translators: The '%s' is replaced with a detailed error message */
		data_book_return_error (invocation, error, _("Cancel operation failed: %s"));
		g_error_free (error);
	} else {
		e_gdbus_book_complete_cancel_operation (object, invocation);
	}

	return TRUE;
}

static gboolean
impl_Book_close (EGdbusBook *object, GDBusMethodInvocation *invocation, EDataBook *book)
{
	e_book_backend_cancel_operation (e_data_book_get_backend (book), book, NULL);
	e_book_backend_remove_client (e_data_book_get_backend (book), book);

	e_gdbus_book_complete_close (object, invocation);
	g_object_unref (book);

	return TRUE;
}

void
e_data_book_report_writable (EDataBook *book, gboolean writable)
{
	g_return_if_fail (book != NULL);

	e_gdbus_book_emit_writable (book->priv->gdbus_object, writable);
}

void
e_data_book_report_connection_status (EDataBook *book, gboolean connected)
{
	g_return_if_fail (book != NULL);

	e_gdbus_book_emit_connection (book->priv->gdbus_object, connected);
}

void
e_data_book_report_auth_required (EDataBook *book)
{
	g_return_if_fail (book != NULL);

	e_gdbus_book_emit_auth_required (book->priv->gdbus_object);
}

static void
return_error_and_list (EGdbusBook *gdbus_object, void (* complete_func) (EGdbusBook *object, GDBusMethodInvocation *invocation, const gchar * const *out_array), guint32 opid, GError *error, const gchar *error_fmt, GList *list, gboolean free_data)
{
	GDBusMethodInvocation *invocation = opid_fetch (opid);

	g_return_if_fail (error_fmt != NULL);
	g_return_if_fail (complete_func != NULL);

	if (error) {
		data_book_return_error (invocation, error, error_fmt);
		g_error_free (error);
	} else {
		gchar **array;
		GList *l;
		gint i = 0;

		array = g_new0 (gchar *, g_list_length (list) + 1);
		for (l = list; l != NULL; l = l->next) {
			array[i++] = l->data;
		}

		complete_func (gdbus_object, invocation, (const gchar * const *) array);

		if (free_data) {
			g_strfreev (array);
		} else {
			g_free (array);
		}
	}
}

/**
 * e_data_book_register_gdbus_object:
 *
 * Registers GDBus object of this EDataBook.
 *
 * Since: 2.32
 **/
guint
e_data_book_register_gdbus_object (EDataBook *book, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	g_return_val_if_fail (book != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_BOOK (book), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_book_register_object (book->priv->gdbus_object, connection, object_path, error);
}

/* Instance init */
static void
e_data_book_init (EDataBook *ebook)
{
	EGdbusBook *gdbus_object;

	ebook->priv = G_TYPE_INSTANCE_GET_PRIVATE (ebook, E_TYPE_DATA_BOOK, EDataBookPrivate);

	ebook->priv->gdbus_object = e_gdbus_book_stub_new ();

	gdbus_object = ebook->priv->gdbus_object;
	g_signal_connect (gdbus_object, "handle-open", G_CALLBACK (impl_Book_open), ebook);
	g_signal_connect (gdbus_object, "handle-remove", G_CALLBACK (impl_Book_remove), ebook);
	g_signal_connect (gdbus_object, "handle-get-contact", G_CALLBACK (impl_Book_getContact), ebook);
	g_signal_connect (gdbus_object, "handle-get-contact-list", G_CALLBACK (impl_Book_getContactList), ebook);
	g_signal_connect (gdbus_object, "handle-authenticate-user", G_CALLBACK (impl_Book_authenticateUser), ebook);
	g_signal_connect (gdbus_object, "handle-add-contact", G_CALLBACK (impl_Book_addContact), ebook);
	g_signal_connect (gdbus_object, "handle-remove-contacts", G_CALLBACK (impl_Book_removeContacts), ebook);
	g_signal_connect (gdbus_object, "handle-modify-contact", G_CALLBACK (impl_Book_modifyContact), ebook);
	g_signal_connect (gdbus_object, "handle-get-static-capabilities", G_CALLBACK (impl_Book_getStaticCapabilities), ebook);
	g_signal_connect (gdbus_object, "handle-get-required-fields", G_CALLBACK (impl_Book_getRequiredFields), ebook);
	g_signal_connect (gdbus_object, "handle-get-supported-fields", G_CALLBACK (impl_Book_getSupportedFields), ebook);
	g_signal_connect (gdbus_object, "handle-get-supported-auth-methods", G_CALLBACK (impl_Book_getSupportedAuthMethods), ebook);
	g_signal_connect (gdbus_object, "handle-get-book-view", G_CALLBACK (impl_Book_getBookView), ebook);
	g_signal_connect (gdbus_object, "handle-get-changes", G_CALLBACK (impl_Book_getChanges), ebook);
	g_signal_connect (gdbus_object, "handle-cancel-operation", G_CALLBACK (impl_Book_cancelOperation), ebook);
	g_signal_connect (gdbus_object, "handle-close", G_CALLBACK (impl_Book_close), ebook);
}

static void
e_data_book_dispose (GObject *object)
{
	EDataBook *book = E_DATA_BOOK (object);

	if (book->priv->backend) {
		g_object_unref (book->priv->backend);
		book->priv->backend = NULL;
	}

	if (book->priv->source) {
		g_object_unref (book->priv->source);
		book->priv->source = NULL;
	}

	G_OBJECT_CLASS (e_data_book_parent_class)->dispose (object);
}

static void
e_data_book_class_init (EDataBookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EDataBookPrivate));

	object_class->dispose = e_data_book_dispose;

	if (!op_pool) {
		op_pool = g_thread_pool_new (operation_thread, NULL, 10, FALSE, NULL);

		/* Kill threads which don't do anything for 10 seconds */
		g_thread_pool_set_max_idle_time (10 * 1000);
	}
}

EDataBook *
e_data_book_new (EBookBackend *backend, ESource *source)
{
	EDataBook *book;

	book = g_object_new (E_TYPE_DATA_BOOK, NULL);
	book->priv->backend = g_object_ref (backend);
	book->priv->source = g_object_ref (source);

	return book;
}
