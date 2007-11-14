/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-mapi.h"
#include "e-cal-backend-mapi-utils.h"

/*
 * Priority
 */

#define	PRIORITY_LOW 	-1
#define	PRIORITY_NORMAL 0
#define	PRIORITY_HIGH 	1

/*
 * Importance
 */

#define	IMPORTANCE_LOW 		0
#define	IMPORTANCE_NORMAL 	1
#define	IMPORTANCE_HIGH		2

/*
 * Sensitivity
 */

#define	SENSITIVITY_NORMAL 		0
#define	SENSITIVITY_PERSONAL 		1
#define	SENSITIVITY_PRIVATE 		2
#define SENSITIVITY_CONFIDENTIAL 	3

/*
 * Appointment flags with PR_APPOINTMENT_BUSY_STATUS
 */

#define	BUSY_STATUS_FREE 	0
#define	BUSY_STATUS_TENTATIVE 	1
#define	BUSY_STATUS_BUSY 	2
#define	BUSY_STATUS_OUTOFOFFICE 3

/*
 * Task status
 */

#define	olTaskNotStarted 	0
#define	olTaskInProgress 	1
#define	olTaskComplete 		2
#define	olTaskWaiting 		3
#define	olTaskDeferred 		4

static struct icaltimetype
foo (const time_t tm, const int is_date, const icaltimezone *comp_zone)
{
	struct icaltimetype itt_utc;
	struct icaltimetype itt;
	const icaltimezone *utc_zone;

	utc_zone = icaltimezone_get_utc_timezone ();

	/* First, get the time in UTC */
	itt_utc = icaltime_from_timet_with_zone (tm, is_date, 0);
	icaltime_set_timezone (&itt_utc, utc_zone);

	if (comp_zone) {
		itt = icaltime_convert_to_zone (itt_utc, comp_zone);
		itt = icaltime_set_timezone (&itt, comp_zone);
	} else {
		itt = icaltime_convert_to_zone (itt_utc, utc_zone);
		itt = icaltime_set_timezone (&itt, utc_zone);
	}

	return itt;
}

