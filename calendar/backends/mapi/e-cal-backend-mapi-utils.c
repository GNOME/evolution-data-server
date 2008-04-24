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

#include <glib/gstdio.h>

#include <fcntl.h>

#include "e-cal-backend-mapi.h"
#include "e-cal-backend-mapi-utils.h"
#include "e-cal-backend-mapi-tz-utils.h"
#if 0
#include "e-cal-backend-mapi-recur-utils.h"
#endif
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

static void appt_build_name_id (struct mapi_nameid *nameid);
static void task_build_name_id (struct mapi_nameid *nameid);
static void note_build_name_id (struct mapi_nameid *nameid);

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
	const char *local_store = e_cal_backend_mapi_get_local_attachments_store (cbmapi);

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

	for (l = new_attach_list; l != NULL; l = l->next)
		g_free (l->data);
	g_slist_free (new_attach_list);
}

static void
set_attachments_to_cal_component (ECalBackendMAPI *cbmapi, ECalComponent *comp, GSList *attach_list)
{
	GSList *comp_attach_list = NULL, *l;
	const char *uid;
	const char *local_store = e_cal_backend_mapi_get_local_attachments_store (cbmapi);
	
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

#define REQUIRED 0
#define OPTIONAL 0
#define RESOURCE 0
#define MEET_ORGANIZER 0
#define MEET_ATTENDEE 0

static void 
ical_attendees_from_props (icalcomponent *ical_comp, GSList *recipients)
{
/*** ALERT: INCOMPLETE ***/
	GSList *l;
	for (l=recipients; l; l=l->next) {
		ExchangeMAPIRecipient *recip = (ExchangeMAPIRecipient *)(l->data);
		icalproperty *prop;
		icalparameter *param;
		/* ORG / ATT */
		if (recip->flags & MEET_ATTENDEE)
			prop = icalproperty_new_attendee (recip->email_id);
		else if (recip->flags & MEET_ORGANIZER)
			prop = icalproperty_new_organizer (recip->email_id);
		/* CN */
		param = icalparameter_new_cn (recip->name);
		icalproperty_add_parameter (prop, param);
		/* PARTSTAT */
		//param = icalparameter_new_partstat ();
	}
}


ECalComponent *
e_cal_backend_mapi_props_to_comp (ECalBackendMAPI *cbmapi, const gchar *mid, struct mapi_SPropValue_array *properties, 
				  GSList *streams, GSList *recipients, GSList *attachments, const icaltimezone *default_zone)
{
	ECalComponent *comp = NULL;
	struct timeval t;
	const gchar *subject = NULL;
	const uint32_t *ui32;
	const bool *b;
	icalcomponent *ical_comp;
	icalproperty *prop = NULL;
	ExchangeMAPIStream *body;

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi))) {
		case ICAL_VEVENT_COMPONENT:
		case ICAL_VTODO_COMPONENT:
		case ICAL_VJOURNAL_COMPONENT:
			comp = e_cal_component_new ();
			ical_comp = icalcomponent_new (e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi)));
			e_cal_component_set_icalcomponent (comp, ical_comp);
			icalcomponent_set_uid (ical_comp, mid);
			e_cal_component_set_uid (comp, mid);
			break;
		default:
			return NULL;
	}
	
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

	body = exchange_mapi_util_find_stream (streams, PR_BODY);
	if (!body)
		body = exchange_mapi_util_find_stream (streams, PR_BODY_HTML);
	if (!body)
		body = exchange_mapi_util_find_stream (streams, PR_HTML);

	/* set dtstamp - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_CREATION_TIME) == MAPI_E_SUCCESS)
		icalcomponent_set_dtstamp (ical_comp, foo (t.tv_sec, 0, 0));

	/* created - in UTC */
	prop = icalproperty_new_created (icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ()));
	icalcomponent_add_property (ical_comp, prop);
	
	/* last modified - in UTC */
	if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS) {
		prop = icalproperty_new_lastmodified (foo (t.tv_sec, 0, 0));
		icalcomponent_add_property (ical_comp, prop);
	}

	if (subject && *subject)
		icalcomponent_set_summary (ical_comp, subject);
	if (body)
		icalcomponent_set_description (ical_comp, (const char *) body->value->data);

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT) {
		const char *location = NULL;
		const gchar *dtstart_tz = NULL, *dtend_tz = NULL;
		ExchangeMAPIStream *stream;

		location = (const char *)exchange_mapi_util_find_array_propval(properties, PROP_TAG(PT_STRING8, 0x8208));
		if (location && *location)
			icalcomponent_set_location (ical_comp, location);

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8215));

		stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x825E));
		if (stream) {
			gchar *buf = e_cal_backend_mapi_util_bin_to_mapi_tz (stream->value);
			dtstart_tz = e_cal_backend_mapi_tz_util_get_ical_equivalent (buf);
			g_free (buf);
		}

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x820D)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, (b && *b), icaltimezone_get_builtin_timezone_from_tzid (dtstart_tz)));

		stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x825F));
		if (stream) {
			gchar *buf = e_cal_backend_mapi_util_bin_to_mapi_tz (stream->value);
			dtend_tz = e_cal_backend_mapi_tz_util_get_ical_equivalent (buf);
			g_free (buf);
		}

		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x820E)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtend (ical_comp, foo (t.tv_sec, (b && *b), icaltimezone_get_builtin_timezone_from_tzid (dtend_tz)));

		ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8205));
		if (ui32) {
			icalproperty_transp ical_transp;
			switch (*ui32) {
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
			prop = icalproperty_new_transp (ical_transp);
			icalcomponent_add_property (ical_comp, prop);
		}

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8223));
		if (b && *b) {
			/* FIXME: recurrence */
			g_warning ("Encountered a recurring event.");
/*			stream = exchange_mapi_util_find_stream (streams, PROP_TAG(PT_BINARY, 0x8216));
			if (stream) {
				e_cal_backend_mapi_util_bin_to_rrule (stream->value, comp);
			}
*/		} 

		/* FIXME: the ALARM definitely needs more work */
		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8503));
		if (b && *b) {
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

		/* NOTE: Exchange tasks are DATE values, not DATE-TIME values, but maybe someday, we could expect Exchange to support it ;) */
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x8104)) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, 1, default_zone));
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x8105)) == MAPI_E_SUCCESS)
			icalcomponent_set_due (ical_comp, foo (t.tv_sec, 1, default_zone));

		ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_LONG, 0x8101));
		if (ui32) {
			icalproperty_status ical_status;
			switch (*ui32) {
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
			if (*ui32 == olTaskComplete 
			&& get_mapi_SPropValue_array_date_timeval (&t, properties, PROP_TAG(PT_SYSTIME, 0x810F)) == MAPI_E_SUCCESS) {
				prop = icalproperty_new_completed (foo (t.tv_sec, 1, default_zone));
				icalcomponent_add_property (ical_comp, prop);
			}
		}

		complete = (const double *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_DOUBLE, 0x8102));
		if (complete) {
			prop = icalproperty_new_percentcomplete ((int)(*complete * 100));
			icalcomponent_add_property (ical_comp, prop);
		}

		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8126));
		if (b && *b) {
			/* FIXME: Evolution does not support recurring tasks */
			g_warning ("Encountered a recurring task.");
		}

		/* FIXME: the ALARM definitely needs more work */
		b = (const bool *)find_mapi_SPropValue_data(properties, PROP_TAG(PT_BOOLEAN, 0x8503));
		if (b && *b) {
			struct timeval abs;

			if (get_mapi_SPropValue_array_date_timeval (&abs, properties, PROP_TAG(PT_SYSTIME, 0x8502)) == MAPI_E_SUCCESS) {
				ECalComponentAlarm *e_alarm = e_cal_component_alarm_new ();
				ECalComponentAlarmTrigger trigger;

				trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE;
				trigger.u.abs_time = icaltime_from_timet_with_zone (abs.tv_sec, 0, 0);

				e_cal_component_alarm_set_action (e_alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
				e_cal_component_alarm_set_trigger (e_alarm, trigger);

				e_cal_component_add_alarm (comp, e_alarm);
			}
		} else
			e_cal_component_remove_all_alarms (comp);

	} else if (icalcomponent_isa (ical_comp) == ICAL_VJOURNAL_COMPONENT) {
		if (get_mapi_SPropValue_array_date_timeval (&t, properties, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS)
			icalcomponent_set_dtstart (ical_comp, foo (t.tv_sec, TRUE, default_zone));
	}

	if (icalcomponent_isa (ical_comp) == ICAL_VEVENT_COMPONENT || icalcomponent_isa (ical_comp) == ICAL_VTODO_COMPONENT) {
		/* priority */
		ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_PRIORITY);
		if (ui32) {
			int ical_priority;
			switch (*ui32) {
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
			prop = icalproperty_new_priority (ical_priority);
			icalcomponent_add_property (ical_comp, prop);
		}
	}

	/* classification */
	ui32 = (const uint32_t *)find_mapi_SPropValue_data(properties, PR_SENSITIVITY);
	if (ui32) {
		icalproperty_class ical_class = ICAL_CLASS_NONE;
		switch (*ui32) {
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
		prop = icalproperty_new_class (ical_class);
		icalcomponent_add_property (ical_comp, prop);
	}

	/* FIXME: categories */

	set_attachments_to_cal_component (cbmapi, comp, attachments);

	e_cal_component_rescan (comp);

	return comp;
}

#define COMMON_NAMED_PROPS_N 8

typedef enum 
{
	I_COMMON_REMMINS = 0 , 
	I_COMMON_REMTIME , 
	I_COMMON_REMSET , 
	I_COMMON_ISPRIVATE , 
	I_COMMON_CTXMENUFLAGS , 
	I_COMMON_START , 
	I_COMMON_END , 
	I_COMMON_REMNEXTTIME 
} CommonNamedPropsIndex;

gboolean
mapi_cal_build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	ECalBackendMAPI *cbmapi	= E_CAL_BACKEND_MAPI (data);
	icalcomponent_kind kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi));

	/* NOTE: Avoid using mapi_nameid_OOM_add because: 
	 * a) its inefficient (uses strcmp) 
	 * b) names may vary in different server/libmapi versions 
	 */

	mapi_nameid_lid_add(nameid, 0x8501, PSETID_Common); 	// PT_LONG - ReminderMinutesBeforeStart
	mapi_nameid_lid_add(nameid, 0x8502, PSETID_Common); 	// PT_SYSTIME - ReminderTime
	mapi_nameid_lid_add(nameid, 0x8503, PSETID_Common); 	// PT_BOOLEAN - ReminderSet
	mapi_nameid_lid_add(nameid, 0x8506, PSETID_Common); 	// PT_BOOLEAN - Private
	mapi_nameid_lid_add(nameid, 0x8510, PSETID_Common); 	// PT_LONG - (context menu flags)
	mapi_nameid_lid_add(nameid, 0x8516, PSETID_Common); 	// PT_SYSTIME - CommonStart
	mapi_nameid_lid_add(nameid, 0x8517, PSETID_Common); 	// PT_SYSTIME - CommonEnd
	mapi_nameid_lid_add(nameid, 0x8560, PSETID_Common); 	// PT_SYSTIME - ReminderNextTime

	if (kind == ICAL_VEVENT_COMPONENT) 
		appt_build_name_id (nameid);
	else if (kind == ICAL_VTODO_COMPONENT)
		task_build_name_id (nameid);
	else if (kind == ICAL_VJOURNAL_COMPONENT)
		note_build_name_id (nameid);

	return TRUE;
}

