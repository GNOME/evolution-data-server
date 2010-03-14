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

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <glib-object.h>

#include "e-cal-backend-sexp.h"
#include "e-data-cal-view.h"
#include "e-data-cal-marshal.h"

extern DBusGConnection *connection;

static gboolean impl_EDataCalView_start (EDataCalView *query, GError **error);
#include "e-data-cal-view-glue.h"

#define THRESHOLD 32

struct _EDataCalViewPrivate {
	/* The backend we are monitoring */
	ECalBackend *backend;

	gboolean started;
	gboolean done;
	EDataCalCallStatus done_status;

	/* Sexp that defines the query */
	ECalBackendSExp *sexp;

	GArray *adds;
	GArray *changes;
	GArray *removes;

	GHashTable *ids;

	gchar *path;
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

/* Signals */
enum {
  OBJECTS_ADDED,
  OBJECTS_MODIFIED,
  OBJECTS_REMOVED,
  PROGRESS,
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Class init */
static void
e_data_cal_view_class_init (EDataCalViewClass *klass)
{
	GParamSpec *param;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EDataCalViewPrivate));

	object_class->set_property = e_data_cal_view_set_property;
	object_class->get_property = e_data_cal_view_get_property;
	object_class->dispose = e_data_cal_view_dispose;
	object_class->finalize = e_data_cal_view_finalize;

	param =  g_param_spec_object ("backend", NULL, NULL, E_TYPE_CAL_BACKEND,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_BACKEND, param);
	param =  g_param_spec_object ("sexp", NULL, NULL, E_TYPE_CAL_BACKEND_SEXP,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_SEXP, param);

        signals[OBJECTS_ADDED] =
          g_signal_new ("objects-added",
                        G_OBJECT_CLASS_TYPE (klass),
                        G_SIGNAL_RUN_LAST,
                        0, NULL, NULL,
                        g_cclosure_marshal_VOID__BOXED,
                        G_TYPE_NONE, 1, G_TYPE_STRV);

        signals[OBJECTS_MODIFIED] =
          g_signal_new ("objects-modified",
                        G_OBJECT_CLASS_TYPE (klass),
                        G_SIGNAL_RUN_LAST,
                        0, NULL, NULL,
                        g_cclosure_marshal_VOID__BOXED,
                        G_TYPE_NONE, 1, G_TYPE_STRV);

        signals[OBJECTS_REMOVED] =
          g_signal_new ("objects-removed",
                        G_OBJECT_CLASS_TYPE (klass),
                        G_SIGNAL_RUN_LAST,
                        0, NULL, NULL,
                        g_cclosure_marshal_VOID__BOXED,
                        G_TYPE_NONE, 1, G_TYPE_STRV);

        signals[PROGRESS] =
          g_signal_new ("progress",
                        G_OBJECT_CLASS_TYPE (klass),
                        G_SIGNAL_RUN_LAST,
                        0, NULL, NULL,
                        e_data_cal_marshal_VOID__STRING_UINT,
                        G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

        signals[DONE] =
          g_signal_new ("done",
                        G_OBJECT_CLASS_TYPE (klass),
                        G_SIGNAL_RUN_LAST,
                        0, NULL, NULL,
                        g_cclosure_marshal_VOID__UINT,
                        G_TYPE_NONE, 1, G_TYPE_UINT);

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass), &dbus_glib_e_data_cal_view_object_info);
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

/* Instance init */
static void
e_data_cal_view_init (EDataCalView *view)
{
	EDataCalViewPrivate *priv = E_DATA_CAL_VIEW_GET_PRIVATE (view);

	view->priv = priv;

	priv->backend = NULL;
	priv->started = FALSE;
	priv->done = FALSE;
	priv->done_status = Success;
	priv->started = FALSE;
	priv->sexp = NULL;

	priv->adds = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);
	priv->changes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);
	priv->removes = g_array_sized_new (TRUE, TRUE, sizeof (gchar *), THRESHOLD);

	priv->ids = g_hash_table_new_full (id_hash, id_equal, (GDestroyNotify)e_cal_component_free_id, NULL);
}

EDataCalView *
e_data_cal_view_new (ECalBackend *backend,
		     const gchar *path, ECalBackendSExp *sexp)
{
	EDataCalView *query;

	query = g_object_new (E_DATA_CAL_VIEW_TYPE, "backend", backend, "sexp", sexp, NULL);
	query->priv->path = g_strdup (path);

	dbus_g_connection_register_g_object (connection, path, G_OBJECT (query));

	return query;
}

