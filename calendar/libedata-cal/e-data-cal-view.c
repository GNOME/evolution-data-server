/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Live search view implementation
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
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

#include <string.h>

#include "e-cal-backend-sexp.h"
#include "e-data-cal-view.h"
#include "e-gdbus-cal-view.h"

#define E_DATA_CAL_VIEW_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_DATA_CAL_VIEW_TYPE, EDataCalViewPrivate))

static void ensure_pending_flush_timeout (EDataCalView *view);

#define THRESHOLD_ITEMS   32	/* how many items can be hold in a cache, before propagated to UI */
#define THRESHOLD_SECONDS  2	/* how long to wait until notifications are propagated to UI; in seconds */

struct _EDataCalViewPrivate {
	EGdbusCalView *gdbus_object;

	/* The backend we are monitoring */
	ECalBackend *backend;

	gboolean started;
	gboolean stopped;
	gboolean complete;

	/* Sexp that defines the view */
	ECalBackendSExp *sexp;

	GArray *adds;
	GArray *changes;
	GArray *removes;

	GHashTable *ids;

	GMutex *pending_mutex;
	guint flush_id;

	/* view flags */
	ECalClientViewFlags flags;

	/* which fields is listener interested in */
	GHashTable *fields_of_interest;
};

G_DEFINE_TYPE (EDataCalView, e_data_cal_view, G_TYPE_OBJECT);

static void e_data_cal_view_dispose (GObject *object);
static void e_data_cal_view_finalize (GObject *object);
static void e_data_cal_view_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void e_data_cal_view_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);

/* Property IDs */
enum props {
	PROP_0,
	PROP_BACKEND,
	PROP_SEXP
};