void
e_cal_backend_mapi_props_to_comp (struct mapi_SPropValue_array *properties, ECalComponent *comp, const icaltimezone *default_zone)
{
	struct timeval t;
	const char *subject = NULL;
	const char *body = NULL;
	const char *location = NULL;
	const uint32_t *priority;
	const uint32_t *sensitivity;
	icalcomponent *ical_comp;
	icalproperty *prop = NULL;
	const icaltimezone *utc_zone;

	ical_comp = e_cal_component_get_icalcomponent (comp);
	utc_zone = icaltimezone_get_utc_timezone ();

//	subject = (const char *)find_mapi_SPropValue_data(properties, PR_CONVERSATION_TOPIC);
	/* FIXME: you gotta better way to do this ?? */
	if (!subject) {
		const gchar *tmp;
		tmp = (const char *)find_mapi_SPropValue_data(properties, PR_URL_COMP_NAME);
		/* the PR_URL_COMP_NAME would end with ".EML". Remove that portion. */
		if (tmp && g_str_has_suffix (tmp, ".EML")) {
			subject = g_strndup (tmp, (strlen(tmp) - 4));
		}
	} 
	body = (const char *)find_mapi_SPropValue_data(properties, PR_BODY);
	location = (const char *)find_mapi_SPropValue_data(properties, 0x8208001e);

	/* set dtstamp - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_CREATION_TIME) == MAPI_E_SUCCESS)
		icalcomponent_set_dtstamp (ical_comp, foo (t.tv_sec, 0, 0));

	/* created - in UTC */
	if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_CREATED_PROPERTY)) != 0)
		icalcomponent_remove_property (ical_comp, prop);
	prop = icalproperty_new_created (icaltime_current_time_with_zone (utc_zone));
	icalcomponent_add_property (ical_comp, prop);
	
	/* last modified - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS) {
		if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_LASTMODIFIED_PROPERTY)) != 0)
			icalcomponent_remove_property (ical_comp, prop);
		prop = icalproperty_new_lastmodified (foo (t.tv_sec, 0, 0));
		icalcomponent_add_property (ical_comp, prop);
	}

	if (subject && *subject)
		icalcomponent_set_summary (ical_comp, subject);
	if (body && *body)
		icalcomponent_set_description (ical_comp, body);
	if (location && *location)
		icalcomponent_set_location (ical_comp, location);

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		const uint32_t *transp;
		const bool *all_day;

		all_day = (const bool *)find_mapi_SPropValue_data(properties, 0x8215000B);

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_START_DATE) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, (all_day && *all_day), default_zone));

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_END_DATE) == MAPI_E_SUCCESS)
			icalcomponent_set_dtend (ical_comp, foo (t.tv_sec, (all_day && *all_day), default_zone));

		transp = (const uint32_t *)find_mapi_SPropValue_data(properties, 0x82050003);
		if (transp) {
			icalproperty_transp ical_transp;
			switch (*transp) {
				/* FIXME: is this mapping correct ? */
				case BUSY_STATUS_TENTATIVE:
				case BUSY_STATUS_FREE:
					ical_transp = ICAL_TRANSP_TRANSPARENT;
					break;
				/* FIXME: is this mapping correct ? */
				case BUSY_STATUS_OUTOFOFFICE:
				case BUSY_STATUS_BUSY:
					ical_transp = ICAL_TRANSP_OPAQUE;
					break;
				default:
					ical_transp = ICAL_TRANSP_OPAQUE;
					break;
			}
			if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_TRANSP_PROPERTY)) != 0)
				icalcomponent_remove_property (ical_comp, prop);
			prop = icalproperty_new_transp (ical_transp);
			icalcomponent_add_property (ical_comp, prop);
		}
	} else if (icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		const double *complete = 0;
		const uint32_t *status;

		/* NOTE: Exchange tasks are DATE values, not DATE-TIME values, but maybe someday, we could expect Exchange to support it ;) */
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, 0x81040040) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, 0, default_zone));
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, 0x81050040) == MAPI_E_SUCCESS)
			icalcomponent_set_due (ical_comp, foo (t.tv_sec, 0, default_zone));

		status = (const uint32_t *)find_mapi_SPropValue_data(properties, 0x81010003);
		if (status) {
			icalproperty_status ical_status;
			switch (*status) {
				case olTaskNotStarted:
					ical_status = ICAL_STATUS_NEEDSACTION;
					break;
				/* FIXME: is this mapping correct ? */
				case olTaskWaiting:
				case olTaskInProgress:
					ical_status = ICAL_STATUS_INPROCESS;
					break;
				case olTaskComplete:
					ical_status = ICAL_STATUS_COMPLETED;
					break;
				case olTaskDeferred:
					ical_status = ICAL_STATUS_CANCELLED;
					break;
				default:
					ical_status = ICAL_STATUS_NEEDSACTION;
					break;
			}
			icalcomponent_set_status (ical_comp, ical_status);
			if (*status == olTaskComplete && get_mapi_SPropValue_array_date_timeval (&t, properties, 0x810F0040) == MAPI_E_SUCCESS) {
				if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_COMPLETED_PROPERTY)) != 0)
					icalcomponent_remove_property (ical_comp, prop);
				prop = icalproperty_new_completed (foo (t.tv_sec, 0, default_zone));
				icalcomponent_add_property (ical_comp, prop);
			}
		}

		complete = (const double *)find_mapi_SPropValue_data(properties, 0x81020005);
		if (complete) {
			if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_PERCENTCOMPLETE_PROPERTY)) != 0)
				icalcomponent_remove_property (ical_comp, prop);
			prop = icalproperty_new_percentcomplete ((int)(*complete * 100));
			icalcomponent_add_property (ical_comp, prop);
		}
	}

	/* priority */
	priority = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_PRIORITY);
	if (priority) {
		int ical_priority;
		switch (*priority) {
			case PRIORITY_LOW:
				ical_priority = 7;
				break;
			case PRIORITY_NORMAL:
				ical_priority = 5;
				break;
			case PRIORITY_HIGH:
				ical_priority = 1;
				break;
			default: 
				ical_priority = 5;
				break;
		}
		if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_PRIORITY_PROPERTY)) != 0)
			icalcomponent_remove_property (ical_comp, prop);
		prop = icalproperty_new_priority (ical_priority);
		icalcomponent_add_property (ical_comp, prop);
	}

	/* classification */
	sensitivity = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_SENSITIVITY);
	if (sensitivity) {
		icalproperty_class ical_class = ICAL_CLASS_NONE;
		switch (*sensitivity) {
			case SENSITIVITY_NORMAL:
				ical_class = ICAL_CLASS_PUBLIC;
				break;
			/* FIXME: is this mapping correct ? */
			case SENSITIVITY_PERSONAL:
			case SENSITIVITY_PRIVATE:
				ical_class = ICAL_CLASS_PRIVATE;
				break;
			case SENSITIVITY_CONFIDENTIAL:
				ical_class = ICAL_CLASS_CONFIDENTIAL;
				break;
			default: 
				ical_class = ICAL_CLASS_PUBLIC;
				break;
		}
		if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_CLASS_PROPERTY)) != 0)
			icalcomponent_remove_property (ical_comp, prop);
		prop = icalproperty_new_class (ical_class);
		icalcomponent_add_property (ical_comp, prop);
	}

	/* FIXME: attachments */
	/* FIXME: categories */

	/* FIXME: the ALARM definitely needs more work */
	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		struct timeval start, before;

		if ((get_mapi_SPropValue_array_date_timeval (&start, properties, 0x85020040) == MAPI_E_SUCCESS) 
		 && (get_mapi_SPropValue_array_date_timeval (&before, properties, 0x85600040) == MAPI_E_SUCCESS)) {
			ECalComponentAlarm *e_alarm = e_cal_component_alarm_new ();
			ECalComponentAlarmTrigger trigger;

			trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
			trigger.u.rel_duration = icaltime_subtract (icaltime_from_timet_with_zone (before.tv_sec, 0, 0), icaltime_from_timet_with_zone (start.tv_sec, 0, 0));

			e_cal_component_alarm_set_action (e_alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
			e_cal_component_alarm_set_trigger (e_alarm, trigger);

			e_cal_component_add_alarm (comp, e_alarm);
		}
	}

	e_cal_component_rescan (comp);
}

