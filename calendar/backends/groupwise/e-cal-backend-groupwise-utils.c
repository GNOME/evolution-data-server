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

#include <string.h>
#include <e-gw-connection.h>
#include <e-gw-message.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-time-util.h>
#include "e-cal-backend-groupwise-utils.h"

static gboolean 
get_recur_instance (ECalComponent *comp, time_t instance_start, time_t instance_end, gpointer data)
{
	GSList **recur_dates = (GSList **) data;
	char *rdate;

	rdate = isodate_from_time_t (instance_start);
	/* convert this into a date */
	rdate[8] ='\0';
	*recur_dates = g_slist_append (*recur_dates, rdate);
	return TRUE;
}

static icaltimezone *
resolve_tzid_cb (const char *tzid, gpointer data)
{
	/* do nothing.. since we are interested only in the event date */
	return NULL;
}

const char *
e_cal_component_get_gw_id (ECalComponent *comp)
{
	icalproperty *prop;	
	
	prop = icalcomponent_get_first_property (e_cal_component_get_icalcomponent (comp),
						 ICAL_X_PROPERTY);
	while (prop) {
		const char *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!strcmp (x_name, "X-GWRECORDID")) {
			return x_val;
		}

		prop = icalcomponent_get_next_property (e_cal_component_get_icalcomponent (comp),
							ICAL_X_PROPERTY);
	}
	return NULL;
}

static void 
set_categories_for_gw_item (EGwItem *item, GList *category_names, ECalBackendGroupwise *cbgw)
{
	GHashTable *categories_by_name, *categories_by_id;
	EGwConnection *cnc;
	GList *category_ids;
	char *id;
	int status;

	category_ids = NULL;
	id = NULL;

	categories_by_name = e_cal_backend_groupwise_get_categories_by_name (cbgw);
	categories_by_id = e_cal_backend_groupwise_get_categories_by_id (cbgw);
	cnc = e_cal_backend_groupwise_get_connection (cbgw);
	
	g_assert (cnc != NULL || categories_by_name != NULL || categories_by_id != NULL);
	
	for (; category_names != NULL; category_names = g_list_next (category_names)) {
                     if (!category_names->data || strlen(category_names->data) == 0 )
                             continue;
                     id = g_hash_table_lookup (categories_by_name, category_names->data);
                     if (id)
                            category_ids = g_list_append (category_ids, g_strdup (id));
                     else {
                             EGwItem *category_item;
                            category_item = e_gw_item_new_empty();
                             e_gw_item_set_item_type (category_item,  E_GW_ITEM_TYPE_CATEGORY);
                             e_gw_item_set_category_name (category_item, category_names->data);
                             status = e_gw_connection_create_item (cnc, category_item, &id);
                             if (status == E_GW_CONNECTION_STATUS_OK && id != NULL) {
                                     char **components = g_strsplit (id, "@", -1);
                                     char *temp_id = components[0];
    
                                     g_hash_table_insert (categories_by_name, g_strdup (category_names->data), g_strdup(temp_id));
                                     g_hash_table_insert (categories_by_id, g_strdup(temp_id), g_strdup (category_names->data));
                                     category_ids = g_list_append (category_ids, g_strdup(temp_id));
                                     g_free (id);
                                     g_strfreev(components);
                             }
                             g_object_unref (category_item);
                     }
             }
             e_gw_item_set_categories (item, category_ids);
}