/**
 * NOTE: The enumerations '(Appt/Task/Note)NamedPropsIndex' have been defined 
 * only to make life a little easier for developers. Here's the logic 
 * behind the definition:
     1) The first element is initialized with 'COMMON_NAMED_PROPS_N' : When 
	adding named props, we add the common named props first and then the 
	specific named props. So.. the index of the first specific 
	named property = COMMON_NAMED_PROPS_N
     2) The order in the enumeration 'must' be the same as that in the routine 
	which adds the specific named props - (appt/task/note)_build_name_id
     3) If a specific named prop is added/deleted, an index needs to
	be created/deleted at the correct position. [Don't forget to update 
	(APPT/TASK/NOTE)_NAMED_PROPS_N]. 

 * To summarize the pros: 
     1) Addition/deletion of a common-named-prop would not affect the indexes 
	of the specific named props once COMMON_NAMED_PROPS_N is updated. 
     2) Values of named props can be added in any order. 
 */


#define APPT_NAMED_PROPS_N  18
#define DEFAULT_APPT_REMINDER_MINS 15

typedef enum 
{
//	I_SENDASICAL = COMMON_NAMED_PROPS_N , 
	I_APPT_BUSYSTATUS = COMMON_NAMED_PROPS_N , 
	I_APPT_LOCATION , 
	I_APPT_START , 
	I_APPT_END , 
	I_APPT_DURATION , 
	I_APPT_ALLDAY , 
	I_APPT_RECURBLOB , 
	I_APPT_MEETINGSTATUS , 
//	I_APPT_RESPONSESTATUS , 
	I_APPT_ISRECURRING , 
	I_APPT_RECURBASE , 
	I_APPT_RECURTYPE , 
	I_APPT_RECURPATTERN , 
	I_APPT_RECURSTART , 
	I_APPT_RECUREND , 
//	I_APPT_ALLATTENDEES , 
//	I_APPT_TOATTENDEES , 
//	I_APPT_CCATTENDEES , 
	I_APPT_ISONLINEMEET , 
	I_APPT_COUNTERPROPOSAL , 
	I_APPT_STARTTZBLOB , 
	I_APPT_ENDTZBLOB 
//	I_APPT_LABEL , 
//	I_APPT_DISPTZ 
} ApptNamedPropsIndex;

