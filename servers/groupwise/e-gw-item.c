/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include "e-gw-item.h"
#include "e-gw-connection.h"
#include "e-gw-message.h"

struct _EGwItemPrivate {
	EGwItemType item_type;
	char *container;

	/* properties */
	char *id;
	struct icaltimetype creation_date;
	struct icaltimetype start_date;
	struct icaltimetype end_date;
	struct icaltimetype due_date;
	gboolean completed;
	char *subject;
	char *message;
	ECalComponentClassification classification;
	char *accept_level;
	char *priority;
	char *place;
	GSList *attendee_list;
};

static GObjectClass *parent_class = NULL;

static void
e_gw_item_dispose (GObject *object)
{
	EGwItem *item = (EGwItem *) object;
	EGwItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;
	if (priv) {
		if (priv->container) {
			g_free (priv->container);
			priv->container = NULL;
		}

		if (priv->id) {
			g_free (priv->id);
			priv->id = NULL;
		}

		if (priv->subject) {
			g_free (priv->subject);
			priv->subject = NULL;
		}

		if (priv->message) {
			g_free (priv->message);
			priv->message = NULL;
		}

		if (priv->accept_level) {
			g_free (priv->accept_level);
			priv->accept_level = NULL;
		}

		if (priv->priority) {
			g_free (priv->priority);
			priv->priority = NULL;
		}

		if (priv->place) {
			g_free (priv->place);
			priv->place = NULL;
		}

		if (priv->attendee_list) {
			g_slist_foreach (priv->attendee_list, (GFunc) g_free, NULL);
		}	
	}

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_gw_item_finalize (GObject *object)
{
	EGwItem *item = (EGwItem *) object;
	EGwItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;

	/* clean up */
	g_free (priv);
	item->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_item_class_init (EGwItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_gw_item_dispose;
	object_class->finalize = e_gw_item_finalize;
}

static void
e_gw_item_init (EGwItem *item, EGwItemClass *klass)
{
	EGwItemPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EGwItemPrivate, 1);
	priv->item_type = E_GW_ITEM_TYPE_UNKNOWN;

	item->priv = priv;
}

GType
e_gw_item_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EGwItemClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_gw_item_class_init,
                        NULL, NULL,
                        sizeof (EGwItem),
                        0,
                        (GInstanceInitFunc) e_gw_item_init
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EGwItem", &info, 0);
	}

	return type;
}

static void 
set_attendee_list_from_soap_parameter (GSList *attendee_list, SoupSoapParameter *param)
{
        SoupSoapParameter *param_recipient;
        char *email, *cn;
	ECalComponentAttendee *attendee;

        for (param_recipient = soup_soap_parameter_get_first_child_by_name (param, "recipient");
                param_recipient != NULL;
                param_recipient = soup_soap_parameter_get_next_child_by_name (param, "recipient")) {

                SoupSoapParameter *subparam;
		attendee = g_new0 (ECalComponentAttendee, 1);	
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "email");
                if (subparam) {
                        email = soup_soap_parameter_get_string_value (subparam);
                        if (email)
                                attendee->value = email;
                }        
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "displayName");
                if (subparam) {
                        cn = soup_soap_parameter_get_string_value (subparam);
                        if (cn)
                                attendee->cn = cn;
                }
                
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "distType");
                if (subparam) {
                        const char *dist_type;
                        dist_type = soup_soap_parameter_get_string_value (subparam);
                        if (!strcmp (dist_type, "TO")) 
                                attendee->role = ICAL_ROLE_REQPARTICIPANT;
                        else if (!strcmp (dist_type, "CC"))
                                attendee->role = ICAL_ROLE_OPTPARTICIPANT;
                        else
                                attendee->role = ICAL_ROLE_NONPARTICIPANT;
                }

                attendee_list = g_slist_append (attendee_list, attendee);
        }        
}

