/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Live search query implementation
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
#include <glib.h>

#include <glib-object.h>
#include <libedataserver/e-debug-log.h>
#include "e-cal-backend-sexp.h"
#include "e-data-cal-view.h"
#include "e-gdbus-egdbuscalview.h"

static void ensure_pending_flush_timeout (EDataCalView *view);

#define THRESHOLD_ITEMS   32	/* how many items can be hold in a cache, before propagated to UI */
#define THRESHOLD_SECONDS  2	/* how long to wait until notifications are propagated to UI; in seconds */

struct _EDataCalViewPrivate {
	EGdbusCalView *gdbus_object;

	/* The backend we are monitoring */
	ECalBackend *backend;

	gboolean started;
	gboolean stopped;
	gboolean done;

	/* Sexp that defines the query */
	ECalBackendSExp *sexp;

	GArray *adds;
	GArray *changes;
	GArray *removes;

	GHashTable *ids;

	GMutex *pending_mutex;
	guint flush_id;
};

G_DEFINE_TYPE (EDataCalView, e_data_cal_view, G_TYPE_OBJECT);
#define E_DATA_CAL_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_DATA_CAL_VIEW_TYPE, EDataCalViewPrivate))

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
e_data_cal_view_class_init (EDataCalViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EDataCalViewPrivate));

	object_class->set_property = e_data_cal_view_set_property;
	object_class->get_property = e_data_cal_view_get_property;
	object_class->dispose = e_data_cal_view_dispose;
	object_class->finalize = e_data_cal_view_finalize;

	g_object_class_install_property (object_class, PROP_BACKEND,
		g_param_spec_object (
			"backend", NULL, NULL, E_TYPE_CAL_BACKEND,
			G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_SEXP,
		g_param_spec_object (
			"sexp", NULL, NULL, E_TYPE_CAL_BACKEND_SEXP,
			G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

static guint
id_hash (gconstpointer key)
{
	const ECalComponentId *id = key;
	return g_str_hash (id->uid) ^ (id->rid ? g_str_hash (id->rid) : 0);
}

static gboolean
id_equal (gconstpointer a, gconstpointer b)
{
	const ECalComponentId *id_a = a, *id_b = b;
	return g_strcmp0 (id_a->uid, id_b->uid) == 0 && g_strcmp0 (id_a->rid, id_b->rid) == 0;
}

EDataCalView *
e_data_cal_view_new (ECalBackend *backend, ECalBackendSExp *sexp)
{
	EDataCalView *query;

	query = g_object_new (E_DATA_CAL_VIEW_TYPE, "backend", backend, "sexp", sexp, NULL);

	return query;
}

/**
 * e_data_cal_view_register_gdbus_object:
 *
 * Since: 2.32
 **/
guint
e_data_cal_view_register_gdbus_object (EDataCalView *query, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	g_return_val_if_fail (query != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (query), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_cal_view_register_object (query->priv->gdbus_object, connection, object_path, error);
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

	priv->flush_id = g_timeout_add (e_data_cal_view_is_done (view) ? 10 : (THRESHOLD_SECONDS * 1000), pending_flush_timeout_cb, view);
}

static void
notify_add (EDataCalView *view, gchar *obj)
{
	EDataCalViewPrivate *priv = view->priv;
	ECalComponent *comp;

	send_pending_changes (view);
	send_pending_removes (view);

	if (priv->adds->len == THRESHOLD_ITEMS) {
		send_pending_adds (view);
	}
	g_array_append_val (priv->adds, obj);

	comp = e_cal_component_new_from_string (obj);
	g_hash_table_insert (priv->ids,
			     e_cal_component_get_id (comp),
			     GUINT_TO_POINTER (1));
	g_object_unref (comp);

	ensure_pending_flush_timeout (view);
}

static void
notify_change (EDataCalView *view, gchar *obj)
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
notify_remove (EDataCalView *view, ECalComponentId *id)
{
	EDataCalViewPrivate *priv = view->priv;
	gchar *ids;
	size_t uid_len, rid_len;

	send_pending_adds (view);
	send_pending_changes (view);

	if (priv->removes->len == THRESHOLD_ITEMS) {
		send_pending_removes (view);
	}

	/* store ECalComponentId as <uid>[\n<rid>] (matches D-Bus API) */
	uid_len = id->uid ? strlen (id->uid) : 0;
	rid_len = id->rid ? strlen (id->rid) : 0;
	ids = g_malloc (uid_len + rid_len + (rid_len ? 2 : 1));
	if (uid_len)
		strcpy (ids, id->uid);
	if (rid_len) {
		ids[uid_len] = '\n';
		strcpy (ids + uid_len + 1, id->rid);
	}
	g_array_append_val (priv->removes, ids);

	g_hash_table_remove (priv->ids, id);

	ensure_pending_flush_timeout (view);
}

static void
notify_done (EDataCalView *view, const GError *error)
{
	send_pending_adds (view);
	send_pending_changes (view);
	send_pending_removes (view);

	e_gdbus_cal_view_emit_done (view->priv->gdbus_object, error ? error->code : 0, error ? error->message : "");
}

static gboolean
impl_DataCalView_start (EGdbusCalView *object, GDBusMethodInvocation *invocation, EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	priv = query->priv;

	if (!priv->started) {
		priv->started = TRUE;
		e_debug_log(FALSE, E_DEBUG_LOG_DOMAIN_CAL_QUERIES, "---;%p;QUERY-START;%s;%s", query, e_data_cal_view_get_text (query), G_OBJECT_TYPE_NAME(priv->backend));
		e_cal_backend_start_query (priv->backend, query);
	}

	e_gdbus_cal_view_complete_start (object, invocation);

	return TRUE;
}

static gboolean
impl_DataCalView_stop (EGdbusCalView *object, GDBusMethodInvocation *invocation, EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	priv = query->priv;

	priv->stopped = TRUE;

	e_gdbus_cal_view_complete_stop (object, invocation);

	return TRUE;
}

static gboolean
impl_DataCalView_dispose (EGdbusCalView *object, GDBusMethodInvocation *invocation, EDataCalView *query)
{
	e_gdbus_cal_view_complete_dispose (object, invocation);

	g_object_unref (query);

	return TRUE;
}

static void
e_data_cal_view_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	query = QUERY (object);
	priv = query->priv;

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
e_data_cal_view_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	query = QUERY (object);
	priv = query->priv;

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
e_data_cal_view_init (EDataCalView *query)
{
	EDataCalViewPrivate *priv = E_DATA_CAL_VIEW_GET_PRIVATE (query);

	query->priv = priv;

	priv->gdbus_object = e_gdbus_cal_view_stub_new ();
	g_signal_connect (priv->gdbus_object, "handle-start", G_CALLBACK (impl_DataCalView_start), query);
	g_signal_connect (priv->gdbus_object, "handle-stop", G_CALLBACK (impl_DataCalView_stop), query);
	g_signal_connect (priv->gdbus_object, "handle-dispose", G_CALLBACK (impl_DataCalView_dispose), query);

	priv->backend = NULL;
	priv->started = FALSE;
	priv->stopped = FALSE;
	priv->done = FALSE;
	priv->sexp = NULL;

	priv->adds = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);
	priv->changes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);
	priv->removes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);

	priv->ids = g_hash_table_new_full (id_hash, id_equal, (GDestroyNotify)e_cal_component_free_id, NULL);

	priv->pending_mutex = g_mutex_new ();
	priv->flush_id = 0;
}

static void
e_data_cal_view_dispose (GObject *object)
{
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_QUERY (object));

	query = QUERY (object);
	priv = query->priv;

	if (priv->backend) {
		e_cal_backend_remove_query (priv->backend, query);
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
	EDataCalView *query;
	EDataCalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_QUERY (object));

	query = QUERY (object);
	priv = query->priv;

	g_hash_table_destroy (priv->ids);

	g_mutex_free (priv->pending_mutex);

	(* G_OBJECT_CLASS (e_data_cal_view_parent_class)->finalize) (object);
}

