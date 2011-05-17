/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Live view client object
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
#include "e-cal-marshal.h"
#include "e-cal.h"
#include "e-cal-view.h"
#include "e-cal-view-private.h"
#include "e-gdbus-egdbuscalview.h"

G_DEFINE_TYPE(ECalView, e_cal_view, G_TYPE_OBJECT);
#define E_CAL_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_CAL_VIEW, ECalViewPrivate))

/* Private part of the ECalView structure */
struct _ECalViewPrivate {
	EGdbusCalView *gdbus_calview;
	ECal *client;
};

/* Property IDs */
enum props {
	PROP_0,
	PROP_VIEW,
	PROP_CLIENT
};

/* Signal IDs */
enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	VIEW_PROGRESS,
	#ifndef E_CAL_DISABLE_DEPRECATED
	VIEW_DONE,
	#endif
	VIEW_COMPLETE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GList *
build_object_list (const gchar * const *seq)
{
	GList *list;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		icalcomponent *comp;

		comp = icalcomponent_new_from_string ((gchar *)seq[i]);
		if (!comp)
			continue;

		list = g_list_prepend (list, comp);
	}

	return g_list_reverse (list);
}

static GList *
build_id_list (const gchar * const *seq)
{
	GList *list;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		ECalComponentId *id;
		const gchar * eol;

		id = g_new (ECalComponentId, 1);
		/* match encoding as in notify_remove() in e-data-cal-view.c: <uid>[\n<rid>] */
		eol = strchr (seq[i], '\n');
		if (eol) {
			id->uid = g_strndup (seq[i], eol - seq[i]);
			id->rid = g_strdup (eol + 1);
		} else {
			id->uid = g_strdup (seq[i]);
			id->rid = NULL;
		}
		list = g_list_prepend (list, id);
	}

	return g_list_reverse (list);
}

static void
objects_added_cb (EGdbusCalView *gdbus_calview, const gchar * const *objects, ECalView *view)
{
        GList *list;

	g_return_if_fail (E_IS_CAL_VIEW (view));
	g_object_ref (view);

	list = build_object_list (objects);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_ADDED], 0, list);

	g_list_foreach (list, (GFunc) icalcomponent_free, NULL);
	g_list_free (list);

	g_object_unref (view);
}

static void
objects_modified_cb (EGdbusCalView *gdbus_calview, const gchar * const *objects, ECalView *view)
{
        GList *list;

	g_return_if_fail (E_IS_CAL_VIEW (view));
	g_object_ref (view);

	list = build_object_list (objects);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_MODIFIED], 0, list);

	g_list_foreach (list, (GFunc) icalcomponent_free, NULL);
	g_list_free (list);

	g_object_unref (view);
}

static void
objects_removed_cb (EGdbusCalView *gdbus_calview, const gchar * const *seq, ECalView *view)
{
        GList *list;

	g_return_if_fail (E_IS_CAL_VIEW (view));
	g_object_ref (view);

	list = build_id_list (seq);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_REMOVED], 0, list);

	g_list_foreach (list, (GFunc) e_cal_component_free_id, NULL);
	g_list_free (list);

	g_object_unref (view);
}

static void
progress_cb (EGdbusCalView *gdbus_calview, const gchar *message, guint percent, ECalView *view)
{
	g_return_if_fail (E_IS_CAL_VIEW (view));

	g_signal_emit (G_OBJECT (view), signals[VIEW_PROGRESS], 0, message, percent);
}

static void
done_cb (EGdbusCalView *gdbus_calview, /* ECalendarStatus */ guint status, const gchar *message, ECalView *view)
{
	g_return_if_fail (E_IS_CAL_VIEW (view));

	#ifndef E_CAL_DISABLE_DEPRECATED
	g_signal_emit (G_OBJECT (view), signals[VIEW_DONE], 0, status);
	#endif

	g_signal_emit (G_OBJECT (view), signals[VIEW_COMPLETE], 0, status, message);
}

/* Object initialization function for the calendar view */
static void
e_cal_view_init (ECalView *view)
{
	view->priv = E_CAL_VIEW_GET_PRIVATE (view);
}