gboolean
build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	ECalComponent *comp = E_CAL_COMPONENT (data);

	mapi_nameid_OOM_add(nameid, "Location", PSETID_Appointment);
	mapi_nameid_OOM_add(nameid, "BusyStatus", PSETID_Appointment);
	mapi_nameid_OOM_add(nameid, "MeetingStatus", PSETID_Appointment);
	mapi_nameid_OOM_add(nameid, "CommonStart", PSETID_Common);
	mapi_nameid_OOM_add(nameid, "CommonEnd", PSETID_Common);
//	mapi_nameid_OOM_add(nameid, "Label", PSETID_Appointment);
//	mapi_nameid_OOM_add(nameid, "ReminderMinutesBeforeStart", PSETID_Common);

	return TRUE;
}

int
build_props (struct SPropValue **value, struct SPropTagArray *SPropTagArray, gpointer data)
{
	ECalComponent *comp = E_CAL_COMPONENT (data);
	icalcomponent *ical_comp = e_cal_component_get_icalcomponent (comp);
	struct SPropValue *props;
	int i=0;
	struct timeval t;
	const icaltimezone *utc_zone;
	uint32_t flag;

	utc_zone = icaltimezone_get_utc_timezone ();

	props = g_new (struct SPropValue, 40); //FIXME: Correct value tbd

	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, 
						   (const void *) icalcomponent_get_summary (ical_comp));
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, 
						   (const void *) icalcomponent_get_summary (ical_comp));

	t.tv_sec = icaltime_as_timet_with_zone (icalcomponent_get_dtstart (ical_comp), utc_zone);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], PR_START_DATE, &t);

	t.tv_sec = icaltime_as_timet_with_zone (icalcomponent_get_dtend (ical_comp), utc_zone);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], PR_END_DATE, &t);

	set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *)"IPM.Appointment");

	flag = 1;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (const void *) &flag);

	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[0], (const void *) icalcomponent_get_location (ical_comp));

	flag = 2;
	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[1], (const void *) &flag);

	flag= MEETING_STATUS_NONMEETING;
	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[2], (const void *) &flag);

//	flag2 = true;

	set_SPropValue_proptag_date_timeval(&props[i++], SPropTagArray->aulPropTag[3], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], SPropTagArray->aulPropTag[4], &t);

//	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[5], (const void *)&oclient->label);

//	flag = 30;
//	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[6], (const void *)&flag);

	set_SPropValue_proptag(&props[i++], PR_BODY, (const void *) icalcomponent_get_description (ical_comp));

	*value = props;
	return i;
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-mapi.h"
#include "e-cal-backend-mapi-utils.h"

