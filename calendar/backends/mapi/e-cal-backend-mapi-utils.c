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

#ifndef O_BINARY
#define O_BINARY 0
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
 * Appointment meeting status
 */

#define MEETING_STATUS_NONMEETING	0
#define MEETING_STATUS_MEETING		1

/*
 * Appointment flags with PR_APPOINTMENT_BUSY_STATUS
 */

#define	BUSY_STATUS_FREE 	0
#define	BUSY_STATUS_TENTATIVE 	1
#define	BUSY_STATUS_BUSY 	2
#define	BUSY_STATUS_OUTOFOFFICE 3

/*
 * Task OwnerShip
 */

#define	olNewTask 	0
#define	olDelegatedTask 1
#define	olOwnTask 	2

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
e_cal_backend_mapi_util_fetch_attachments (ECalBackendMAPI *cbmapi, ECalComponent *comp, GSList **attach_list)
{
	GSList *comp_attach_list = NULL, *new_attach_list = NULL;
	GSList *l;
	char *dest_file;
	int fd;
	const char *uid;
	const char *local_store = e_cal_backend_mapi_get_local_attachments_store (E_CAL_BACKEND (cbmapi));

	e_cal_component_get_attachment_list (comp, &comp_attach_list);
	e_cal_component_get_uid (comp, &uid);
	
	for (l = comp_attach_list; l ; l = l->next) {
		gchar *sfname = (gchar *) l->data;
		gchar *filename, *new_filename;
		GMappedFile *mapped_file;
		GError *error = NULL;
		guint filelength = 0;

		mapped_file = g_mapped_file_new (sfname, FALSE, &error);
		if (!mapped_file) {
			g_message ("DEBUG: could not map %s: %s\n", sfname, error->message);
			g_error_free (error);
			continue;
		}

		filename = g_path_get_basename (sfname);
		new_filename = g_strconcat (uid, "-", filename, NULL);
		g_free (filename);
		dest_file = g_build_filename (local_store, new_filename, NULL);
		g_free (new_filename);

		filelength = g_mapped_file_get_length (mapped_file);

		fd = g_open (dest_file, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
		if (fd == -1) {
			/* skip gracefully */
			g_message ("DEBUG: could not open %s for writing\n", dest_file);
		} else if (write (fd, g_mapped_file_get_contents (mapped_file), filelength) == -1) {
			/* skip gracefully */
			g_message ("DEBUG: attachment write failed.\n");
		}
		if (fd != -1) {
			ExchangeMAPIAttachment *attach_item;

			close (fd);
			new_attach_list = g_slist_append (new_attach_list, g_filename_to_uri (dest_file, NULL, NULL));

			attach_item = g_new0 (ExchangeMAPIAttachment, 1);
			attach_item->filename = g_path_get_basename (sfname);
			attach_item->value = g_byte_array_sized_new (filelength);
			attach_item->value = g_byte_array_append (attach_item->value, g_mapped_file_get_contents (mapped_file), filelength);
			*attach_list = g_slist_append (*attach_list, attach_item);
		}

		g_mapped_file_free (mapped_file);
		g_free (dest_file);
	}

	e_cal_component_set_attachment_list (comp, new_attach_list);
}

static void
set_attachments_to_cal_component (ECalBackendMAPI *cbmapi, ECalComponent *comp, GSList *attach_list)
{
	GSList *comp_attach_list = NULL, *l;
	const char *uid;
	const char *local_store = e_cal_backend_mapi_get_local_attachments_store (E_CAL_BACKEND (cbmapi));
	
	e_cal_component_get_uid (comp, &uid);
	for (l = attach_list; l ; l = l->next) {
		ExchangeMAPIAttachment *attach_item = (ExchangeMAPIAttachment *) (l->data);
		gchar *attach_file_url, *filename;
		struct stat st;

		attach_file_url = g_strconcat (local_store, "/", uid, "-", attach_item->filename, NULL);
		filename = g_filename_from_uri (attach_file_url, NULL, NULL);

		if (g_stat (filename, &st) == -1) {
			int fd = g_open (filename, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
			if (fd == -1) { 
				/* skip gracefully */
				g_message ("DEBUG: could not open %s for writing\n", filename);
			} else if (write (fd, attach_item->value->data, attach_item->value->len) == -1) {
				/* skip gracefully */
				g_message ("DEBUG: attachment write failed.\n");
			}
			if (fd != -1) {
				close (fd);
				comp_attach_list = g_slist_append (comp_attach_list, g_strdup (attach_file_url));
			}
		}

		g_free (filename);
		g_free (attach_file_url);
	}

	e_cal_component_set_attachment_list (comp, comp_attach_list);
}

void
e_cal_backend_mapi_props_to_comp (ECalBackendMAPI *cbmapi, struct mapi_SPropValue_array *properties, ECalComponent *comp, 
				  GSList *recipients, GSList *attachments, const icaltimezone *default_zone)
{
	struct timeval t;
	const gchar *subject = NULL;
	const char *body = NULL;
	const uint32_t *priority;
	const uint32_t *sensitivity;
	icalcomponent *ical_comp;
	icalproperty *prop = NULL;
	const icaltimezone *utc_zone;

	ical_comp = e_cal_component_get_icalcomponent (comp);
	utc_zone = icaltimezone_get_utc_timezone ();

	subject = (const gchar *)find_mapi_SPropValue_data(properties, PR_SUBJECT);
	if (!subject)
		subject = (const gchar *)find_mapi_SPropValue_data(properties, PR_NORMALIZED_SUBJECT);
	/* FIXME: you gotta better way to do this ?? */
	if (!subject) {
		const gchar *tmp;
		tmp = (const char *)find_mapi_SPropValue_data(properties, PR_URL_COMP_NAME);
		/* the PR_URL_COMP_NAME would end with ".EML". Remove that portion. */
		if (tmp && g_str_has_suffix (tmp, ".EML")) {
			subject = g_strndup (tmp, (strlen(tmp) - 4));
		}
	} 
//	body = (const char *)find_mapi_SPropValue_data(properties, PR_BODY);

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

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		const char *location = NULL;
		const uint32_t *transp;
		const bool *all_day;
		const bool *recurring;
		const bool *reminder_set;

		location = (const char *)exchange_mapi_util_find_array_propval(properties, PROP_TAG(PT_STRING8, 0x8208));
		if (location && *location)
			icalcomponent_set_location (ical_comp, location);

		all_day = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8215));

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x820D)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, (all_day && *all_day), default_zone));

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x820E)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtend (ical_comp, foo (t.tv_sec, (all_day && *all_day), default_zone));

		transp = (const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8205));
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

		recurring = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8223));
		if (recurring && *recurring) {
			/* FIXME: recurrence */
		}

		/* FIXME: the ALARM definitely needs more work */
		reminder_set = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8503));
		if (reminder_set && *reminder_set) {
			struct timeval start, before;

			if ((get_mapi_SPropValue_array_date_timeval (&start, properties, PROP_TAG(PT_SYSTIME, 0x8502)) == MAPI_E_SUCCESS) 
			 && (get_mapi_SPropValue_array_date_timeval (&before, properties, PROP_TAG(PT_SYSTIME, 0x8560)) == MAPI_E_SUCCESS)) {
				ECalComponentAlarm *e_alarm = e_cal_component_alarm_new ();
				ECalComponentAlarmTrigger trigger;

				trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
				trigger.u.rel_duration = icaltime_subtract (icaltime_from_timet_with_zone (before.tv_sec, 0, 0), 
									    icaltime_from_timet_with_zone (start.tv_sec, 0, 0));

				e_cal_component_alarm_set_action (e_alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
				e_cal_component_alarm_set_trigger (e_alarm, trigger);

				e_cal_component_add_alarm (comp, e_alarm);
			}
		} else
			e_cal_component_remove_all_alarms (comp);

	} else if (icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		const double *complete = 0;
		const uint32_t *status;

		/* NOTE: Exchange tasks are DATE values, not DATE-TIME values, but maybe someday, we could expect Exchange to support it ;) */
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x8104)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, 0, default_zone));
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x8105)) == MAPI_E_SUCCESS)
			icalcomponent_set_due (ical_comp, foo (t.tv_sec, 0, default_zone));

		status = (const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8101));
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
			if (*status == olTaskComplete 
			&& get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x810F)) == MAPI_E_SUCCESS) {
				if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_COMPLETED_PROPERTY)) != 0)
					icalcomponent_remove_property (ical_comp, prop);
				prop = icalproperty_new_completed (foo (t.tv_sec, 0, default_zone));
				icalcomponent_add_property (ical_comp, prop);
			}
		}

		complete = (const double *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_DOUBLE, 0x8102));
		if (complete) {
			if ((prop = icalcomponent_get_first_property (ical_comp, ICAL_PERCENTCOMPLETE_PROPERTY)) != 0)
				icalcomponent_remove_property (ical_comp, prop);
			prop = icalproperty_new_percentcomplete ((int)(*complete * 100));
			icalcomponent_add_property (ical_comp, prop);
		}
	} else if (icalcomponent_isa (ical_comp) == ICAL_VJOURNAL_COMPONENT) {
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, TRUE, default_zone));
	}

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT || icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
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

	/* FIXME: categories */

	e_cal_component_rescan (comp);

	set_attachments_to_cal_component (cbmapi, comp, attachments);
}