/* Class init */
static void
e_data_cal_view_class_init (EDataCalViewClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	g_type_class_add_private (class, sizeof (EDataCalViewPrivate));

	object_class->set_property = e_data_cal_view_set_property;
	object_class->get_property = e_data_cal_view_get_property;
	object_class->dispose = e_data_cal_view_dispose;
	object_class->finalize = e_data_cal_view_finalize;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			NULL,
			NULL,
			E_TYPE_CAL_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_SEXP,
		g_param_spec_object (
			"sexp",
			NULL,
			NULL,
			E_TYPE_CAL_BACKEND_SEXP,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static guint
str_ic_hash (gconstpointer key)
{
	guint32 hash = 5381;
	const gchar *str = key;
	gint ii;

	if (!str)
		return hash;

	for (ii = 0; str[ii]; ii++) {
		hash = hash * 33 + g_ascii_tolower (str[ii]);
	}

	return hash;
}

static gboolean
str_ic_equal (gconstpointer a,
              gconstpointer b)
{
	const gchar *stra = a, *strb = b;
	gint ii;

	if (!stra && !strb)
		return TRUE;

	if (!stra || !strb)
		return FALSE;

	for (ii = 0; stra[ii] && strb[ii]; ii++) {
		if (g_ascii_tolower (stra[ii]) != g_ascii_tolower (strb[ii]))
			return FALSE;
	}

	return stra[ii] == strb[ii];
}

static guint
id_hash (gconstpointer key)
{
	const ECalComponentId *id = key;
	return g_str_hash (id->uid) ^ (id->rid ? g_str_hash (id->rid) : 0);
}

static gboolean
id_equal (gconstpointer a,
          gconstpointer b)
{
	const ECalComponentId *id_a = a, *id_b = b;
	return g_strcmp0 (id_a->uid, id_b->uid) == 0 && g_strcmp0 (id_a->rid, id_b->rid) == 0;
}

EDataCalView *
e_data_cal_view_new (ECalBackend *backend,
                     ECalBackendSExp *sexp)
{
	EDataCalView *view;

	view = g_object_new (E_DATA_CAL_VIEW_TYPE, "backend", backend, "sexp", sexp, NULL);

	return view;
}

/**
 * e_data_cal_view_register_gdbus_object:
 *
 * Since: 2.32
 **/
guint
e_data_cal_view_register_gdbus_object (EDataCalView *view,
                                       GDBusConnection *connection,
                                       const gchar *object_path,
                                       GError **error)
{
	g_return_val_if_fail (view != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_cal_view_register_object (view->priv->gdbus_object, connection, object_path, error);
}

static void
reset_array (GArray *array)
{
	gint i = 0;
	gchar *tmp = NULL;

	/* Free stored strings */
	for (i = 0; i < array->len; i++) {
		tmp = g_array_index (array, gchar *, i);
		g_free (tmp);
	}

	/* Force the array size to 0 */
	g_array_set_size (array, 0);
}

static void
send_pending_adds (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	if (priv->adds->len == 0)
		return;

	e_gdbus_cal_view_emit_objects_added (view->priv->gdbus_object, (const gchar * const *) priv->adds->data);
	reset_array (priv->adds);
}

static void
send_pending_changes (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	if (priv->changes->len == 0)
		return;

	e_gdbus_cal_view_emit_objects_modified (view->priv->gdbus_object, (const gchar * const *) priv->changes->data);
	reset_array (priv->changes);
}

static void
send_pending_removes (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	if (priv->removes->len == 0)
		return;

	/* send ECalComponentIds as <uid>[\n<rid>], as encoded in notify_remove() */
	e_gdbus_cal_view_emit_objects_removed (view->priv->gdbus_object, (const gchar * const *) priv->removes->data);
	reset_array (priv->removes);
}

static gboolean
pending_flush_timeout_cb (gpointer data)
{
	EDataCalView *view = data;
	EDataCalViewPrivate *priv = view->priv;

	g_mutex_lock (priv->pending_mutex);

	priv->flush_id = 0;

	send_pending_adds (view);
	send_pending_changes (view);
	send_pending_removes (view);

	g_mutex_unlock (priv->pending_mutex);

	return FALSE;
}

static void
ensure_pending_flush_timeout (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	if (priv->flush_id)
		return;

	priv->flush_id = g_timeout_add (e_data_cal_view_is_completed (view) ? 10 : (THRESHOLD_SECONDS * 1000), pending_flush_timeout_cb, view);
}

static void
notify_add (EDataCalView *view,
            gchar *obj)
{
	EDataCalViewPrivate *priv = view->priv;
	ECalComponent *comp;
	ECalClientViewFlags flags;

	send_pending_changes (view);
	send_pending_removes (view);

	/* Do not send object add notifications during initial stage */
	flags = e_data_cal_view_get_flags (view);
	if (priv->complete || (flags & E_CAL_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL) != 0) {
		if (priv->adds->len == THRESHOLD_ITEMS) {
			send_pending_adds (view);
		}
		g_array_append_val (priv->adds, obj);

		ensure_pending_flush_timeout (view);
	}

	comp = e_cal_component_new_from_string (obj);
	g_hash_table_insert (
		priv->ids,
		e_cal_component_get_id (comp),
		GUINT_TO_POINTER (1));
	g_object_unref (comp);
}

static void
notify_add_component (EDataCalView *view,
                      /* const */ ECalComponent *comp)
{
	EDataCalViewPrivate *priv = view->priv;
	gchar               *obj;
	ECalClientViewFlags flags;

	obj = e_data_cal_view_get_component_string (view, comp);

	send_pending_changes (view);
	send_pending_removes (view);

	/* Do not send component add notifications during initial stage */
	flags = e_data_cal_view_get_flags (view);
	if (priv->complete || (flags & E_CAL_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL) != 0) {
		if (priv->adds->len == THRESHOLD_ITEMS) {
			send_pending_adds (view);
		}
		g_array_append_val (priv->adds, obj);

		ensure_pending_flush_timeout (view);
	}

	g_hash_table_insert (
		priv->ids,
		e_cal_component_get_id (comp),
		GUINT_TO_POINTER (1));
}

static void
notify_change (EDataCalView *view,
               gchar *obj)
{
	EDataCalViewPrivate *priv = view->priv;

	send_pending_adds (view);
	send_pending_removes (view);

	if (priv->changes->len == THRESHOLD_ITEMS) {
		send_pending_changes (view);
	}

	g_array_append_val (priv->changes, obj);

	ensure_pending_flush_timeout (view);
}

static void
notify_change_component (EDataCalView *view,
                         ECalComponent *comp)
{
	gchar *obj;

	obj = e_data_cal_view_get_component_string (view, comp);

	notify_change (view, obj);
}

static void
notify_remove (EDataCalView *view,
               ECalComponentId *id)
{
	EDataCalViewPrivate *priv = view->priv;
	gchar *ids;
	gchar *uid, *rid;
	gsize uid_len, rid_len;

	send_pending_adds (view);
	send_pending_changes (view);

	if (priv->removes->len == THRESHOLD_ITEMS) {
		send_pending_removes (view);
	}

	/* store ECalComponentId as <uid>[\n<rid>] (matches D-Bus API) */
	if (id->uid) {
		uid = e_util_utf8_make_valid (id->uid);
		uid_len = strlen (uid);
	} else {
		uid = NULL;
		uid_len = 0;
	}
	if (id->rid) {
		rid = e_util_utf8_make_valid (id->rid);
		rid_len = strlen (rid);
	} else {
		rid = NULL;
		rid_len = 0;
	}
	if (uid_len && !rid_len) {
		/* shortcut */
		ids = uid;
		uid = NULL;
	} else {
		/* concatenate */
		ids = g_malloc (uid_len + rid_len + (rid_len ? 2 : 1));
		if (uid_len)
			strcpy (ids, uid);
		if (rid_len) {
			ids[uid_len] = '\n';
			strcpy (ids + uid_len + 1, rid);
		}
	}
	g_array_append_val (priv->removes, ids);
	g_free (uid);
	g_free (rid);

	g_hash_table_remove (priv->ids, id);

	ensure_pending_flush_timeout (view);
}

static void
notify_complete (EDataCalView *view,
                 const GError *error)
{
	gchar **error_strv;

	send_pending_adds (view);
	send_pending_changes (view);
	send_pending_removes (view);

	error_strv = e_gdbus_templates_encode_error (error);

	e_gdbus_cal_view_emit_complete (view->priv->gdbus_object, (const gchar * const *) error_strv);

	g_strfreev (error_strv);
}

static gpointer
calview_start_thread (gpointer data)
{
	EDataCalView *view = data;

	if (view->priv->started && !view->priv->stopped)
		e_cal_backend_start_view (view->priv->backend, view);
	g_object_unref (view);

	return NULL;
}

static gboolean
impl_DataCalView_start (EGdbusCalView *object,
                        GDBusMethodInvocation *invocation,
                        EDataCalView *view)
{
	EDataCalViewPrivate *priv;

	priv = view->priv;

	if (!priv->started) {
		GThread *thread;

		priv->started = TRUE;
		e_debug_log (FALSE, E_DEBUG_LOG_DOMAIN_CAL_QUERIES, "---;%p;VIEW-START;%s;%s", view, e_data_cal_view_get_text (view), G_OBJECT_TYPE_NAME (priv->backend));

		thread = g_thread_new (NULL, calview_start_thread, g_object_ref (view));
		g_thread_unref (thread);
	}

	e_gdbus_cal_view_complete_start (object, invocation, NULL);

	return TRUE;
}

static gpointer
calview_stop_thread (gpointer data)
{
	EDataCalView *view = data;

	if (view->priv->stopped)
		e_cal_backend_stop_view (view->priv->backend, view);
	g_object_unref (view);

	return NULL;
}

static gboolean
impl_DataCalView_stop (EGdbusCalView *object,
                       GDBusMethodInvocation *invocation,
                       EDataCalView *view)
{
	EDataCalViewPrivate *priv;
	GThread *thread;

	priv = view->priv;

	priv->stopped = TRUE;

	thread = g_thread_new (NULL, calview_stop_thread, g_object_ref (view));
	g_thread_unref (thread);

	e_gdbus_cal_view_complete_stop (object, invocation, NULL);

	return TRUE;
}

static gboolean
impl_DataCalView_setFlags (EGdbusCalView *object,
                           GDBusMethodInvocation *invocation,
                           ECalClientViewFlags flags,
                           EDataCalView *view)
{
	view->priv->flags = flags;

	e_gdbus_cal_view_complete_set_flags (object, invocation, NULL);

	return TRUE;
}

static gboolean
impl_DataCalView_dispose (EGdbusCalView *object,
                          GDBusMethodInvocation *invocation,
                          EDataCalView *view)
{
	e_gdbus_cal_view_complete_dispose (object, invocation, NULL);

	view->priv->stopped = TRUE;
	e_cal_backend_stop_view (view->priv->backend, view);

	g_object_unref (view);

	return TRUE;
}

static gboolean
impl_DataCalView_set_fields_of_interest (EGdbusCalView *object,
                                         GDBusMethodInvocation *invocation,
                                         const gchar * const *in_fields_of_interest,
                                         EDataCalView *view)
{
	EDataCalViewPrivate *priv;
	gint ii;

	g_return_val_if_fail (in_fields_of_interest != NULL, TRUE);

	priv = view->priv;

	if (priv->fields_of_interest)
		g_hash_table_destroy (priv->fields_of_interest);
	priv->fields_of_interest = NULL;

	for (ii = 0; in_fields_of_interest[ii]; ii++) {
		const gchar *field = in_fields_of_interest[ii];

		if (!*field)
			continue;

		if (!priv->fields_of_interest)
			priv->fields_of_interest = g_hash_table_new_full (str_ic_hash, str_ic_equal, g_free, NULL);

		g_hash_table_insert (priv->fields_of_interest, g_strdup (field), GINT_TO_POINTER (1));
	}

	e_gdbus_cal_view_complete_set_fields_of_interest (object, invocation, NULL);

	return TRUE;
}

static void
e_data_cal_view_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	EDataCalView *view;
	EDataCalViewPrivate *priv;

	view = E_DATA_CAL_VIEW (object);
	priv = view->priv;

	switch (property_id) {
	case PROP_BACKEND:
		priv->backend = E_CAL_BACKEND (g_value_dup_object (value));
		break;
	case PROP_SEXP:
		priv->sexp = E_CAL_BACKEND_SEXP (g_value_dup_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
e_data_cal_view_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	EDataCalView *view;
	EDataCalViewPrivate *priv;

	view = E_DATA_CAL_VIEW (object);
	priv = view->priv;

	switch (property_id) {
	case PROP_BACKEND:
		g_value_set_object (value, priv->backend);
		break;
	case PROP_SEXP:
		g_value_set_object (value, priv->sexp);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* Instance init */
static void
e_data_cal_view_init (EDataCalView *view)
{
	view->priv = E_DATA_CAL_VIEW_GET_PRIVATE (view);

	view->priv->flags = E_CAL_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL;

	view->priv->gdbus_object = e_gdbus_cal_view_stub_new ();
	g_signal_connect (
		view->priv->gdbus_object, "handle-start",
		G_CALLBACK (impl_DataCalView_start), view);
	g_signal_connect (
		view->priv->gdbus_object, "handle-stop",
		G_CALLBACK (impl_DataCalView_stop), view);
	g_signal_connect (
		view->priv->gdbus_object, "handle-set-flags",
		G_CALLBACK (impl_DataCalView_setFlags), view);
	g_signal_connect (
		view->priv->gdbus_object, "handle-dispose",
		G_CALLBACK (impl_DataCalView_dispose), view);
	g_signal_connect (
		view->priv->gdbus_object, "handle-set-fields-of-interest",
		G_CALLBACK (impl_DataCalView_set_fields_of_interest), view);

	view->priv->backend = NULL;
	view->priv->started = FALSE;
	view->priv->stopped = FALSE;
	view->priv->complete = FALSE;
	view->priv->sexp = NULL;
	view->priv->fields_of_interest = NULL;

	view->priv->adds = g_array_sized_new (
		TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);
	view->priv->changes = g_array_sized_new (
		TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);
	view->priv->removes = g_array_sized_new (
		TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);

	view->priv->ids = g_hash_table_new_full (
		(GHashFunc) id_hash,
		(GEqualFunc) id_equal,
		(GDestroyNotify) e_cal_component_free_id,
		(GDestroyNotify) NULL);

	view->priv->pending_mutex = g_mutex_new ();
	view->priv->flush_id = 0;
}

static void
e_data_cal_view_dispose (GObject *object)
{
	EDataCalView *view;
	EDataCalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_DATA_CAL_VIEW (object));

	view = E_DATA_CAL_VIEW (object);
	priv = view->priv;

	if (priv->backend) {
		e_cal_backend_remove_view (priv->backend, view);
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	if (priv->sexp) {
		g_object_unref (priv->sexp);
		priv->sexp = NULL;
	}

	g_mutex_lock (priv->pending_mutex);

	if (priv->flush_id) {
		g_source_remove (priv->flush_id);
		priv->flush_id = 0;
	}

	g_mutex_unlock (priv->pending_mutex);

	(* G_OBJECT_CLASS (e_data_cal_view_parent_class)->dispose) (object);
}

static void
e_data_cal_view_finalize (GObject *object)
{
	EDataCalView *view;
	EDataCalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_DATA_CAL_VIEW (object));

	view = E_DATA_CAL_VIEW (object);
	priv = view->priv;

	reset_array (priv->adds);
	reset_array (priv->changes);
	reset_array (priv->removes);

	g_array_free (priv->adds, TRUE);
	g_array_free (priv->changes, TRUE);
	g_array_free (priv->removes, TRUE);

	g_hash_table_destroy (priv->ids);

	if (priv->fields_of_interest)
		g_hash_table_destroy (priv->fields_of_interest);

	g_mutex_free (priv->pending_mutex);

	(* G_OBJECT_CLASS (e_data_cal_view_parent_class)->finalize) (object);
}

/**
 * e_data_cal_view_get_text:
 * @view: A #EDataCalView object.
 *
 * Get the expression used for the given view.
 *
 * Returns: the view expression used to search.
 */
const gchar *
e_data_cal_view_get_text (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	return e_cal_backend_sexp_text (view->priv->sexp);
}

/**
 * e_data_cal_view_get_object_sexp:
 * @view: A view object.
 *
 * Get the #ECalBackendSExp object used for the given view.
 *
 * Returns: The expression object used to search.
 */
ECalBackendSExp *
e_data_cal_view_get_object_sexp (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	return view->priv->sexp;
}

/**
 * e_data_cal_view_object_matches:
 * @view: A view object.
 * @object: Object to match.
 *
 * Compares the given @object to the regular expression used for the
 * given view.
 *
 * Returns: TRUE if the object matches the expression, FALSE if not.
 */
gboolean
e_data_cal_view_object_matches (EDataCalView *view,
                                const gchar *object)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	priv = view->priv;

	return e_cal_backend_sexp_match_object (priv->sexp, object, priv->backend);
}

/**
 * e_data_cal_view_component_matches:
 * @view: A view object.
 * @component: the #ECalComponent object to match.
 *
 * Compares the given @component to the regular expression used for the
 * given view.
 *
 * Returns: TRUE if the object matches the expression, FALSE if not.
 *
 * Since: 3.4
 */
gboolean
e_data_cal_view_component_matches (EDataCalView *view,
                                   ECalComponent *component)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (component), FALSE);

	priv = view->priv;

	return e_cal_backend_sexp_match_comp (priv->sexp, component, priv->backend);
}

/**
 * e_data_cal_view_is_started:
 * @view: A view object.
 *
 * Checks whether the given view has already been started.
 *
 * Returns: TRUE if the view has already been started, FALSE otherwise.
 */
gboolean
e_data_cal_view_is_started (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);

	return view->priv->started;
}

/**
 * e_data_cal_view_is_stopped:
 * @view: A view object.
 *
 * Checks whether the given view has been stopped.
 *
 * Returns: TRUE if the view has been stopped, FALSE otherwise.
 *
 * Since: 2.32
 */
gboolean
e_data_cal_view_is_stopped (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);

	return view->priv->stopped;
}

