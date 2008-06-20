/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar listener
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

#include <config.h>

#include <bonobo/bonobo-main.h>
#include "e-cal-marshal.h"
#include "e-cal-listener.h"



/* Private part of the CalListener structure */
struct ECalListenerPrivate {
	/* Notification functions and their closure data */
	ECalListenerCalSetModeFn cal_set_mode_fn;
	gpointer fn_data;

	/* Whether notification is desired */
	guint notify : 1;
};

/* Signal IDs */
enum {
	READ_ONLY,
	CAL_ADDRESS,
	ALARM_ADDRESS,
	LDAP_ATTRIBUTE,
	STATIC_CAPABILITIES,
	OPEN,
	REMOVE,
	CREATE_OBJECT,
	MODIFY_OBJECT,
	REMOVE_OBJECT,
	DISCARD_ALARM,
	RECEIVE_OBJECTS,
	SEND_OBJECTS,
	DEFAULT_OBJECT,
	OBJECT,
	OBJECT_LIST,
	ATTACHMENT_LIST,
	GET_TIMEZONE,
	ADD_TIMEZONE,
	SET_DEFAULT_TIMEZONE,
	GET_CHANGES,
	GET_FREE_BUSY,
	QUERY,
	AUTH_REQUIRED,
	BACKEND_ERROR,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static BonoboObjectClass *parent_class;

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
	case GNOME_Evolution_Calendar_InvalidObject:
		return E_CALENDAR_STATUS_INVALID_OBJECT;
	case GNOME_Evolution_Calendar_ObjectIdAlreadyExists:
		return E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS;
	case GNOME_Evolution_Calendar_AuthenticationFailed:
		return E_CALENDAR_STATUS_AUTHENTICATION_FAILED;
	case GNOME_Evolution_Calendar_AuthenticationRequired:
		return E_CALENDAR_STATUS_AUTHENTICATION_REQUIRED;
	case GNOME_Evolution_Calendar_UnknownUser:
		return E_CALENDAR_STATUS_UNKNOWN_USER;
	case GNOME_Evolution_Calendar_InvalidServerVersion:
		return E_CALENDAR_STATUS_INVALID_SERVER_VERSION;
	case GNOME_Evolution_Calendar_NoSuchCal:
	      return E_CALENDAR_STATUS_NO_SUCH_CALENDAR;

	case GNOME_Evolution_Calendar_OtherError:
	default:
		return E_CALENDAR_STATUS_OTHER_ERROR;
	}
}

static void
impl_notifyReadOnly (PortableServer_Servant servant,
		     GNOME_Evolution_Calendar_CallStatus status,
		     const CORBA_boolean read_only,
		     CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[READ_ONLY], 0, convert_status (status), read_only);
}

static void
impl_notifyCalAddress (PortableServer_Servant servant,
		       GNOME_Evolution_Calendar_CallStatus status,
		       const CORBA_char *address,
		       CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[CAL_ADDRESS], 0, convert_status (status), address);
}

static void
impl_notifyAlarmEmailAddress (PortableServer_Servant servant,
			      GNOME_Evolution_Calendar_CallStatus status,
			      const CORBA_char *address,
			      CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[ALARM_ADDRESS], 0, convert_status (status), address);
}

static void
impl_notifyLDAPAttribute (PortableServer_Servant servant,
			  GNOME_Evolution_Calendar_CallStatus status,
			  const CORBA_char *ldap_attribute,
			  CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[LDAP_ATTRIBUTE], 0, convert_status (status), ldap_attribute);
}

static void
impl_notifyStaticCapabilities (PortableServer_Servant servant,
			       GNOME_Evolution_Calendar_CallStatus status,
			       const CORBA_char *capabilities,
			       CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[STATIC_CAPABILITIES], 0, convert_status (status), capabilities);
}

/* ::notifyCalOpened method */
static void
impl_notifyCalOpened (PortableServer_Servant servant,
		      GNOME_Evolution_Calendar_CallStatus status,
		      CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[OPEN], 0, convert_status (status));
}

