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

#include <glib/gi18n-lib.h>

#include <string.h>
#include "e-cal-client.h"
#include "e-cal-client-view.h"
#include "e-cal-client-view-private.h"

#include "libedataserver/e-gdbus-marshallers.h"

#include "e-gdbus-cal-view.h"

G_DEFINE_TYPE (ECalClientView, e_cal_client_view, G_TYPE_OBJECT);

/* Private part of the ECalClientView structure */
struct _ECalClientViewPrivate {
	GDBusProxy *gdbus_calview;
	ECalClient *client;
	gboolean running;
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
	PROGRESS,
	COMPLETE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GSList *
build_object_list (const gchar * const *seq)
{
	GSList *list;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		icalcomponent *comp;

		comp = icalcomponent_new_from_string ((gchar *)seq[i]);
		if (!comp)
			continue;

		list = g_slist_prepend (list, comp);
	}

	return g_slist_reverse (list);
}

static GSList *
build_id_list (const gchar * const *seq)
{
	GSList *list;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		ECalComponentId *id;
		id = g_new (ECalComponentId, 1);
		id->uid = g_strdup (seq[i]);
		id->rid = NULL; /* TODO */
		list = g_slist_prepend (list, id);
	}

	return g_slist_reverse (list);
}

static void
objects_added_cb (EGdbusCalView *gdbus_calview, const gchar * const *objects, ECalClientView *view)
{
	GSList *list;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	if (!view->priv->running)
		return;

	g_object_ref (view);

	list = build_object_list (objects);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_ADDED], 0, list);

	g_slist_foreach (list, (GFunc) icalcomponent_free, NULL);
	g_slist_free (list);

	g_object_unref (view);
}

static void
objects_modified_cb (EGdbusCalView *gdbus_calview, const gchar * const *objects, ECalClientView *view)
{
	GSList *list;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	if (!view->priv->running)
		return;

	g_object_ref (view);

	list = build_object_list (objects);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_MODIFIED], 0, list);

	g_slist_foreach (list, (GFunc) icalcomponent_free, NULL);
	g_slist_free (list);

	g_object_unref (view);
}

static void
objects_removed_cb (EGdbusCalView *gdbus_calview, const gchar * const *uids, ECalClientView *view)
{
	GSList *list;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	if (!view->priv->running)
		return;

	g_object_ref (view);

	list = build_id_list (uids);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_REMOVED], 0, list);

	g_slist_foreach (list, (GFunc) e_cal_component_free_id, NULL);
	g_slist_free (list);

	g_object_unref (view);
}

static void
progress_cb (EGdbusCalView *gdbus_calview, guint percent, const gchar *message, ECalClientView *view)
{
	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	if (!view->priv->running)
		return;

	g_signal_emit (G_OBJECT (view), signals[PROGRESS], 0, percent, message);
}

static void
complete_cb (EGdbusCalView *gdbus_calview, const gchar * const *arg_error, ECalClientView *view)
{
	GError *error = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	if (!view->priv->running)
		return;

	g_return_if_fail (e_gdbus_templates_decode_error (arg_error, &error));

	g_signal_emit (G_OBJECT (view), signals[COMPLETE], 0, error);

	if (error)
		g_error_free (error);
}

/* Object initialization function for the calendar view */
static void
e_cal_client_view_init (ECalClientView *view)
{
	view->priv = G_TYPE_INSTANCE_GET_PRIVATE (view, E_TYPE_CAL_CLIENT_VIEW, ECalClientViewPrivate);
	view->priv->running = FALSE;
}