/**
 * e_data_cal_view_is_completed:
 * @view: A view object.
 *
 * Checks whether the given view is already completed. Being completed means the initial
 * matching of objects have been finished, not that no more notifications about
 * changes will be sent. In fact, even after completed, notifications will still be sent
 * if there are changes in the objects matching the view search expression.
 *
 * Returns: TRUE if the view is completed, FALSE if still in progress.
 *
 * Since: 3.2
 */
gboolean
e_data_cal_view_is_completed (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);

	return view->priv->complete;
}

/**
 * e_data_cal_view_get_fields_of_interest:
 * @view: A view object.
 *
 * Returns: Hash table of field names which the listener is interested in.
 * Backends can return fully populated objects, but the listener advertised
 * that it will use only these. Returns %NULL for all available fields.
 *
 * Note: The data pointer in the hash table has no special meaning, it's
 * only GINT_TO_POINTER(1) for easier checking. Also, field names are
 * compared case insensitively.
 *
 * Since: 3.2
 **/
/* const */ GHashTable *
e_data_cal_view_get_fields_of_interest (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	return view->priv->fields_of_interest;
}

/**
 * e_data_cal_view_get_flags:
 * @view: A view object.
 *
 * Gets the #ECalClientViewFlags that control the behaviour of @view.
 *
 * Returns: the flags for @view.
 *
 * Since: 3.6
 **/