static void
impl_notifyCalRemoved (PortableServer_Servant servant,
		      GNOME_Evolution_Calendar_CallStatus status,
		      CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[REMOVE], 0, convert_status (status));
}

static void
impl_notifyObjectCreated (PortableServer_Servant servant,
			  GNOME_Evolution_Calendar_CallStatus status,
			  const CORBA_char *uid,
			  CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[CREATE_OBJECT], 0, convert_status (status), uid);
}

static void
impl_notifyObjectModified (PortableServer_Servant servant,
			   GNOME_Evolution_Calendar_CallStatus status,
			   CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[MODIFY_OBJECT], 0, convert_status (status));
}

static void
impl_notifyObjectRemoved (PortableServer_Servant servant,
			  GNOME_Evolution_Calendar_CallStatus status,
			  CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[REMOVE_OBJECT], 0, convert_status (status));
}

static void
impl_notifyAlarmDiscarded (PortableServer_Servant servant,
			   GNOME_Evolution_Calendar_CallStatus status,
			   CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[DISCARD_ALARM], 0, convert_status (status));
}

static void
impl_notifyObjectsReceived (PortableServer_Servant servant,
			    GNOME_Evolution_Calendar_CallStatus status,
			    CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[RECEIVE_OBJECTS], 0, convert_status (status));
}

static void
impl_notifyObjectsSent (PortableServer_Servant servant,
			const GNOME_Evolution_Calendar_CallStatus status,
			const GNOME_Evolution_Calendar_UserList *user_list,
			const CORBA_char *calobj,
			CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;
	GList *users = NULL;
	int i;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	for (i = 0; i < user_list->_length; i++)
		users = g_list_append (users, g_strdup (user_list->_buffer[i]));

	g_signal_emit (G_OBJECT (listener), signals[SEND_OBJECTS], 0, convert_status (status), users, calobj);

	g_list_foreach (users, (GFunc) g_free, NULL);
	g_list_free (users);
}

static void
impl_notifyDefaultObjectRequested (PortableServer_Servant servant,
				   const GNOME_Evolution_Calendar_CallStatus status,
				   const CORBA_char *object,
				   CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[DEFAULT_OBJECT], 0, convert_status (status), object);
}

static void
impl_notifyObjectRequested (PortableServer_Servant servant,
			    const GNOME_Evolution_Calendar_CallStatus status,
			    const CORBA_char *object,
			    CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[OBJECT], 0, convert_status (status), object);
}

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

static void
impl_notifyObjectListRequested (PortableServer_Servant servant,
				const GNOME_Evolution_Calendar_CallStatus status,
				const GNOME_Evolution_Calendar_stringlist *objects,
				CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;
	GList *object_list, *l;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	object_list = build_object_list (objects);

	g_signal_emit (G_OBJECT (listener), signals[OBJECT_LIST], 0, convert_status (status), object_list);

	for (l = object_list; l; l = l->next)
		icalcomponent_free (l->data);
	g_list_free (object_list);
}

static void
impl_notifyAttachmentListRequested (PortableServer_Servant servant,
				const GNOME_Evolution_Calendar_CallStatus status,
				const GNOME_Evolution_Calendar_stringlist *attachments,
				CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;
	GSList *a_list = NULL;
	int i;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	for (i = 0; i < attachments->_length; i++) {
		a_list = g_slist_append (a_list, g_strdup (attachments->_buffer[i]));
	}

	g_signal_emit (G_OBJECT (listener), signals[ATTACHMENT_LIST], 0, convert_status (status), a_list);

	g_slist_foreach (a_list, (GFunc) g_free, NULL);
	g_slist_free (a_list);
}

static void
impl_notifyTimezoneRequested (PortableServer_Servant servant,
			      const GNOME_Evolution_Calendar_CallStatus status,
			      const CORBA_char *object,
			      CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[GET_TIMEZONE], 0, convert_status (status), object);
}

static void
impl_notifyTimezoneAdded (PortableServer_Servant servant,
			  const GNOME_Evolution_Calendar_CallStatus status,
			  const CORBA_char *tzid,
			  CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[ADD_TIMEZONE], 0, convert_status (status), tzid);
}

