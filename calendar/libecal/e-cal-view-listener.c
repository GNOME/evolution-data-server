/* Evolution calendar - Live search query listener convenience object
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

#include "e-cal-marshal.h"
#include "e-cal-view-listener.h"



/* Private part of the CalViewListener structure */

struct _ECalViewListenerPrivate {
	int dummy;
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

static guint signals[LAST_SIGNAL] = { 0 };

static BonoboObjectClass *parent_class;

/* CORBA method implementations */
/* FIXME This is duplicated from cal-listener.c */
static ECalendarStatus
convert_status (const GNOME_Evolution_Calendar_CallStatus status)
{
	switch (status) {
	case GNOME_Evolution_Calendar_Success:
		return E_CALENDAR_STATUS_OK;
	case GNOME_Evolution_Calendar_RepositoryOffline:
		return E_CALENDAR_STATUS_REPOSITORY_OFFLINE;
	case GNOME_Evolution_Calendar_PermissionDenied:
		return E_CALENDAR_STATUS_PERMISSION_DENIED;
	case GNOME_Evolution_Calendar_ObjectNotFound:
		return E_CALENDAR_STATUS_OBJECT_NOT_FOUND;
	case GNOME_Evolution_Calendar_ObjectIdAlreadyExists:
		return E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS;
	case GNOME_Evolution_Calendar_AuthenticationFailed:
		return E_CALENDAR_STATUS_AUTHENTICATION_FAILED;
	case GNOME_Evolution_Calendar_AuthenticationRequired:
		return E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED;
	case GNOME_Evolution_Calendar_UnknownUser:
		return E_CALENDAR_STATUS_UNKNOWN_USER;
	case GNOME_Evolution_Calendar_OtherError:
	default:
		return E_CALENDAR_STATUS_OTHER_ERROR;
	}
}

/* FIXME This is duplicated from cal-listener.c */
static GList *
build_object_list (const GNOME_Evolution_Calendar_stringlist *seq)
{
	GList *list;
	int i;

	list = NULL;
	for (i = 0; i < seq->_length; i++) {
		icalcomponent *comp;

		comp = icalcomponent_new_from_string (seq->_buffer[i]);
		if (!comp)
			continue;

		list = g_list_prepend (list, comp);
	}

	return list;
}

static GList *
build_id_list (const GNOME_Evolution_Calendar_CalObjIDSeq *seq)
{
	GList *list;
	int i;

	list = NULL;
	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalObjID *corba_id;
		ECalComponentId *id;

		corba_id = &seq->_buffer[i];
		id = g_new (ECalComponentId, 1);

		id->uid = g_strdup (corba_id->uid);
		id->rid = g_strdup (corba_id->rid);

		list = g_list_prepend (list, id);
	}

	return list;
}

static void
impl_notifyObjectsAdded (PortableServer_Servant servant,
			 const GNOME_Evolution_Calendar_stringlist *objects,
			 CORBA_Environment *ev)
{
	ECalViewListener *ql;
	ECalViewListenerPrivate *priv;
	GList *object_list, *l;

	ql = E_CAL_VIEW_LISTENER (bonobo_object_from_servant (servant));
	priv = ql->priv;

	object_list = build_object_list (objects);

	g_signal_emit (G_OBJECT (ql), signals[OBJECTS_ADDED], 0, object_list);

	for (l = object_list; l; l = l->next)
		icalcomponent_free (l->data);
	g_list_free (object_list);
}

static void
impl_notifyObjectsModified (PortableServer_Servant servant,
			    const GNOME_Evolution_Calendar_stringlist *objects,
			    CORBA_Environment *ev)
{
	ECalViewListener *ql;
	ECalViewListenerPrivate *priv;
	GList *object_list, *l;

	ql = E_CAL_VIEW_LISTENER (bonobo_object_from_servant (servant));
	priv = ql->priv;

	object_list = build_object_list (objects);

	g_signal_emit (G_OBJECT (ql), signals[OBJECTS_MODIFIED], 0, object_list);

	for (l = object_list; l; l = l->next)
		icalcomponent_free (l->data);
	g_list_free (object_list);
}

