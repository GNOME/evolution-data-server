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

#include <libedataserver/libedataserver.h>

#include "e-data-cal.h"
#include "e-gdbus-cal.h"

#define E_DATA_CAL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_CAL, EDataCalPrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

struct _EDataCalPrivate {
	EGdbusCal *gdbus_object;

	ECalBackend *backend;

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
	OP_GET_BACKEND_PROPERTY,
	OP_SET_BACKEND_PROPERTY,
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
	OP_CANCEL_OPERATION,
	OP_CANCEL_ALL,
	OP_CLOSE
} OperationID;

typedef struct {
	OperationID op;
	guint32 id; /* operation id */
	EDataCal *cal; /* calendar */
	GCancellable *cancellable;

	union {
		/* OP_OPEN */
		gboolean only_if_exists;
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
			EDataCalObjModType mod;
		} mo;
		/* OP_REMOVE_OBJECTS */
		struct _ro {
			GSList *ids;
			EDataCalObjModType mod;
		} ro;
		/* OP_GET_TIMEZONE */
		gchar *tzid;
		/* OP_ADD_TIMEZONE */
		gchar *tzobject;
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

G_DEFINE_TYPE (EDataCal, e_data_cal, G_TYPE_OBJECT);

/* Function to get a new EDataCalView path, used by get_view below */
static gchar *
construct_calview_path (void)
{
	static guint counter = 1;
	return g_strdup_printf ("/org/gnome/evolution/dataserver/CalendarView/%d/%d", getpid (), counter++);
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
	ECalBackend *backend;

	backend = e_data_cal_get_backend (op->cal);

	switch (op->op) {
	case OP_OPEN:
		e_cal_backend_open (backend, op->cal, op->id, op->cancellable, op->d.only_if_exists);
		break;
	case OP_REMOVE:
		e_cal_backend_remove (backend, op->cal, op->id, op->cancellable);
		break;
	case OP_REFRESH:
		e_cal_backend_refresh (backend, op->cal, op->id, op->cancellable);
		break;
	case OP_GET_BACKEND_PROPERTY:
		e_cal_backend_get_backend_property (backend, op->cal, op->id, op->cancellable, op->d.prop_name);
		g_free (op->d.prop_name);
		break;
	case OP_SET_BACKEND_PROPERTY:
		e_cal_backend_set_backend_property (backend, op->cal, op->id, op->cancellable, op->d.sbp.prop_name, op->d.sbp.prop_value);
		g_free (op->d.sbp.prop_name);
		g_free (op->d.sbp.prop_value);
		break;
	case OP_GET_OBJECT:
		e_cal_backend_get_object (backend, op->cal, op->id, op->cancellable, op->d.ur.uid, op->d.ur.rid && *op->d.ur.rid ? op->d.ur.rid : NULL);
		g_free (op->d.ur.uid);
		g_free (op->d.ur.rid);
		break;
	case OP_GET_OBJECT_LIST:
		e_cal_backend_get_object_list (backend, op->cal, op->id, op->cancellable, op->d.sexp);
		g_free (op->d.sexp);
		break;
	case OP_GET_FREE_BUSY:
		e_cal_backend_get_free_busy (backend, op->cal, op->id, op->cancellable, op->d.fb.users, op->d.fb.start, op->d.fb.end);
		g_slist_free_full (op->d.fb.users, g_free);
		break;
	case OP_CREATE_OBJECTS:
		e_cal_backend_create_objects (backend, op->cal, op->id, op->cancellable, op->d.calobjs);
		g_slist_free_full (op->d.calobjs, g_free);
		break;
	case OP_MODIFY_OBJECTS:
		e_cal_backend_modify_objects (backend, op->cal, op->id, op->cancellable, op->d.mo.calobjs, op->d.mo.mod);
		g_slist_free_full (op->d.mo.calobjs, g_free);
		break;
	case OP_REMOVE_OBJECTS:
		e_cal_backend_remove_objects (backend, op->cal, op->id, op->cancellable, op->d.ro.ids, op->d.ro.mod);
		g_slist_free_full (op->d.ro.ids, (GDestroyNotify) e_cal_component_free_id);
		break;
	case OP_RECEIVE_OBJECTS:
		e_cal_backend_receive_objects (backend, op->cal, op->id, op->cancellable, op->d.co.calobj);
		g_free (op->d.co.calobj);
		break;
	case OP_SEND_OBJECTS:
		e_cal_backend_send_objects (backend, op->cal, op->id, op->cancellable, op->d.co.calobj);
		g_free (op->d.co.calobj);
		break;
	case OP_GET_ATTACHMENT_URIS:
		e_cal_backend_get_attachment_uris (backend, op->cal, op->id, op->cancellable, op->d.ur.uid, op->d.ur.rid && *op->d.ur.rid ? op->d.ur.rid : NULL);
		g_free (op->d.ur.uid);
		g_free (op->d.ur.rid);
		break;
	case OP_DISCARD_ALARM:
		e_cal_backend_discard_alarm (backend, op->cal, op->id, op->cancellable, op->d.ura.uid, op->d.ura.rid && *op->d.ura.rid ? op->d.ura.rid : NULL, op->d.ura.auid);
		g_free (op->d.ura.uid);
		g_free (op->d.ura.rid);
		g_free (op->d.ura.auid);
		break;
	case OP_GET_VIEW:
		if (op->d.sexp) {
			EDataCalView *view;
			ECalBackendSExp *obj_sexp;
			gchar *path;
			GError *error = NULL;

			/* we handle this entirely here, since it doesn't require any
			 * backend involvement now that we have e_cal_view_start to
			 * actually kick off the search. */

			obj_sexp = e_cal_backend_sexp_new (op->d.sexp);
			if (!obj_sexp) {
				g_free (op->d.sexp);
				e_data_cal_respond_get_view (op->cal, op->id, EDC_ERROR (InvalidQuery), NULL);
				break;
			}

			view = e_data_cal_view_new (backend, obj_sexp);
			g_object_unref (obj_sexp);
			if (!view) {
				g_free (op->d.sexp);
				e_data_cal_respond_get_view (op->cal, op->id, EDC_ERROR (OtherError), NULL);
				break;
			}

			path = construct_calview_path ();
			e_data_cal_view_register_gdbus_object (view, e_gdbus_cal_stub_get_connection (op->cal->priv->gdbus_object), path, &error);

			if (error) {
				g_object_unref (view);
				g_free (op->d.sexp);
				e_data_cal_respond_get_view (op->cal, op->id, EDC_ERROR_EX (OtherError, error->message), NULL);
				g_error_free (error);
				g_free (path);

				break;
			}

			e_cal_backend_add_view (backend, view);

			e_data_cal_respond_get_view (op->cal, op->id, EDC_ERROR (Success), path);

			g_free (path);
		}
		g_free (op->d.sexp);
		break;
	case OP_GET_TIMEZONE:
		e_cal_backend_get_timezone (backend, op->cal, op->id, op->cancellable, op->d.tzid);
		g_free (op->d.tzid);
		break;
	case OP_ADD_TIMEZONE:
		e_cal_backend_add_timezone (backend, op->cal, op->id, op->cancellable, op->d.tzobject);
		g_free (op->d.tzobject);
		break;
	case OP_CANCEL_OPERATION:
		g_static_rec_mutex_lock (&op->cal->priv->pending_ops_lock);

		if (g_hash_table_lookup (op->cal->priv->pending_ops, GUINT_TO_POINTER (op->d.opid))) {
			GCancellable *cancellable = g_hash_table_lookup (op->cal->priv->pending_ops, GUINT_TO_POINTER (op->d.opid));

			g_cancellable_cancel (cancellable);
		}

		g_static_rec_mutex_unlock (&op->cal->priv->pending_ops_lock);
		break;
	case OP_CLOSE:
		/* close just cancels all pending ops and frees data cal */
		e_cal_backend_remove_client (backend, op->cal);
	case OP_CANCEL_ALL:
		g_static_rec_mutex_lock (&op->cal->priv->pending_ops_lock);
		g_hash_table_foreach (op->cal->priv->pending_ops, cancel_ops_cb, NULL);
		g_static_rec_mutex_unlock (&op->cal->priv->pending_ops_lock);
		break;
	}

	g_object_unref (op->cal);
	g_object_unref (op->cancellable);
	g_slice_free (OperationData, op);
}

static OperationData *
op_new (OperationID op,
        EDataCal *cal)
{
	OperationData *data;

	data = g_slice_new0 (OperationData);
	data->op = op;
	data->cal = g_object_ref (cal);
	data->id = e_operation_pool_reserve_opid (ops_pool);
	data->cancellable = g_cancellable_new ();

	g_static_rec_mutex_lock (&cal->priv->pending_ops_lock);
	g_hash_table_insert (cal->priv->pending_ops, GUINT_TO_POINTER (data->id), g_object_ref (data->cancellable));
	g_static_rec_mutex_unlock (&cal->priv->pending_ops_lock);

	return data;
}

static void
op_complete (EDataCal *cal,
             guint32 opid)
{
	g_return_if_fail (cal != NULL);

	e_operation_pool_release_opid (ops_pool, opid);

	g_static_rec_mutex_lock (&cal->priv->pending_ops_lock);
	g_hash_table_remove (cal->priv->pending_ops, GUINT_TO_POINTER (opid));
	g_static_rec_mutex_unlock (&cal->priv->pending_ops_lock);
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

/**
 * e_data_cal_register_gdbus_object:
 *
 * Registers GDBus object of this EDataCal.
 *
 * Since: 2.32
 **/
guint
e_data_cal_register_gdbus_object (EDataCal *cal,
                                  GDBusConnection *connection,
                                  const gchar *object_path,
                                  GError **error)
{
	g_return_val_if_fail (cal != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_CAL (cal), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_cal_register_object (cal->priv->gdbus_object, connection, object_path, error);
}

static gboolean
impl_Cal_open (EGdbusCal *object,
               GDBusMethodInvocation *invocation,
               gboolean in_only_if_exists,
               EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_OPEN, cal);
	op->d.only_if_exists = in_only_if_exists;

	e_gdbus_cal_complete_open (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_remove (EGdbusCal *object,
                 GDBusMethodInvocation *invocation,
                 EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_REMOVE, cal);

	e_gdbus_cal_complete_remove (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_refresh (EGdbusCal *object,
                  GDBusMethodInvocation *invocation,
                  EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_REFRESH, cal);

	e_gdbus_cal_complete_refresh (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_backend_property (EGdbusCal *object,
                               GDBusMethodInvocation *invocation,
                               const gchar *in_prop_name,
                               EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_BACKEND_PROPERTY, cal);
	op->d.prop_name = g_strdup (in_prop_name);

	e_gdbus_cal_complete_get_backend_property (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_set_backend_property (EGdbusCal *object,
                               GDBusMethodInvocation *invocation,
                               const gchar * const *in_prop_name_value,
                               EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_SET_BACKEND_PROPERTY, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_set_backend_property (in_prop_name_value, &op->d.sbp.prop_name, &op->d.sbp.prop_value), FALSE);

	e_gdbus_cal_complete_set_backend_property (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_object (EGdbusCal *object,
                     GDBusMethodInvocation *invocation,
                     const gchar * const *in_uid_rid,
                     EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_OBJECT, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_get_object (in_uid_rid, &op->d.ur.uid, &op->d.ur.rid), FALSE);

	e_gdbus_cal_complete_get_object (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_object_list (EGdbusCal *object,
                          GDBusMethodInvocation *invocation,
                          const gchar *in_sexp,
                          EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_OBJECT_LIST, cal);
	op->d.sexp = g_strdup (in_sexp);

	e_gdbus_cal_complete_get_object_list (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_free_busy (EGdbusCal *object,
                        GDBusMethodInvocation *invocation,
                        const gchar * const *in_start_end_userlist,
                        EDataCal *cal)
{
	OperationData *op;
	guint start, end;

	op = op_new (OP_GET_FREE_BUSY, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_get_free_busy (in_start_end_userlist, &start, &end, &op->d.fb.users), FALSE);

	op->d.fb.start = (time_t) start;
	op->d.fb.end = (time_t) end;

	e_gdbus_cal_complete_get_free_busy (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_create_objects (EGdbusCal *object,
                         GDBusMethodInvocation *invocation,
                         const gchar * const *in_calobjs,
                         EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_CREATE_OBJECTS, cal);
	op->d.calobjs = e_util_strv_to_slist (in_calobjs);

	e_gdbus_cal_complete_create_objects (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_modify_objects (EGdbusCal *object,
                         GDBusMethodInvocation *invocation,
                         const gchar * const *in_mod_calobjs,
                         EDataCal *cal)
{
	OperationData *op;
	guint mod;

	op = op_new (OP_MODIFY_OBJECTS, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_modify_objects (in_mod_calobjs, &op->d.mo.calobjs, &mod), FALSE);
	op->d.mo.mod = mod;

	e_gdbus_cal_complete_modify_objects (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_remove_objects (EGdbusCal *object,
                         GDBusMethodInvocation *invocation,
                         const gchar * const *in_mod_ids,
                         EDataCal *cal)
{
	OperationData *op;
	guint mod = 0;

	op = op_new (OP_REMOVE_OBJECTS, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_remove_objects (in_mod_ids, &op->d.ro.ids, &mod), FALSE);
	op->d.ro.mod = mod;

	e_gdbus_cal_complete_remove_objects (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_receive_objects (EGdbusCal *object,
                          GDBusMethodInvocation *invocation,
                          const gchar *in_calobj,
                          EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_RECEIVE_OBJECTS, cal);
	op->d.co.calobj = g_strdup (in_calobj);

	e_gdbus_cal_complete_receive_objects (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_send_objects (EGdbusCal *object,
                       GDBusMethodInvocation *invocation,
                       const gchar *in_calobj,
                       EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_SEND_OBJECTS, cal);
	op->d.co.calobj = g_strdup (in_calobj);

	e_gdbus_cal_complete_send_objects (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_attachment_uris (EGdbusCal *object,
                              GDBusMethodInvocation *invocation,
                              const gchar * const *in_uid_rid,
                              EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_ATTACHMENT_URIS, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_get_attachment_uris (in_uid_rid, &op->d.ur.uid, &op->d.ur.rid), FALSE);

	e_gdbus_cal_complete_get_attachment_uris (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_discard_alarm (EGdbusCal *object,
                        GDBusMethodInvocation *invocation,
                        const gchar * const *in_uid_rid_auid,
                        EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_DISCARD_ALARM, cal);
	g_return_val_if_fail (e_gdbus_cal_decode_discard_alarm (in_uid_rid_auid, &op->d.ura.uid, &op->d.ura.rid, &op->d.ura.auid), FALSE);

	e_gdbus_cal_complete_discard_alarm (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_view (EGdbusCal *object,
                   GDBusMethodInvocation *invocation,
                   const gchar *in_sexp,
                   EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_VIEW, cal);
	op->d.sexp = g_strdup (in_sexp);

	e_gdbus_cal_complete_get_view (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_get_timezone (EGdbusCal *object,
                       GDBusMethodInvocation *invocation,
                       const gchar *in_tzid,
                       EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_GET_TIMEZONE, cal);
	op->d.tzid = g_strdup (in_tzid);

	e_gdbus_cal_complete_get_timezone (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_add_timezone (EGdbusCal *object,
                       GDBusMethodInvocation *invocation,
                       const gchar *in_tzobject,
                       EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_ADD_TIMEZONE, cal);
	op->d.tzobject = g_strdup (in_tzobject);

	e_gdbus_cal_complete_add_timezone (cal->priv->gdbus_object, invocation, op->id);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_cancel_operation (EGdbusCal *object,
                           GDBusMethodInvocation *invocation,
                           guint in_opid,
                           EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_CANCEL_OPERATION, cal);
	op->d.opid = in_opid;

	e_gdbus_cal_complete_cancel_operation (cal->priv->gdbus_object, invocation, NULL);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_cancel_all (EGdbusCal *object,
                     GDBusMethodInvocation *invocation,
                     EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_CANCEL_ALL, cal);

	e_gdbus_cal_complete_cancel_all (cal->priv->gdbus_object, invocation, NULL);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

static gboolean
impl_Cal_close (EGdbusCal *object,
                GDBusMethodInvocation *invocation,
                EDataCal *cal)
{
	OperationData *op;

	op = op_new (OP_CLOSE, cal);
	/* unref here makes sure the cal is freed in a separate thread */
	g_object_unref (cal);

	e_gdbus_cal_complete_close (cal->priv->gdbus_object, invocation, NULL);
	e_operation_pool_push (ops_pool, op);

	return TRUE;
}

/* free returned pointer with g_strfreev() */
static gchar **
gslist_to_strv (const GSList *lst)
{
	gchar **seq;
	const GSList *l;
	gint i;

	seq = g_new0 (gchar *, g_slist_length ((GSList *) lst) + 1);
	for (l = lst, i = 0; l; l = l->next, i++) {
		seq[i] = e_util_utf8_make_valid (l->data);
	}

	return seq;
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
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot open calendar: "));

	e_gdbus_cal_emit_open_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
}

/**
 * e_data_cal_respond_remove:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the remove method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_remove (EDataCal *cal,
                           guint32 opid,
                           GError *error)
{
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove calendar: "));

	e_gdbus_cal_emit_remove_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
	else
		e_cal_backend_set_is_removed (cal->priv->backend, TRUE);
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
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot refresh calendar: "));

	e_gdbus_cal_emit_refresh_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
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
	gchar *gdbus_prop_value = NULL;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve backend property: "));

	e_gdbus_cal_emit_get_backend_property_done (cal->priv->gdbus_object, opid, error, e_util_ensure_gdbus_string (prop_value, &gdbus_prop_value));

	g_free (gdbus_prop_value);
	if (error)
		g_error_free (error);
}

/**
 * e_data_cal_respond_set_backend_property:
 * @cal: A calendar client interface.
 * @error: Operation error, if any, automatically freed if passed it.
 *
 * Notifies listeners of the completion of the set_backend_property method call.
 *
 * Since: 3.2
 */
void
e_data_cal_respond_set_backend_property (EDataCal *cal,
                                         guint32 opid,
                                         GError *error)
{
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot set backend property: "));

	e_gdbus_cal_emit_set_backend_property_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
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
	gchar *gdbus_object = NULL;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve calendar object path: "));

	e_gdbus_cal_emit_get_object_done (cal->priv->gdbus_object, opid, error, e_util_ensure_gdbus_string (object, &gdbus_object));

	g_free (gdbus_object);
	if (error)
		g_error_free (error);
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
	gchar **strv_objects;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve calendar object list: "));

	strv_objects = gslist_to_strv (objects);

	e_gdbus_cal_emit_get_object_list_done (cal->priv->gdbus_object, opid, error, (const gchar * const *) strv_objects);

	g_strfreev (strv_objects);
	if (error)
		g_error_free (error);
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
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot retrieve calendar free/busy list: "));

	e_gdbus_cal_emit_get_free_busy_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
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
                                   /* const */ GSList *new_components)
{
	gchar **array = NULL;
	const GSList *l;
	gint i = 0;

	op_complete (cal, opid);

	array = g_new0 (gchar *, g_slist_length ((GSList *) uids) + 1);
	for (l = uids; l != NULL; l = l->next) {
		array[i++] = e_util_utf8_make_valid (l->data);
	}

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot create calendar object: "));

	e_gdbus_cal_emit_create_objects_done (cal->priv->gdbus_object, opid, error, (const gchar * const *) array);

	g_strfreev (array);
	if (error)
		g_error_free (error);
	else {
		for (l = new_components; l; l = l->next) {
			e_cal_backend_notify_component_created (cal->priv->backend, l->data);
		}
	}
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
                                   /* const */ GSList *old_components,
                                   /* const */ GSList *new_components)
{
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot modify calendar object: "));

	e_gdbus_cal_emit_modify_objects_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
	else {
		const GSList *lold = old_components, *lnew = new_components;
		while (lold && lnew) {
			e_cal_backend_notify_component_modified (cal->priv->backend, lold->data, lnew->data);
			lold = lold->next;
			lnew = lnew->next;
		}
	}
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
                                  /* const */ GSList *old_components,
                                  /* const */ GSList *new_components)
{
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove calendar object: "));

	e_gdbus_cal_emit_remove_objects_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
	else {
		const GSList *lid = ids, *lold = old_components, *lnew = new_components;
		while (lid && lold) {
			e_cal_backend_notify_component_removed (cal->priv->backend, lid->data, lold->data, lnew ? lnew->data : NULL);

			lid = lid->next;
			lold = lold->next;

			if (lnew)
				lnew = lnew->next;
		}
	}
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
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot receive calendar objects: "));

	e_gdbus_cal_emit_receive_objects_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
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
	gchar **strv_users_calobj;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot send calendar objects: "));

	strv_users_calobj = e_gdbus_cal_encode_send_objects (calobj, users);

	e_gdbus_cal_emit_send_objects_done (cal->priv->gdbus_object, opid, error, (const gchar * const *) strv_users_calobj);

	g_strfreev (strv_users_calobj);
	if (error)
		g_error_free (error);
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
	gchar **strv_attachment_uris;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not retrieve attachment uris: "));

	strv_attachment_uris = gslist_to_strv (attachment_uris);

	e_gdbus_cal_emit_get_attachment_uris_done (cal->priv->gdbus_object, opid, error, (const gchar * const *) strv_attachment_uris);

	g_strfreev (strv_attachment_uris);
	if (error)
		g_error_free (error);
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
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not discard reminder: "));

	e_gdbus_cal_emit_discard_alarm_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
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
	gchar *gdbus_view_path = NULL;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not get calendar view path: "));

	e_gdbus_cal_emit_get_view_done (cal->priv->gdbus_object, opid, error, e_util_ensure_gdbus_string (view_path, &gdbus_view_path));

	g_free (gdbus_view_path);
	if (error)
		g_error_free (error);
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
	gchar *gdbus_tzobject = NULL;

	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not retrieve calendar time zone: "));

	e_gdbus_cal_emit_get_timezone_done (cal->priv->gdbus_object, opid, error, e_util_ensure_gdbus_string (tzobject, &gdbus_tzobject));

	g_free (gdbus_tzobject);
	if (error)
		g_error_free (error);
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
	op_complete (cal, opid);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Could not add calendar time zone: "));

	e_gdbus_cal_emit_add_timezone_done (cal->priv->gdbus_object, opid, error);

	if (error)
		g_error_free (error);
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
	g_return_if_fail (cal != NULL);
	g_return_if_fail (message != NULL);

	e_gdbus_cal_emit_backend_error (cal->priv->gdbus_object, message);
}

/**
 * e_data_cal_report_readonly:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_cal_report_readonly (EDataCal *cal,
                            gboolean readonly)
{
	g_return_if_fail (cal != NULL);

	e_gdbus_cal_emit_readonly (cal->priv->gdbus_object, readonly);
}

/**
 * e_data_cal_report_online:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_data_cal_report_online (EDataCal *cal,
                          gboolean is_online)
{
	g_return_if_fail (cal != NULL);

	e_gdbus_cal_emit_online (cal->priv->gdbus_object, is_online);
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
 **/
void
e_data_cal_report_opened (EDataCal *cal,
                          const GError *error)
{
	gchar **strv_error;

	strv_error = e_gdbus_templates_encode_error (error);

	e_gdbus_cal_emit_opened (cal->priv->gdbus_object, (const gchar * const *) strv_error);

	g_strfreev (strv_error);
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
	gchar **strv_freebusy;

	g_return_if_fail (cal != NULL);

	strv_freebusy = gslist_to_strv (freebusy);

	e_gdbus_cal_emit_free_busy_data (cal->priv->gdbus_object, (const gchar * const *) strv_freebusy);

	g_strfreev (strv_freebusy);
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
	gchar **strv;

	g_return_if_fail (cal != NULL);
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name != '\0');
	g_return_if_fail (prop_value != NULL);

	strv = e_gdbus_templates_encode_two_strings (prop_name, prop_value);
	g_return_if_fail (strv != NULL);

	e_gdbus_cal_emit_backend_property_changed (cal->priv->gdbus_object, (const gchar * const *) strv);

	g_strfreev (strv);
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
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cal_dispose (GObject *object)
{
	EDataCalPrivate *priv;

	priv = E_DATA_CAL_GET_PRIVATE (object);

	if (priv->backend) {
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
	G_OBJECT_CLASS (e_data_cal_parent_class)->finalize (object);
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

	if (!ops_pool)
		ops_pool = e_operation_pool_new (10, operation_thread, NULL);
}

static void
e_data_cal_init (EDataCal *ecal)
{
	EGdbusCal *gdbus_object;

	ecal->priv = E_DATA_CAL_GET_PRIVATE (ecal);

	ecal->priv->gdbus_object = e_gdbus_cal_stub_new ();
	ecal->priv->pending_ops = g_hash_table_new_full (
		g_direct_hash, g_direct_equal, NULL, g_object_unref);
	g_static_rec_mutex_init (&ecal->priv->pending_ops_lock);

	gdbus_object = ecal->priv->gdbus_object;
	g_signal_connect (
		gdbus_object, "handle-open",
		G_CALLBACK (impl_Cal_open), ecal);
	g_signal_connect (
		gdbus_object, "handle-remove",
		G_CALLBACK (impl_Cal_remove), ecal);
	g_signal_connect (
		gdbus_object, "handle-refresh",
		G_CALLBACK (impl_Cal_refresh), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-backend-property",
		G_CALLBACK (impl_Cal_get_backend_property), ecal);
	g_signal_connect (
		gdbus_object, "handle-set-backend-property",
		G_CALLBACK (impl_Cal_set_backend_property), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-object",
		G_CALLBACK (impl_Cal_get_object), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-object-list",
		G_CALLBACK (impl_Cal_get_object_list), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-free-busy",
		G_CALLBACK (impl_Cal_get_free_busy), ecal);
	g_signal_connect (
		gdbus_object, "handle-create-objects",
		G_CALLBACK (impl_Cal_create_objects), ecal);
	g_signal_connect (
		gdbus_object, "handle-modify-objects",
		G_CALLBACK (impl_Cal_modify_objects), ecal);
	g_signal_connect (
		gdbus_object, "handle-remove-objects",
		G_CALLBACK (impl_Cal_remove_objects), ecal);
	g_signal_connect (
		gdbus_object, "handle-receive-objects",
		G_CALLBACK (impl_Cal_receive_objects), ecal);
	g_signal_connect (
		gdbus_object, "handle-send-objects",
		G_CALLBACK (impl_Cal_send_objects), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-attachment-uris",
		G_CALLBACK (impl_Cal_get_attachment_uris), ecal);
	g_signal_connect (
		gdbus_object, "handle-discard-alarm",
		G_CALLBACK (impl_Cal_discard_alarm), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-view",
		G_CALLBACK (impl_Cal_get_view), ecal);
	g_signal_connect (
		gdbus_object, "handle-get-timezone",
		G_CALLBACK (impl_Cal_get_timezone), ecal);
	g_signal_connect (
		gdbus_object, "handle-add-timezone",
		G_CALLBACK (impl_Cal_add_timezone), ecal);
	g_signal_connect (
		gdbus_object, "handle-cancel-operation",
		G_CALLBACK (impl_Cal_cancel_operation), ecal);
	g_signal_connect (
		gdbus_object, "handle-cancel-all",
		G_CALLBACK (impl_Cal_cancel_all), ecal);
	g_signal_connect (
		gdbus_object, "handle-close",
		G_CALLBACK (impl_Cal_close), ecal);
}

EDataCal *
e_data_cal_new (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return g_object_new (E_TYPE_DATA_CAL, "backend", backend, NULL);
}

ECalBackend *
e_data_cal_get_backend (EDataCal *cal)
{
	g_return_val_if_fail (E_IS_DATA_CAL (cal), NULL);

	return cal->priv->backend;
}