#define CAL_NAMED_PROPS_N  0
#define TASK_NAMED_PROPS_N 0
#define MEMO_NAMED_PROPS_N 0

gboolean
build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	ECalBackendMAPI *cbmapi	= E_CAL_BACKEND_MAPI (data);
	icalcomponent_kind kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi));

	/* NOTE: Avoid using mapi_nameid_OOM_add because: 
	 * a) its inefficient (uses strcmp) 
	 * b) names may vary in different server versions 
	 */

	mapi_nameid_lid_add(nameid, 0x8501, PSETID_Common); 	// PT_LONG - ReminderMinutesBeforeStart
	mapi_nameid_lid_add(nameid, 0x8502, PSETID_Common); 	// PT_SYSTIME - ReminderTime
	mapi_nameid_lid_add(nameid, 0x8503, PSETID_Common); 	// PT_BOOLEAN - ReminderSet
	mapi_nameid_lid_add(nameid, 0x8506, PSETID_Common); 	// PT_BOOLEAN - Private
	mapi_nameid_lid_add(nameid, 0x8516, PSETID_Common); 	// PT_SYSTIME - CommonStart
	mapi_nameid_lid_add(nameid, 0x8517, PSETID_Common); 	// PT_SYSTIME - CommonEnd
	mapi_nameid_lid_add(nameid, 0x8560, PSETID_Common); 	// PT_SYSTIME - ReminderNextTime

	if (kind == ICAL_VEVENT_COMPONENT) {
//		mapi_nameid_lid_add(nameid, 0x8200, PSETID_Appointment); 	// PT_BOOLEAN - SendAsICAL
		mapi_nameid_lid_add(nameid, 0x8205, PSETID_Appointment); 	// PT_LONG - BusyStatus
		mapi_nameid_lid_add(nameid, 0x8208, PSETID_Appointment); 	// PT_STRING8 - Location
		mapi_nameid_lid_add(nameid, 0x820D, PSETID_Appointment); 	// PT_SYSTIME - Start/ApptStartWhole
		mapi_nameid_lid_add(nameid, 0x820E, PSETID_Appointment); 	// PT_SYSTIME - End/ApptEndWhole
		mapi_nameid_lid_add(nameid, 0x8213, PSETID_Appointment); 	// PT_LONG - Duration/ApptDuration
//		mapi_nameid_lid_add(nameid, 0x8214, PSETID_Appointment); 	// PT_LONG - Label
		mapi_nameid_lid_add(nameid, 0x8215, PSETID_Appointment); 	// PT_BOOLEAN - AllDayEvent
		mapi_nameid_lid_add(nameid, 0x8217, PSETID_Appointment); 	// PT_LONG - MeetingStatus
		mapi_nameid_lid_add(nameid, 0x8218, PSETID_Appointment); 	// PT_LONG - ResponseStatus
		mapi_nameid_lid_add(nameid, 0x8223, PSETID_Appointment); 	// PT_BOOLEAN - IsRecurring/Recurring
//		mapi_nameid_lid_add(nameid, 0x8231, PSETID_Appointment); 	// PT_LONG - RecurrenceType
//		mapi_nameid_lid_add(nameid, 0x8232, PSETID_Appointment); 	// PT_STRING8 - RecurrencePattern
//		mapi_nameid_lid_add(nameid, 0x8234, PSETID_Appointment); 	// PT_STRING8 - TimeZone
		mapi_nameid_lid_add(nameid, 0x8235, PSETID_Appointment); 	// PT_SYSTIME - (dtstart)(for recurring events UTC 12 AM of day of start)
		mapi_nameid_lid_add(nameid, 0x8236, PSETID_Appointment); 	// PT_SYSTIME - (dtend)(for recurring events UTC 12 AM of day of end)
//		mapi_nameid_lid_add(nameid, 0x8238, PSETID_Appointment); 	// PT_STRING8 - AllAttendees
//		mapi_nameid_lid_add(nameid, 0x823B, PSETID_Appointment); 	// PT_STRING8 - ToAttendeesString (dupe PR_DISPLAY_TO)
//		mapi_nameid_lid_add(nameid, 0x823C, PSETID_Appointment); 	// PT_STRING8 - CCAttendeesString (dupe PR_DISPLAY_CC)
		mapi_nameid_lid_add(nameid, 0x8240, PSETID_Appointment); 	// PT_BOOLEAN - IsOnlineMeeting
		mapi_nameid_lid_add(nameid, 0x825E, PSETID_Appointment); 	// PT_BINARY - (timezone for dtstart)
		mapi_nameid_lid_add(nameid, 0x825F, PSETID_Appointment); 	// PT_BINARY - (timezone for dtend)
	} else if (kind == ICAL_VTODO_COMPONENT) {
		mapi_nameid_lid_add(nameid, 0x8101, PSETID_Task); 	// PT_LONG - Status
		mapi_nameid_lid_add(nameid, 0x8102, PSETID_Task); 	// PT_DOUBLE - PercentComplete
//		mapi_nameid_lid_add(nameid, 0x8103, PSETID_Task); 	// PT_BOOLEAN - TeamTask
		mapi_nameid_lid_add(nameid, 0x8104, PSETID_Task); 	// PT_SYSTIME - StartDate/TaskStartDate
		mapi_nameid_lid_add(nameid, 0x8105, PSETID_Task); 	// PT_SYSTIME - DueDate/TaskDueDate
		mapi_nameid_lid_add(nameid, 0x810F, PSETID_Task); 	// PT_SYSTIME - DateCompleted
//		mapi_nameid_lid_add(nameid, 0x8110, PSETID_Task); 	// PT_LONG - ActualWork/TaskActualEffort
//		mapi_nameid_lid_add(nameid, 0x8111, PSETID_Task); 	// PT_LONG - TotalWork/TaskEstimatedEffort
		mapi_nameid_lid_add(nameid, 0x811C, PSETID_Task); 	// PT_BOOLEAN - Complete
//		mapi_nameid_lid_add(nameid, 0x811F, PSETID_Task); 	// PT_STRING8 - Owner
//		mapi_nameid_lid_add(nameid, 0x8121, PSETID_Task); 	// PT_STRING8 - Delegator
		mapi_nameid_lid_add(nameid, 0x8126, PSETID_Task); 	// PT_BOOLEAN - IsRecurring/TaskFRecur
//		mapi_nameid_lid_add(nameid, 0x8127, PSETID_Task); 	// PT_STRING8 - Role
//		mapi_nameid_lid_add(nameid, 0x8129, PSETID_Task); 	// PT_LONG - Ownership
//		mapi_nameid_lid_add(nameid, 0x812A, PSETID_Task); 	// PT_LONG - DelegationState
	} else if (kind == ICAL_VJOURNAL_COMPONENT) {
//		mapi_nameid_lid_add(nameid, 0x8B00, PSETID_Note); 	// PT_LONG - Color
	}

	return TRUE;
}