/**
 * e_data_cal_view_get_text:
 * @query: A #EDataCalView object.
 *
 * Get the expression used for the given query.
 *
 * Returns: the query expression used to search.
 */
const gchar *
e_data_cal_view_get_text (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);

	return e_cal_backend_sexp_text (query->priv->sexp);
}

/**
 * e_data_cal_view_get_object_sexp:
 * @query: A query object.
 *
 * Get the #ECalBackendSExp object used for the given query.
 *
 * Returns: The expression object used to search.
 */
ECalBackendSExp *
e_data_cal_view_get_object_sexp (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);

	return query->priv->sexp;
}

/**
 * e_data_cal_view_object_matches:
 * @query: A query object.
 * @object: Object to match.
 *
 * Compares the given @object to the regular expression used for the
 * given query.
 *
 * Returns: TRUE if the object matches the expression, FALSE if not.
 */
gboolean
e_data_cal_view_object_matches (EDataCalView *query, const gchar *object)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (IS_QUERY (query), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	priv = query->priv;

	return e_cal_backend_sexp_match_object (priv->sexp, object, priv->backend);
}

/**
 * e_data_cal_view_get_matched_objects:
 * @query: A query object.
 *
 * Gets the list of objects already matched for the given query.
 *
 * Returns: A list of matched objects.
 */
GList *
e_data_cal_view_get_matched_objects (EDataCalView *query)
{
	g_return_val_if_fail (IS_QUERY (query), NULL);
	/* TODO e_data_cal_view_get_matched_objects */
	return NULL;
}