static EGwItem *
set_properties_from_cal_component (EGwItem *item, ECalComponent *comp, ECalBackendGroupwise *cbgw)
{
	const char *uid, *location;
	ECalComponentDateTime dt;
	ECalComponentClassification classif;
	ECalComponentTransparency transp;
	ECalComponentText text;
	int *priority;
	GList *categories;
	GSList *slist, *sl;
	icaltimezone *default_zone;
	struct icaltimetype itt_utc;
	
	default_zone = e_cal_backend_groupwise_get_default_zone (cbgw);
	
	g_assert (default_zone != NULL);
	
	/* first set specific properties */
	switch (e_cal_component_get_vtype (comp)) {
	case E_CAL_COMPONENT_EVENT :
		e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_APPOINTMENT);

		/* transparency */
		e_cal_component_get_transparency (comp, &transp);
		if (transp == E_CAL_COMPONENT_TRANSP_OPAQUE)
			e_gw_item_set_accept_level (item, E_GW_ITEM_ACCEPT_LEVEL_BUSY);
		else
			e_gw_item_set_accept_level (item, NULL);

		/* location */
		e_cal_component_get_location (comp, &location);
		e_gw_item_set_place (item, location);

		/* categories */
		e_cal_component_get_categories_list (comp, &categories);
		set_categories_for_gw_item (item, categories, cbgw);

		/* alarms */
		if (e_cal_component_has_alarms (comp)) {
			ECalComponentAlarm *alarm;
			ECalComponentAlarmTrigger trigger;
			int duration;
			GList *l = e_cal_component_get_alarm_uids (comp);

			alarm = e_cal_component_get_alarm (comp, l->data);
			e_cal_component_alarm_get_trigger (alarm, &trigger);
			duration = abs (icaldurationtype_as_int (trigger.u.rel_duration));
			e_gw_item_set_trigger (item, duration);
		}
		
		

		/* end date */
		e_cal_component_get_dtend (comp, &dt);
		if (dt.value) {
			if (!icaltime_get_timezone (*dt.value))
				icaltime_set_timezone (dt.value, default_zone);
			itt_utc = icaltime_convert_to_zone (*dt.value, icaltimezone_get_utc_timezone ());
			e_gw_item_set_end_date (item, icaltime_as_ical_string (itt_utc));
		}

		break;

	case E_CAL_COMPONENT_TODO :
		e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_TASK);

		/* due date */
		e_cal_component_get_due (comp, &dt);
		if (dt.value) {
			if (!icaltime_get_timezone (*dt.value))
				icaltime_set_timezone (dt.value, default_zone);
			itt_utc = icaltime_convert_to_zone (*dt.value, icaltimezone_get_utc_timezone ());
			e_gw_item_set_due_date (item, icaltime_as_ical_string (itt_utc));
		}

		/* priority */
		priority = NULL;
		e_cal_component_get_priority (comp, &priority);
		if (priority && *priority) {
			if (*priority >= 7)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_LOW);
			else if (*priority >= 5)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_STANDARD);
			else if (*priority >= 1)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_HIGH);
			else
				e_gw_item_set_priority (item, NULL);

			e_cal_component_free_priority (priority);
		}

		/* completed */
		e_cal_component_get_completed (comp, &dt.value);
		if (dt.value) {
			e_gw_item_set_completed (item, TRUE);
		} else
			e_gw_item_set_completed (item, FALSE);

		break;

	default :
		g_object_unref (item);
		return NULL;
	}

	/* set common properties */
	/* GW server ID */
	e_gw_item_set_id (item, e_cal_component_get_gw_id (comp));
	
	
	/* UID */
	e_cal_component_get_uid (comp, &uid);
	e_gw_item_set_icalid (item, uid);

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
		if (!icaltime_get_timezone (*dt.value))
			icaltime_set_timezone (dt.value, default_zone);
		itt_utc = icaltime_convert_to_zone (*dt.value, icaltimezone_get_utc_timezone ());
		e_gw_item_set_start_date (item, icaltime_as_ical_string (itt_utc));
	} else if (e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_APPOINTMENT) {
		/* appointments need the start date property */
		g_object_unref (item);
		return NULL;
	}

	/* creation date */
	e_cal_component_get_created (comp, &dt.value);
	if (dt.value) {
		if (!icaltime_get_timezone (*dt.value))
			icaltime_set_timezone (dt.value, default_zone);
		itt_utc = icaltime_convert_to_zone (*dt.value, icaltimezone_get_utc_timezone ());
		e_gw_item_set_creation_date (item, icaltime_as_ical_string (itt_utc));
	} else {
		struct icaltimetype itt;

		e_cal_component_get_dtstamp (comp, &itt);
		e_gw_item_set_creation_date (item, icaltime_as_ical_string (itt));
	}

	/* classification */
	e_cal_component_get_classification (comp, &classif);
	switch (classif) {
	case E_CAL_COMPONENT_CLASS_PUBLIC :
		e_gw_item_set_classification (item, E_GW_ITEM_CLASSIFICATION_PUBLIC);
		break;
	case E_CAL_COMPONENT_CLASS_PRIVATE :
		e_gw_item_set_classification (item, E_GW_ITEM_CLASSIFICATION_PRIVATE);
		break;
	case E_CAL_COMPONENT_CLASS_CONFIDENTIAL :
		e_gw_item_set_classification (item, E_GW_ITEM_CLASSIFICATION_CONFIDENTIAL);
		break;
	default :
		e_gw_item_set_classification (item, NULL);
	}

	/* get_attendee_list from cal comp and convert into
	 * egwitemrecipient and set it on recipient_list*/
	if (e_cal_component_has_attendees (comp)) {
		GSList *attendee_list, *recipient_list = NULL, *al;

		e_cal_component_get_attendee_list (comp, &attendee_list);	
		for (al = attendee_list; al != NULL; al = al->next) {
			ECalComponentAttendee *attendee = (ECalComponentAttendee *) al->data;
			EGwItemRecipient *recipient = g_new0 (EGwItemRecipient, 1);
			
			/* len (MAILTO:) + 1 = 7 */
			recipient->email = g_strdup (attendee->value + 7);
			if (attendee->cn != NULL)
				recipient->display_name = g_strdup (attendee->cn);
			if (attendee->role == ICAL_ROLE_REQPARTICIPANT) 
				recipient->type = E_GW_ITEM_RECIPIENT_TO;
			else if (attendee->role == ICAL_ROLE_OPTPARTICIPANT)
				recipient->type = E_GW_ITEM_RECIPIENT_CC;
			else recipient->type = E_GW_ITEM_RECIPIENT_NONE;

			if (attendee->status == ICAL_PARTSTAT_ACCEPTED)
				recipient->status = E_GW_ITEM_STAT_ACCEPTED;
			else if (attendee->status == ICAL_PARTSTAT_DECLINED)
				recipient->status = E_GW_ITEM_STAT_DECLINED;
			else 
				recipient->status = E_GW_ITEM_STAT_NONE;

			recipient_list = g_slist_append (recipient_list, recipient);
		}
				
		e_gw_item_set_recipient_list (item, recipient_list);
	}

	if (e_cal_component_has_organizer (comp)) {
		ECalComponentOrganizer cal_organizer;
		EGwItemOrganizer *organizer = NULL;

		e_cal_component_get_organizer (comp, &cal_organizer);
		organizer = g_new0 (EGwItemOrganizer, 1);
		organizer->display_name = g_strdup (cal_organizer.cn);
		organizer->email = g_strdup (cal_organizer.value + 7);
		e_gw_item_set_organizer (item, organizer);
	}

	
	/* check if recurrences exist and update the item */
	if (e_cal_component_has_recurrences (comp)) {

		GSList *recur_dates = NULL;
		

		e_cal_recur_generate_instances (comp, -1, -1,
				get_recur_instance, &recur_dates, resolve_tzid_cb, NULL, 
				(icaltimezone *) default_zone);		
		recur_dates = g_slist_delete_link (recur_dates, recur_dates);
		
		e_gw_item_set_recurrence_dates (item, recur_dates);
	    }

	return item;
}

