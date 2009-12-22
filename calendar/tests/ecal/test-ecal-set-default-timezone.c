/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

#define TZID_NEW "XYZ"
#define TZNAME_NEW "Ex Wye Zee"
#define EVENT_SUMMARY "Creation of new test event in the " TZID_NEW " timezone"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
	icalproperty *property;
	icalcomponent *component;
	icaltimezone *zone;
	icaltimezone *zone_final;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);

	/* Build up new timezone */
	component = icalcomponent_new_vtimezone ();
	property = icalproperty_new_tzid (TZID_NEW);
	icalcomponent_add_property (component, property);
	property = icalproperty_new_tzname (TZNAME_NEW);
	icalcomponent_add_property (component, property);
	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, component);

	/* set default; must be done before opening the calendar */
	ecal_test_utils_cal_open (cal, FALSE);
	ecal_test_utils_cal_add_timezone (cal, zone);
	ecal_test_utils_cal_set_default_timezone (cal, zone);

	/* verify */
	/* FIXME: enhance the validation; confirm that the timezone was actually
	 * set as the default */
	zone_final = ecal_test_utils_cal_get_timezone (cal, TZID_NEW);
	g_assert (!g_strcmp0 (icaltimezone_get_tzid (zone),
			icaltimezone_get_tzid (zone_final)));
	g_assert (!g_strcmp0 (icaltimezone_get_tznames (zone),
			icaltimezone_get_tznames (zone_final)));

	ecal_test_utils_cal_remove (cal);
	icaltimezone_free (zone, TRUE);

	return 0;
}