/*
 * Priority
 */

#define	PRIORITY_LOW 	-1
#define	PRIORITY_NORMAL 0
#define	PRIORITY_HIGH 	1

/*
 * Importance
 */

#define	IMPORTANCE_LOW 		0
#define	IMPORTANCE_NORMAL 	1
#define	IMPORTANCE_HIGH		2

/*
 * Sensitivity
 */

#define	SENSITIVITY_NORMAL 		0
#define	SENSITIVITY_PERSONAL 		1
#define	SENSITIVITY_PRIVATE 		2
#define SENSITIVITY_CONFIDENTIAL 	3

/*
 * Appointment flags with PR_APPOINTMENT_BUSY_STATUS
 */

#define	BUSY_STATUS_FREE 	0
#define	BUSY_STATUS_TENTATIVE 	1
#define	BUSY_STATUS_BUSY 	2
#define	BUSY_STATUS_OUTOFOFFICE 3

/*
 * Task status
 */

#define	olTaskNotStarted 	0
#define	olTaskInProgress 	1
#define	olTaskComplete 		2
#define	olTaskWaiting 		3
#define	olTaskDeferred 		4

static struct icaltimetype
foo (const time_t tm, const int is_date, const icaltimezone *comp_zone)
{
	struct icaltimetype itt_utc;
	struct icaltimetype itt;
	const icaltimezone *utc_zone;

	utc_zone = icaltimezone_get_utc_timezone ();

	/* First, get the time in UTC */
	itt_utc = icaltime_from_timet_with_zone (tm, is_date, 0);
	icaltime_set_timezone (&itt_utc, utc_zone);

	if (comp_zone) {
		itt = icaltime_convert_to_zone (itt_utc, comp_zone);
		itt = icaltime_set_timezone (&itt, comp_zone);
	} else {
		itt = icaltime_convert_to_zone (itt_utc, utc_zone);
		itt = icaltime_set_timezone (&itt, utc_zone);
	}

	return itt;
}