static void
impl_notifyDefaultTimezoneSet (PortableServer_Servant servant,
			       const GNOME_Evolution_Calendar_CallStatus status,
			       CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[SET_DEFAULT_TIMEZONE], 0, convert_status (status));
}

static GList *
build_change_list (const GNOME_Evolution_Calendar_CalObjChangeSeq *seq)
{
	GList *list = NULL;
	icalcomponent *icalcomp;
	int i;

	/* Create the list in reverse order */
	for (i = 0; i < seq->_length; i++) {
		GNOME_Evolution_Calendar_CalObjChange *corba_coc;
		ECalChange *ccc;

		corba_coc = &seq->_buffer[i];
		ccc = g_new (ECalChange, 1);

		icalcomp = icalparser_parse_string (corba_coc->calobj);
		if (!icalcomp)
			continue;

		ccc->comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (ccc->comp, icalcomp)) {
			icalcomponent_free (icalcomp);
			g_object_unref (G_OBJECT (ccc->comp));
			continue;
		}
		ccc->type = corba_coc->type;

		list = g_list_prepend (list, ccc);
	}

	list = g_list_reverse (list);

	return list;
}

static void
impl_notifyChanges (PortableServer_Servant servant,
		    const GNOME_Evolution_Calendar_CallStatus status,
		    const GNOME_Evolution_Calendar_CalObjChangeSeq *seq,
		    CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;
	GList *changes, *l;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	changes = build_change_list (seq);

	g_signal_emit (G_OBJECT (listener), signals[GET_CHANGES], 0, convert_status (status), changes);

	for (l = changes; l; l = l->next)
		g_free (l->data);
	g_list_free (changes);
}

static GList *
build_free_busy_list (const GNOME_Evolution_Calendar_CalObjSeq *seq)
{
	GList *list = NULL;
	int i;

	/* Create the list in reverse order */
	for (i = 0; i < seq->_length; i++) {
		ECalComponent *comp;
		icalcomponent *icalcomp;
		icalcomponent_kind kind;

		icalcomp = icalcomponent_new_from_string (seq->_buffer[i]);
		if (!icalcomp)
			continue;

		kind = icalcomponent_isa (icalcomp);
		if (kind == ICAL_VFREEBUSY_COMPONENT) {
			comp = e_cal_component_new ();
			if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
				icalcomponent_free (icalcomp);
				g_object_unref (G_OBJECT (comp));
				continue;
			}

			list = g_list_append (list, comp);
		} else {
			icalcomponent_free (icalcomp);
		}
	}

	return list;
}

static void
impl_notifyFreeBusy (PortableServer_Servant servant,
		     const GNOME_Evolution_Calendar_CallStatus status,
		     const GNOME_Evolution_Calendar_CalObjSeq *seq,
		     CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;
	GList *freebusy, *l;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	freebusy = build_free_busy_list (seq);

	g_signal_emit (G_OBJECT (listener), signals[GET_FREE_BUSY], 0, convert_status (status), freebusy);

	for (l = freebusy; l; l = l->next)
		g_object_unref (G_OBJECT (l->data));
	g_list_free (freebusy);
}

static void
impl_notifyQuery (PortableServer_Servant servant,
		  const GNOME_Evolution_Calendar_CallStatus status,
		  const GNOME_Evolution_Calendar_CalView query,
		  CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[QUERY], 0, convert_status (status), query);
}

/* ::notifyCalSetMode method */
static void
impl_notifyCalSetMode (PortableServer_Servant servant,
		       GNOME_Evolution_Calendar_CalListener_SetModeStatus status,
		       GNOME_Evolution_Calendar_CalMode mode,
		       CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_assert (priv->cal_set_mode_fn != NULL);
	(* priv->cal_set_mode_fn) (listener, status, mode, priv->fn_data);
}

static void
impl_notifyAuthRequired (PortableServer_Servant servant,
			 CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[AUTH_REQUIRED], 0);
}

/* ::notifyErrorOccurred method */
static void
impl_notifyErrorOccurred (PortableServer_Servant servant,
			  const CORBA_char *message,
			  CORBA_Environment *ev)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	listener = E_CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_signal_emit (G_OBJECT (listener), signals[BACKEND_ERROR], 0, message);
}