static void 
appt_build_name_id (struct mapi_nameid *nameid)
{
//	mapi_nameid_lid_add(nameid, 0x8200, PSETID_Appointment); 	// PT_BOOLEAN - SendAsICAL
	mapi_nameid_lid_add(nameid, 0x8205, PSETID_Appointment); 	// PT_LONG - BusyStatus
	mapi_nameid_lid_add(nameid, 0x8208, PSETID_Appointment); 	// PT_STRING8 - Location
	mapi_nameid_lid_add(nameid, 0x820D, PSETID_Appointment); 	// PT_SYSTIME - Start/ApptStartWhole
	mapi_nameid_lid_add(nameid, 0x820E, PSETID_Appointment); 	// PT_SYSTIME - End/ApptEndWhole
	mapi_nameid_lid_add(nameid, 0x8213, PSETID_Appointment); 	// PT_LONG - Duration/ApptDuration
	mapi_nameid_lid_add(nameid, 0x8215, PSETID_Appointment); 	// PT_BOOLEAN - AllDayEvent
	mapi_nameid_lid_add(nameid, 0x8216, PSETID_Appointment); 	// PT_BINARY - (recurrence blob)
	mapi_nameid_lid_add(nameid, 0x8217, PSETID_Appointment); 	// PT_LONG - MeetingStatus
//	mapi_nameid_lid_add(nameid, 0x8218, PSETID_Appointment); 	// PT_LONG - ResponseStatus
	mapi_nameid_lid_add(nameid, 0x8223, PSETID_Appointment); 	// PT_BOOLEAN - IsRecurring/Recurring
	mapi_nameid_lid_add(nameid, 0x8228, PSETID_Appointment); 	// PT_SYSTIME - RecurrenceBase
	mapi_nameid_lid_add(nameid, 0x8231, PSETID_Appointment); 	// PT_LONG - RecurrenceType
	mapi_nameid_lid_add(nameid, 0x8232, PSETID_Appointment); 	// PT_STRING8 - RecurrencePattern
	mapi_nameid_lid_add(nameid, 0x8235, PSETID_Appointment); 	// PT_SYSTIME - (dtstart)(for recurring events UTC 12 AM of day of start)
	mapi_nameid_lid_add(nameid, 0x8236, PSETID_Appointment); 	// PT_SYSTIME - (dtend)(for recurring events UTC 12 AM of day of end)
//	mapi_nameid_lid_add(nameid, 0x8238, PSETID_Appointment); 	// PT_STRING8 - AllAttendees
//	mapi_nameid_lid_add(nameid, 0x823B, PSETID_Appointment); 	// PT_STRING8 - ToAttendeesString (dupe PR_DISPLAY_TO)
//	mapi_nameid_lid_add(nameid, 0x823C, PSETID_Appointment); 	// PT_STRING8 - CCAttendeesString (dupe PR_DISPLAY_CC)
	mapi_nameid_lid_add(nameid, 0x8240, PSETID_Appointment); 	// PT_BOOLEAN - IsOnlineMeeting
	mapi_nameid_lid_add(nameid, 0x8257, PSETID_Appointment); 	// PT_BOOLEAN - ApptCounterProposal
	mapi_nameid_lid_add(nameid, 0x825E, PSETID_Appointment); 	// PT_BINARY - (timezone for dtstart)
	mapi_nameid_lid_add(nameid, 0x825F, PSETID_Appointment); 	// PT_BINARY - (timezone for dtend)

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8214, PSETID_Appointment); 	// PT_LONG - Label
//	mapi_nameid_lid_add(nameid, 0x8234, PSETID_Appointment); 	// PT_STRING8 - display TimeZone
}