EGwItem *
e_gw_item_new_from_cal_component (const char *container, ECalBackendGroupwise *cbgw, ECalComponent *comp)
{
	EGwItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	item = e_gw_item_new_empty ();
	e_gw_item_set_container_id (item, container);
	
	return set_properties_from_cal_component (item, comp, cbgw);
}

ECalComponent *
e_gw_item_to_cal_component (EGwItem *item, ECalBackendGroupwise *cbgw)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt;
	const char *description;
	char *t, *name;
	GList *category_ids, *categories;
	GHashTable *categories_by_id;
	icaltimezone *default_zone;

	struct icaltimetype itt, itt_utc;
	int priority;
	int percent;
	int alarm_duration;
	GSList *recipient_list, *rl, *attendee_list = NULL;
	EGwItemOrganizer *organizer;
	EGwItemType item_type;

	default_zone = e_cal_backend_groupwise_get_default_zone (cbgw);
	categories_by_id = e_cal_backend_groupwise_get_categories_by_id (cbgw);

	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	g_assert (default_zone != NULL || categories_by_id != NULL);

	comp = e_cal_component_new ();

	item_type = e_gw_item_get_item_type (item);

	if (item_type == E_GW_ITEM_TYPE_APPOINTMENT)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	else if (item_type == E_GW_ITEM_TYPE_TASK)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
	else {
		g_object_unref (comp);
		return NULL;
	}

	/* set common properties */
	/* GW server ID */
	description = e_gw_item_get_id (item);
	if (description) {
		icalproperty *icalprop;

		icalprop = icalproperty_new_x (description);
		icalproperty_set_x_name (icalprop, "X-GWRECORDID");
		icalcomponent_add_property (e_cal_component_get_icalcomponent (comp), icalprop);
	}

	/* UID */
	e_cal_component_set_uid (comp, e_gw_item_get_icalid (item));

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
	t = e_gw_item_get_creation_date (item);
	itt_utc = icaltime_from_string (t);
	if (!icaltime_get_timezone (itt_utc))
		icaltime_set_timezone (&itt_utc, icaltimezone_get_utc_timezone());
	if (default_zone) {
		itt = icaltime_convert_to_zone (itt_utc, default_zone); 
		icaltime_set_timezone (&itt, default_zone);
		e_cal_component_set_created (comp, &itt);
		e_cal_component_set_dtstamp (comp, &itt);
		
	} else {
		e_cal_component_set_created (comp, &itt_utc);
		e_cal_component_set_dtstamp (comp, &itt_utc);
	}
	g_free (t);
	
	/* categories */
	category_ids = e_gw_item_get_categories (item);
	categories = NULL;
	if (category_ids) {
		for (; category_ids != NULL; category_ids = g_list_next (category_ids)) {
			name = g_hash_table_lookup (categories_by_id, category_ids->data);
			if (name)
				categories = g_list_append (categories, name);
		}
		if (categories) {
			e_cal_component_set_categories_list (comp,categories);
			g_list_free (categories);
		}
	}	

	/* start date */
	/* should i duplicate here ? */
	t = e_gw_item_get_start_date (item);
	if (t) {
		itt_utc = icaltime_from_string (t);
		if (!icaltime_get_timezone (itt_utc))
			icaltime_set_timezone (&itt_utc, icaltimezone_get_utc_timezone());
		if (default_zone) {
			itt = icaltime_convert_to_zone (itt_utc, default_zone); 
			icaltime_set_timezone (&itt, default_zone);
			dt.value = &itt;
			dt.tzid = icaltimezone_get_tzid (default_zone);
		} else {
			dt.value = &itt_utc;
			dt.tzid = g_strdup ("UTC");
		}
		e_cal_component_set_dtstart (comp, &dt);
		g_free (t);
	}
	else 
		return NULL;
	

	/* classification */
	description = e_gw_item_get_classification (item);
	if (description) {
		if (strcmp (description, E_GW_ITEM_CLASSIFICATION_PUBLIC) == 0)
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
		else if (strcmp (description, E_GW_ITEM_CLASSIFICATION_PRIVATE) == 0)
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PRIVATE);
		else if (strcmp (description, E_GW_ITEM_CLASSIFICATION_CONFIDENTIAL) == 0)
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_CONFIDENTIAL);
		else
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);
	} else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_NONE);

	recipient_list = e_gw_item_get_recipient_list (item);
	if (recipient_list != NULL) {
		for (rl = recipient_list; rl != NULL; rl = rl->next) {
			EGwItemRecipient *recipient = (EGwItemRecipient *) rl->data;
			ECalComponentAttendee *attendee = g_new0 (ECalComponentAttendee, 1);

			attendee->cn = g_strdup (recipient->display_name);
			attendee->value = g_strconcat("MAILTO:", recipient->email, NULL);
			if (recipient->type == E_GW_ITEM_RECIPIENT_TO)
				attendee->role = ICAL_ROLE_REQPARTICIPANT;
			else if (recipient->type == E_GW_ITEM_RECIPIENT_CC || recipient->type == E_GW_ITEM_RECIPIENT_BC)
				attendee->role = ICAL_ROLE_OPTPARTICIPANT;
			else 
				attendee->role = ICAL_ROLE_NONE;
			/* FIXME  needs a server fix on the interface 
			 * for getting cutype and the status */
			attendee->cutype = ICAL_CUTYPE_INDIVIDUAL;
			 
			if (recipient->status == E_GW_ITEM_STAT_ACCEPTED)
				attendee->status = ICAL_PARTSTAT_ACCEPTED;
			else if (recipient->status == E_GW_ITEM_STAT_DECLINED)
				attendee->status = ICAL_PARTSTAT_DECLINED;
			else
				attendee->status = ICAL_PARTSTAT_NEEDSACTION;	
				
			
			attendee_list = g_slist_append (attendee_list, attendee);				
		}

		e_cal_component_set_attendee_list (comp, attendee_list);
	}

	/* set organizer if it exists */
	organizer = e_gw_item_get_organizer (item);
	if (organizer) {
		ECalComponentOrganizer *cal_organizer;
		
		cal_organizer = g_new0 (ECalComponentOrganizer, 1);
		cal_organizer->cn = g_strdup (organizer->display_name);
		cal_organizer->value = g_strconcat("MAILTO:", organizer->email, NULL);
		e_cal_component_set_organizer (comp, cal_organizer);
	}

	/* set specific properties */
	switch (item_type) {
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

		/* end date */
		t = e_gw_item_get_end_date (item);
		if (t) {
			itt_utc = icaltime_from_string (t);
			if (!icaltime_get_timezone (itt_utc))
				icaltime_set_timezone (&itt_utc, icaltimezone_get_utc_timezone());
			if (default_zone) {
				itt = icaltime_convert_to_zone (itt_utc, default_zone); 
				icaltime_set_timezone (&itt, default_zone);
				dt.value = &itt;
				dt.tzid = icaltimezone_get_tzid (default_zone);
			} else {
				dt.value = &itt_utc;
				dt.tzid = g_strdup ("UTC");
			}
		
			e_cal_component_set_dtend (comp, &dt);
		}
		

		/* alarms*/
		/* we negate the value as GW supports only "before" the start of event alarms */
		alarm_duration = 0 - e_gw_item_get_trigger (item);
		if (alarm_duration != 0) {
			ECalComponentAlarm *alarm;
			ECalComponentAlarmTrigger trigger;
			
			alarm = e_cal_component_alarm_new ();
			e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
			trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
			trigger.u.rel_duration = (struct icaldurationtype) icaldurationtype_from_int (alarm_duration);
			e_cal_component_alarm_set_trigger (alarm, trigger);
			e_cal_component_add_alarm (comp, alarm);
		}

		

		break;
	case E_GW_ITEM_TYPE_TASK :
		/* due date */
		t = e_gw_item_get_due_date (item);
		if (t) {
			itt_utc = icaltime_from_string (t);
			if (!icaltime_get_timezone (itt_utc))
				icaltime_set_timezone (&itt_utc, icaltimezone_get_utc_timezone());
			if (default_zone) {
				itt = icaltime_convert_to_zone (itt_utc, default_zone); 
				icaltime_set_timezone (&itt, default_zone);
				dt.value = &itt;
				dt.tzid = icaltimezone_get_tzid (default_zone);
			} else {
				dt.value = &itt_utc;
				dt.tzid = g_strdup ("UTC");
			}
			e_cal_component_set_due (comp, &dt);
		}
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

		/* EGwItem's completed is a boolean */
		if (e_gw_item_get_completed (item)) 
			percent = 100;
		else 
			percent =0;
		e_cal_component_set_percent (comp, &percent);

		break;
	default :
		return NULL;
	}

	return comp;
}

