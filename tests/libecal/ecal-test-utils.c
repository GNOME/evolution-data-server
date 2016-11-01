/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#include <stdlib.h>
#include <gio/gio.h>
#include <libecal/libecal.h>

#include "ecal-test-utils.h"

typedef struct {
        GSourceFunc  cb;
        gpointer     user_data;
	CalMode      mode;
	ECal        *cal;
} ECalTestClosure;

void
test_print (const gchar *format,
            ...)
{
	va_list args;
	const gchar *debug_string;
	static gboolean debug_set = FALSE;
	static gboolean debug = FALSE;

	if (!debug_set) {
		debug_string = g_getenv ("EDS_TEST_DEBUG");
		if (debug_string) {
			debug = (g_ascii_strtoll (debug_string, NULL, 10) >= 1);
		}
		debug_set = TRUE;
	}

	if (debug) {
		va_start (args, format);
		vprintf (format, args);
		va_end (args);
	}
}

gchar *
ecal_test_utils_cal_get_alarm_email_address (ECal *cal)
{
	GError *error = NULL;
	gchar *address = NULL;

	if (!e_cal_get_alarm_email_address (cal, &address, &error)) {
		g_warning ("failed to get alarm email address; %s\n", error->message);
		exit (1);
	}
	test_print ("successfully got the alarm email address\n");

	return address;
}

gchar *
ecal_test_utils_cal_get_cal_address (ECal *cal)
{
	GError *error = NULL;
	gchar *address = NULL;

	if (!e_cal_get_cal_address (cal, &address, &error)) {
		g_warning ("failed to get calendar address; %s\n", error->message);
		exit (1);
	}
	test_print ("successfully got the calendar address\n");

	return address;
}

static const gchar *
b2s (gboolean value)
{
	return value ? "true" : "false";
}

void
ecal_test_utils_cal_get_capabilities (ECal *cal)
{
	test_print ("calendar capabilities:\n");
	test_print (
		"        One alarm only:                  %s\n"
		"        Organizers must attend meetings: %s\n"
		"        Organizers must accept meetings: %s\n"
		"        Master object for recurrences:   %s\n"
		"        Can save schedules:              %s\n"
		"        No alarm repeat:                 %s\n"
		"        No audio alarms:                 %s\n"
		"        No display alarms:               %s\n"
		"        No email alarms:                 %s\n"
		"        No procedure alarms:             %s\n"
		"        No task assignment:              %s\n"
		"        No 'this and future':            %s\n"
		"        No 'this and prior':             %s\n"
		"        No transparency:                 %s\n"
		"        Organizer not email address:     %s\n"
		"        Remove alarms:                   %s\n"
		"        Create messages:                 %s\n"
		"        No conv. to assigned task:       %s\n"
		"        No conv. to recurring:           %s\n"
		"        No general options:              %s\n"
		"        Requires send options:           %s\n"
		"        Delegate supported:              %s\n"
		"        No organizer required:           %s\n"
		"        Delegate to many:                %s\n"
		"        Has unaccepted meeting:          %s\n"
		,
		b2s (e_cal_get_one_alarm_only (cal)),
		b2s (e_cal_get_organizer_must_attend (cal)),
		b2s (e_cal_get_organizer_must_accept (cal)),
		b2s (e_cal_get_recurrences_no_master (cal)),
		b2s (e_cal_get_save_schedules (cal)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_THISANDFUTURE)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_THISANDPRIOR)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_TRANSPARENCY)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_REMOVE_ALARMS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_CREATE_MESSAGES)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_NO_ORGANIZER)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY)),
		b2s (e_cal_get_static_capability (cal,
		CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING))
		);
}

