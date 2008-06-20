/* Evolution calendar - Live view client object
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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
#include <bonobo/bonobo-exception.h>
#include "e-cal-marshal.h"
#include "e-cal.h"
#include "e-cal-view.h"
#include "e-cal-view-listener.h"
#include "e-cal-view-private.h"



/* Private part of the ECalView structure */
struct _ECalViewPrivate {
	/* Handle to the view in the server */
	GNOME_Evolution_Calendar_CalView view;

	/* Our view listener implementation */
	ECalViewListener *listener;

	/* The CalClient associated with this view */
	ECal *client;
};

/* Property IDs */
enum props {
	PROP_0,
	PROP_VIEW,
	PROP_LISTENER,
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



static void
objects_added_cb (ECalViewListener *listener, GList *objects, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_object_ref (view);
	g_signal_emit (G_OBJECT (view), signals[OBJECTS_ADDED], 0, objects);
	g_object_unref (view);
}

static void
objects_modified_cb (ECalViewListener *listener, GList *objects, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_object_ref (view);
	g_signal_emit (G_OBJECT (view), signals[OBJECTS_MODIFIED], 0, objects);
	g_object_unref (view);
}

static void
objects_removed_cb (ECalViewListener *listener, GList *ids, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_object_ref (view);
	g_signal_emit (G_OBJECT (view), signals[OBJECTS_REMOVED], 0, ids);
	g_object_unref (view);
}

static void
view_progress_cb (ECalViewListener *listener, const char *message, int percent, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_signal_emit (G_OBJECT (view), signals[VIEW_PROGRESS], 0, message, percent);
}

static void
view_done_cb (ECalViewListener *listener, ECalendarStatus status, gpointer data)
{
	ECalView *view;

	view = E_CAL_VIEW (data);

	g_signal_emit (G_OBJECT (view), signals[VIEW_DONE], 0, status);
}

/* Object initialization function for the calendar view */
static void
e_cal_view_init (ECalView *view, ECalViewClass *klass)
{
	ECalViewPrivate *priv;

	priv = g_new0 (ECalViewPrivate, 1);
	view->priv = priv;

	priv->listener = NULL;
	priv->view = CORBA_OBJECT_NIL;
}

static void
e_cal_view_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalView *view;
	ECalViewPrivate *priv;

	view = E_CAL_VIEW (object);
	priv = view->priv;

	switch (property_id) {
	case PROP_VIEW:
		if (priv->view != CORBA_OBJECT_NIL)
			bonobo_object_release_unref (priv->view, NULL);

		priv->view = bonobo_object_dup_ref (g_value_get_pointer (value), NULL);
		break;
	case PROP_LISTENER:
		if (priv->listener) {
			g_signal_handlers_disconnect_matched (priv->listener, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, view);
			bonobo_object_unref (BONOBO_OBJECT (priv->listener));
		}

		priv->listener = bonobo_object_ref (g_value_get_pointer (value));

		g_signal_connect (G_OBJECT (priv->listener), "objects_added",
				  G_CALLBACK (objects_added_cb), view);
		g_signal_connect (G_OBJECT (priv->listener), "objects_modified",
				  G_CALLBACK (objects_modified_cb), view);
		g_signal_connect (G_OBJECT (priv->listener), "objects_removed",
				  G_CALLBACK (objects_removed_cb), view);
		g_signal_connect (G_OBJECT (priv->listener), "view_progress",
				  G_CALLBACK (view_progress_cb), view);
		g_signal_connect (G_OBJECT (priv->listener), "view_done",
				  G_CALLBACK (view_done_cb), view);
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
	priv = view->priv;

	switch (property_id) {
	case PROP_VIEW:
		g_value_set_pointer (value, priv->view);
		break;
	case PROP_LISTENER:
		g_value_set_pointer (value, priv->listener);
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

	/* The server keeps a copy of the view listener, so we must unref it */
	g_signal_handlers_disconnect_matched (priv->listener, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, view);
	bonobo_object_unref (BONOBO_OBJECT (priv->listener));

	if (priv->view != CORBA_OBJECT_NIL)
		bonobo_object_release_unref (priv->view, NULL);

	g_free (priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Class initialization function for the calendar view */
static void
e_cal_view_class_init (ECalViewClass *klass)
{
	GObjectClass *object_class;
	GParamSpec *param;

	object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = e_cal_view_set_property;
	object_class->get_property = e_cal_view_get_property;
	object_class->finalize = e_cal_view_finalize;

	param =  g_param_spec_pointer ("view", "The corba view object", NULL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_VIEW, param);
	/* FIXME type this property as object? */
	param =  g_param_spec_pointer ("listener", "The view listener object to use", NULL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_LISTENER, param);
	param =  g_param_spec_object ("client", "The e-cal for the view", NULL, E_TYPE_CAL,
				      G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_CLIENT, param);

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
			      e_cal_marshal_VOID__STRING_INT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);
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
 * e_cal_view_get_type:
 *
 * Registers the #ECalView class if necessary, and returns the type ID assigned
 * to it.
 *
 * Return value: The type ID of the #ECalView class.
 **/
GType
e_cal_view_get_type (void)
{
	static GType e_cal_view_type = 0;

	if (!e_cal_view_type) {
		static GTypeInfo info = {
                        sizeof (ECalViewClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_view_class_init,
                        NULL, NULL,
                        sizeof (ECalView),
                        0,
                        (GInstanceInitFunc) e_cal_view_init
                };
		e_cal_view_type = g_type_register_static (G_TYPE_OBJECT, "ECalView", &info, 0);
	}

	return e_cal_view_type;
}

/**
 * e_cal_view_new:
 * @corba_view: The CORBA object for the view.
 * @listener: An #ECalViewListener.
 * @client: An #ECal object.
 *
 * Creates a new view object by issuing the view creation request to the
 * calendar server.
 *
 * Return value: A newly-created view object, or NULL if the request failed.
 **/
ECalView *
e_cal_view_new (GNOME_Evolution_Calendar_CalView corba_view, ECalViewListener *listener, ECal *client)
{
	ECalView *view;

	view = g_object_new (E_TYPE_CAL_VIEW, "view", corba_view, "listener",
			      listener, "client", client, NULL);

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
	CORBA_Environment ev;

	g_return_if_fail (view != NULL);
	g_return_if_fail (E_IS_CAL_VIEW (view));

	priv = view->priv;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Calendar_CalView_start (priv->view, &ev);
	if (BONOBO_EX (&ev))
		g_warning (G_STRLOC ": Unable to start view");

	CORBA_exception_free (&ev);
}