#define TASK_NAMED_PROPS_N 7
#define DEFAULT_TASK_REMINDER_MINS 1080

typedef enum 
{
	I_TASK_STATUS = COMMON_NAMED_PROPS_N , 
	I_TASK_PERCENT , 
//	I_TASK_ISTEAMTASK , 
	I_TASK_START , 
	I_TASK_DUE , 
	I_TASK_COMPLETED , 
//	I_TASK_RECURBLOB , 
	I_TASK_ISCOMPLETE , 
//	I_TASK_OWNER , 
//	I_TASK_DELEGATOR , 
	I_TASK_ISRECURRING , 
//	I_TASK_ROLE , 
//	I_TASK_OWNERSHIP , 
//	I_TASK_DELEGATIONSTATE , 
//	I_TASK_ACTUALWORK , 
//	I_TASK_TOTALWORK 
} TaskNamedPropsIndex;

static void 
task_build_name_id (struct mapi_nameid *nameid)
{
	mapi_nameid_lid_add(nameid, 0x8101, PSETID_Task); 	// PT_LONG - Status
	mapi_nameid_lid_add(nameid, 0x8102, PSETID_Task); 	// PT_DOUBLE - PercentComplete
//	mapi_nameid_lid_add(nameid, 0x8103, PSETID_Task); 	// PT_BOOLEAN - TeamTask
	mapi_nameid_lid_add(nameid, 0x8104, PSETID_Task); 	// PT_SYSTIME - StartDate/TaskStartDate
	mapi_nameid_lid_add(nameid, 0x8105, PSETID_Task); 	// PT_SYSTIME - DueDate/TaskDueDate
	mapi_nameid_lid_add(nameid, 0x810F, PSETID_Task); 	// PT_SYSTIME - DateCompleted
//	mapi_nameid_lid_add(nameid, 0x8116, PSETID_Task); 	// PT_BINARY - (recurrence blob)
	mapi_nameid_lid_add(nameid, 0x811C, PSETID_Task); 	// PT_BOOLEAN - Complete
//	mapi_nameid_lid_add(nameid, 0x811F, PSETID_Task); 	// PT_STRING8 - Owner
//	mapi_nameid_lid_add(nameid, 0x8121, PSETID_Task); 	// PT_STRING8 - Delegator
	mapi_nameid_lid_add(nameid, 0x8126, PSETID_Task); 	// PT_BOOLEAN - IsRecurring/TaskFRecur
//	mapi_nameid_lid_add(nameid, 0x8127, PSETID_Task); 	// PT_STRING8 - Role
//	mapi_nameid_lid_add(nameid, 0x8129, PSETID_Task); 	// PT_LONG - Ownership
//	mapi_nameid_lid_add(nameid, 0x812A, PSETID_Task); 	// PT_LONG - DelegationState

	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8110, PSETID_Task); 	// PT_LONG - ActualWork/TaskActualEffort