EGwConnectionStatus
e_gw_connection_send_appointment (EGwConnection *cnc, const char *container, ECalComponent *comp, icalproperty_method method, gboolean *remove)
{
	EGwConnectionStatus status;
	icalparameter_partstat partstat;
	char *item_id;
	

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	e_cal_component_commit_sequence (comp);
	/* When the icalcomponent is obtained through the itip message rather
	 * than from the SOAP protocol, the container id has to be explicitly 
	 * added to the xgwrecordid inorder to obtain the item id. */
	
	switch  (e_cal_component_get_vtype (comp)) {
	case E_CAL_COMPONENT_EVENT: 
		item_id = g_strconcat (e_cal_component_get_gw_id (comp), GW_EVENT_TYPE_ID, container, NULL);
		break;
	case E_CAL_COMPONENT_TODO:
		item_id = g_strconcat (e_cal_component_get_gw_id (comp), GW_TODO_TYPE_ID, container, NULL);
		break;
	default:
		return E_GW_CONNECTION_STATUS_INVALID_OBJECT;
	}
	switch (method) {
	case ICAL_METHOD_REQUEST:
		/* get attendee here and add the list along. */
		if (e_cal_component_has_attendees (comp)) {
			GSList *attendee_list, *l;
			ECalComponentAttendee  *attendee = NULL, *tmp;

			
			e_cal_component_get_attendee_list (comp, &attendee_list);
			for (l = attendee_list; l ; l = g_slist_next (l)) {
				tmp = (ECalComponentAttendee *) (l->data);
				if (!strcmp (tmp->value + 7, e_gw_connection_get_user_email (cnc))) {
					attendee = tmp;
					break;
				}
			}
			if (attendee) {
				partstat = attendee->status;
			}
			else {
				status = E_GW_CONNECTION_STATUS_INVALID_OBJECT;
				break;
			}
			if (attendee_list)
				e_cal_component_free_attendee_list (attendee_list);
			
		}
		else {
			status = E_GW_CONNECTION_STATUS_INVALID_OBJECT;
			break;
		}
		switch (partstat) {
		ECalComponentTransparency transp;
			
		case ICAL_PARTSTAT_ACCEPTED: 
			e_cal_component_get_transparency (comp, &transp);
			if (transp == E_CAL_COMPONENT_TRANSP_OPAQUE) 
				status = e_gw_connection_accept_request (cnc, item_id, "Busy");
			else 
				status = e_gw_connection_accept_request (cnc, item_id, "Free");
			break;
		case ICAL_PARTSTAT_DECLINED:
			status = e_gw_connection_decline_request (cnc, item_id);
			*remove = TRUE;
			break;
		case ICAL_PARTSTAT_TENTATIVE:
			status = e_gw_connection_accept_request (cnc, item_id, "Tentative");
			break;
		case ICAL_PARTSTAT_COMPLETED:
			status = e_gw_connection_complete_request (cnc, item_id);

		default :
			status = E_GW_CONNECTION_STATUS_INVALID_OBJECT;
	
		}

		break;

	case ICAL_METHOD_CANCEL:
		status = e_gw_connection_retract_request (cnc, item_id, NULL, FALSE, FALSE);
		*remove = TRUE;
		break;
	default:
		status = E_GW_CONNECTION_STATUS_INVALID_OBJECT;
	}	

	return status;
}