void
ecal_test_utils_cal_assert_objects_equal_shallow (icalcomponent *a,
                                                  icalcomponent *b)
{
	const gchar *uid_a, *uid_b;

	if (!icalcomponent_is_valid (a) && !icalcomponent_is_valid (b)) {
		g_warning ("both components invalid");
		return;
	}

	if (!icalcomponent_is_valid (a) || !icalcomponent_is_valid (b)) {
		g_error ("exactly one of the components being compared is invalid");
	}

	uid_a = icalcomponent_get_uid (a);
	uid_b = icalcomponent_get_uid (b);
	if (g_strcmp0 (uid_a, uid_b)) {
		g_error (
			"icomponents not equal:\n"
			"        uid A: '%s'\n"
			"        uid b: '%s'\n",
			uid_a, uid_b);
	}
}

void
ecal_test_utils_cal_assert_e_cal_components_equal (ECalComponent *a,
                                                   ECalComponent *b)
{
	icalcomponent *ical_a, *ical_b;
	ECalComponentTransparency transp_a, transp_b;

	ical_a = e_cal_component_get_icalcomponent (a);
	ical_b = e_cal_component_get_icalcomponent (b);
	ecal_test_utils_cal_assert_objects_equal_shallow (ical_a, ical_b);

        /* Dumping icalcomp into a string is not useful as the retrieved object
         * has some generated information like timestamps. We compare
         * member values we set during creation*/
	g_assert (e_cal_component_event_dates_match (a, b));

	e_cal_component_get_transparency (a, &transp_a);
	e_cal_component_get_transparency (b, &transp_b);
	g_assert (transp_a == transp_b);
}

icalcomponent *
ecal_test_utils_cal_get_object (ECal *cal,
                                const gchar *uid)
{
	GError *error = NULL;
	icalcomponent *component = NULL;

	if (!e_cal_get_object (cal, uid, NULL, &component, &error)) {
		g_warning ("failed to get icalcomponent object '%s'; %s\n", uid, error->message);
		exit (1);
	}
	if (!icalcomponent_is_valid (component)) {
		g_warning ("retrieved icalcomponent is invalid\n");
		exit (1);
	}
	test_print ("successfully got the icalcomponent object '%s'\n", uid);

	return component;
}

void
ecal_test_utils_cal_modify_object (ECal *cal,
                                   icalcomponent *component,
                                   ECalObjModType mod_type)
{
	GError *error = NULL;

	if (!icalcomponent_is_valid (component)) {
		g_warning (G_STRLOC ": icalcomponent argument is invalid\n");
		exit (1);
	}
	if (!e_cal_modify_object (cal, component, mod_type, &error)) {
		g_warning ("failed to modify icalcomponent object; %s\n", error->message);
		exit (1);
	}
	test_print ("successfully modified the icalcomponent object\n");
}

void
ecal_test_utils_cal_remove_object (ECal *cal,
                                   const gchar *uid)
{
	GError *error = NULL;

	if (!e_cal_remove_object (cal, uid, &error)) {
		g_warning ("failed to remove icalcomponent object '%s'; %s\n", uid, error->message);
		exit (1);
	}
	test_print ("successfully remoed the icalcomponent object '%s'\n", uid);
}

icalcomponent *
ecal_test_utils_cal_get_default_object (ECal *cal)
{
	GError *error = NULL;
	icalcomponent *component = NULL;

	if (!e_cal_get_default_object (cal, &component, &error)) {
		g_warning ("failed to get default icalcomponent object; %s\n", error->message);
		exit (1);
	}
	if (!icalcomponent_is_valid (component)) {
		g_warning ("default icalcomponent is invalid\n");
		exit (1);
	}
	test_print ("successfully got the default icalcomponent object\n");

	return component;
}

GList *
ecal_test_utils_cal_get_object_list (ECal *cal,
                                     const gchar *query)
{
	GError *error = NULL;
	GList *objects = NULL;

	if (!e_cal_get_object_list (cal, query, &objects, &error)) {
		g_warning ("failed to get list of icalcomponent objects for query '%s'; %s\n", query, error->message);
		exit (1);
	}
	test_print ("successfully got list of icalcomponent objects for the query '%s'\n", query);

	return objects;
}

