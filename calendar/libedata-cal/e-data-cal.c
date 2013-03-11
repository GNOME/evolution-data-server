/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar client interface object
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
 *          Ross Burton <ross@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libical/ical.h>
#include <glib/gi18n-lib.h>
#include <unistd.h>

/* Private D-Bus classes. */
#include <e-dbus-calendar.h>

#include <libedataserver/libedataserver.h>

#include "e-data-cal.h"
#include "e-cal-backend.h"
#include "e-cal-backend-sexp.h"

#define E_DATA_CAL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_CAL, EDataCalPrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

struct _EDataCalPrivate {
	GDBusConnection *connection;
	EDBusCalendar *dbus_interface;
	ECalBackend *backend;
	gchar *object_path;

	GRecMutex pending_ops_lock;
	GHashTable *pending_ops; /* opid -> OperationData */

	/* Operations are queued while an
	 * open operation is in progress. */
	GMutex open_lock;
	guint32 open_opid;
	GQueue open_queue;
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
	OP_GET_BACKEND_PROPERTY,
	OP_GET_OBJECT,
	OP_GET_OBJECT_LIST,
	OP_GET_FREE_BUSY,
	OP_CREATE_OBJECTS,
	OP_MODIFY_OBJECTS,
	OP_REMOVE_OBJECTS,
	OP_RECEIVE_OBJECTS,
	OP_SEND_OBJECTS,
	OP_GET_ATTACHMENT_URIS,
	OP_DISCARD_ALARM,
	OP_GET_VIEW,
	OP_GET_TIMEZONE,
	OP_ADD_TIMEZONE,
	OP_CLOSE
} OperationID;

typedef struct {
	volatile gint ref_count;

	OperationID op;
	guint32 id; /* operation id */
	EDataCal *cal; /* calendar */
	GCancellable *cancellable;
	GDBusMethodInvocation *invocation;
	guint watcher_id;

	union {
		/* OP_GET_OBJECT */
		/* OP_GET_ATTACHMENT_URIS */
		struct _ur {
			gchar *uid;
			gchar *rid;
		} ur;
		/* OP_DISCARD_ALARM */
		struct _ura {
			gchar *uid;
			gchar *rid;
			gchar *auid;
		} ura;
		/* OP_GET_OBJECT_LIST */
		/* OP_GET_VIEW */
		gchar *sexp;
		/* OP_GET_FREE_BUSY */
		struct _free_busy {
			time_t start, end;
			GSList *users;
		} fb;
		/* OP_CREATE_OBJECTS */
		GSList *calobjs;
		/* OP_RECEIVE_OBJECTS */
		/* OP_SEND_OBJECTS */
		struct _co {
			gchar *calobj;
		} co;
		/* OP_MODIFY_OBJECTS */
		struct _mo {
			GSList *calobjs;
			ECalObjModType mod;
		} mo;
		/* OP_REMOVE_OBJECTS */
		struct _ro {
			GSList *ids;
			ECalObjModType mod;
		} ro;
		/* OP_GET_TIMEZONE */
		gchar *tzid;
		/* OP_ADD_TIMEZONE */
		gchar *tzobject;
		/* OP_GET_BACKEND_PROPERTY */
		const gchar *prop_name;

		/* OP_REFRESH */
		/* OP_CLOSE */
	} d;
} OperationData;

/* Forward Declarations */
static void	e_data_cal_initable_init	(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EDataCal,
	e_data_cal,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_cal_initable_init))

/* Function to get a new EDataCalView path, used by get_view below */
static gchar *
construct_calview_path (void)
{
	static guint counter = 1;
	return g_strdup_printf ("/org/gnome/evolution/dataserver/CalendarView/%d/%d", getpid (), counter++);
}

static OperationData *
op_ref (OperationData *data)
{
	g_return_val_if_fail (data != NULL, data);
	g_return_val_if_fail (data->ref_count > 0, data);

	g_atomic_int_inc (&data->ref_count);

	return data;
}

static void
op_sender_vanished_cb (GDBusConnection *connection,
                       const gchar *sender,
                       GCancellable *cancellable)
{
	g_cancellable_cancel (cancellable);
}

static OperationData *
op_new (OperationID op,
        EDataCal *cal,
        GDBusMethodInvocation *invocation)
{
	OperationData *data;

	data = g_slice_new0 (OperationData);
	data->ref_count = 1;
	data->op = op;
	data->id = e_operation_pool_reserve_opid (ops_pool);
	data->cal = g_object_ref (cal);
	data->cancellable = g_cancellable_new ();

	/* This is optional so we can fake client requests. */
	if (invocation != NULL) {
		GDBusConnection *connection;
		const gchar *sender;

		data->invocation = g_object_ref (invocation);

		connection = e_data_cal_get_connection (cal);
		sender = g_dbus_method_invocation_get_sender (invocation);

		data->watcher_id = g_bus_watch_name_on_connection (
			connection, sender,
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			(GBusNameAppearedCallback) NULL,
			(GBusNameVanishedCallback) op_sender_vanished_cb,
			g_object_ref (data->cancellable),
			(GDestroyNotify) g_object_unref);
	}

	g_rec_mutex_lock (&cal->priv->pending_ops_lock);
	g_hash_table_insert (
		cal->priv->pending_ops,
		GUINT_TO_POINTER (data->id),
		op_ref (data));
	g_rec_mutex_unlock (&cal->priv->pending_ops_lock);

	return data;
}

static void
op_unref (OperationData *data)
{
	g_return_if_fail (data != NULL);
	g_return_if_fail (data->ref_count > 0);

	if (g_atomic_int_dec_and_test (&data->ref_count)) {

		switch (data->op) {
			case OP_GET_OBJECT:
			case OP_GET_ATTACHMENT_URIS:
				g_free (data->d.ur.uid);
				g_free (data->d.ur.rid);
				break;
			case OP_DISCARD_ALARM:
				g_free (data->d.ura.uid);
				g_free (data->d.ura.rid);
				g_free (data->d.ura.auid);
				break;
			case OP_GET_OBJECT_LIST:
			case OP_GET_VIEW:
				g_free (data->d.sexp);
				break;
			case OP_GET_FREE_BUSY:
				g_slist_free_full (
					data->d.fb.users,
					(GDestroyNotify) g_free);
				break;
			case OP_CREATE_OBJECTS:
				g_slist_free_full (
					data->d.calobjs,
					(GDestroyNotify) g_free);
				break;
			case OP_RECEIVE_OBJECTS:
			case OP_SEND_OBJECTS:
				g_free (data->d.co.calobj);
				break;
			case OP_MODIFY_OBJECTS:
				g_slist_free_full (
					data->d.mo.calobjs,
					(GDestroyNotify) g_free);
				break;
			case OP_REMOVE_OBJECTS:
				g_slist_free_full (
					data->d.ro.ids, (GDestroyNotify)
					e_cal_component_free_id);
				break;
			case OP_GET_TIMEZONE:
				g_free (data->d.tzid);
				break;
			case OP_ADD_TIMEZONE:
				g_free (data->d.tzobject);
				break;
			default:
				break;
		}

		g_object_unref (data->cal);
		g_object_unref (data->cancellable);

		if (data->invocation != NULL)
			g_object_unref (data->invocation);

		if (data->watcher_id > 0)
			g_bus_unwatch_name (data->watcher_id);

		g_slice_free (OperationData, data);
	}
}