/**
 * e_data_cal_view_is_started:
 * @query: A query object.
 *
 * Checks whether the given query has already been started.
 *
 * Returns: TRUE if the query has already been started, FALSE otherwise.
 */
gboolean
e_data_cal_view_is_started (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), FALSE);

	return view->priv->started;
}

/**
 * e_data_cal_view_is_stopped:
 * @query: A query object.
 *
 * Checks whether the given query has been stopped.
 *
 * Returns: TRUE if the query has been stopped, FALSE otherwise.
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
 * e_data_cal_view_is_done:
 * @query: A query object.
 *
 * Checks whether the given query is already done. Being done means the initial
 * matching of objects have been finished, not that no more notifications about
 * changes will be sent. In fact, even after done, notifications will still be sent
 * if there are changes in the objects matching the query search expression.
 *
 * Returns: TRUE if the query is done, FALSE if still in progress.
 */
gboolean
e_data_cal_view_is_done (EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (IS_QUERY (query), FALSE);

	priv = query->priv;

	return priv->done;
}

/**
 * e_data_cal_view_notify_objects_added:
 * @query: A query object.
 * @objects: List of objects that have been added.
 *
 * Notifies all query listeners of the addition of a list of objects.
 */
void
e_data_cal_view_notify_objects_added (EDataCalView *view, const GList *objects)
{
	EDataCalViewPrivate *priv;
	const GList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (objects == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = objects; l; l = l->next) {
		notify_add (view, g_strdup (l->data));
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_objects_added_1:
 * @query: A query object.
 * @object: The object that has been added.
 *
 * Notifies all the query listeners of the addition of a single object.
 */
void
e_data_cal_view_notify_objects_added_1 (EDataCalView *view, const gchar *object)
{
	GList l = {0,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (object);

	l.data = (gpointer)object;
	e_data_cal_view_notify_objects_added (view, &l);
}

/**
 * e_data_cal_view_notify_objects_modified:
 * @query: A query object.
 * @objects: List of modified objects.
 *
 * Notifies all query listeners of the modification of a list of objects.
 */
void
e_data_cal_view_notify_objects_modified (EDataCalView *view, const GList *objects)
{
	EDataCalViewPrivate *priv;
	const GList *l;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (objects == NULL)
		return;

	g_mutex_lock (priv->pending_mutex);

	for (l = objects; l; l = l->next) {
		/* TODO: send add/remove/change as relevant, based on ->ids */
		notify_change (view, g_strdup (l->data));
	}

	g_mutex_unlock (priv->pending_mutex);
}

/**
 * e_data_cal_view_notify_objects_modified_1:
 * @query: A query object.
 * @object: The modified object.
 *
 * Notifies all query listeners of the modification of a single object.
 */
void
e_data_cal_view_notify_objects_modified_1 (EDataCalView *view, const gchar *object)
{
	GList l = {0,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (object);

	l.data = (gpointer)object;
	e_data_cal_view_notify_objects_modified (view, &l);
}

/**
 * e_data_cal_view_notify_objects_removed:
 * @query: A query object.
 * @ids: List of IDs for the objects that have been removed.
 *
 * Notifies all query listener of the removal of a list of objects.
 */
void
e_data_cal_view_notify_objects_removed (EDataCalView *view, const GList *ids)
{
	EDataCalViewPrivate *priv;
	const GList *l;

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
 * @query: A query object.
 * @id: ID of the removed object.
 *
 * Notifies all query listener of the removal of a single object.
 */
void
e_data_cal_view_notify_objects_removed_1 (EDataCalView *view, const ECalComponentId *id)
{
	GList l = {0,};

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	g_return_if_fail (id);

	l.data = (gpointer)id;
	e_data_cal_view_notify_objects_removed (view, &l);
}

/**
 * e_data_cal_view_notify_progress:
 * @query: A query object.
 * @message: Progress message to send to listeners.
 * @percent: Percentage completed.
 *
 * Notifies all query listeners of progress messages.
 */
void
e_data_cal_view_notify_progress (EDataCalView *view, const gchar *message, gint percent)
{
	EDataCalViewPrivate *priv;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started || priv->stopped)
		return;

	e_gdbus_cal_view_emit_progress (view->priv->gdbus_object, message ? message : "", percent);
}

/**
 * e_data_cal_view_notify_done:
 * @query: A query object.
 * @error: Query completion error, if any.
 *
 * Notifies all query listeners of the completion of the query, including a
 * status code.
 */
void
e_data_cal_view_notify_done (EDataCalView *view, const GError *error)
{
	EDataCalViewPrivate *priv;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started || priv->stopped)
		return;

	g_mutex_lock (priv->pending_mutex);

	priv->done = TRUE;

	notify_done (view, error);

	g_mutex_unlock (priv->pending_mutex);
}
