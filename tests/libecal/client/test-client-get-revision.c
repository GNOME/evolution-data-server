/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libecal/libecal.h>

#include "client-test-utils.h"

#define CYCLES 10

static void
get_revision_compare_cycle (ECalClient *client)
{
       icalcomponent *icalcomp;
       struct icaltimetype now;
       gchar    *revision_before = NULL, *revision_after = NULL, *uid = NULL;
       GError   *error = NULL;

       /* Build up new component */
       now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
       icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
       icalcomponent_set_summary (icalcomp, "Test event summary");
       icalcomponent_set_dtstart (icalcomp, now);
       icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

       if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION,
						&revision_before, NULL, &error))
	       g_error ("Error getting book revision: %s", error->message);

       if (!e_cal_client_create_object_sync (client, icalcomp, &uid, NULL, &error))
	       g_error ("Error creating object: %s", error->message);

       if (!e_client_get_backend_property_sync (E_CLIENT (client), CLIENT_BACKEND_PROPERTY_REVISION,
						&revision_after, NULL, &error))
	       g_error ("Error getting book revision: %s", error->message);

       g_assert (revision_before);
       g_assert (revision_after);
       g_assert (strcmp (revision_before, revision_after) != 0);

       g_message ("Passed cycle, revision before '%s' revision after '%s'",
		  revision_before, revision_after);

       g_free (revision_before);
       g_free (revision_after);
       g_free (uid);

       icalcomponent_free (icalcomp);
}

gint
main (gint argc,
      gchar **argv)
{
	ECalClient *cal_client;
	GError      *error = NULL;
	gint         i;

	main_initialize ();

	/*
	 * Setup
	 */
	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);

	g_return_val_if_fail (cal_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return 1;
	}

	/* Test that modifications make the revisions increment */
	for (i = 0; i < CYCLES; i++)
		get_revision_compare_cycle (cal_client);

	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		return 1;
	}

	g_object_unref (cal_client);

	return 0;
}