/* Object initialization function for the calendar listener */
static void
e_cal_listener_init (ECalListener *listener, ECalListenerClass *klass)
{
	ECalListenerPrivate *priv;

	priv = g_new0 (ECalListenerPrivate, 1);
	listener->priv = priv;

	priv->notify = TRUE;
}

/* Finalize handler for the calendar listener */
static void
e_cal_listener_finalize (GObject *object)
{
	ECalListener *listener;
	ECalListenerPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_LISTENER (object));

	listener = E_CAL_LISTENER (object);
	priv = listener->priv;

	priv->fn_data = NULL;

	priv->notify = FALSE;

	g_free (priv);
	listener->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Class initialization function for the calendar listener */
static void
e_cal_listener_class_init (ECalListenerClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	klass->epv.notifyReadOnly = impl_notifyReadOnly;
	klass->epv.notifyCalAddress = impl_notifyCalAddress;
	klass->epv.notifyAlarmEmailAddress = impl_notifyAlarmEmailAddress;
	klass->epv.notifyLDAPAttribute = impl_notifyLDAPAttribute;
	klass->epv.notifyStaticCapabilities = impl_notifyStaticCapabilities;
	klass->epv.notifyCalOpened = impl_notifyCalOpened;
	klass->epv.notifyCalRemoved = impl_notifyCalRemoved;
	klass->epv.notifyObjectCreated = impl_notifyObjectCreated;
	klass->epv.notifyObjectModified = impl_notifyObjectModified;
	klass->epv.notifyObjectRemoved = impl_notifyObjectRemoved;
	klass->epv.notifyAlarmDiscarded = impl_notifyAlarmDiscarded;
	klass->epv.notifyObjectsReceived = impl_notifyObjectsReceived;
	klass->epv.notifyObjectsSent = impl_notifyObjectsSent;
	klass->epv.notifyDefaultObjectRequested = impl_notifyDefaultObjectRequested;
	klass->epv.notifyObjectRequested = impl_notifyObjectRequested;
	klass->epv.notifyObjectListRequested = impl_notifyObjectListRequested;
	klass->epv.notifyAttachmentListRequested = impl_notifyAttachmentListRequested;
	klass->epv.notifyTimezoneRequested = impl_notifyTimezoneRequested;
	klass->epv.notifyTimezoneAdded = impl_notifyTimezoneAdded;
	klass->epv.notifyDefaultTimezoneSet = impl_notifyDefaultTimezoneSet;
	klass->epv.notifyChanges = impl_notifyChanges;
	klass->epv.notifyFreeBusy = impl_notifyFreeBusy;
	klass->epv.notifyQuery = impl_notifyQuery;
	klass->epv.notifyCalSetMode = impl_notifyCalSetMode;
	klass->epv.notifyErrorOccurred = impl_notifyErrorOccurred;
	klass->epv.notifyAuthRequired      = impl_notifyAuthRequired;
	object_class->finalize = e_cal_listener_finalize;

	signals[READ_ONLY] =
		g_signal_new ("read_only",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, read_only),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_BOOLEAN,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_BOOLEAN);
	signals[CAL_ADDRESS] =
		g_signal_new ("cal_address",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, cal_address),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[ALARM_ADDRESS] =
		g_signal_new ("alarm_address",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, alarm_address),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[LDAP_ATTRIBUTE] =
		g_signal_new ("ldap_attribute",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, ldap_attribute),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[STATIC_CAPABILITIES] =
		g_signal_new ("static_capabilities",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, static_capabilities),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[OPEN] =
		g_signal_new ("open",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, open),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[REMOVE] =
		g_signal_new ("remove",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, remove),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[CREATE_OBJECT] =
		g_signal_new ("create_object",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, create_object),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[MODIFY_OBJECT] =
		g_signal_new ("modify_object",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, modify_object),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[REMOVE_OBJECT] =
		g_signal_new ("remove_object",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, remove_object),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[DISCARD_ALARM] =
		g_signal_new ("discard_alarm",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, discard_alarm),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[RECEIVE_OBJECTS] =
		g_signal_new ("receive_objects",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, receive_objects),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[SEND_OBJECTS] =
		g_signal_new ("send_objects",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, send_objects),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_POINTER_STRING,
			      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_POINTER, G_TYPE_STRING);
	signals[DEFAULT_OBJECT] =
		g_signal_new ("default_object",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, default_object),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[OBJECT] =
		g_signal_new ("object",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, object),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[OBJECT_LIST] =
		g_signal_new ("object_list",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, object_list),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_POINTER,
		      	      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
	signals[ATTACHMENT_LIST] =
		g_signal_new ("attachment_list",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, attachment_list),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
	signals[GET_TIMEZONE] =
		g_signal_new ("get_timezone",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, get_timezone),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[ADD_TIMEZONE] =
		g_signal_new ("add_timezone",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, add_timezone),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
	signals[SET_DEFAULT_TIMEZONE] =
		g_signal_new ("set_default_timezone",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, set_default_timezone),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals[GET_CHANGES] =
		g_signal_new ("get_changes",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, get_changes),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
	signals[GET_FREE_BUSY] =
		g_signal_new ("get_free_busy",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, get_free_busy),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
	signals[QUERY] =
		g_signal_new ("query",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, query),
			      NULL, NULL,
			      e_cal_marshal_VOID__INT_POINTER,
			      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
	signals [AUTH_REQUIRED] =
		g_signal_new ("auth_required",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, auth_required),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[BACKEND_ERROR] =
		g_signal_new ("backend_error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ECalListenerClass, backend_error),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
}