static void
op_dispatch (EDataCal *cal,
             OperationData *data)
{
	g_mutex_lock (&cal->priv->open_lock);

	/* If an open operation is currently in progress, queue this
	 * operation to be dispatched when the open operation finishes. */
	if (cal->priv->open_opid > 0) {
		g_queue_push_tail (&cal->priv->open_queue, data);
	} else {
		if (data->op == OP_OPEN)
			cal->priv->open_opid = data->id;
		e_operation_pool_push (ops_pool, data);
	}

	g_mutex_unlock (&cal->priv->open_lock);
}

static OperationData *
op_claim (EDataCal *cal,
          guint32 opid)
{
	OperationData *data;

	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	e_operation_pool_release_opid (ops_pool, opid);

	g_rec_mutex_lock (&cal->priv->pending_ops_lock);
	data = g_hash_table_lookup (
		cal->priv->pending_ops,
		GUINT_TO_POINTER (opid));
	if (data != NULL) {
		/* Steal the hash table's reference. */
		g_hash_table_steal (
			cal->priv->pending_ops,
			GUINT_TO_POINTER (opid));
	}
	g_rec_mutex_unlock (&cal->priv->pending_ops_lock);

	return data;
}

static void
op_complete (EDataCal *cal,
             guint32 opid)
{
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_operation_pool_release_opid (ops_pool, opid);

	g_rec_mutex_lock (&cal->priv->pending_ops_lock);
	g_hash_table_remove (
		cal->priv->pending_ops,
		GUINT_TO_POINTER (opid));
	g_rec_mutex_unlock (&cal->priv->pending_ops_lock);
}

static void
data_cal_convert_to_client_error (GError *error)
{
	g_return_if_fail (error != NULL);

	if (error->domain != E_DATA_CAL_ERROR)
		return;

	switch (error->code) {
		case RepositoryOffline:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_REPOSITORY_OFFLINE;
			break;

		case PermissionDenied:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_PERMISSION_DENIED;
			break;

		case InvalidRange:
			error->domain = E_CAL_CLIENT_ERROR;
			error->code = E_CAL_CLIENT_ERROR_INVALID_RANGE;
			break;

		case ObjectNotFound:
			error->domain = E_CAL_CLIENT_ERROR;
			error->code = E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND;
			break;

		case InvalidObject:
			error->domain = E_CAL_CLIENT_ERROR;
			error->code = E_CAL_CLIENT_ERROR_INVALID_OBJECT;
			break;

		case ObjectIdAlreadyExists:
			error->domain = E_CAL_CLIENT_ERROR;
			error->code = E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS;
			break;

		case AuthenticationFailed:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_AUTHENTICATION_FAILED;
			break;

		case AuthenticationRequired:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_AUTHENTICATION_REQUIRED;
			break;

		case UnsupportedAuthenticationMethod:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD;
			break;

		case TLSNotAvailable:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_TLS_NOT_AVAILABLE;
			break;

		case NoSuchCal:
			error->domain = E_CAL_CLIENT_ERROR;
			error->code = E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR;
			break;

		case UnknownUser:
			error->domain = E_CAL_CLIENT_ERROR;
			error->code = E_CAL_CLIENT_ERROR_UNKNOWN_USER;
			break;

		case OfflineUnavailable:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_OFFLINE_UNAVAILABLE;
			break;

		case SearchSizeLimitExceeded:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED;
			break;

		case SearchTimeLimitExceeded:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED;
			break;

		case InvalidQuery:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_INVALID_QUERY;
			break;

		case QueryRefused:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_QUERY_REFUSED;
			break;

		case CouldNotCancel:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_COULD_NOT_CANCEL;
			break;

		case InvalidArg:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_INVALID_ARG;
			break;

		case NotSupported:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_NOT_SUPPORTED;
			break;

		case NotOpened:
			error->domain = E_CLIENT_ERROR;
			error->code = E_CLIENT_ERROR_NOT_OPENED;
			break;

		case UnsupportedField:
		case UnsupportedMethod:
		case OtherError:
		case InvalidServerVersion:
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
	ECalBackend *backend;
	GHashTableIter iter;
	gpointer value;

	backend = e_data_cal_get_backend (op->cal);

	switch (op->op) {
	case OP_OPEN:
		e_cal_backend_open (
			backend, op->cal, op->id,
			op->cancellable, FALSE);
		break;

	case OP_REFRESH:
		e_cal_backend_refresh (
			backend, op->cal, op->id, op->cancellable);
		break;

	case OP_GET_BACKEND_PROPERTY:
		e_cal_backend_get_backend_property (
			backend, op->cal, op->id,
			op->cancellable, op->d.prop_name);
		break;

	case OP_GET_OBJECT:
		e_cal_backend_get_object (
			backend, op->cal, op->id,
			op->cancellable,
			op->d.ur.uid,
			op->d.ur.rid && *op->d.ur.rid ? op->d.ur.rid : NULL);
		break;

	case OP_GET_OBJECT_LIST:
		e_cal_backend_get_object_list (
			backend, op->cal, op->id,
			op->cancellable, op->d.sexp);
		break;

	case OP_GET_FREE_BUSY:
		e_cal_backend_get_free_busy (
			backend, op->cal, op->id,
			op->cancellable,
			op->d.fb.users,
			op->d.fb.start,
			op->d.fb.end);
		break;

	case OP_CREATE_OBJECTS:
		e_cal_backend_create_objects (
			backend, op->cal, op->id,
			op->cancellable, op->d.calobjs);
		break;

	case OP_MODIFY_OBJECTS:
		e_cal_backend_modify_objects (
			backend, op->cal, op->id,
			op->cancellable,
			op->d.mo.calobjs,
			op->d.mo.mod);
		break;

	case OP_REMOVE_OBJECTS:
		e_cal_backend_remove_objects (
			backend, op->cal, op->id,
			op->cancellable,
			op->d.ro.ids,
			op->d.ro.mod);
		break;

	case OP_RECEIVE_OBJECTS:
		e_cal_backend_receive_objects (
			backend, op->cal, op->id,
			op->cancellable, op->d.co.calobj);
		break;

	case OP_SEND_OBJECTS:
		e_cal_backend_send_objects (
			backend, op->cal, op->id,
			op->cancellable, op->d.co.calobj);
		break;

	case OP_GET_ATTACHMENT_URIS:
		e_cal_backend_get_attachment_uris (
			backend, op->cal, op->id,
			op->cancellable,
			op->d.ur.uid,
			op->d.ur.rid && *op->d.ur.rid ? op->d.ur.rid : NULL);
		break;

	case OP_DISCARD_ALARM:
		e_cal_backend_discard_alarm (
			backend, op->cal, op->id,
			op->cancellable,
			op->d.ura.uid,
			op->d.ura.rid && *op->d.ura.rid ? op->d.ura.rid : NULL,
			op->d.ura.auid);
		break;

	case OP_GET_VIEW:
		if (op->d.sexp) {
			EDataCalView *view;
			ECalBackendSExp *obj_sexp;
			GDBusConnection *connection;
			gchar *object_path;
			GError *error = NULL;

			/* we handle this entirely here, since it doesn't require any
			 * backend involvement now that we have e_cal_view_start to
			 * actually kick off the search. */

			obj_sexp = e_cal_backend_sexp_new (op->d.sexp);
			if (!obj_sexp) {
				g_dbus_method_invocation_return_error_literal (
					op->invocation,
					E_CLIENT_ERROR,
					E_CLIENT_ERROR_INVALID_QUERY,
					_("Invalid query"));

				op_complete (op->cal, op->id);
				break;
			}

			object_path = construct_calview_path ();
			connection = e_data_cal_get_connection (op->cal);

			view = e_data_cal_view_new (
				backend, obj_sexp,
				connection, object_path, &error);

			g_object_unref (obj_sexp);

			/* Sanity check. */
			g_return_if_fail (
				((view != NULL) && (error == NULL)) ||
				((view == NULL) && (error != NULL)));

			if (error != NULL) {
				/* Translators: This is a prefix to a detailed error message *
 */
				g_prefix_error (&error, "%s", _("Invalid query: "));
				data_cal_convert_to_client_error (error);
				g_dbus_method_invocation_take_error (
					op->invocation, error);

				op_complete (op->cal, op->id);
				g_free (object_path);
				break;
			}

			e_cal_backend_add_view (backend, view);

			e_dbus_calendar_complete_get_view (
				op->cal->priv->dbus_interface,
				op->invocation,
				object_path);

			op_complete (op->cal, op->id);
			g_free (object_path);
		}
		break;

	case OP_GET_TIMEZONE:
		e_cal_backend_get_timezone (
			backend, op->cal, op->id,
			op->cancellable, op->d.tzid);
		break;

	case OP_ADD_TIMEZONE:
		e_cal_backend_add_timezone (
			backend, op->cal, op->id,
			op->cancellable, op->d.tzobject);
		break;

	case OP_CLOSE:
		/* close just cancels all pending ops and frees data cal */
		e_cal_backend_remove_client (backend, op->cal);

		g_rec_mutex_lock (&op->cal->priv->pending_ops_lock);

		g_hash_table_iter_init (&iter, op->cal->priv->pending_ops);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			OperationData *cancel_op = value;
			g_cancellable_cancel (cancel_op->cancellable);
		}

		g_rec_mutex_unlock (&op->cal->priv->pending_ops_lock);

		e_dbus_calendar_complete_close (
			op->cal->priv->dbus_interface,
			op->invocation);

		op_complete (op->cal, op->id);
		break;
	}

	op_unref (op);
}