int
build_props (struct SPropValue **value, struct SPropTagArray *proptag_array, gpointer data)
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

	/* FIXME: convert to unicode */
	set_SPropValue_proptag(&props[i++], PR_SUBJECT, 
					(const void *) icalcomponent_get_summary (ical_comp));
	/* FIXME: convert to unicode */
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, 
					(const void *) icalcomponent_get_summary (ical_comp));
	/* FIXME: convert to unicode */
	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, 
					(const void *) icalcomponent_get_summary (ical_comp));

	/* we don't support HTML event/task/memo editor */
	flag = EDITOR_FORMAT_PLAINTEXT;
	set_SPropValue_proptag(&props[i++], PR_MSG_EDITOR_FORMAT, &flag);

	/* it'd be better to convert, then set it in unicode */
	set_SPropValue_proptag(&props[i++], PR_BODY, 
					(const void *) icalcomponent_get_description (ical_comp));

/* 
PR_SENT_REPRESENTING_NAME
PR_SENT_REPRESENTING_ADDRTYPE
PR_SENT_REPRESENTING_EMAIL_ADDRESS
PR_SENDER_NAME
PR_SENDER_ADDRTYPE
PR_SENDER_EMAIL_ADDRESS
*/
	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *)"IPM.Appointment");

		flag = e_cal_component_has_attendees (comp) ? MEETING_STATUS_MEETING : MEETING_STATUS_NONMEETING;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[2], (const void *) &flag);

	} else if (icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *)"IPM.Task");

	} else if (icalcomponent_isa (ical_comp) == ICAL_VJOURNAL_COMPONENT) {
		set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *)"IPM.StickyNote");

	}



	t.tv_sec = icaltime_as_timet_with_zone (icalcomponent_get_dtstart (ical_comp), utc_zone);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], PR_START_DATE, &t);

	t.tv_sec = icaltime_as_timet_with_zone (icalcomponent_get_dtend (ical_comp), utc_zone);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], PR_END_DATE, &t);


	flag = 1;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (const void *) &flag);

	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[0], (const void *) icalcomponent_get_location (ical_comp));

	flag = 2;
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[1], (const void *) &flag);


//	flag2 = true;

	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[3], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[4], &t);

//	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[5], (const void *)&oclient->label);

//	flag = 30;
//	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[6], (const void *)&flag);

	*value = props;
	return i;
}


