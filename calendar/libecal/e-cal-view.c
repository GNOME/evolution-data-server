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
#include "e-data-cal-view-bindings.h"

G_DEFINE_TYPE(ECalView, e_cal_view, G_TYPE_OBJECT);
#define E_CAL_VIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_CAL_VIEW, ECalViewPrivate))

#define LOCK_VIEW()   g_static_rec_mutex_lock (priv->view_proxy_lock)
#define UNLOCK_VIEW() g_static_rec_mutex_unlock (priv->view_proxy_lock)

/* Private part of the ECalView structure */
struct _ECalViewPrivate {
	DBusGProxy *view_proxy;
	GStaticRecMutex *view_proxy_lock;
	ECal *client;
};

/* Property IDs */
enum props {
	PROP_0,
	PROP_VIEW,
	PROP_VIEW_LOCK,
	PROP_CLIENT
};

/* Signal IDs */
enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	VIEW_PROGRESS,
	VIEW_DONE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GObjectClass *parent_class;

static GList *
build_object_list (const gchar **seq)
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

	return list;
}

static GList *
build_id_list (const gchar **seq)
{
	GList *list;
	gint i;

	list = NULL;
	for (i = 0; seq[i]; i++) {
		ECalComponentId *id;
		id = g_new (ECalComponentId, 1);
		id->uid = g_strdup (seq[i]);
		id->rid = NULL; /* TODO */
		list = g_list_prepend (list, id);
	}
	return list;
}

static void
objects_added_cb (DBusGProxy *proxy, const gchar **objects, gpointer data)
{
	ECalView *view;
        GList *list;

	view = E_CAL_VIEW (data);
	g_object_ref (view);

	list = build_object_list (objects);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_ADDED], 0, list);

        while (list) {
          icalcomponent_free (list->data);
          list = g_list_delete_link (list, list);
        }

	g_object_unref (view);
}

static void
objects_modified_cb (DBusGProxy *proxy, const gchar **objects, gpointer data)
{
	ECalView *view;
        GList *list;

	view = E_CAL_VIEW (data);
	g_object_ref (view);

	list = build_object_list (objects);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_MODIFIED], 0, list);

        while (list) {
          icalcomponent_free (list->data);
          list = g_list_delete_link (list, list);
        }

	g_object_unref (view);
}

static void
objects_removed_cb (DBusGProxy *proxy, const gchar **uids, gpointer data)
{
	ECalView *view;
        GList *list;

	view = E_CAL_VIEW (data);
	g_object_ref (view);

	list = build_id_list (uids);

	g_signal_emit (G_OBJECT (view), signals[OBJECTS_REMOVED], 0, list);

        while (list) {
		e_cal_component_free_id (list->data);
		list = g_list_delete_link (list, list);
        }

	g_object_unref (view);
}

static void
progress_cb (DBusGProxy *proxy, const gchar *message, gint percent, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_signal_emit (G_OBJECT (view), signals[VIEW_PROGRESS], 0, message, percent);
}

static void
done_cb (DBusGProxy *proxy, ECalendarStatus status, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_signal_emit (G_OBJECT (view), signals[VIEW_DONE], 0, status);
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
		if (priv->view_proxy != NULL)
			g_object_unref (priv->view_proxy);

		priv->view_proxy = g_object_ref (g_value_get_pointer (value));

                dbus_g_proxy_add_signal (priv->view_proxy, "ObjectsAdded", G_TYPE_STRV, G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (priv->view_proxy, "ObjectsAdded", G_CALLBACK (objects_added_cb), view, NULL);

                dbus_g_proxy_add_signal (priv->view_proxy, "ObjectsModified", G_TYPE_STRV, G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (priv->view_proxy, "ObjectsModified", G_CALLBACK (objects_modified_cb), view, NULL);

                dbus_g_proxy_add_signal (priv->view_proxy, "ObjectsRemoved", G_TYPE_STRV, G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (priv->view_proxy, "ObjectsRemoved", G_CALLBACK (objects_removed_cb), view, NULL);

                dbus_g_object_register_marshaller (e_cal_marshal_VOID__STRING_UINT, G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
                dbus_g_proxy_add_signal (priv->view_proxy, "Progress", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (priv->view_proxy, "Progress", G_CALLBACK (progress_cb), view, NULL);

                dbus_g_proxy_add_signal (priv->view_proxy, "Done", G_TYPE_UINT, G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (priv->view_proxy, "Done", G_CALLBACK (done_cb), view, NULL);

		break;
	case PROP_VIEW_LOCK:
		priv->view_proxy_lock = g_value_get_pointer (value);
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
		g_value_set_pointer (value, priv->view_proxy);
		break;
	case PROP_VIEW_LOCK:
		g_value_set_pointer (value, priv->view_proxy_lock);
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

	if (priv->view_proxy != NULL)
		g_object_unref (priv->view_proxy);

	g_object_unref (priv->client);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Class initialization function for the calendar view */
static void
e_cal_view_class_init (ECalViewClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = e_cal_view_set_property;
	object_class->get_property = e_cal_view_get_property;
	object_class->finalize = e_cal_view_finalize;

	g_type_class_add_private (klass, sizeof (ECalViewPrivate));

	g_object_class_install_property (object_class, PROP_VIEW,
		g_param_spec_pointer ("view", "The DBus view proxy", NULL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_VIEW_LOCK,
		g_param_spec_pointer ("view-lock", "The DBus view proxy lock", NULL,
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
	signals[VIEW_DONE] =
		g_signal_new ("view_done",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewClass, view_done),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
}

/**
 * _e_cal_view_new:
 * @corba_view: The CORBA object for the view.
 * @client: An #ECal object.
 *
 * Creates a new view object by issuing the view creation request to the
 * calendar server.
 *
 * Return value: A newly-created view object, or NULL if the request failed.
 **/
ECalView *
_e_cal_view_new (ECal *client, DBusGProxy *view_proxy, GStaticRecMutex *connection_lock)
{
	ECalView *view;

	view = g_object_new (E_TYPE_CAL_VIEW,
		"client", client,
		"view", view_proxy,
		"view-lock", connection_lock,
		NULL);

	return view;
}

/**
 * e_cal_view_get_client
 * @view: A #ECalView object.
 *
 * Get the #ECal associated with this view.
 *
 * Return value: the associated client.
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
 */
void
e_cal_view_start (ECalView *view)
{
	ECalViewPrivate *priv;
	GError *error = NULL;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_VIEW (view));

	priv = E_CAL_VIEW_GET_PRIVATE(view);

	LOCK_VIEW ();
	if (!org_gnome_evolution_dataserver_calendar_CalView_start (priv->view_proxy, &error)) {
		UNLOCK_VIEW ();
		g_printerr("%s: %s\n", G_STRFUNC, error->message);
		g_error_free (error);
		g_warning (G_STRLOC ": Unable to start view");
		return;
	}
	UNLOCK_VIEW ();
}