/* Create the EDataCal error quark */
GQuark
e_data_cal_error_quark (void)
{
	#define ERR_PREFIX "org.gnome.evolution.dataserver.Calendar."

	static const GDBusErrorEntry entries[] = {
		{ Success,				ERR_PREFIX "Success" },
		{ Busy,					ERR_PREFIX "Busy" },
		{ RepositoryOffline,			ERR_PREFIX "RepositoryOffline" },
		{ PermissionDenied,			ERR_PREFIX "PermissionDenied" },
		{ InvalidRange,				ERR_PREFIX "InvalidRange" },
		{ ObjectNotFound,			ERR_PREFIX "ObjectNotFound" },
		{ InvalidObject,			ERR_PREFIX "InvalidObject" },
		{ ObjectIdAlreadyExists,		ERR_PREFIX "ObjectIdAlreadyExists" },
		{ AuthenticationFailed,			ERR_PREFIX "AuthenticationFailed" },
		{ AuthenticationRequired,		ERR_PREFIX "AuthenticationRequired" },
		{ UnsupportedField,			ERR_PREFIX "UnsupportedField" },
		{ UnsupportedMethod,			ERR_PREFIX "UnsupportedMethod" },
		{ UnsupportedAuthenticationMethod,	ERR_PREFIX "UnsupportedAuthenticationMethod" },
		{ TLSNotAvailable,			ERR_PREFIX "TLSNotAvailable" },
		{ NoSuchCal,				ERR_PREFIX "NoSuchCal" },
		{ UnknownUser,				ERR_PREFIX "UnknownUser" },
		{ OfflineUnavailable,			ERR_PREFIX "OfflineUnavailable" },
		{ SearchSizeLimitExceeded,		ERR_PREFIX "SearchSizeLimitExceeded" },
		{ SearchTimeLimitExceeded,		ERR_PREFIX "SearchTimeLimitExceeded" },
		{ InvalidQuery,				ERR_PREFIX "InvalidQuery" },
		{ QueryRefused,				ERR_PREFIX "QueryRefused" },
		{ CouldNotCancel,			ERR_PREFIX "CouldNotCancel" },
		{ OtherError,				ERR_PREFIX "OtherError" },
		{ InvalidServerVersion,			ERR_PREFIX "InvalidServerVersion" },
		{ InvalidArg,				ERR_PREFIX "InvalidArg" },
		{ NotSupported,				ERR_PREFIX "NotSupported" },
		{ NotOpened,				ERR_PREFIX "NotOpened" }
	};

	#undef ERR_PREFIX

	static volatile gsize quark_volatile = 0;

	g_dbus_error_register_error_domain ("e-data-cal-error", &quark_volatile, entries, G_N_ELEMENTS (entries));

	return (GQuark) quark_volatile;
}

/**
 * e_data_cal_status_to_string:
 *
 * Since: 2.32
 **/
const gchar *
e_data_cal_status_to_string (EDataCalCallStatus status)
{
	gint i;
	static struct _statuses {
		EDataCalCallStatus status;
		const gchar *msg;
	} statuses[] = {
		{ Success,				N_("Success") },
		{ Busy,					N_("Backend is busy") },
		{ RepositoryOffline,			N_("Repository offline") },
		{ PermissionDenied,			N_("Permission denied") },
		{ InvalidRange,				N_("Invalid range") },
		{ ObjectNotFound,			N_("Object not found") },
		{ InvalidObject,			N_("Invalid object") },
		{ ObjectIdAlreadyExists,		N_("Object ID already exists") },
		{ AuthenticationFailed,			N_("Authentication Failed") },
		{ AuthenticationRequired,		N_("Authentication Required") },
		{ UnsupportedField,			N_("Unsupported field") },
		{ UnsupportedMethod,			N_("Unsupported method") },
		{ UnsupportedAuthenticationMethod,	N_("Unsupported authentication method") },
		{ TLSNotAvailable,			N_("TLS not available") },
		{ NoSuchCal,				N_("Calendar does not exist") },
		{ UnknownUser,				N_("Unknown user") },
		{ OfflineUnavailable,			N_("Not available in offline mode") },
		{ SearchSizeLimitExceeded,		N_("Search size limit exceeded") },
		{ SearchTimeLimitExceeded,		N_("Search time limit exceeded") },
		{ InvalidQuery,				N_("Invalid query") },
		{ QueryRefused,				N_("Query refused") },
		{ CouldNotCancel,			N_("Could not cancel") },
		/* { OtherError,			N_("Other error") }, */
		{ InvalidServerVersion,			N_("Invalid server version") },
		{ InvalidArg,				N_("Invalid argument") },
		/* Translators: The string for NOT_SUPPORTED error */
		{ NotSupported,				N_("Not supported") },
		{ NotOpened,				N_("Backend is not opened yet") }
	};

	for (i = 0; i < G_N_ELEMENTS (statuses); i++) {
		if (statuses[i].status == status)
			return _(statuses[i].msg);
	}

	return _("Other error");
}