/**
 * e_data_cal_view_get_dbus_path:
 * @view: an #EDataCalView
 *
 * Returns the D-Bus path for @view.
 *
 * Returns: the D-Bus path for @view
 *
 * Since: 2.30
 **/
const gchar *
e_data_cal_view_get_dbus_path (EDataCalView *view)
{
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	return view->priv->path;
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

	g_signal_emit (view, signals[OBJECTS_ADDED], 0, priv->adds->data);
	reset_array (priv->adds);
}

static void
send_pending_changes (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	if (priv->changes->len == 0)
		return;

	g_signal_emit (view, signals[OBJECTS_MODIFIED], 0, priv->changes->data);
	reset_array (priv->changes);
}

static void
send_pending_removes (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	if (priv->removes->len == 0)
		return;

	/* TODO: send ECalComponentIds as a list of pairs */
	g_signal_emit (view, signals[OBJECTS_REMOVED], 0, priv->removes->data);
	reset_array (priv->removes);
}

static void
notify_add (EDataCalView *view, gchar *obj)
{
	EDataCalViewPrivate *priv = view->priv;
	ECalComponent *comp;

	send_pending_changes (view);
	send_pending_removes (view);

	if (priv->adds->len == THRESHOLD) {
		send_pending_adds (view);
	}
	g_array_append_val (priv->adds, obj);

	comp = e_cal_component_new_from_string (obj);
	g_hash_table_insert (priv->ids,
			     e_cal_component_get_id (comp),
			     GUINT_TO_POINTER (1));
	g_object_unref (comp);
}

static void
notify_change (EDataCalView *view, gchar *obj)
{
	EDataCalViewPrivate *priv = view->priv;

	send_pending_adds (view);
	send_pending_removes (view);

	g_array_append_val (priv->changes, obj);
}

static void
notify_remove (EDataCalView *view, ECalComponentId *id)
{
	EDataCalViewPrivate *priv = view->priv;
	gchar *uid;

	send_pending_adds (view);
	send_pending_changes (view);

	/* TODO: store ECalComponentId instead of just uid*/
	uid = g_strdup (id->uid);
	g_array_append_val (priv->removes, uid);

	g_hash_table_remove (priv->ids, id);
}

static void
notify_done (EDataCalView *view)
{
	EDataCalViewPrivate *priv = view->priv;

	send_pending_adds (view);
	send_pending_changes (view);
	send_pending_removes (view);

	g_signal_emit (view, signals[DONE], 0, priv->done_status);
}

static gboolean
impl_EDataCalView_start (EDataCalView *query, GError **error)
{
	EDataCalViewPrivate *priv;

	priv = query->priv;

	if (!priv->started) {
		priv->started = TRUE;
		e_cal_backend_start_query (priv->backend, query);
	}

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
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	if (priv->sexp) {
		g_object_unref (priv->sexp);
		priv->sexp = NULL;
	}

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

	g_free (priv->path);

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
 * e_data_cal_view_get_done_status:
 * @query: A query object.
 *
 * Gets the status code obtained when the initial matching of objects was done
 * for the given query.
 *
 * Returns: The query status.
 */
EDataCalCallStatus
e_data_cal_view_get_done_status (EDataCalView *query)
{
	EDataCalViewPrivate *priv;

	g_return_val_if_fail (IS_QUERY (query), FALSE);

	priv = query->priv;

	if (priv->done)
		return priv->done_status;

	return Success;
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

	for (l = objects; l; l = l->next) {
		notify_add (view, g_strdup (l->data));
	}

	send_pending_adds (view);
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

	for (l = objects; l; l = l->next) {
		/* TODO: send add/remove/change as relevant, based on ->ids */
		notify_change (view, g_strdup (l->data));
	}

	send_pending_changes (view);
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

	for (l = ids; l; l = l->next) {
		ECalComponentId *id = l->data;
		if (g_hash_table_lookup (priv->ids, id))
		    notify_remove (view, id);
	}

	send_pending_removes (view);
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

	if (!priv->started)
		return;

	g_signal_emit (view, signals[PROGRESS], 0, message, percent);
}

/**
 * e_data_cal_view_notify_done:
 * @query: A query object.
 * @status: Query completion status code.
 *
 * Notifies all query listeners of the completion of the query, including a
 * status code.
 */
void
e_data_cal_view_notify_done (EDataCalView *view, GNOME_Evolution_Calendar_CallStatus status)
{
	EDataCalViewPrivate *priv;

	g_return_if_fail (view && E_IS_DATA_CAL_VIEW (view));
	priv = view->priv;

	if (!priv->started)
		return;

	priv->done = TRUE;
	priv->done_status = status;

	notify_done (view);
}