//	mapi_nameid_lid_add(nameid, 0x8111, PSETID_Task); 	// PT_LONG - TotalWork/TaskEstimatedEffort
}


#define NOTE_NAMED_PROPS_N 0

/*
typedef enum 
{
//	I_NOTE_COLOR 
} NoteNamedPropsIndex;
*/

static void 
note_build_name_id (struct mapi_nameid *nameid)
{
	/* These probably would never be used from Evolution */
//	mapi_nameid_lid_add(nameid, 0x8B00, PSETID_Note); 	// PT_LONG - Color
}


#define MINUTES_IN_HOUR 60
#define SECS_IN_MINUTE 60

/* regular props count includes common named props */
#define REGULAR_PROPS_N    COMMON_NAMED_PROPS_N + 11

/** 
 * NOTE: When a new regular property (PR_***) is added, 'REGULAR_PROPS_N' 
 * should be updated. 
 */
int
mapi_cal_build_props (struct SPropValue **value, struct SPropTagArray *proptag_array, gpointer data)
{
	ECalComponent *comp = E_CAL_COMPONENT (data);
	icalcomponent *ical_comp = e_cal_component_get_icalcomponent (comp);
	icalcomponent_kind  kind = icalcomponent_isa (ical_comp);
	struct SPropValue *props;
	int i=0;
	int32_t flag32;
	bool b;
	icalproperty *prop;
	struct icaltimetype dtstart, dtend, utc_dtstart, utc_dtend;
	const icaltimezone *utc_zone;
	const char *dtstart_tzid, *dtend_tzid, *text = NULL;
	struct timeval t;

	switch (kind) {
		case ICAL_VEVENT_COMPONENT:
			props = g_new (struct SPropValue, REGULAR_PROPS_N + APPT_NAMED_PROPS_N);
			set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Appointment");
			break;
		case ICAL_VTODO_COMPONENT:
			props = g_new (struct SPropValue, REGULAR_PROPS_N + TASK_NAMED_PROPS_N);
			set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.Task");
			break;
		case ICAL_VJOURNAL_COMPONENT:
			props = g_new (struct SPropValue, REGULAR_PROPS_N + NOTE_NAMED_PROPS_N);
			set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *) "IPM.StickyNote");
			break;
		default:
			return 0;
	} 											/* prop count: 1 */

	utc_zone = icaltimezone_get_utc_timezone ();

	dtstart = icalcomponent_get_dtstart (ical_comp);

	/* For VEVENTs */
	if (icalcomponent_get_first_property (ical_comp, ICAL_DTEND_PROPERTY) != 0)
		dtend = icalcomponent_get_dtend (ical_comp);
	/* For VTODOs */
	else if (icalcomponent_get_first_property (ical_comp, ICAL_DUE_PROPERTY) != 0)
		dtend = icalcomponent_get_due (ical_comp);
	else 
		dtend = icalcomponent_get_dtstart (ical_comp);

	dtstart_tzid = icaltime_get_tzid (dtstart);
	dtend_tzid = icaltime_get_tzid (dtend);

	utc_dtstart = icaltime_convert_to_zone (dtstart, utc_zone);
	utc_dtend = icaltime_convert_to_zone (dtend, utc_zone);

	/* FIXME: convert to unicode */
	text = icalcomponent_get_summary (ical_comp);
	if (!(text && *text)) 
		text = "";
	set_SPropValue_proptag(&props[i++], PR_SUBJECT, 					/* prop count: 2 */ 
					(const void *) text);
	set_SPropValue_proptag(&props[i++], PR_NORMALIZED_SUBJECT, 				/* prop count: 3 */ 
					(const void *) text);
	set_SPropValue_proptag(&props[i++], PR_CONVERSATION_TOPIC, 				/* prop count: 4 */
					(const void *) text);
	text = NULL;

	/* we don't support HTML event/task/memo editor */
	flag32 = EDITOR_FORMAT_PLAINTEXT;
	set_SPropValue_proptag(&props[i++], PR_MSG_EDITOR_FORMAT, &flag32); 			/* prop count: 5 */

	/* it'd be better to convert, then set it in unicode */
	text = icalcomponent_get_description (ical_comp);
	if (!(text && *text)) 
		text = "";
	set_SPropValue_proptag(&props[i++], PR_BODY, 						/* prop count: 6 */
					(const void *) text);
	text = NULL;

	/* Priority */
	flag32 = PRIORITY_NORMAL; 	/* default */
	prop = icalcomponent_get_first_property (ical_comp, ICAL_PRIORITY_PROPERTY);
	if (prop) {
		int priority = icalproperty_get_priority (prop);
		if (priority > 0 && priority <= 4)
			flag32 = PRIORITY_HIGH;
		else if (priority > 5 && priority <= 9)
			flag32 = PRIORITY_LOW;
	} 
	set_SPropValue_proptag(&props[i++], PR_PRIORITY, (const void *) &flag32); 		/* prop count: 7 */