/**
 * e_data_cal_create_error:
 * @status: #EDataCalStatus code
 * @custom_msg: Custom message to use for the error. When NULL,
 *              then uses a default message based on the @status code.
 *
 * Returns: NULL, when the @status is Success,
 *          or a newly allocated GError, which should be freed
 *          with g_error_free() call.
 *
 * Since: 2.32
 **/
GError *
e_data_cal_create_error (EDataCalCallStatus status,
                         const gchar *custom_msg)
{
	if (status == Success)
		return NULL;

	return g_error_new_literal (E_DATA_CAL_ERROR, status, custom_msg ? custom_msg : e_data_cal_status_to_string (status));
}

/**
 * e_data_cal_create_error_fmt:
 *
 * Similar as e_data_cal_create_error(), only here, instead of custom_msg,
 * is used a printf() format to create a custom_msg for the error.
 *
 * Since: 2.32
 **/
GError *
e_data_cal_create_error_fmt (EDataCalCallStatus status,
                             const gchar *custom_msg_fmt,
                             ...)
{
	GError *error;
	gchar *custom_msg;
	va_list ap;

	if (!custom_msg_fmt)
		return e_data_cal_create_error (status, NULL);

	va_start (ap, custom_msg_fmt);
	custom_msg = g_strdup_vprintf (custom_msg_fmt, ap);
	va_end (ap);

	error = e_data_cal_create_error (status, custom_msg);

	g_free (custom_msg);

	return error;
}