GList *
ecal_test_utils_cal_get_objects_for_uid (ECal *cal,
                                         const gchar *uid)
{
	GError *error = NULL;
	GList *objects = NULL;

	if (!e_cal_get_objects_for_uid (cal, uid, &objects, &error)) {
		g_warning ("failed to get icalcomponent objects for UID '%s'; %s\n", uid, error->message);
		exit (1);
	}
	test_print ("successfully got objects for the icalcomponent with UID '%s'\n", uid);

	return objects;
}

gchar *
ecal_test_utils_cal_create_object (ECal *cal,
                                   icalcomponent *component)
{
	GError *error = NULL;
	gchar *uid = NULL;
	gchar *ical_string = NULL;

	if (!icalcomponent_is_valid (component)) {
		g_warning ("supplied icalcomponent is invalid\n");
		exit (1);
	}

	if (!e_cal_create_object (cal, component, &uid, &error)) {
		g_warning ("failed to get create an icalcomponent object; %s\n", error->message);
		exit (1);
	}

	ical_string = icalcomponent_as_ical_string (component);
	test_print (
		"successfully created icalcomponent object '%s'\n%s\n", uid,
		ical_string);
	g_free (ical_string);

	return uid;
}

static void
cal_set_mode_cb (ECal *cal,
                 ECalendarStatus status,
                 CalMode mode,
                 ECalTestClosure *closure)
{
	if (FALSE) {
	} else if (status == E_CALENDAR_STATUS_BUSY) {
		test_print ("calendar server is busy; waiting...");
		return;
	} else if (status != E_CALENDAR_STATUS_OK) {
		g_warning ("failed to asynchronously remove the calendar: "
				"status %d", status);
		exit (1);
	}

	closure->mode = mode;

	test_print ("successfully set the calendar mode to %d\n", mode);
	if (closure->cb)
		(*closure->cb) (closure);

	g_signal_handlers_disconnect_by_func (cal, cal_set_mode_cb, closure);
	g_free (closure);
}

void
ecal_test_utils_cal_set_mode (ECal *cal,
                              CalMode mode,
                              GSourceFunc callback,
                              gpointer user_data)
{
	ECalTestClosure *closure;

	closure = g_new0 (ECalTestClosure, 1);
	closure->cb = callback;
	closure->user_data = user_data;

	g_signal_connect (G_OBJECT (cal), "cal_set_mode", G_CALLBACK (cal_set_mode_cb), closure);
	e_cal_set_mode (cal, mode);
}

void
ecal_test_utils_create_component (ECal *cal,
                                  const gchar *dtstart,
                                  const gchar *dtstart_tzid,
                                  const gchar *dtend,
                                  const gchar *dtend_tzid,
                                  const gchar *summary,
                                  ECalComponent **comp_out,
                                  gchar **uid_out)
{
	ECalComponent *comp;
	icalcomponent *icalcomp;
	struct icaltimetype tt;
	ECalComponentText text;
	ECalComponentDateTime dt;
	gchar *uid;

	comp = e_cal_component_new ();
        /* set fields */
	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	text.value = summary;
	text.altrep = NULL;
	e_cal_component_set_summary (comp, &text);
	tt = icaltime_from_string (dtstart);
	dt.value = &tt;
	dt.tzid = dtstart_tzid;
	e_cal_component_set_dtstart (comp, &dt);
	tt = icaltime_from_string (dtend);
	dt.value = &tt;
	dt.tzid = dtend_tzid;
	e_cal_component_set_dtend (comp, &dt);
	e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);

	e_cal_component_commit_sequence (comp);
	icalcomp = e_cal_component_get_icalcomponent (comp);

	uid = ecal_test_utils_cal_create_object (cal, icalcomp);
	e_cal_component_commit_sequence (comp);

        *comp_out = comp;
        *uid_out = uid;
}