static void
cal_client_view_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalClientView *view;
	ECalClientViewPrivate *priv;

	view = E_CAL_CLIENT_VIEW (object);
	priv = view->priv;

	switch (property_id) {
	case PROP_VIEW:
		/* gdbus_calview can be set only once */
		g_return_if_fail (priv->gdbus_calview == NULL);

		priv->gdbus_calview = g_object_ref (g_value_get_pointer (value));
		g_signal_connect (priv->gdbus_calview, "objects-added", G_CALLBACK (objects_added_cb), view);
		g_signal_connect (priv->gdbus_calview, "objects-modified", G_CALLBACK (objects_modified_cb), view);
		g_signal_connect (priv->gdbus_calview, "objects-removed", G_CALLBACK (objects_removed_cb), view);
		g_signal_connect (priv->gdbus_calview, "progress", G_CALLBACK (progress_cb), view);
		g_signal_connect (priv->gdbus_calview, "complete", G_CALLBACK (complete_cb), view);
		break;
	case PROP_CLIENT:
		priv->client = E_CAL_CLIENT (g_value_dup_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
cal_client_view_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	ECalClientView *view;
	ECalClientViewPrivate *priv;

	view = E_CAL_CLIENT_VIEW (object);
	priv = view->priv;

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
cal_client_view_finalize (GObject *object)
{
	ECalClientView *view;
	ECalClientViewPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (object));

	view = E_CAL_CLIENT_VIEW (object);
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

	if (priv->client) {
		g_object_unref (priv->client);
		priv->client = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_client_view_parent_class)->finalize (object);
}

/* Class initialization function for the calendar view */
static void
e_cal_client_view_class_init (ECalClientViewClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) klass;

	object_class->set_property = cal_client_view_set_property;
	object_class->get_property = cal_client_view_get_property;
	object_class->finalize = cal_client_view_finalize;

	g_type_class_add_private (klass, sizeof (ECalClientViewPrivate));

	g_object_class_install_property (object_class, PROP_VIEW,
		g_param_spec_pointer ("view", "The GDBus view proxy", NULL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_CLIENT,
		g_param_spec_object ("client", "The e-cal-client for the view", NULL, E_TYPE_CAL_CLIENT,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
        /**
         * ECalClientView::objects-added:
         * @view:: self
         * @objects: (type GSList) (transfer none) (element-type long):
         */
	signals[OBJECTS_ADDED] =
		g_signal_new ("objects-added",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClientViewClass, objects_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
        /**
         * ECalClientView::objects-modified:
         * @view:: self
         * @objects: (type GSList) (transfer none) (element-type long):
         */
	signals[OBJECTS_MODIFIED] =
		g_signal_new ("objects-modified",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClientViewClass, objects_modified),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
        /**
         * ECalClientView::objects-removed:
         * @view:: self
         * @objects: (type GSList) (transfer none) (element-type ECalComponentId):
         */
	signals[OBJECTS_REMOVED] =
		g_signal_new ("objects-removed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClientViewClass, objects_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals[PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClientViewClass, progress),
			      NULL, NULL,
			      e_gdbus_marshallers_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

	signals[COMPLETE] =
		g_signal_new ("complete",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalClientViewClass, complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_ERROR);
}

/**
 * _e_cal_client_view_new:
 * @client: An #ECalClient object.
 * @gdbuc_calview: The GDBus object for the view.
 *
 * Creates a new view object by issuing the view creation request to the
 * calendar server.
 *
 * Returns: A newly-created view object, or NULL if the request failed.
 **/
ECalClientView *
_e_cal_client_view_new (ECalClient *client, EGdbusCalView *gdbus_calview)
{
	ECalClientView *view;

	view = g_object_new (E_TYPE_CAL_CLIENT_VIEW,
		"client", client,
		"view", gdbus_calview,
		NULL);

	return view;
}

/**
 * e_cal_client_view_get_client
 * @view: A #ECalClientView object.
 *
 * Get the #ECalClient associated with this view.
 *
 * Returns: the associated client.
 **/
ECalClient *
e_cal_client_view_get_client (ECalClientView *view)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (view), NULL);

	return view->priv->client;
}

/**
 * e_cal_client_view_is_running:
 * @view: an #ECalClientView
 *
 * Retunrs: Whether view is running. Not running views are ignoring
 * all events sent from the server.
 **/
gboolean
e_cal_client_view_is_running (ECalClientView *view)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT_VIEW (view), FALSE);

	return view->priv->running;
}

/**
 * e_cal_client_view_start:
 * @view: An #ECalClientView object.
 * @error: A #Gerror
 *
 * Starts a live query to the calendar/tasks backend.
 **/
void
e_cal_client_view_start (ECalClientView *view, GError **error)
{
	ECalClientViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	priv = view->priv;

	if (priv->gdbus_calview) {
		GError *local_error = NULL;

		if (e_gdbus_cal_view_call_start_sync (priv->gdbus_calview, NULL, &local_error))
			priv->running = TRUE;

		e_client_util_unwrap_dbus_error (local_error, error, NULL, 0, 0, FALSE);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR, _("Cannot start view, D-Bus proxy gone"));
	}
}

/**
 * e_cal_client_view_stop:
 * @view: An #ECalClientView object.
 * @error: A #GError
 *
 * Stops a live query to the calendar/tasks backend.
 */
void
e_cal_client_view_stop (ECalClientView *view, GError **error)
{
	ECalClientViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	priv = view->priv;
	priv->running = FALSE;

	if (priv->gdbus_calview) {
		GError *local_error = NULL;

		e_gdbus_cal_view_call_stop_sync (priv->gdbus_calview, NULL, &local_error);

		e_client_util_unwrap_dbus_error (local_error, error, NULL, 0, 0, FALSE);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR, _("Cannot stop view, D-Bus proxy gone"));
	}
}

/**
 * e_cal_client_view_set_fields_of_interest:
 * @view: An #ECalClientView object
 * @fields_of_interest: List of field names in which the client is interested
 * @error: A #GError
 *
 * Client can instruct server to which fields it is interested in only, thus
 * the server can return less data over the wire. The server can still return
 * complete objects, this is just a hint to it that the listed fields will
 * be used only. The UID/RID fields are returned always. Initial views has no fields
 * of interest and using %NULL for @fields_of_interest will unset any previous
 * changes.
 *
 * Some backends can use summary information of its cache to create artifical
 * objects, which will omit stored object parsing. If this cannot be done then
 * it will simply return object as is stored in the cache.
 **/
void
e_cal_client_view_set_fields_of_interest (ECalClientView *view, const GSList *fields_of_interest, GError **error)
{
	ECalClientViewPrivate *priv;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT_VIEW (view));

	priv = view->priv;

	if (priv->gdbus_calview) {
		GError *local_error = NULL;
		gchar **strv;

		strv = e_client_util_slist_to_strv (fields_of_interest);
		e_gdbus_cal_view_call_set_fields_of_interest_sync (priv->gdbus_calview, (const gchar * const *) strv, NULL, &local_error);
		g_strfreev (strv);

		e_client_util_unwrap_dbus_error (local_error, error, NULL, 0, 0, FALSE);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR, _("Cannot set fields of interest, D-Bus proxy gone"));
	}
}