ECalClientViewFlags
e_data_cal_view_get_flags (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), 0);

	return view->priv->flags;
}

static gboolean
filter_component (icalcomponent *icomponent,
                  GHashTable *fields_of_interest,
                  GString *string)
{
	gchar             *str;

	/* RFC 2445 explicitly says that the newline is *ALWAYS* a \r\n (CRLF)!!!! */
	const gchar        newline[] = "\r\n";

	icalcomponent_kind kind;
	const gchar       *kind_string;
	icalproperty      *prop;
	icalcomponent     *icomp;
	gboolean           fail = FALSE;

	g_return_val_if_fail (icomponent != NULL, FALSE);

	/* Open iCalendar string */
	g_string_append (string, "BEGIN:");

	kind = icalcomponent_isa (icomponent);

	/* if (kind != ICAL_X_COMPONENT) { */
	/* 	kind_string  = icalcomponent_kind_to_string (kind); */
	/* } else { */
	/* 	kind_string = icomponent->x_name; */
	/* } */

	kind_string  = icalcomponent_kind_to_string (kind);

	g_string_append (string, kind_string);
	g_string_append (string, newline);

	for (prop = icalcomponent_get_first_property (icomponent, ICAL_ANY_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icomponent, ICAL_ANY_PROPERTY)) {
		const gchar *name;
		gboolean     is_field_of_interest;

		name = icalproperty_get_property_name (prop);

		if (!name) {
			g_warning ("NULL ical property name encountered while serializing component");
			fail = TRUE;
			break;
		}

		is_field_of_interest = GPOINTER_TO_INT (g_hash_table_lookup (fields_of_interest, name));

		/* Append any name that is mentioned in the fields-of-interest */
		if (is_field_of_interest) {
			str = icalproperty_as_ical_string_r (prop);
			g_string_append (string, str);
			g_free (str);
		}
	}

	for (icomp = icalcomponent_get_first_component (icomponent, ICAL_ANY_COMPONENT);
	     fail == FALSE && icomp;
	     icomp = icalcomponent_get_next_component (icomponent, ICAL_ANY_COMPONENT)) {

		if (!filter_component (icomp, fields_of_interest, string)) {
			fail = TRUE;
			break;
		}
	}

	g_string_append (string, "END:");
	g_string_append (string, icalcomponent_kind_to_string (kind));
	g_string_append (string, newline);

	return fail == FALSE;
}