EGwConnectionStatus
e_gw_connection_create_appointment (EGwConnection *cnc, const char *container, ECalBackendGroupwise *cbgw, ECalComponent *comp, GSList **id_list)
{
	EGwItem *item;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	item = e_gw_item_new_from_cal_component (container, cbgw, comp);
	e_gw_item_set_container_id (item, container);
	status = e_gw_connection_send_item (cnc, item, id_list);
	g_object_unref (item);

	return status;
}

static EGwConnectionStatus
start_freebusy_session (EGwConnection *cnc, GList *users, 
               time_t start, time_t end, const char **session)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;
        GList *l;
        icaltimetype icaltime;
	icaltimezone *utc;
        const char *start_date, *end_date;

	if (users == NULL)
                return E_GW_CONNECTION_STATUS_INVALID_OBJECT;

        /* build the SOAP message */
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc),
					    e_gw_connection_get_session_id (cnc),
					    "startFreeBusySessionRequest");
        /* FIXME users is just a buch of user names - associate it with uid,
         * email id apart from the name*/
        
        soup_soap_message_start_element (msg, "users", NULL, NULL); 
        for ( l = users; l != NULL; l = g_list_next (l)) {
		soup_soap_message_start_element (msg, "user", NULL, NULL); 
                e_gw_message_write_string_parameter (msg, "email", NULL, l->data);
		soup_soap_message_end_element (msg);
        }

        soup_soap_message_end_element (msg);


	utc = icaltimezone_get_utc_timezone ();
	icaltime = icaltime_from_timet_with_zone (start, FALSE, utc);
	start_date = icaltime_as_ical_string (icaltime);
	
	icaltime = icaltime_from_timet_with_zone (end, FALSE, utc);
	end_date = icaltime_as_ical_string (icaltime);
        	
        e_gw_message_write_string_parameter (msg, "startDate", NULL, start_date);
        e_gw_message_write_string_parameter (msg, "endDate", NULL, end_date);
        
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK)
        {
                g_object_unref (msg);
                g_object_unref (response);
                return status;
        }
        
       	/* if status is OK - parse result, return the list */
        param = soup_soap_response_get_first_parameter_by_name (response, "freeBusySessionId");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
	*session = soup_soap_parameter_get_string_value (param); 
        /* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

static EGwConnectionStatus 
close_freebusy_session (EGwConnection *cnc, const char *session)
{
        SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

        /* build the SOAP message */
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc),
					    e_gw_connection_get_session_id (cnc),
					    "closeFreeBusySessionRequest");
       	e_gw_message_write_string_parameter (msg, "freeBusySessionId", NULL, session);
        e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);

        g_object_unref (msg);
        g_object_unref (response);
        return status;
}