void
ecal_test_utils_cal_component_set_icalcomponent (ECalComponent *e_component,
                                                 icalcomponent *component)
{
	if (!e_cal_component_set_icalcomponent (e_component, component)) {
		g_error ("Could not set icalcomponent\n");
	}
}

icaltimezone *
ecal_test_utils_cal_get_timezone (ECal *cal,
                                  const gchar *tzid)
{
	GError *error = NULL;
	icaltimezone *zone = NULL;

	if (!e_cal_get_timezone (cal, tzid, &zone, &error)) {
		g_warning ("failed to get icaltimezone* for ID '%s'; %s\n", tzid, error->message);
		exit (1);
	}
	test_print ("successfully got icaltimezone* for ID '%s'\n", tzid);

	return zone;
}

void
ecal_test_utils_cal_add_timezone (ECal *cal,
                                  icaltimezone *zone)
{
	GError *error = NULL;
	const gchar *name;

	name = icaltimezone_get_display_name (zone);

	if (!e_cal_add_timezone (cal, zone, &error)) {
		g_warning ("failed to add icaltimezone '%s'; %s\n", name, error->message);
		exit (1);
	}
	test_print ("successfully added icaltimezone '%s'\n", name);
}

void
ecal_test_utils_cal_set_default_timezone (ECal *cal,
                                          icaltimezone *zone)
{
	GError *error = NULL;
	const gchar *name;

	name = icaltimezone_get_display_name (zone);

	if (!e_cal_set_default_timezone (cal, zone, &error)) {
		g_warning ("failed to set default icaltimezone '%s'; %s\n", name, error->message);
		exit (1);
	}
	test_print ("successfully set default icaltimezone '%s'\n", name);
}

GList *
ecal_test_utils_cal_get_free_busy (ECal *cal,
                                   GList *users,
                                   time_t start,
                                   time_t end)
{
	GList *free_busy = NULL;
	GList *l = NULL;
	GError *error = NULL;

	if (!e_cal_get_free_busy (cal, users, start, end, &free_busy, &error)) {
		g_error ("Test free/busy : Could not retrieve free busy information :  %s\n", error->message);
	}
	if (free_busy) {
		test_print ("Printing free/busy information\n");

		for (l = free_busy; l; l = l->next) {
			gchar *comp_string;
			ECalComponent *comp = E_CAL_COMPONENT (l->data);

			comp_string = e_cal_component_get_as_string (comp);
			test_print ("%s\n", comp_string);
			g_free (comp_string);
		}
	} else {
		g_error ("got empty free/busy information");
	}

	return free_busy;
}

void
ecal_test_utils_cal_send_objects (ECal *cal,
                                  icalcomponent *component,
                                  GList **users,
                                  icalcomponent **component_final)
{
	GList *l = NULL;
	GError *error = NULL;

	if (!e_cal_send_objects (cal, component, users, component_final, &error)) {
		g_error ("sending objects: %s\n", error->message);
	}

	test_print ("successfully sent the objects to the following users:\n");
	if (g_list_length (*users) <= 0) {
		test_print ("        (none)\n");
		return;
	}
	for (l = *users; l; l = l->next) {
		test_print ("        %s\n", (const gchar *) l->data);
	}
}

void
ecal_test_utils_cal_receive_objects (ECal *cal,
                                     icalcomponent *component)
{
	GError *error = NULL;

	if (!e_cal_receive_objects (cal, component, &error)) {
		g_error ("receiving objects: %s\n", error->message);
	}

	test_print ("successfully received the objects\n");
}

ECalView *
ecal_test_utils_get_query (ECal *cal,
                           const gchar *sexp)
{
	GError *error = NULL;
	ECalView *query = NULL;

	if (!e_cal_get_query (cal, sexp, &query, &error)) {
		g_error (
			G_STRLOC ": Unable to obtain calendar view: %s\n",
			error->message);
	}
	test_print ("successfully retrieved calendar view for query '%s'", sexp);

	return query;
}