void
e_cal_backend_mapi_props_to_comp (struct mapi_SPropValue_array *properties, ECalComponent *comp, const icaltimezone *default_zone)
{
	struct timeval t;
	const char *subject = NULL;
	const char *body = NULL;
	const char *location = NULL;
	const uint32_t *priority;
	const uint32_t *sensitivity;
	icalcomponent *ical_comp;
	icalproperty *prop = NULL;
	const icaltimezone *utc_zone;

	ical_comp = e_cal_component_get_icalcomponent (comp);
	utc_zone = icaltimezone_get_utc_timezone ();

//	subject = (const char *)find_mapi_SPropValue_data(properties, PR_CONVERSATION_TOPIC);
	/* FIXME: you gotta better way to do this ?? */
	if (!subject) {
		const gchar *tmp;
		tmp = (const char *)find_mapi_SPropValue_data(properties, PR_URL_COMP_NAME);
		/* the PR_URL_COMP_NAME would end with ".EML". Remove that portion. */
		if (tmp && g_str_has_suffix (tmp, ".EML")) {
			subject = g_strndup (tmp, (strlen(tmp) - 4));
		}
	} 
	body = (const char *)find_mapi_SPropValue_data(properties, PR_BODY);
	location = (const char *)find_mapi_SPropValue_data(properties, 0x8208001e);

	/* set dtstamp - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_CREATION_TIME) == MAPI_E_SUCCESS)
		icalcomponent_set_dtstamp (ical_comp, foo (t.tv_sec, 0, 0));

	/* created - in UTC */
	if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_CREATED_PROPERTY)) != 0)
		icalcomponent_remove_property (ical_comp, prop);
	prop = icalproperty_new_created (icaltime_current_time_with_zone (utc_zone));
	icalcomponent_add_property (ical_comp, prop);
	
	/* last modified - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS) {
		if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_LASTMODIFIED_PROPERTY)) != 0)
			icalcomponent_remove_property (ical_comp, prop);
		prop = icalproperty_new_lastmodified (foo (t.tv_sec, 0, 0));
		icalcomponent_add_property (ical_comp, prop);
	}

	if (subject && *subject)
		icalcomponent_set_summary (ical_comp, subject);
	if (body && *body)
		icalcomponent_set_description (ical_comp, body);
	if (location && *location)
		icalcomponent_set_location (ical_comp, location);

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		const uint32_t *transp;
		const bool *all_day;

		all_day = (const bool *)find_mapi_SPropValue_data(properties, 0x8215000B);

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_START_DATE) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, (all_day && *all_day), default_zone));

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_END_DATE) == MAPI_E_SUCCESS)
			icalcomponent_set_dtend (ical_comp, foo (t.tv_sec, (all_day && *all_day), default_zone));

		transp = (const uint32_t *)find_mapi_SPropValue_data(properties, 0x82050003);
		if (transp) {
			icalproperty_transp ical_transp;
			switch (*transp) {
				/* FIXME: is this mapping correct ? */
				case BUSY_STATUS_TENTATIVE:
				case BUSY_STATUS_FREE:
					ical_transp = ICAL_TRANSP_TRANSPARENT;
					break;
				/* FIXME: is this mapping correct ? */
				case BUSY_STATUS_OUTOFOFFICE:
				case BUSY_STATUS_BUSY:
					ical_transp = ICAL_TRANSP_OPAQUE;
					break;
				default:
					ical_transp = ICAL_TRANSP_OPAQUE;
					break;
			}
			if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_TRANSP_PROPERTY)) != 0)
				icalcomponent_remove_property (ical_comp, prop);
			prop = icalproperty_new_transp (ical_transp);
			icalcomponent_add_property (ical_comp, prop);
		}
	} else if (icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		const double *complete = 0;
		const uint32_t *status;

		/* NOTE: Exchange tasks are DATE values, not DATE-TIME values, but maybe someday, we could expect Exchange to support it ;) */
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, 0x81040040) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, 0, default_zone));
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, 0x81050040) == MAPI_E_SUCCESS)
			icalcomponent_set_due (ical_comp, foo (t.tv_sec, 0, default_zone));

		status = (const uint32_t *)find_mapi_SPropValue_data(properties, 0x81010003);
		if (status) {
			icalproperty_status ical_status;
			switch (*status) {
				case olTaskNotStarted:
					ical_status = ICAL_STATUS_NEEDSACTION;
					break;
				/* FIXME: is this mapping correct ? */
				case olTaskWaiting:
				case olTaskInProgress:
					ical_status = ICAL_STATUS_INPROCESS;
					break;
				case olTaskComplete:
					ical_status = ICAL_STATUS_COMPLETED;
					break;
				case olTaskDeferred:
					ical_status = ICAL_STATUS_CANCELLED;
					break;
				default:
					ical_status = ICAL_STATUS_NEEDSACTION;
					break;
			}
			icalcomponent_set_status (ical_comp, ical_status);
			if (*status == olTaskComplete && get_mapi_SPropValue_array_date_timeval (&t, properties, 0x810F0040) == MAPI_E_SUCCESS) {
				if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_COMPLETED_PROPERTY)) != 0)
					icalcomponent_remove_property (ical_comp, prop);
				prop = icalproperty_new_completed (foo (t.tv_sec, 0, default_zone));
				icalcomponent_add_property (ical_comp, prop);
			}
		}

		complete = (const double *)find_mapi_SPropValue_data(properties, 0x81020005);
		if (complete) {
			if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_PERCENTCOMPLETE_PROPERTY)) != 0)
				icalcomponent_remove_property (ical_comp, prop);
			prop = icalproperty_new_percentcomplete ((int)(*complete * 100));
			icalcomponent_add_property (ical_comp, prop);
		}
	}

	/* priority */
	priority = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_PRIORITY);
	if (priority) {
		int ical_priority;
		switch (*priority) {
			case PRIORITY_LOW:
				ical_priority = 7;
				break;
			case PRIORITY_NORMAL:
				ical_priority = 5;
				break;
			case PRIORITY_HIGH:
				ical_priority = 1;
				break;
			default: 
				ical_priority = 5;
				break;
		}
		if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_PRIORITY_PROPERTY)) != 0)
			icalcomponent_remove_property (ical_comp, prop);
		prop = icalproperty_new_priority (ical_priority);
		icalcomponent_add_property (ical_comp, prop);
	}

	/* classification */
	sensitivity = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_SENSITIVITY);
	if (sensitivity) {
		icalproperty_class ical_class = ICAL_CLASS_NONE;
		switch (*sensitivity) {
			case SENSITIVITY_NORMAL:
				ical_class = ICAL_CLASS_PUBLIC;
				break;
			/* FIXME: is this mapping correct ? */
			case SENSITIVITY_PERSONAL:
			case SENSITIVITY_PRIVATE:
				ical_class = ICAL_CLASS_PRIVATE;
				break;
			case SENSITIVITY_CONFIDENTIAL:
				ical_class = ICAL_CLASS_CONFIDENTIAL;
				break;
			default: 
				ical_class = ICAL_CLASS_PUBLIC;
				break;
		}
		if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_CLASS_PROPERTY)) != 0)
			icalcomponent_remove_property (ical_comp, prop);
		prop = icalproperty_new_class (ical_class);
		icalcomponent_add_property (ical_comp, prop);
	}

	/* FIXME: attachments */
	/* FIXME: categories */

	/* FIXME: the ALARM definitely needs more work */
	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		struct timeval start, before;

		if ((get_mapi_SPropValue_array_date_timeval (&start, properties, 0x85020040) == MAPI_E_SUCCESS) 
		 && (get_mapi_SPropValue_array_date_timeval (&before, properties, 0x85600040) == MAPI_E_SUCCESS)) {
			ECalComponentAlarm *e_alarm = e_cal_component_alarm_new ();
			ECalComponentAlarmTrigger trigger;

			trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
			trigger.u.rel_duration = icaltime_subtract (icaltime_from_timet_with_zone (before.tv_sec, 0, 0), icaltime_from_timet_with_zone (start.tv_sec, 0, 0));

			e_cal_component_alarm_set_action (e_alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
			e_cal_component_alarm_set_trigger (e_alarm, trigger);

			e_cal_component_add_alarm (comp, e_alarm);
		}
	}

	e_cal_component_rescan (comp);
}