EGwConnectionStatus
e_gw_connection_get_freebusy_info (EGwConnection *cnc, GList *users, time_t start, time_t end, GList **freebusy, icaltimezone *default_zone)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;
        const char *session;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

        /* Perform startFreeBusySession */
        status = start_freebusy_session (cnc, users, start, end, &session); 
        /*FIXME log error messages  */
        if (status != E_GW_CONNECTION_STATUS_OK)
                return status;

        /* getFreeBusy */
        /* build the SOAP message */
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc),
					    e_gw_connection_get_session_id (cnc),
					    "getFreeBusyRequest");
       	e_gw_message_write_string_parameter (msg, "freeBusySessionId", NULL, session);
        e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
                g_object_unref (msg);
                g_object_unref (response);
                return status;
        }

        /* FIXME  the FreeBusyStats are not used currently.  */
        param = soup_soap_response_get_first_parameter_by_name (response, "freeBusyInfo");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        for (subparam = soup_soap_parameter_get_first_child_by_name (param, "user");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "user")) {
		SoupSoapParameter *param_blocks, *subparam_block, *tmp;
		const char *uuid = NULL, *email = NULL, *name = NULL;
		ECalComponent *comp;
		ECalComponentAttendee attendee;
		GSList *attendee_list = NULL;
		icalcomponent *icalcomp = NULL;
		
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "email");
		if (tmp)
			email = soup_soap_parameter_get_string_value (tmp);
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "uuid");
		if (tmp)
			uuid = soup_soap_parameter_get_string_value (tmp);
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "displayName");
		if (tmp)
			name = soup_soap_parameter_get_string_value (tmp);

		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_FREEBUSY); 
		e_cal_component_commit_sequence (comp);
		icalcomp = e_cal_component_get_icalcomponent (comp);
		
		memset (&attendee, 0, sizeof (ECalComponentAttendee));
		if (name)
			attendee.cn = name;
		if (email)
			attendee.value = email;
		
		attendee.cutype = ICAL_CUTYPE_INDIVIDUAL;
		attendee.role = ICAL_ROLE_REQPARTICIPANT;
		attendee.status = ICAL_PARTSTAT_NEEDSACTION;

		/* XXX the uuid is not currently used. hence it is
		 * discarded */

		attendee_list = g_slist_append (attendee_list, &attendee);	

		e_cal_component_set_attendee_list (comp, attendee_list);

		
		param_blocks = soup_soap_parameter_get_first_child_by_name (subparam, "blocks");
		if (!param_blocks) {
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}
        
		for (subparam_block = soup_soap_parameter_get_first_child_by_name (param_blocks, "block");
		     subparam_block != NULL;
		     subparam_block = soup_soap_parameter_get_next_child_by_name (subparam_block, "block")) {

			/* process each block and create ECal free/busy components.*/ 
			SoupSoapParameter *tmp;
			struct icalperiodtype ipt;
			icalproperty *icalprop;
			icaltimetype itt;
			time_t t;
			const char *start, *end, *accept_level;
			
			memset (&ipt, 0, sizeof (struct icalperiodtype));
			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "startDate");
			if (tmp) {
				start = soup_soap_parameter_get_string_value (tmp);
				t = e_gw_connection_get_date_from_string (start);
				itt = icaltime_from_timet_with_zone (t, 0, default_zone ? default_zone : 0);
				ipt.start = itt;
			}        

			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "endDate");
			if (tmp) {
				end = soup_soap_parameter_get_string_value (tmp);
				t = e_gw_connection_get_date_from_string (end);
				itt = icaltime_from_timet_with_zone (t, 0, default_zone ? default_zone : 0);
				ipt.end = itt;
			}
			icalprop = icalproperty_new_freebusy (ipt);

			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "acceptLevel");
			if (tmp) {
				accept_level = soup_soap_parameter_get_string_value (tmp);
				if (!strcmp (accept_level, "Busy"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "BUSY");
				else if (!strcmp (accept_level, "Tentative"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "BUSYTENTATIVE");
				else if (!strcmp (accept_level, "OutOfOffice"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "BUSYUNAVAILABLE");
				else if (!strcmp (accept_level, "Free"))
					icalproperty_set_parameter_from_string (icalprop, "FBTYPE", "FREE");
							}
			icalcomponent_add_property(icalcomp, icalprop);

		}

		e_cal_component_commit_sequence (comp);
		*freebusy = g_list_append (*freebusy, g_strdup (e_cal_component_get_as_string (comp)));
		g_object_unref (comp);
	}

        g_object_unref (msg);
        g_object_unref (response);

        /* closeFreeBusySession*/
        return close_freebusy_session (cnc, session);
}