static void
e_cal_view_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalView *view;
	ECalViewPrivate *priv;

	view = E_CAL_VIEW (object);
	priv = E_CAL_VIEW_GET_PRIVATE(view);

	switch (property_id) {
	case PROP_VIEW:
		/* gdbus_calview can be set only once */
		g_return_if_fail (priv->gdbus_calview == NULL);

		priv->gdbus_calview = g_object_ref (g_value_get_pointer (value));
		g_signal_connect (priv->gdbus_calview, "objects-added", G_CALLBACK (objects_added_cb), view);
		g_signal_connect (priv->gdbus_calview, "objects-modified", G_CALLBACK (objects_modified_cb), view);
		g_signal_connect (priv->gdbus_calview, "objects-removed", G_CALLBACK (objects_removed_cb), view);
		g_signal_connect (priv->gdbus_calview, "progress", G_CALLBACK (progress_cb), view);
		g_signal_connect (priv->gdbus_calview, "done", G_CALLBACK (done_cb), view);
		break;
	case PROP_CLIENT:
		priv->client = E_CAL (g_value_dup_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
e_cal_view_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	ECalView *view;
	ECalViewPrivate *priv;

	view = E_CAL_VIEW (object);
	priv = E_CAL_VIEW_GET_PRIVATE(view);

	switch (property_id) {
	case PROP_VIEW:
		g_value_set_pointer (value, priv->gdbus_calview);
		break;
	case PROP_CLIENT:
		g_value_set_object (value, priv->client);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* Finalize handler for the calendar view */
static void
e_cal_view_finalize (GObject *object)
{
	ECalView *view;
	ECalViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_VIEW (object));

	view = E_CAL_VIEW (object);
	priv = view->priv;

	if (priv->gdbus_calview != NULL) {
		GError *error = NULL;

		g_signal_handlers_disconnect_matched (priv->gdbus_calview, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, view);
		e_gdbus_cal_view_call_dispose_sync (priv->gdbus_calview, NULL, &error);
		g_object_unref (priv->gdbus_calview);
		priv->gdbus_calview = NULL;

		if (error) {
			g_warning ("Failed to dispose cal view: %s", error->message);
			g_error_free (error);
		}
	}

	g_object_unref (priv->client);

	if (G_OBJECT_CLASS (e_cal_view_parent_class)->finalize)
		(* G_OBJECT_CLASS (e_cal_view_parent_class)->finalize) (object);
}

/* Class initialization function for the calendar view */
static void
e_cal_view_class_init (ECalViewClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) klass;

	object_class->set_property = e_cal_view_set_property;
	object_class->get_property = e_cal_view_get_property;
	object_class->finalize = e_cal_view_finalize;

	g_type_class_add_private (klass, sizeof (ECalViewPrivate));

	g_object_class_install_property (object_class, PROP_VIEW,
		g_param_spec_pointer ("view", "The GDBus view proxy", NULL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_CLIENT,
		g_param_spec_object ("client", "The e-cal for the view", NULL, E_TYPE_CAL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	signals[OBJECTS_ADDED] =
		g_signal_new ("objects_added",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, objects_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[OBJECTS_MODIFIED] =
		g_signal_new ("objects_modified",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, objects_modified),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[OBJECTS_REMOVED] =
		g_signal_new ("objects_removed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, objects_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[VIEW_PROGRESS] =
		g_signal_new ("view_progress",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, view_progress),
			      NULL, NULL,
			      e_cal_marshal_VOID__STRING_UINT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

	#ifndef E_CAL_DISABLE_DEPRECATED
	signals[VIEW_DONE] =
		g_signal_new ("view_done",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, view_done),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	#endif

	signals[VIEW_COMPLETE] =
		g_signal_new ("view_complete",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, view_complete),
			      NULL, NULL,
			      e_cal_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

/**
 * _e_cal_view_new:
 * @client: An #ECal object.
 * @gdbuc_calview: The GDBus object for the view.
 *
 * Creates a new view object by issuing the view creation request to the
 * calendar server.
 *
 * Returns: A newly-created view object, or NULL if the request failed.
 **/
ECalView *
_e_cal_view_new (ECal *client, EGdbusCalView *gdbus_calview)
{
	ECalView *view;

	view = g_object_new (E_TYPE_CAL_VIEW,
		"client", client,
		"view", gdbus_calview,
		NULL);

	return view;
}

/**
 * e_cal_view_get_client
 * @view: A #ECalView object.
 *
 * Get the #ECal associated with this view.
 *
 * Returns: the associated client.
 *
 * Since: 2.22
 */
ECal *
e_cal_view_get_client (ECalView *view)
{
	g_return_val_if_fail (E_IS_CAL_VIEW (view), NULL);

	return view->priv->client;
}

/**
 * e_cal_view_start:
 * @view: A #ECalView object.
 *
 * Starts a live query to the calendar/tasks backend.
 *
 * Since: 2.22
 */
void
e_cal_view_start (ECalView *view)
{
	GError *error = NULL;
	ECalViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_VIEW (view));

	priv = E_CAL_VIEW_GET_PRIVATE(view);

	if (priv->gdbus_calview) {
		e_gdbus_cal_view_call_start_sync (priv->gdbus_calview, NULL, &error);
	}

	if (error) {
		g_warning ("Cannot start cal view: %s\n", error->message);

		/* Fake a sequence-complete so that the application knows this failed */
		#ifndef E_CAL_DISABLE_DEPRECATED
		g_signal_emit (view, signals[VIEW_DONE], 0, E_CALENDAR_STATUS_DBUS_EXCEPTION);
		#endif
		g_signal_emit (view, signals[VIEW_COMPLETE], 0, E_CALENDAR_STATUS_DBUS_EXCEPTION, error->message);

		g_error_free (error);
	}
}

/**
 * e_cal_view_stop:
 * @view: A #ECalView object.
 *
 * Stops a live query to the calendar/tasks backend.
 *
 * Since: 2.32
 */
void
e_cal_view_stop (ECalView *view)
{
	GError *error = NULL;
	ECalViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_VIEW (view));

	priv = E_CAL_VIEW_GET_PRIVATE(view);

	if (priv->gdbus_calview) {
		e_gdbus_cal_view_call_stop_sync (priv->gdbus_calview, NULL, &error);
	}

	if (error) {
		g_warning ("Failed to stop view: %s", error->message);
		g_error_free (error);
	}
}