BONOBO_TYPE_FUNC_FULL (ECalListener,
		       GNOME_Evolution_Calendar_CalListener,
		       BONOBO_TYPE_OBJECT,
		       e_cal_listener);

/**
 * e_cal_listener_construct:
 * @listener: A calendar listener.
 * @cal_set_mode_fn: Function callback for notification that a
 * calendar changed modes.
 * @fn_data: Closure data pointer that will be passed to the notification
 * functions.
 *
 * Constructs all internal information for a calendar listener. This function
 * usually does not need to be called, unless creating a ECalListener-derived
 * class.
 *
 * Return value: the calendar listener ready to be used.
 */
ECalListener *
e_cal_listener_construct (ECalListener *listener,
			  ECalListenerCalSetModeFn cal_set_mode_fn,
			  gpointer fn_data)
{
	ECalListenerPrivate *priv;

	g_return_val_if_fail (listener != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_LISTENER (listener), NULL);
 	g_return_val_if_fail (cal_set_mode_fn != NULL, NULL);

	priv = listener->priv;

	priv->cal_set_mode_fn = cal_set_mode_fn;
	priv->fn_data = fn_data;

	return listener;
}

/**
 * e_cal_listener_new:
 * @cal_set_mode_fn: Function callback for notification that a
 * calendar changed modes.
 * @fn_data: Closure data pointer that will be passed to the notification
 * functions.
 *
 * Creates a new #ECalListener object.
 *
 * Return value: A newly-created #ECalListener object.
 **/
ECalListener *
e_cal_listener_new (ECalListenerCalSetModeFn cal_set_mode_fn,
		    gpointer fn_data)
{
	ECalListener *listener;

	g_return_val_if_fail (cal_set_mode_fn != NULL, NULL);

	listener = g_object_new (E_TYPE_CAL_LISTENER,
				 "poa", bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL),
				 NULL);

	return e_cal_listener_construct (listener,
					 cal_set_mode_fn,
					 fn_data);
}

/**
 * e_cal_listener_stop_notification:
 * @listener: A calendar listener.
 *
 * Informs a calendar listener that no further notification is desired.  The
 * callbacks specified when the listener was created will no longer be invoked
 * after this function is called.
 **/
void
e_cal_listener_stop_notification (ECalListener *listener)
{
	ECalListenerPrivate *priv;

	g_return_if_fail (listener != NULL);
	g_return_if_fail (E_IS_CAL_LISTENER (listener));

	priv = listener->priv;
	g_return_if_fail (priv->notify != FALSE);

	priv->notify = FALSE;
}