EGwItem *
e_gw_item_new_from_soap_parameter (const char *container, SoupSoapParameter *param)
{
	EGwItem *item;
	const char *item_type;
	SoupSoapParameter *subparam, *child;
	
	g_return_val_if_fail (param != NULL, NULL);

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return NULL;
	}

	item = g_object_new (E_TYPE_GW_ITEM, NULL);
	item_type = soup_soap_parameter_get_property (param, "type");
	if (!g_ascii_strcasecmp (item_type, "Appointment"))
		item->priv->item_type = E_GW_ITEM_TYPE_APPOINTMENT;
	else if (!g_ascii_strcasecmp (item_type, "Task"))
		item->priv->item_type = E_GW_ITEM_TYPE_TASK;
	else {
		g_object_unref (item);
		return NULL;
	}

	item->priv->container = g_strdup (container);

	/* If the parameter consists of changes - populate deltas */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "changes");
	if (subparam) {
		SoupSoapParameter *changes = subparam;
		subparam = soup_soap_parameter_get_first_child_by_name (changes, "add");
		if (!subparam)
			subparam = soup_soap_parameter_get_first_child_by_name (changes, "delete");
		if (!subparam)
			subparam = soup_soap_parameter_get_first_child_by_name (changes, "update");
	}
	else subparam = param; /* The item is a complete one, not a delta  */
	
	/* now add all properties to the private structure */
	for (child = soup_soap_parameter_get_first_child (subparam);
	     child != NULL;
	     child = soup_soap_parameter_get_next_child (child)) {
		const char *name;
		char *value;

		name = soup_soap_parameter_get_name (child);

		if (!g_ascii_strcasecmp (name, "acceptLevel"))
			item->priv->accept_level = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "class")) {
			value = soup_soap_parameter_get_string_value (child);

			if (!g_ascii_strcasecmp (value, "Public"))
				item->priv->classification = E_CAL_COMPONENT_CLASS_PUBLIC;
			else if (!g_ascii_strcasecmp (value, "Private"))
				item->priv->classification = E_CAL_COMPONENT_CLASS_PRIVATE;
			else if (!g_ascii_strcasecmp (value, "Confidential"))
				item->priv->classification = E_CAL_COMPONENT_CLASS_CONFIDENTIAL;
			else
				item->priv->classification = E_CAL_COMPONENT_CLASS_UNKNOWN;

			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "completed")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!g_ascii_strcasecmp (value, "true"))
				item->priv->completed = TRUE;
			else
				item->priv->completed = FALSE;
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "created")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->creation_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "distribution")) {
			SoupSoapParameter *tp;

			tp = soup_soap_parameter_get_first_child_by_name (child, "recipients");
			if (tp) {
				/* FIXME: see set_attendee_list_from... in e-gw-connection.c */
				item->priv->attendee_list = NULL;
				set_attendee_list_from_soap_parameter (item->priv->attendee_list, tp);
			}

		} else if (!g_ascii_strcasecmp (name, "dueDate")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->due_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "endDate")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->end_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "id"))
			item->priv->id = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "message"))
			item->priv->message = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "place"))
			item->priv->place = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "priority"))
			item->priv->priority = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "startDate")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->start_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "subject"))
			item->priv->subject = soup_soap_parameter_get_string_value (child);
	}

	return item;
}