/*
PR_SENT_REPRESENTING_NAME
PR_SENT_REPRESENTING_ADDRTYPE
PR_SENT_REPRESENTING_EMAIL_ADDRESS
PR_SENDER_NAME
PR_SENDER_ADDRTYPE
PR_SENDER_EMAIL_ADDRESS
*/

	flag32 = 0x1;
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_FLAGS, (const void *) &flag32); 		/* prop count: 8 */

	flag32 = 0x0;
	b = e_cal_component_has_alarms (comp);
	if (b) {
		/* FIXME: write code here */
	} else {
		switch (kind) {
			case ICAL_VEVENT_COMPONENT:
				flag32 = DEFAULT_APPT_REMINDER_MINS;
				break;
			case ICAL_VTODO_COMPONENT:
				flag32 = DEFAULT_TASK_REMINDER_MINS;
				break;
			default:
				break;
		}
		t.tv_sec = icaltime_as_timet (utc_dtstart) - (flag32 * SECS_IN_MINUTE);
		t.tv_usec = 0;
	}
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_REMMINS], (const void *) &flag32);
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_REMTIME], &t);
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_REMSET], (const void *) &b);
												/* prop count: 8 (no regular props added) */

	/* ReminderNextTime: FIXME for recurrence */
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_REMNEXTTIME], &t);

	/* Sensitivity, Private */
	flag32 = SENSITIVITY_NORMAL; 	/* default */
	b = 0; 				/* default */
	prop = icalcomponent_get_first_property (ical_comp, ICAL_CLASS_PROPERTY);
	if (prop) 
		switch (icalproperty_get_class (prop)) {
			/* FIXME: is this mapping correct ? */
			case ICAL_CLASS_PRIVATE:
				flag32 = SENSITIVITY_PRIVATE;
				b = 1;
				break;
			case ICAL_CLASS_CONFIDENTIAL:
				flag32 = SENSITIVITY_CONFIDENTIAL;
				b = 1;
				break;
			default: 
				break;
		}
	set_SPropValue_proptag(&props[i++], PR_SENSITIVITY, (const void *) &flag32); 		/* prop count: 9 */
	set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_ISPRIVATE], (const void *) &b);

	t.tv_sec = icaltime_as_timet (utc_dtstart);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_START], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], PR_START_DATE, &t); 			/* prop count: 10 */

	t.tv_sec = icaltime_as_timet (utc_dtend);
	t.tv_usec = 0;
	set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_COMMON_END], &t);
	set_SPropValue_proptag_date_timeval(&props[i++], PR_END_DATE, &t); 			/* prop count: 11 */

	if (kind == ICAL_VEVENT_COMPONENT) {
		const char *mapi_tzid;
		struct SBinary start_tz, end_tz; 

		/* Context menu flags */
		flag32 = 369; 
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_CTXMENUFLAGS], (const void *) &flag32);

		/* Busy Status */
		flag32 = BUSY_STATUS_BUSY; 	/* default */
		prop = icalcomponent_get_first_property (ical_comp, ICAL_TRANSP_PROPERTY);
		if (prop)
			switch (icalproperty_get_transp (prop)) {
				/* FIXME: is this mapping correct ? */
				case ICAL_TRANSP_TRANSPARENT:
				case ICAL_TRANSP_TRANSPARENTNOCONFLICT:
					flag32 = BUSY_STATUS_FREE;
					break;
				case ICAL_TRANSP_OPAQUE:
				case ICAL_TRANSP_OPAQUENOCONFLICT:
					flag32 = BUSY_STATUS_BUSY;
					break;
				default:
					break;
			}
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_BUSYSTATUS], (const void *) &flag32);

		/* Location */
		text = icalcomponent_get_location (ical_comp);
		if (!(text && *text)) 
			text = "";
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_LOCATION], (const void *) text);
		text = NULL;

		/* Start */
		t.tv_sec = icaltime_as_timet (utc_dtstart);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_APPT_START], &t);

		/* Start TZ */
		mapi_tzid = e_cal_backend_mapi_tz_util_get_mapi_equivalent ((dtstart_tzid && *dtstart_tzid) ? dtstart_tzid : "UTC");
		if (mapi_tzid && *mapi_tzid) {
			e_cal_backend_mapi_util_mapi_tz_to_bin (mapi_tzid, &start_tz);
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_STARTTZBLOB], (const void *) &start_tz);
		}

		/* End */
		t.tv_sec = icaltime_as_timet (utc_dtend);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_APPT_END], &t);

		/* End TZ */
		mapi_tzid = e_cal_backend_mapi_tz_util_get_mapi_equivalent ((dtend_tzid && *dtend_tzid) ? dtend_tzid : "UTC");
		if (mapi_tzid && *mapi_tzid) {
			e_cal_backend_mapi_util_mapi_tz_to_bin (mapi_tzid, &end_tz);
			set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ENDTZBLOB], (const void *) &end_tz);
		}

		/* Duration */
		flag32 = icaldurationtype_as_int (icaltime_subtract (dtend, dtstart));
		flag32 /= MINUTES_IN_HOUR;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_DURATION], (const void *) &flag32);

		/* All-day event */
		b = (icaltime_is_date (dtstart) && icaltime_is_date (dtend));
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ALLDAY], (const void *) &b);

		/* Meeting status */
		flag32 = e_cal_component_has_attendees (comp);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_MEETINGSTATUS], (const void *) &flag32);

		/* Recurring */
		b = e_cal_component_has_recurrences (comp);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ISRECURRING], (const void *) &b);

		/* Online Meeting : we probably would never support this */
		b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_ISONLINEMEET], (const void *) &b);

		/* Counter Proposal for appointments : not supported as of now */
		b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_APPT_COUNTERPROPOSAL], (const void *) &b);

	} else if (kind == ICAL_VTODO_COMPONENT) {
		double d;

		/* Context menu flags */ /* FIXME: for assigned tasks */
		flag32 = 272; 
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_CTXMENUFLAGS], (const void *) &flag32);

		/* Status, Percent complete, IsComplete */
		flag32 = olTaskNotStarted; 	/* default */
		b = 0; 				/* default */
		d = 0.0;
		prop = icalcomponent_get_first_property (ical_comp, ICAL_PERCENTCOMPLETE_PROPERTY);
		if (prop)
			d = 0.01 * icalproperty_get_percentcomplete (prop);

		switch (icalcomponent_get_status (ical_comp)) {
			/* FIXME: is this mapping correct ? */
			case ICAL_STATUS_INPROCESS:
				flag32 = olTaskInProgress;
				break;
			case ICAL_STATUS_COMPLETED:
				flag32 = olTaskComplete;
				b = 1;
				d = 1.0;
				break;
			case ICAL_STATUS_CANCELLED:
				flag32 = olTaskDeferred;
				break;
			default:
				break;
		}

		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_STATUS], (const void *) &flag32);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_PERCENT], (const void *) &d);
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_ISCOMPLETE], (const void *) &b);

		/* Date completed */
		if (b) {
			struct icaltimetype completed;
			prop = icalcomponent_get_first_property (ical_comp, ICAL_COMPLETED_PROPERTY);
			completed = icalproperty_get_completed (prop);

			t.tv_sec = icaltime_as_timet (completed);
			t.tv_usec = 0;
			set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_TASK_COMPLETED], &t);
		}

		/* Start */
		t.tv_sec = icaltime_as_timet (dtstart);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_TASK_START], &t);

		/* Due */
		t.tv_sec = icaltime_as_timet (dtend);
		t.tv_usec = 0;
		set_SPropValue_proptag_date_timeval(&props[i++], proptag_array->aulPropTag[I_TASK_DUE], &t);

		/* FIXME: Evolution does not support recurring tasks */
		b = 0;
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_TASK_ISRECURRING], (const void *) &b);

	} else if (kind == ICAL_VJOURNAL_COMPONENT) {
		/* Context menu flags */
		flag32 = 272; 
		set_SPropValue_proptag(&props[i++], proptag_array->aulPropTag[I_COMMON_CTXMENUFLAGS], (const void *) &flag32);

	}

	*value = props;

	return i;
}