static void
impl_notifyObjectsRemoved (PortableServer_Servant servant,
			   const GNOME_Evolution_Calendar_CalObjIDSeq *ids,
			   CORBA_Environment *ev)
{
	ECalViewListener *ql;
	ECalViewListenerPrivate *priv;
	GList *id_list, *l;

	ql = E_CAL_VIEW_LISTENER (bonobo_object_from_servant (servant));
	priv = ql->priv;

	id_list = build_id_list (ids);

	g_signal_emit (G_OBJECT (ql), signals[OBJECTS_REMOVED], 0, id_list);

	for (l = id_list; l; l = l->next)
		e_cal_component_free_id (l->data);
	g_list_free (id_list);
}

static void
impl_notifyQueryProgress (PortableServer_Servant servant,
			  const CORBA_char *message,
			  const CORBA_short percent,
			  CORBA_Environment *ev)
{
	ECalViewListener *ql;
	ECalViewListenerPrivate *priv;

	ql = E_CAL_VIEW_LISTENER (bonobo_object_from_servant (servant));
	priv = ql->priv;

	g_signal_emit (G_OBJECT (ql), signals[VIEW_PROGRESS], 0, message, percent);
}

static void
impl_notifyQueryDone (PortableServer_Servant servant,
		      const GNOME_Evolution_Calendar_CallStatus status,
		      CORBA_Environment *ev)
{
	ECalViewListener *ql;
	ECalViewListenerPrivate *priv;

	ql = E_CAL_VIEW_LISTENER (bonobo_object_from_servant (servant));
	priv = ql->priv;

	g_signal_emit (G_OBJECT (ql), signals[VIEW_DONE], 0, convert_status (status));
}

/* Object initialization function for the live search query listener */
static void
e_cal_view_listener_init (ECalViewListener *ql, ECalViewListenerClass *class)
{
	ECalViewListenerPrivate *priv;

	priv = g_new0 (ECalViewListenerPrivate, 1);
	ql->priv = priv;
}

/* Finalize handler for the live search query listener */
static void
e_cal_view_listener_finalize (GObject *object)
{
	ECalViewListener *ql;
	ECalViewListenerPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_VIEW_LISTENER (object));

	ql = E_CAL_VIEW_LISTENER (object);
	priv = ql->priv;

	g_free (priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Class initialization function for the live search query listener */
static void
e_cal_view_listener_class_init (ECalViewListenerClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = e_cal_view_listener_finalize;

	klass->epv.notifyObjectsAdded = impl_notifyObjectsAdded;
	klass->epv.notifyObjectsModified = impl_notifyObjectsModified;
	klass->epv.notifyObjectsRemoved = impl_notifyObjectsRemoved;
	klass->epv.notifyQueryProgress = impl_notifyQueryProgress;
	klass->epv.notifyQueryDone = impl_notifyQueryDone;

	signals[OBJECTS_ADDED] =
		g_signal_new ("objects_added",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewListenerClass, objects_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[OBJECTS_MODIFIED] =
		g_signal_new ("objects_modified",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewListenerClass, objects_modified),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[OBJECTS_REMOVED] =
		g_signal_new ("objects_removed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewListenerClass, objects_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[VIEW_PROGRESS] =
		g_signal_new ("view_progress",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewListenerClass, view_progress),
			      NULL, NULL,
			      e_cal_marshal_VOID__STRING_INT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);
	signals[VIEW_DONE] =
		g_signal_new ("view_done",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalViewListenerClass, view_done),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
}

BONOBO_TYPE_FUNC_FULL (ECalViewListener,
		       GNOME_Evolution_Calendar_CalViewListener,
		       BONOBO_TYPE_OBJECT,
		       e_cal_view_listener);

/**
 * e_cal_view_listener_new:
 *
 * Creates a new ECalViewListener object.
 *
 * Return value: the newly created ECalViewListener.
 */
ECalViewListener *
e_cal_view_listener_new (void)
{
	ECalViewListener *ql;

	ql = g_object_new (E_TYPE_CAL_VIEW_LISTENER, NULL);

	return ql;
}