static EGwItem *
set_properties_from_cal_component (EGwItem *item, ECalComponent *comp)
{
	const char *uid, *location;
	ECalComponentDateTime dt;
	ECalComponentClassification classif;
	ECalComponentTransparency transp;
	ECalComponentText text;
	int *priority;
	GSList *slist, *sl;
	EGwItemPrivate *priv = item->priv;

	/* first set specific properties */
	switch (e_cal_component_get_vtype (comp)) {
	case E_CAL_COMPONENT_EVENT :
		priv->item_type = E_GW_ITEM_TYPE_APPOINTMENT;

		/* transparency */
		e_cal_component_get_transparency (comp, &transp);
		if (transp == E_CAL_COMPONENT_TRANSP_OPAQUE)
			e_gw_item_set_accept_level (item, E_GW_ITEM_ACCEPT_LEVEL_BUSY);
		else
			e_gw_item_set_accept_level (item, NULL);

		/* location */
		e_cal_component_get_location (comp, &location);
		e_gw_item_set_place (item, location);

		/* FIXME: attendee list, set_distribution */
		break;

	case E_CAL_COMPONENT_TODO :
		priv->item_type = E_GW_ITEM_TYPE_TASK;

		/* due date */
		e_cal_component_get_due (comp, &dt);
		if (dt.value) {
			e_gw_item_set_due_date (item, *dt.value);
			e_cal_component_free_datetime (&dt);
		}

		/* priority */
		priority = NULL;
		e_cal_component_get_priority (comp, &priority);
		if (priority && *priority) {
			if (*priority >= 7)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_LOW);
			else if (*priority >= 5)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_STANDARD);
			else if (*priority >= 3)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_HIGH);
			else
				e_gw_item_set_priority (item, NULL);

			e_cal_component_free_priority (priority);
		}

		/* completed */
		e_cal_component_get_completed (comp, &dt.value);
		if (dt.value) {
			e_gw_item_set_completed (item, TRUE);
			e_cal_component_free_datetime (&dt);
		} else
			e_gw_item_set_completed (item, FALSE);

		break;

	default :
		g_object_unref (item);
		return NULL;
	}

	/* set common properties */
	/* UID */
	e_cal_component_get_uid (comp, &uid);
	e_gw_item_set_id (item, uid);

	/* subject */
	e_cal_component_get_summary (comp, &text);
	e_gw_item_set_subject (item, text.value);

	/* description */
	e_cal_component_get_description_list (comp, &slist);
	if (slist) {
		GString *str = g_string_new ("");

		for (sl = slist; sl != NULL; sl = sl->next) {
			ECalComponentText *pt = sl->data;

			if (pt && pt->value)
				str = g_string_append (str, pt->value);
		}

		e_gw_item_set_message (item, (const char *) str->str);

		g_string_free (str, TRUE);
		e_cal_component_free_text_list (slist);
	}

	/* start date */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value) {
		e_gw_item_set_start_date (item, *dt.value);
	} else if (priv->item_type == E_GW_ITEM_TYPE_APPOINTMENT) {
		/* appointments need the start date property */
		g_object_unref (item);
		return NULL;
	}

	/* end date */
	e_cal_component_get_dtend (comp, &dt);
	if (dt.value) {
		e_gw_item_set_end_date (item, *dt.value);
	}

	/* creation date */
	e_cal_component_get_created (comp, &dt.value);
	if (dt.value) {
		e_gw_item_set_creation_date (item, *dt.value);
		e_cal_component_free_datetime (&dt);
	} else {
		struct icaltimetype itt;

		e_cal_component_get_dtstamp (comp, &itt);
		e_gw_item_set_creation_date (item, itt);
	}

	/* classification */
	e_cal_component_get_classification (comp, &classif);
	e_gw_item_set_classification (item, classif);
	
	return item;
}

EGwItem *
e_gw_item_new_from_cal_component (const char *container, ECalComponent *comp)
{
	EGwItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	item = g_object_new (E_TYPE_GW_ITEM, NULL);
	item->priv->container = g_strdup (container);

	return set_properties_from_cal_component (item, comp);
}

const char *
e_gw_item_get_id (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->id;
}

void
e_gw_item_set_id (EGwItem *item, const char *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->id)
		g_free (item->priv->id);
	item->priv->id = g_strdup (new_id);
}

struct icaltimetype
e_gw_item_get_creation_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), icaltime_null_time ());

	return item->priv->creation_date;
}

void
e_gw_item_set_creation_date (EGwItem *item, struct icaltimetype new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->creation_date = new_date;
}

struct icaltimetype
e_gw_item_get_start_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), icaltime_null_time ());

	return item->priv->start_date;
}