#define SET_DELTA(fieldname) G_STMT_START{                                                                \
	fieldname = e_gw_item_get_##fieldname (item);                                                       \
	cache_##fieldname = e_gw_item_get_##fieldname (cache_item);                                           \
	if ( cache_##fieldname ) {                                                                            \
		if (!fieldname )                                                                               \
			e_gw_item_set_change (item, E_GW_ITEM_CHANGE_TYPE_DELETE, #fieldname, (gpointer) cache_##fieldname );\
		else if (strcmp ( fieldname, cache_##fieldname ))                                               \
			e_gw_item_set_change (item, E_GW_ITEM_CHANGE_TYPE_UPDATE, #fieldname, (gpointer) fieldname );\
	}                                                                                                 \
	else if ( fieldname )                                                                               \
		e_gw_item_set_change (item, E_GW_ITEM_CHANGE_TYPE_ADD, #fieldname, (gpointer) fieldname );           \
	}G_STMT_END

static void 
set_categories_changes (EGwItem *new_item, EGwItem *old_item)
{
	GList *old_category_list;
	GList *new_category_list;
	GList *temp, *old_categories_copy, *added_categories = NULL;
	gboolean categories_matched;
	char *category1, *category2;
	old_category_list = e_gw_item_get_categories (old_item);
	new_category_list = e_gw_item_get_categories (new_item);
	if (old_category_list && new_category_list) {
		old_categories_copy = g_list_copy (old_category_list);
		for ( ; new_category_list != NULL; new_category_list = g_list_next (new_category_list)) {
			
			category1  = new_category_list->data;
			temp = old_category_list;
			categories_matched  = FALSE;
			for(; temp != NULL; temp = g_list_next (temp)) {
				category2 = temp->data;
				if ( g_str_equal (category1, category2)) {
					categories_matched = TRUE;
					old_categories_copy = g_list_remove (old_categories_copy, category2);
					break;
				}
				
			}
			if (!categories_matched)
				added_categories = g_list_append (added_categories, category1);
		}
		
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "categories", added_categories);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "categories", old_categories_copy);

	} else if (!new_category_list && old_category_list) {
		e_gw_item_set_change (new_item,  E_GW_ITEM_CHANGE_TYPE_DELETE, "categories", old_category_list);
	} else if (new_category_list && !old_category_list) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "categories", new_category_list);
	}

}