static gboolean
data_cal_handle_open_cb (EDBusCalendar *interface,
                         GDBusMethodInvocation *invocation,
                         EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_OPEN, cal, invocation);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_refresh_cb (EDBusCalendar *interface,
                            GDBusMethodInvocation *invocation,
                            EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_REFRESH, cal, invocation);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_get_object_cb (EDBusCalendar *interface,
                               GDBusMethodInvocation *invocation,
                               const gchar *in_uid,
                               const gchar *in_rid,
                               EDataCal *cal)
{
	OperationData *op;

	/* Recurrence ID is optional.  Its omission is denoted
	 * via D-Bus by an emptry string.  Convert it to NULL. */
	if (in_rid != NULL && *in_rid == '\0')
		in_rid = NULL;

	op = op_new (OP_GET_OBJECT, cal, invocation);
	op->d.ur.uid = g_strdup (in_uid);
	op->d.ur.rid = g_strdup (in_rid);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_get_object_list_cb (EDBusCalendar *interface,
                                    GDBusMethodInvocation *invocation,
                                    const gchar *in_sexp,
                                    EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_OBJECT_LIST, cal, invocation);
	op->d.sexp = g_strdup (in_sexp);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_get_free_busy_cb (EDBusCalendar *interface,
                                  GDBusMethodInvocation *invocation,
                                  gint64 in_start,
                                  gint64 in_end,
                                  const gchar **in_users,
                                  EDataCal *cal)
{
	OperationData *op;
	GSList *tmp = NULL;
	gint ii;

	for (ii = 0; in_users[ii] != NULL; ii++)
		tmp = g_slist_prepend (tmp, g_strdup (in_users[ii]));

	op = op_new (OP_GET_FREE_BUSY, cal, invocation);
	op->d.fb.start = (time_t) in_start;
	op->d.fb.end = (time_t) in_end;
	op->d.fb.users = g_slist_reverse (tmp);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_create_objects_cb (EDBusCalendar *interface,
                                   GDBusMethodInvocation *invocation,
                                   const gchar **in_calobjs,
                                   EDataCal *cal)
{
	OperationData *op;
	GSList *tmp = NULL;
	gint ii;

	for (ii = 0; in_calobjs[ii] != NULL; ii++)
		tmp = g_slist_prepend (tmp, g_strdup (in_calobjs[ii]));

	op = op_new (OP_CREATE_OBJECTS, cal, invocation);
	op->d.calobjs = g_slist_reverse (tmp);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_modify_objects_cb (EDBusCalendar *interface,
                                   GDBusMethodInvocation *invocation,
                                   const gchar **in_ics_objects,
                                   const gchar *in_mod_type,
                                   EDataCal *cal)
{
	GFlagsClass *flags_class;
	ECalObjModType mod = 0;
	OperationData *op;
	GSList *tmp = NULL;
	gchar **flags_strv;
	gint ii;

	flags_class = g_type_class_ref (E_TYPE_CAL_OBJ_MOD_TYPE);
	flags_strv = g_strsplit (in_mod_type, ":", -1);
	for (ii = 0; flags_strv[ii] != NULL; ii++) {
		GFlagsValue *flags_value;

		flags_value = g_flags_get_value_by_nick (
			flags_class, flags_strv[ii]);
		if (flags_value != NULL) {
			mod |= flags_value->value;
		} else {
			g_warning (
				"%s: Unknown flag: %s",
				G_STRFUNC, flags_strv[ii]);
		}
	}
	g_strfreev (flags_strv);
	g_type_class_unref (flags_class);

	for (ii = 0; in_ics_objects[ii] != NULL; ii++)
		tmp = g_slist_prepend (tmp, g_strdup (in_ics_objects[ii]));

	op = op_new (OP_MODIFY_OBJECTS, cal, invocation);
	op->d.mo.calobjs = g_slist_reverse (tmp);
	op->d.mo.mod = mod;

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_remove_objects_cb (EDBusCalendar *interface,
                                   GDBusMethodInvocation *invocation,
                                   GVariant *in_uid_rid_array,
                                   const gchar *in_mod_type,
                                   EDataCal *cal)
{
	GFlagsClass *flags_class;
	ECalObjModType mod = 0;
	OperationData *op;
	GSList *tmp = NULL;
	gchar **flags_strv;
	gsize n_children, ii;

	flags_class = g_type_class_ref (E_TYPE_CAL_OBJ_MOD_TYPE);
	flags_strv = g_strsplit (in_mod_type, ":", -1);
	for (ii = 0; flags_strv[ii] != NULL; ii++) {
		GFlagsValue *flags_value;

		flags_value = g_flags_get_value_by_nick (
			flags_class, flags_strv[ii]);
		if (flags_value != NULL) {
			mod |= flags_value->value;
		} else {
			g_warning (
				"%s: Unknown flag: %s",
				G_STRFUNC, flags_strv[ii]);
		}
	}
	g_strfreev (flags_strv);
	g_type_class_unref (flags_class);

	n_children = g_variant_n_children (in_uid_rid_array);
	for (ii = 0; ii < n_children; ii++) {
		ECalComponentId *id;

		/* e_cal_component_free_id() uses g_free(),
		 * not g_slice_free().  Therefore allocate
		 * with g_malloc(), not g_slice_new(). */
		id = g_malloc0 (sizeof (ECalComponentId));

		g_variant_get_child (
			in_uid_rid_array, ii, "(ss)", &id->uid, &id->rid);

		if (id->uid != NULL && *id->uid == '\0') {
			e_cal_component_free_id (id);
			continue;
		}

		/* Recurrence ID is optional.  Its omission is denoted
		 * via D-Bus by an empty string.  Convert it to NULL. */
		if (id->rid != NULL && *id->rid == '\0') {
			g_free (id->rid);
			id->rid = NULL;
		}

		tmp = g_slist_prepend (tmp, id);
	}

	op = op_new (OP_REMOVE_OBJECTS, cal, invocation);
	op->d.ro.ids = g_slist_reverse (tmp);
	op->d.ro.mod = mod;

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_receive_objects_cb (EDBusCalendar *interface,
                                    GDBusMethodInvocation *invocation,
                                    const gchar *in_calobj,
                                    EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_RECEIVE_OBJECTS, cal, invocation);
	op->d.co.calobj = g_strdup (in_calobj);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_send_objects_cb (EDBusCalendar *interface,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *in_calobj,
                                 EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_SEND_OBJECTS, cal, invocation);
	op->d.co.calobj = g_strdup (in_calobj);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_get_attachment_uris_cb (EDBusCalendar *interface,
                                        GDBusMethodInvocation *invocation,
                                        const gchar *in_uid,
                                        const gchar *in_rid,
                                        EDataCal *cal)
{
	OperationData *op;

	/* Recurrence ID is optional.  Its omission is denoted
	 * via D-Bus by an empty string.  Convert it to NULL. */
	if (in_rid != NULL && *in_rid == '\0')
		in_rid = NULL;

	op = op_new (OP_GET_ATTACHMENT_URIS, cal, invocation);
	op->d.ur.uid = g_strdup (in_uid);
	op->d.ur.rid = g_strdup (in_rid);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_discard_alarm_cb (EDBusCalendar *interface,
                                  GDBusMethodInvocation *invocation,
                                  const gchar *in_uid,
                                  const gchar *in_rid,
                                  const gchar *in_alarm_uid,
                                  EDataCal *cal)
{
	OperationData *op;

	/* Recurrence ID is optional.  Its omission is denoted
	 * via D-Bus by an empty string.  Convert it to NULL. */
	if (in_rid != NULL && *in_rid == '\0')
		in_rid = NULL;

	op = op_new (OP_DISCARD_ALARM, cal, invocation);
	op->d.ura.uid = g_strdup (in_uid);
	op->d.ura.rid = g_strdup (in_rid);
	op->d.ura.auid = g_strdup (in_alarm_uid);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_get_view_cb (EDBusCalendar *interface,
                             GDBusMethodInvocation *invocation,
                             const gchar *in_sexp,
                             EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_VIEW, cal, invocation);
	op->d.sexp = g_strdup (in_sexp);

	/* This operation is never queued. */
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
data_cal_handle_get_timezone_cb (EDBusCalendar *interface,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *in_tzid,
                                 EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_TIMEZONE, cal, invocation);
	op->d.tzid = g_strdup (in_tzid);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_add_timezone_cb (EDBusCalendar *interface,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *in_tzobject,
                                 EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_ADD_TIMEZONE, cal, invocation);
	op->d.tzobject = g_strdup (in_tzobject);

	op_dispatch (cal, op);

	return TRUE;
}

static gboolean
data_cal_handle_close_cb (EDBusCalendar *interface,
                          GDBusMethodInvocation *invocation,
                          EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_CLOSE, cal, invocation);
	/* unref here makes sure the cal is freed in a separate thread */
	g_object_unref (cal);

	/* This operation is never queued. */
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

/**
 * e_data_cal_respond_open:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the open method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_open (EDataCal *cal,
                         guint32 opid,
                         GError *error)
{
	OperationData *data;
	GError *copy = NULL;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot open calendar: "));

	/* This function is deprecated, but it's the only way to
	 * set ECalBackend's internal 'opened' flag.  We should
	 * be the only ones calling this. */
	if (error != NULL)
		copy = g_error_copy (error);
	e_cal_backend_notify_opened (cal->priv->backend, copy);

	if (error == NULL) {
		e_dbus_calendar_complete_open (
			cal->priv->dbus_interface,
			data->invocation);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);

	/* Dispatch any pending operations. */

	g_mutex_lock (&cal->priv->open_lock);

	if (opid == cal->priv->open_opid) {
		OperationData *op;

		cal->priv->open_opid = 0;

		while (!g_queue_is_empty (&cal->priv->open_queue)) {
			op = g_queue_pop_head (&cal->priv->open_queue);
			e_operation_pool_push (ops_pool, op);
		}
	}

	g_mutex_unlock (&cal->priv->open_lock);
}

/**
 * e_data_cal_respond_refresh:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the refresh method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_refresh (EDataCal *cal,
                            guint32 opid,
                            GError *error)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot refresh calendar: "));

	if (error == NULL) {
		e_dbus_calendar_complete_refresh (
			cal->priv->dbus_interface,
			data->invocation);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_get_backend_property:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @prop_value: Value of a property
 *
 * Notifies listeners of the completion of the get_backend_property method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_get_backend_property (EDataCal *cal,
                                         guint32 opid,
                                         GError *error,
                                         const gchar *prop_value)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve backend property: "));

	if (error == NULL) {
		e_data_cal_report_backend_property_changed (
			cal, data->d.prop_name, prop_value);
	} else {
		/* This should never happen, since all backend property
		 * requests now originate from our constructed() method. */
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_set_backend_property:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the set_backend_property method call.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: This function no longer does anything.
 */
void
e_data_cal_respond_set_backend_property (EDataCal *cal,
                                         guint32 opid,
                                         GError *error)
{
	/* Do nothing. */
}

/**
 * e_data_cal_respond_get_object:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @object: The object retrieved as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_object method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_get_object (EDataCal *cal,
                               guint32 opid,
                               GError *error,
                               const gchar *object)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve calendar object path: "));

	if (error == NULL) {
		gchar *utf8_object;

		utf8_object = e_util_utf8_make_valid (object);

		e_dbus_calendar_complete_get_object (
			cal->priv->dbus_interface,
			data->invocation,
			utf8_object);

		g_free (utf8_object);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_get_object_list:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @objects: List of retrieved objects.
 *
 * Notifies listeners of the completion of the get_object_list method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_get_object_list (EDataCal *cal,
                                    guint32 opid,
                                    GError *error,
                                    const GSList *objects)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve calendar object list: "));

	if (error == NULL) {
		gchar **strv;
		guint length;
		gint ii = 0;

		length = g_slist_length ((GSList *) objects);
		strv = g_new0 (gchar *, length + 1);

		while (objects != NULL) {
			strv[ii++] = e_util_utf8_make_valid (objects->data);
			objects = g_slist_next (objects);
		}

		e_dbus_calendar_complete_get_object_list (
			cal->priv->dbus_interface,
			data->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_get_free_busy:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the get_free_busy method call.
 * To pass actual free/busy objects to the client use e_data_cal_report_free_busy_data().
 *
 * Since: 3.2
 */
void
e_data_cal_respond_get_free_busy (EDataCal *cal,
                                  guint32 opid,
                                  GError *error)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve calendar free/busy list: "));

	if (error == NULL) {
		e_dbus_calendar_complete_get_free_busy (
			cal->priv->dbus_interface,
			data->invocation);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_create_objects:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @uids: UIDs of the objects created.
 * @new_components: The newly created #ECalComponent objects.
 *
 * Notifies listeners of the completion of the create_objects method call.
 *
 * Since: 3.6
 */
void
e_data_cal_respond_create_objects (EDataCal *cal,
                                   guint32 opid,
                                   GError *error,
                                   const GSList *uids,
                                   GSList *new_components)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot create calendar object: "));

	if (error == NULL) {
		ECalBackend *backend;
		gchar **strv;
		guint length;
		gint ii = 0;

		backend = e_data_cal_get_backend (cal);

		length = g_slist_length ((GSList *) uids);
		strv = g_new0 (gchar *, length + 1);

		while (uids != NULL) {
			strv[ii++] = e_util_utf8_make_valid (uids->data);
			uids = g_slist_next ((GSList *) uids);
		}

		e_dbus_calendar_complete_create_objects (
			cal->priv->dbus_interface,
			data->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);

		while (new_components != NULL) {
			ECalComponent *component;

			component = E_CAL_COMPONENT (new_components->data);
			e_cal_backend_notify_component_created (
				backend, component);

			new_components = g_slist_next (new_components);
		}
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_modify_objects:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @old_components: The old #ECalComponents.
 * @new_components: The new #ECalComponents.
 *
 * Notifies listeners of the completion of the modify_objects method call.
 *
 * Since: 3.6
 */
void
e_data_cal_respond_modify_objects (EDataCal *cal,
                                   guint32 opid,
                                   GError *error,
                                   GSList *old_components,
                                   GSList *new_components)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot modify calendar object: "));

	if (error == NULL) {
		ECalBackend *backend;

		backend = e_data_cal_get_backend (cal);

		e_dbus_calendar_complete_modify_objects (
			cal->priv->dbus_interface,
			data->invocation);

		while (old_components != NULL && new_components != NULL) {
			ECalComponent *old_component;
			ECalComponent *new_component;

			old_component = E_CAL_COMPONENT (old_components->data);
			new_component = E_CAL_COMPONENT (new_components->data);

			e_cal_backend_notify_component_modified (
				backend, old_component, new_component);

			old_components = g_slist_next (old_components);
			new_components = g_slist_next (new_components);
		}
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_remove_objects:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @ids: IDs of the removed objects.
 * @old_components: The old #ECalComponents.
 * @new_components: The new #ECalComponents. They will not be NULL only
 * when removing instances of recurring appointments.
 *
 * Notifies listeners of the completion of the remove_objects method call.
 *
 * Since: 3.6
 */
void
e_data_cal_respond_remove_objects (EDataCal *cal,
                                  guint32 opid,
                                  GError *error,
                                  const GSList *ids,
                                  GSList *old_components,
                                  GSList *new_components)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove calendar object: "));

	if (error == NULL) {
		ECalBackend *backend;

		backend = e_data_cal_get_backend (cal);

		e_dbus_calendar_complete_remove_objects (
			cal->priv->dbus_interface,
			data->invocation);

		while (ids != NULL && old_components != NULL) {
			ECalComponentId *id;
			ECalComponent *old_component;
			ECalComponent *new_component = NULL;

			id = ids->data;
			old_component = E_CAL_COMPONENT (old_components->data);
			new_component = (new_components != NULL) ?
				E_CAL_COMPONENT (new_components->data) : NULL;

			e_cal_backend_notify_component_removed (
				backend, id, old_component, new_component);

			ids = g_slist_next ((GSList *) ids);
			old_components = g_slist_next (old_components);

			if (new_components != NULL)
				new_components = g_slist_next (new_components);
		}
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_receive_objects:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the receive_objects method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_receive_objects (EDataCal *cal,
                                    guint32 opid,
                                    GError *error)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot receive calendar objects: "));

	if (error == NULL) {
		e_dbus_calendar_complete_receive_objects (
			cal->priv->dbus_interface,
			data->invocation);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_send_objects:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @users: List of users.
 * @calobj: An iCalendar string representing the object sent.
 *
 * Notifies listeners of the completion of the send_objects method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_send_objects (EDataCal *cal,
                                 guint32 opid,
                                 GError *error,
                                 const GSList *users,
                                 const gchar *calobj)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot send calendar objects: "));

	if (error == NULL) {
		gchar *utf8_calobj;
		gchar **strv;
		guint length;
		gint ii = 0;

		length = g_slist_length ((GSList *) users);
		strv = g_new0 (gchar *, length + 1);

		while (users != NULL) {
			strv[ii++] = e_util_utf8_make_valid (users->data);
			users = g_slist_next ((GSList *) users);
		}

		utf8_calobj = e_util_utf8_make_valid (calobj);

		e_dbus_calendar_complete_send_objects (
			cal->priv->dbus_interface,
			data->invocation,
			(const gchar *const *) strv,
			utf8_calobj);

		g_free (utf8_calobj);

		g_strfreev (strv);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_get_attachment_uris:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @attachment_uris: List of retrieved attachment uri's.
 *
 * Notifies listeners of the completion of the get_attachment_uris method call.
 *
 * Since: 3.2
 **/
void
e_data_cal_respond_get_attachment_uris (EDataCal *cal,
                                        guint32 opid,
                                        GError *error,
                                        const GSList *attachment_uris)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not retrieve attachment uris: "));

	if (error == NULL) {
		gchar **strv;
		guint length;
		gint ii = 0;

		length = g_slist_length ((GSList *) attachment_uris);
		strv = g_new0 (gchar *, length + 1);

		while (attachment_uris != NULL) {
			strv[ii++] = e_util_utf8_make_valid (attachment_uris->data);
			attachment_uris = g_slist_next ((GSList *) attachment_uris);
		}

		e_dbus_calendar_complete_get_attachment_uris (
			cal->priv->dbus_interface,
			data->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}
}

/**
 * e_data_cal_respond_discard_alarm:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the discard_alarm method call.
 *
 * Since: 3.2
 **/
void
e_data_cal_respond_discard_alarm (EDataCal *cal,
                                  guint32 opid,
                                  GError *error)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not discard reminder: "));

	if (error == NULL) {
		e_dbus_calendar_complete_discard_alarm (
			cal->priv->dbus_interface,
			data->invocation);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_get_view:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @view_path: The new live view path.
 *
 * Notifies listeners of the completion of the get_view method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_get_view (EDataCal *cal,
                             guint32 opid,
                             GError *error,
                             const gchar *view_path)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not get calendar view path: "));

	if (error == NULL) {
		gchar *utf8_view_path;

		utf8_view_path = e_util_utf8_make_valid (view_path);

		e_dbus_calendar_complete_get_view (
			cal->priv->dbus_interface,
			data->invocation,
			utf8_view_path);

		g_free (utf8_view_path);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_get_timezone:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 * @tzobject: The requested timezone as an iCalendar string.
 *
 * Notifies listeners of the completion of the get_timezone method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_get_timezone (EDataCal *cal,
                                 guint32 opid,
                                 GError *error,
                                 const gchar *tzobject)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not retrieve calendar time zone: "));

	if (error == NULL) {
		gchar *utf8_tz_object;

		utf8_tz_object = e_util_utf8_make_valid (tzobject);

		e_dbus_calendar_complete_get_timezone (
			cal->priv->dbus_interface,
			data->invocation,
			utf8_tz_object);

		g_free (utf8_tz_object);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_respond_add_timezone:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the add_timezone method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_add_timezone (EDataCal *cal,
                                 guint32 opid,
                                 GError *error)
{
	OperationData *data;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	data = op_claim (cal, opid);
	g_return_if_fail (data != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not add calendar time zone: "));

	if (error == NULL) {
		e_dbus_calendar_complete_add_timezone (
			cal->priv->dbus_interface,
			data->invocation);
	} else {
		data_cal_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			data->invocation, error);
	}

	op_unref (data);
}

/**
 * e_data_cal_report_error:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_cal_report_error (EDataCal *cal,
                         const gchar *message)
{
	g_return_if_fail (E_IS_DATA_CAL (cal));
	g_return_if_fail (message != NULL);

	e_dbus_calendar_emit_error (cal->priv->dbus_interface, message);
}

/**
 * e_data_cal_report_readonly:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: Use e_cal_backend_set_writable() instead.
 **/
void
e_data_cal_report_readonly (EDataCal *cal,
                            gboolean readonly)
{
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_cal_backend_set_writable (cal->priv->backend, !readonly);
}

/**
 * e_data_cal_report_online:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: Use e_backend_set_online() instead.
 **/
void
e_data_cal_report_online (EDataCal *cal,
                          gboolean is_online)
{
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_backend_set_online (E_BACKEND (cal->priv->backend), is_online);
}

/**
 * e_data_cal_report_opened:
 *
 * Reports to associated client that opening phase of the cal is finished.
 * error being NULL means successfully, otherwise reports an error which
 * happened during opening phase. By opening phase is meant a process
 * including successfull authentication to the server/storage.
 *
 * Since: 3.2
 *
 * Deprecated: 3.8: This function no longer does anything.
 **/
void
e_data_cal_report_opened (EDataCal *cal,
                          const GError *error)
{
	/* Do nothing. */
}

/**
 * e_data_cal_report_free_busy_data:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_cal_report_free_busy_data (EDataCal *cal,
                                  const GSList *freebusy)
{
	gchar **strv;
	guint length;
	gint ii = 0;

	g_return_if_fail (E_IS_DATA_CAL (cal));

	length = g_slist_length ((GSList *) freebusy);
	strv = g_new0 (gchar *, length + 1);

	while (freebusy != NULL) {
		strv[ii++] = e_util_utf8_make_valid (freebusy->data);
		freebusy = g_slist_next ((GSList *) freebusy);
	}

	e_dbus_calendar_emit_free_busy_data (
		cal->priv->dbus_interface,
		(const gchar * const *) strv);

	g_strfreev (strv);
}

/**
 * e_data_cal_report_backend_property_changed:
 *
 * Notifies client about certain property value change 
 *
 * Since: 3.2
 **/
void
e_data_cal_report_backend_property_changed (EDataCal *cal,
                                            const gchar *prop_name,
                                            const gchar *prop_value)
{
	EDBusCalendar *dbus_interface;
	gchar **strv;

	g_return_if_fail (E_IS_DATA_CAL (cal));
	g_return_if_fail (prop_name != NULL);

	if (prop_value == NULL)
		prop_value = "";

	dbus_interface = cal->priv->dbus_interface;

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_calendar_set_capabilities (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_REVISION))
		e_dbus_calendar_set_revision (dbus_interface, prop_value);

	if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS))
		e_dbus_calendar_set_cal_email_address (dbus_interface, prop_value);

	if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS))
		e_dbus_calendar_set_alarm_email_address (dbus_interface, prop_value);

	if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT))
		e_dbus_calendar_set_default_object (dbus_interface, prop_value);

	/* Disregard anything else. */
}

static void
data_cal_set_backend (EDataCal *cal,
                      ECalBackend *backend)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal->priv->backend == NULL);

	cal->priv->backend = g_object_ref (backend);
}

static void
data_cal_set_connection (EDataCal *cal,
                         GDBusConnection *connection)
{
	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (cal->priv->connection == NULL);

	cal->priv->connection = g_object_ref (connection);
}

static void
data_cal_set_object_path (EDataCal *cal,
                          const gchar *object_path)
{
	g_return_if_fail (object_path != NULL);
	g_return_if_fail (cal->priv->object_path == NULL);

	cal->priv->object_path = g_strdup (object_path);
}

static void
data_cal_set_property (GObject *object,
                       guint property_id,
                       const GValue *value,
                       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			data_cal_set_backend (
				E_DATA_CAL (object),
				g_value_get_object (value));
			return;

		case PROP_CONNECTION:
			data_cal_set_connection (
				E_DATA_CAL (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			data_cal_set_object_path (
				E_DATA_CAL (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cal_get_property (GObject *object,
                       guint property_id,
                       GValue *value,
                       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			g_value_set_object (
				value,
				e_data_cal_get_backend (
				E_DATA_CAL (object)));
			return;

		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_data_cal_get_connection (
				E_DATA_CAL (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_data_cal_get_object_path (
				E_DATA_CAL (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cal_dispose (GObject *object)
{
	EDataCalPrivate *priv;

	priv = E_DATA_CAL_GET_PRIVATE (object);

	if (priv->connection != NULL) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (priv->backend != NULL) {
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_cal_parent_class)->dispose (object);
}

static void
data_cal_finalize (GObject *object)
{
	EDataCalPrivate *priv;

	priv = E_DATA_CAL_GET_PRIVATE (object);

	g_free (priv->object_path);

	if (priv->pending_ops) {
		g_hash_table_destroy (priv->pending_ops);
		priv->pending_ops = NULL;
	}

	g_rec_mutex_clear (&priv->pending_ops_lock);

	if (priv->dbus_interface) {
		g_object_unref (priv->dbus_interface);
		priv->dbus_interface = NULL;
	}

	g_mutex_clear (&priv->open_lock);

	/* This should be empty now, else we leak memory. */
	g_warn_if_fail (g_queue_is_empty (&priv->open_queue));

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_cal_parent_class)->finalize (object);
}

static void
data_cal_constructed (GObject *object)
{
	EDataCal *cal = E_DATA_CAL (object);
	OperationData *op;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_data_cal_parent_class)->constructed (object);

	g_object_bind_property (
		cal->priv->backend, "cache-dir",
		cal->priv->dbus_interface, "cache-dir",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		cal->priv->backend, "online",
		cal->priv->dbus_interface, "online",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property (
		cal->priv->backend, "writable",
		cal->priv->dbus_interface, "writable",
		G_BINDING_SYNC_CREATE);

	/* XXX Initialize the rest of the properties by faking client
	 *     requests.  At present it's the only way to fish values
	 *     from ECalBackend's antiquated API. */

	op = op_new (OP_GET_BACKEND_PROPERTY, cal, NULL);
	op->d.prop_name = CLIENT_BACKEND_PROPERTY_CAPABILITIES;
	e_cal_backend_get_backend_property (
		cal->priv->backend, cal, op->id,
		op->cancellable, op->d.prop_name);
	op_unref (op);

	op = op_new (OP_GET_BACKEND_PROPERTY, cal, NULL);
	op->d.prop_name = CLIENT_BACKEND_PROPERTY_REVISION;
	e_cal_backend_get_backend_property (
		cal->priv->backend, cal, op->id,
		op->cancellable, op->d.prop_name);
	op_unref (op);

	op = op_new (OP_GET_BACKEND_PROPERTY, cal, NULL);
	op->d.prop_name = CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS;
	e_cal_backend_get_backend_property (
		cal->priv->backend, cal, op->id,
		op->cancellable, op->d.prop_name);
	op_unref (op);

	op = op_new (OP_GET_BACKEND_PROPERTY, cal, NULL);
	op->d.prop_name = CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS;
	e_cal_backend_get_backend_property (
		cal->priv->backend, cal, op->id,
		op->cancellable, op->d.prop_name);
	op_unref (op);

	op = op_new (OP_GET_BACKEND_PROPERTY, cal, NULL);
	op->d.prop_name = CAL_BACKEND_PROPERTY_DEFAULT_OBJECT;
	e_cal_backend_get_backend_property (
		cal->priv->backend, cal, op->id,
		op->cancellable, op->d.prop_name);
	op_unref (op);
}

static gboolean
data_cal_initable_init (GInitable *initable,
                        GCancellable *cancellable,
                        GError **error)
{
	EDataCal *cal;

	cal = E_DATA_CAL (initable);

	return g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (cal->priv->dbus_interface),
		cal->priv->connection,
		cal->priv->object_path,
		error);
}

static void
e_data_cal_class_init (EDataCalClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EDataCalPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = data_cal_set_property;
	object_class->get_property = data_cal_get_property;
	object_class->dispose = data_cal_dispose;
	object_class->finalize = data_cal_finalize;
	object_class->constructed = data_cal_constructed;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			"Backend",
			"The backend driving this connection",
			E_TYPE_CAL_BACKEND,
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
			"export the calendar interface",
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
			"export the calendar interface",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	if (!ops_pool)
		ops_pool = e_operation_pool_new (10, operation_thread, NULL);
}

static void
e_data_cal_initable_init (GInitableIface *interface)
{
	interface->init = data_cal_initable_init;
}

static void
e_data_cal_init (EDataCal *ecal)
{
	EDBusCalendar *dbus_interface;

	ecal->priv = E_DATA_CAL_GET_PRIVATE (ecal);

	dbus_interface = e_dbus_calendar_skeleton_new ();
	ecal->priv->dbus_interface = dbus_interface;

	ecal->priv->pending_ops = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) op_unref);
	g_rec_mutex_init (&ecal->priv->pending_ops_lock);

	g_mutex_init (&ecal->priv->open_lock);

	g_signal_connect (
		dbus_interface, "handle-open",
		G_CALLBACK (data_cal_handle_open_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-refresh",
		G_CALLBACK (data_cal_handle_refresh_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-get-object",
		G_CALLBACK (data_cal_handle_get_object_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-get-object-list",
		G_CALLBACK (data_cal_handle_get_object_list_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-get-free-busy",
		G_CALLBACK (data_cal_handle_get_free_busy_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-create-objects",
		G_CALLBACK (data_cal_handle_create_objects_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-modify-objects",
		G_CALLBACK (data_cal_handle_modify_objects_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-remove-objects",
		G_CALLBACK (data_cal_handle_remove_objects_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-receive-objects",
		G_CALLBACK (data_cal_handle_receive_objects_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-send-objects",
		G_CALLBACK (data_cal_handle_send_objects_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-get-attachment-uris",
		G_CALLBACK (data_cal_handle_get_attachment_uris_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-discard-alarm",
		G_CALLBACK (data_cal_handle_discard_alarm_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-get-view",
		G_CALLBACK (data_cal_handle_get_view_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-get-timezone",
		G_CALLBACK (data_cal_handle_get_timezone_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-add-timezone",
		G_CALLBACK (data_cal_handle_add_timezone_cb), ecal);
	g_signal_connect (
		dbus_interface, "handle-close",
		G_CALLBACK (data_cal_handle_close_cb), ecal);
}

/**
 * e_data_cal_new:
 * @backend: an #ECalBackend
 * @connection: a #GDBusConnection
 * @object_path: object path for the D-Bus interface
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EDataCal and exports the Calendar D-Bus interface
 * on @connection at @object_path.  The #EDataCal handles incoming remote
 * method invocations and forwards them to the @backend.  If the Calendar
 * interface fails to export, the function sets @error and returns %NULL.
 *
 * Returns: an #EDataCal, or %NULL on error
 **/
EDataCal *
e_data_cal_new (ECalBackend *backend,
                GDBusConnection *connection,
                const gchar *object_path,
                GError **error)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	g_return_val_if_fail (object_path != NULL, NULL);

	return g_initable_new (
		E_TYPE_DATA_CAL, NULL, error,
		"backend", backend,
		"connection", connection,
		"object-path", object_path,
		NULL);
}

/**
 * e_data_cal_get_backend:
 * @cal: an #EDataCal
 *
 * Returns the #ECalBackend to which incoming remote method invocations
 * are being forwarded.
 *
 * Returns: the #ECalBackend
 **/
ECalBackend *
e_data_cal_get_backend (EDataCal *cal)
{
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	return cal->priv->backend;
}

/**
 * e_data_cal_get_connection:
 * @cal: an #EDataCal
 *
 * Returns the #GDBusConnection on which the Calendar D-Bus interface
 * is exported.
 *
 * Returns: the #GDBusConnection
 *
 * Since: 3.8
 **/
GDBusConnection *
e_data_cal_get_connection (EDataCal *cal)
{
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	return cal->priv->connection;
}

/**
 * e_data_cal_get_object_path:
 * @cal: an #EDataCal
 *
 * Returns the object path at which the Calendar D-Bus interface is
 * exported.
 *
 * Returns: the object path
 *
 * Since: 3.8
 **/
const gchar *
e_data_cal_get_object_path (EDataCal *cal)
{
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	return cal->priv->object_path;
}