void
e_gw_item_set_start_date (EGwItem *item, struct icaltimetype new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->start_date = new_date;
}

struct icaltimetype
e_gw_item_get_end_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), icaltime_null_time ());

	return item->priv->end_date;
}

void
e_gw_item_set_end_date (EGwItem *item, struct icaltimetype new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->end_date = new_date;
}

struct icaltimetype
e_gw_item_get_due_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), icaltime_null_time ());

	return item->priv->due_date;
}

void
e_gw_item_set_due_date (EGwItem *item, struct icaltimetype new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->due_date = new_date;
}

const char *
e_gw_item_get_subject (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->subject;
}

void
e_gw_item_set_subject (EGwItem *item, const char *new_subject)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->subject)
		g_free (item->priv->subject);
	item->priv->subject = g_strdup (new_subject);
}

const char *
e_gw_item_get_message (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->message;
}

void
e_gw_item_set_message (EGwItem *item, const char *new_message)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->message)
		g_free (item->priv->message);
	item->priv->message = g_strdup (new_message);
}

const char *
e_gw_item_get_place (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->place;
}

void
e_gw_item_set_place (EGwItem *item, const char *new_place)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->place)
		g_free (item->priv->place);
	item->priv->place = g_strdup (new_place);
}

ECalComponentClassification
e_gw_item_get_classification (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_CAL_COMPONENT_CLASS_UNKNOWN);

	return item->priv->classification;
}

void
e_gw_item_set_classification (EGwItem *item, ECalComponentClassification new_class)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->classification = new_class;
}

gboolean
e_gw_item_get_completed (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->completed;
}

void
e_gw_item_set_completed (EGwItem *item, gboolean new_completed)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->completed = new_completed;
}

const char *
e_gw_item_get_accept_level (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->accept_level;
}

void
e_gw_item_set_accept_level (EGwItem *item, const char *new_level)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->accept_level)
		g_free (item->priv->accept_level);
	item->priv->accept_level = g_strdup (new_level);
}

const char *
e_gw_item_get_priority (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->priority;
}

void
e_gw_item_set_priority (EGwItem *item, const char *new_priority)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->priority)
		g_free (item->priv->priority);
	item->priv->priority = g_strdup (new_priority);
}

gboolean
e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	EGwItemPrivate *priv;

	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "item", "types", NULL);

	switch (priv->item_type) {
	case E_GW_ITEM_TYPE_APPOINTMENT :
		soup_soap_message_add_attribute (msg, "type", "Appointment", "xsi", NULL);

		e_gw_message_write_string_parameter (msg, "acceptLevel", NULL, priv->accept_level ? priv->accept_level : "");
		e_gw_message_write_string_parameter (msg, "place", NULL, priv->place ? priv->place : "");
		/* FIXME: distribution */
		break;
	case E_GW_ITEM_TYPE_TASK :
		soup_soap_message_add_attribute (msg, "type", "Task", "xsi", NULL);

		if (icaltime_is_valid_time (priv->due_date))
			e_gw_message_write_string_parameter (msg, "dueDate", NULL, icaltime_as_ical_string (priv->due_date));
		else
			e_gw_message_write_string_parameter (msg, "dueDate", NULL, "");

		if (priv->completed)
			e_gw_message_write_string_parameter (msg, "completed", NULL, "1");
		else
			e_gw_message_write_string_parameter (msg, "completed", NULL, "0");

		e_gw_message_write_string_parameter (msg, "priority", NULL, priv->priority ? priv->priority : "");
		break;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

	/* add all properties */
	e_gw_message_write_string_parameter (msg, "id", NULL, priv->id);
	e_gw_message_write_string_parameter (msg, "subject", NULL, priv->subject ? priv->subject : "");
	e_gw_message_write_string_parameter (msg, "message", NULL, priv->message ? priv->message : "");
	if (icaltime_is_valid_time (priv->start_date))
		e_gw_message_write_string_parameter (msg, "startDate", NULL, icaltime_as_ical_string (priv->start_date));
	if (icaltime_is_valid_time (priv->end_date))
		e_gw_message_write_string_parameter (msg, "endDate", NULL, icaltime_as_ical_string (priv->end_date));
	else
		e_gw_message_write_string_parameter (msg, "endDate", NULL, "");
	if (icaltime_is_valid_time (priv->creation_date))
		e_gw_message_write_string_parameter (msg, "created", NULL, icaltime_as_ical_string (priv->creation_date));

	switch (priv->classification) {
	case E_CAL_COMPONENT_CLASS_PUBLIC :
		e_gw_message_write_string_parameter (msg, "class", NULL, "Public");
		break;
	case E_CAL_COMPONENT_CLASS_PRIVATE :
		e_gw_message_write_string_parameter (msg, "class", NULL, "Private");
		break;
	case E_CAL_COMPONENT_CLASS_CONFIDENTIAL :
		e_gw_message_write_string_parameter (msg, "class", NULL, "Confidential");
		break;
	default :
		e_gw_message_write_string_parameter (msg, "class", NULL, "");
	}

	/* finalize the SOAP element */
	soup_soap_message_end_element (msg);

	return TRUE;
}