void
e_gw_item_set_changes (EGwItem *item, EGwItem *cache_item)
{
	const char *subject, *cache_subject;
	const char *message, *cache_message;
	const char *classification, *cache_classification;
	const char *accept_level, *cache_accept_level;
	const char *place, *cache_place;
	const char *priority, *cache_priority;
	int trigger, cache_trigger;
	char *due_date, *cache_due_date;
	char *start_date, *cache_start_date;
	char *end_date, *cache_end_date;

	/* TODO assert the types of the items are the same */

	SET_DELTA(subject);
	SET_DELTA(message);
	SET_DELTA(classification);

	
	SET_DELTA(start_date);
	set_categories_changes (item, cache_item);
	/*FIXME  recipient_list modifications need go here after server starts
	 * supporting retraction */
	if (e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_APPOINTMENT) {

		SET_DELTA(end_date);
		SET_DELTA(accept_level);
		SET_DELTA(place);
		trigger = e_gw_item_get_trigger (item);
		cache_trigger = e_gw_item_get_trigger (cache_item);
		if (cache_trigger) {                                                                            
			if (!trigger)                                                                               
				e_gw_item_set_change (item, E_GW_ITEM_CHANGE_TYPE_DELETE, "alarm", &cache_trigger);
			else if (trigger != cache_trigger)
				e_gw_item_set_change (item, E_GW_ITEM_CHANGE_TYPE_UPDATE, "alarm", &trigger);
		}                                                                                                 
		else if (trigger)                                                                               
			e_gw_item_set_change (item, E_GW_ITEM_CHANGE_TYPE_ADD, "alarm", &trigger);
	}
	else if ( e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_TASK) {
		SET_DELTA(due_date);
		SET_DELTA (priority);
	}
}