/**
 * e_data_cal_view_get_component_string:
 * @view: A view object.
 * @component: The #ECalComponent to get the string for.
 *
 * This function is similar to e_cal_component_get_as_string() except
 * that it takes into account the fields-of-interest that @view is 
 * configured with and filters out any unneeded fields.
 *
 * Returns: (transfer full): A newly allocated string representation of
 * @component suitable for @view.
 *
 * Since: 3.4
 */
gchar *
e_data_cal_view_get_component_string (EDataCalView *view,
                                      /* const */ ECalComponent *component)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);
	g_return_val_if_fail (component != NULL, NULL);

	if (view->priv->fields_of_interest) {
		GString *string = g_string_new ("");
		icalcomponent *icalcomp = e_cal_component_get_icalcomponent (component);

		if (filter_component (icalcomp, view->priv->fields_of_interest, string))
			return g_string_free (string, FALSE);

		g_string_free (string, TRUE);
	}

	return e_cal_component_get_as_string (component);
}

/**
 * e_data_cal_view_notify_components_added:
 * @view: A view object.
 * @ecalcomponents: List of #ECalComponent-s that have been added.
 *
 * Notifies all view listeners of the addition of a list of components.
 *
 * Like e_data_cal_view_notify_objects_added() except takes a list
 * of #ECalComponent-s instead of ical string representations and uses the 
 * #EDataCalView's fields-of-interest to filter out unwanted information 
 * from ical strings sent over the bus.
 *
 * Since: 3.4
 */