ECalComponent *
e_gw_item_to_cal_component (EGwItem *item)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt;
	const char *description;
	struct icaltimetype itt;
	int priority;

	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	comp = e_cal_component_new ();

	if (item->priv->item_type == E_GW_ITEM_TYPE_APPOINTMENT)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	else if (item->priv->item_type == E_GW_ITEM_TYPE_TASK)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
	else {
		g_object_unref (comp);
		return NULL;
	}

	/* set common properties */
	/* UID */
	e_cal_component_set_uid (comp, e_gw_item_get_id (item));

	/* summary */
	text.value = e_gw_item_get_subject (item);
	text.altrep = NULL;
	e_cal_component_set_summary (comp, &text);

	/* description */
	description = e_gw_item_get_message (item);
	if (description) {
		GSList l;

		text.value = description;
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;

		e_cal_component_set_description_list (comp, &l);
	}

	/* creation date */
	itt = e_gw_item_get_creation_date (item);
	e_cal_component_set_created (comp, &itt);
	e_cal_component_set_dtstamp (comp, &itt);

	/* start date */
	itt = e_gw_item_get_start_date (item);
	dt.value = &itt;
	e_cal_component_set_dtstart (comp, &dt);

	/* end date */
	itt = e_gw_item_get_end_date (item);
	dt.value = &itt;
	e_cal_component_set_dtend (comp, &dt);

	/* classification */
	e_cal_component_set_classification (comp, e_gw_item_get_classification (item));

	/* set specific properties */
	switch (item->priv->item_type) {
	case E_GW_ITEM_TYPE_APPOINTMENT :
		/* transparency */
		description = e_gw_item_get_accept_level (item);
		if (description &&
		    (!strcmp (description, E_GW_ITEM_ACCEPT_LEVEL_BUSY) ||
		     !strcmp (description, E_GW_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE)))
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
		else
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

		/* location */
		e_cal_component_set_location (comp, e_gw_item_get_place (item));

		/* FIXME: attendee list, get_distribution */
		break;
	case E_GW_ITEM_TYPE_TASK :
		/* due date */
		itt = e_gw_item_get_due_date (item);
		dt.value = &itt;
		e_cal_component_set_due (comp, &dt);
		break;

		/* priority */
		description = e_gw_item_get_priority (item);
		if (description) {
			if (!strcmp (description, E_GW_ITEM_PRIORITY_STANDARD))
				priority = 5;
			else if (!strcmp (description, E_GW_ITEM_PRIORITY_HIGH))
				priority = 3;
			else
				priority = 7;
		} else
			priority = 7;

		e_cal_component_set_priority (comp, &priority);

		/* FIXME: EGwItem's completed is a boolean */
		break;
	default :
		return NULL;
	}

	return comp;
}