void
e_cal_backend_mapi_util_dump_properties (struct mapi_SPropValue_array *properties)
{
	int i;

	for (i = 0; i < properties->cValues; i++) { 
		struct mapi_SPropValue *lpProp = &properties->lpProps[i];
		const char *tmp =  get_proptag_name (lpProp->ulPropTag);
		struct timeval t;
		if (tmp && *tmp)
			printf("\n%s \t",tmp);
		else
			printf("\n%x \t", lpProp->ulPropTag);
		switch(lpProp->ulPropTag & 0xFFFF) {
			case PT_BOOLEAN:
				printf(" (bool) - %d", lpProp->value.b);
				break;
			case PT_I2:
				printf(" (uint16_t) - %d", lpProp->value.i);
				break;
			case PT_LONG:
				printf(" (long) - %d", lpProp->value.l);
				break;
			case PT_DOUBLE:
				printf (" (double) - %lld", lpProp->value.dbl);
				break;
			case PT_I8:
				printf (" (int) - %lld", lpProp->value.d);
				break;
			case PT_SYSTIME:
				get_mapi_SPropValue_array_date_timeval (&t, properties, lpProp->ulPropTag);
				printf (" (struct FILETIME *) - %p\t[%s]\t", &lpProp->value.ft, icaltime_as_ical_string (icaltime_from_timet_with_zone (t.tv_sec, 0, 0)));
				break;
			case PT_ERROR:
//				printf (" (error) - %p", lpProp->value.err);
				break;
			case PT_STRING8:
				printf(" (string) - %s", lpProp->value.lpszA ? lpProp->value.lpszA : "null" );
				break;
			case PT_UNICODE:
				printf(" (unicodestring) - %s", lpProp->value.lpszW ? lpProp->value.lpszW : "null");
				break;
			case PT_BINARY:
				printf(" (struct SBinary_short *) - %p\t[%s]\t", &lpProp->value.bin, (const char *) (lpProp->value.bin.lpb));
				break;
			case PT_MV_STRING8:
// 				printf(" (struct mapi_SLPSTRArray *) - %p", &lpProp->value.MVszA);
				break;
			default:
				printf(" - NONE NULL");
				break;
		}
	}
}