void
e_data_cal_view_notify_components_added (EDataCalView *view,
                                         const GSList *ecalcomponents)
{
	EDataCalViewPrivate *priv;
	const GSList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (ecalcomponents == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = ecalcomponents; l; l = l->next) {
		ECalComponent *comp = l->data;

		g_warn_if_fail (E_IS_CAL_COMPONENT (comp));

		notify_add_component (view, comp);
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_components_added_1:
 * @view: A view object.
 * @component: The #ECalComponent that has been added.
 *
 * Notifies all the view listeners of the addition of a single object.
 *
 * Like e_data_cal_view_notify_objects_added_1() except takes an #ECalComponent
 * instead of an ical string representation and uses the #EDataCalView's
 * fields-of-interest to filter out unwanted information from ical strings
 * sent over the bus.
 *
 * Since: 3.4
 */
void
e_data_cal_view_notify_components_added_1 (EDataCalView *view,
                                           /* const */ ECalComponent *component)
{
	GSList l = {NULL,};

	g_return_if_fail (E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (component != NULL);

	l.data = (gpointer) component;
	e_data_cal_view_notify_components_added (view, &l);
}

/**
 * e_data_cal_view_notify_components_modified:
 * @view: A view object.
 * @ecalcomponents: List of modified #ECalComponent-s.
 *
 * Notifies all view listeners of the modification of a list of components.
 *
 * Like e_data_cal_view_notify_objects_modified() except takes a list of
 * #ECalComponents instead of a ical string representations and uses the
 * #EDataCalView's fields-of-interest to filter out unwanted information
 * from ical strings sent over the bus.
 *
 * Since: 3.4
 */
void
e_data_cal_view_notify_components_modified (EDataCalView *view,
                                            const GSList *ecalcomponents)
{
	EDataCalViewPrivate *priv;
	const GSList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (ecalcomponents == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = ecalcomponents; l; l = l->next) {
		ECalComponent *comp = l->data;

		g_warn_if_fail (E_IS_CAL_COMPONENT (comp));

		notify_change_component (view, comp);
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_components_modified_1:
 * @view: A view object.
 * @component: The modified #ECalComponent.
 *
 * Notifies all view listeners of the modification of @component.
 * 
 * Like e_data_cal_view_notify_objects_modified_1() except takes an
 * #ECalComponent instead of an ical string representation and uses the
 * #EDataCalView's fields-of-interest to filter out unwanted information
 * from ical strings sent over the bus.
 *
 * Since: 3.4
 */
void
e_data_cal_view_notify_components_modified_1 (EDataCalView *view,
                                              /* const */ ECalComponent *component)
{
	GSList l = {NULL,};

	g_return_if_fail (E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (component != NULL);

	l.data = (gpointer) component;
	e_data_cal_view_notify_components_modified (view, &l);
}

/**
 * e_data_cal_view_notify_objects_added:
 * @view: A view object.
 * @objects: List of objects that have been added.
 *
 * Notifies all view listeners of the addition of a list of objects.
 *
 * Deprecated: 3.4: If possible e_data_cal_view_notify_components_added()
 * should be used instead.
 */
void
e_data_cal_view_notify_objects_added (EDataCalView *view,
                                      const GSList *objects)
{
	EDataCalViewPrivate *priv;
	const GSList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (objects == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = objects; l; l = l->next) {
		notify_add (view, e_util_utf8_make_valid (l->data));
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_objects_added_1:
 * @view: A view object.
 * @object: The object that has been added.
 *
 * Notifies all the view listeners of the addition of a single object.
 *
 * Deprecated: 3.4: If possible e_data_cal_view_notify_components_added_1()
 * should be used instead.
 */
void
e_data_cal_view_notify_objects_added_1 (EDataCalView *view,
                                        const gchar *object)
{
	GSList l = {NULL,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (object);

	l.data = (gpointer) object;
	e_data_cal_view_notify_objects_added (view, &l);
}

/**
 * e_data_cal_view_notify_objects_modified:
 * @view: A view object.
 * @objects: List of modified objects.
 *
 * Notifies all view listeners of the modification of a list of objects.
 *
 * Deprecated: 3.4: If possible e_data_cal_view_notify_components_modified()
 * should be used instead.
 */
void
e_data_cal_view_notify_objects_modified (EDataCalView *view,
                                         const GSList *objects)
{
	EDataCalViewPrivate *priv;
	const GSList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (objects == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = objects; l; l = l->next) {
		/* TODO: send add/remove/change as relevant, based on ->ids */
		notify_change (view, e_util_utf8_make_valid (l->data));
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_objects_modified_1:
 * @view: A view object.
 * @object: The modified object.
 *
 * Notifies all view listeners of the modification of a single object.
 *
 * Deprecated: 3.4: If possible e_data_cal_view_notify_components_modified_1()
 * should be used instead.
 */
void
e_data_cal_view_notify_objects_modified_1 (EDataCalView *view,
                                           const gchar *object)
{
	GSList l = {NULL,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (object);

	l.data = (gpointer) object;
	e_data_cal_view_notify_objects_modified (view, &l);
}

/**
 * e_data_cal_view_notify_objects_removed:
 * @view: A view object.
 * @ids: List of IDs for the objects that have been removed.
 *
 * Notifies all view listener of the removal of a list of objects.
 */
void
e_data_cal_view_notify_objects_removed (EDataCalView *view,
                                        const GSList *ids)
{
	EDataCalViewPrivate *priv;
	const GSList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (ids == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = ids; l; l = l->next) {
		ECalComponentId *id = l->data;
		if (g_hash_table_lookup (priv->ids, id))
		    notify_remove (view, id);
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_objects_removed_1:
 * @view: A view object.
 * @id: ID of the removed object.
 *
 * Notifies all view listener of the removal of a single object.
 */
void
e_data_cal_view_notify_objects_removed_1 (EDataCalView *view,
                                          const ECalComponentId *id)
{
	GSList l = {NULL,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (id);

	l.data = (gpointer) id;
	e_data_cal_view_notify_objects_removed (view, &l);
}

/**
 * e_data_cal_view_notify_progress:
 * @view: A view object.
 * @percent: Percentage completed.
 * @message: Progress message to send to listeners.
 *
 * Notifies all view listeners of progress messages.
 */
void
e_data_cal_view_notify_progress (EDataCalView *view,
                                 gint percent,
                                 const gchar *message)
{
	EDataCalViewPrivate *priv;
	gchar *gdbus_message = NULL;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started || priv->stopped)
		return;

	e_gdbus_cal_view_emit_progress (view->priv->gdbus_object, percent, e_util_ensure_gdbus_string (message, &gdbus_message));

	g_free (gdbus_message);
}

/**
 * e_data_cal_view_notify_complete:
 * @view: A view object.
 * @error: View completion error, if any.
 *
 * Notifies all view listeners of the completion of the view, including a
 * status code.
 *
 * Since: 3.2
 **/
void
e_data_cal_view_notify_complete (EDataCalView *view,
                                 const GError *error)
{
	EDataCalViewPrivate *priv;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started || priv->stopped)
		return;

	g_mutex_lock (priv->pending_mutex);

	priv->complete = TRUE;

	notify_complete (view, error);

	g_mutex_unlock (priv->pending_mutex);
}