gboolean
build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	ECalComponent *comp = E_CAL_COMPONENT (data);

	mapi_nameid_OOM_add(nameid, "Location", PSETID_Appointment);
	mapi_nameid_OOM_add(nameid, "BusyStatus", PSETID_Appointment);
	mapi_nameid_OOM_add(nameid, "MeetingStatus", PSETID_Appointment);
	mapi_nameid_OOM_add(nameid, "CommonStart", PSETID_Common);
	mapi_nameid_OOM_add(nameid, "CommonEnd", PSETID_Common);
//	mapi_nameid_OOM_add(nameid, "Label", PSETID_Appointment);
//	mapi_nameid_OOM_add(nameid, "ReminderMinutesBeforeStart", PSETID_Common);

	return TRUE;
}

int
build_props (struct SPropValue **value, struct SPropTagArray *SPropTagArray, gpointer data)
{
	ECalComponent *comp = E_CAL_COMPONENT (data);
	icalcomponent *ical_comp = e_cal_component_get_icalcomponent (comp);
	struct SPropValue *props;
	int i=0;
	struct timeval t;
	const icaltimezone *utc_zone;
	uint32_t flag;

	utc_zone = icaltimezone_get_utc_timezone ();

	props = g_new (struct SPropValue, 40); //FIXME: Correct value tbd

	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, 
						   (const void *) icalcomponent_get_summary (ical_comp));
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, 
						   (const void *) icalcomponent_get_summary (ical_comp));

	t.tv_sec = icaltime_as_timet_with_zone (icalcomponent_get_dtstart (ical_comp), utc_zone);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], PR_START_DATE, &t);

	t.tv_sec = icaltime_as_timet_with_zone (icalcomponent_get_dtend (ical_comp), utc_zone);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], PR_END_DATE, &t);

	set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *)"IPM.Appointment");

	flag = 1;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (const void *) &flag);

	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[0], (const void *) icalcomponent_get_location (ical_comp));

	flag = 2;
	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[1], (const void *) &flag);

	flag= MEETING_STATUS_NONMEETING;
	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[2], (const void *) &flag);

//	flag2 = true;

	set_SPropValue_proptag_date_timeval(&props[i++], SPropTagArray->aulPropTag[3], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], SPropTagArray->aulPropTag[4], &t);

//	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[5], (const void *)&oclient->label);

//	flag = 30;
//	set_SPropValue_proptag(&props[i++], SPropTagArray->aulPropTag[6], (const void *)&flag);

	set_SPropValue_proptag(&props[i++], PR_BODY, (const void *) icalcomponent_get_description (ical_comp));

	*value = props;
	return i;
}


