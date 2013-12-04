/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"

#define TZID_NEW "XYZ"
#define TZNAME_NEW "Ex Wye Zee"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

static void
test_add_timezone_sync (ETestServerFixture *fixture,
                        gconstpointer user_data)
{
	ECalClient *cal_client;
	icalproperty *property;
	icalcomponent *component;
	icaltimezone *zone;
	icaltimezone *zone2 = NULL;
	GError *error = NULL;

	/* Build up new timezone */
	component = icalcomponent_new_vtimezone ();
	property = icalproperty_new_tzid (TZID_NEW);
	icalcomponent_add_property (component, property);
	property = icalproperty_new_tzname (TZNAME_NEW);
	icalcomponent_add_property (component, property);
	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, component);

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	if (!e_cal_client_add_timezone_sync (cal_client, zone, NULL, &error))
		g_error ("add timezone sync: %s", error->message);

	if (!e_cal_client_get_timezone_sync (cal_client, TZID_NEW, &zone2, NULL, &error))
		g_error ("get timezone sync: %s", error->message);

	if (!zone2)
		g_error ("Failure: get timezone returned NULL");

	g_assert_cmpstr (icaltimezone_get_tzid (zone), ==, icaltimezone_get_tzid (zone2));
	g_assert_cmpstr (icaltimezone_get_tznames (zone), ==, icaltimezone_get_tznames (zone2));

	icaltimezone_free (zone, TRUE);
}

typedef struct {
	icaltimezone *zone;
	GMainLoop *loop;
} AsyncData;

static void
async_read_result_ready (GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	AsyncData *data = (AsyncData *) user_data;
	icaltimezone *zone1 = data->zone, *zone2 = NULL;

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_timezone_finish (cal_client, result, &zone2, &error))
		g_error ("get timezone finish: %s", error->message);

	if (!zone2)
		g_error ("Failure: get timezone returned NULL");

	g_assert_cmpstr (icaltimezone_get_tzid (zone1), ==, icaltimezone_get_tzid (zone2));
	g_assert_cmpstr (icaltimezone_get_tznames (zone1), ==, icaltimezone_get_tznames (zone2));

	g_main_loop_quit (data->loop);
}

static void
async_write_result_ready (GObject *source_object,
                          GAsyncResult *result,
                          gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (user_data != NULL);
	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_add_timezone_finish (cal_client, result, &error))
		g_error ("add timezone finish: %s", error->message);

	e_cal_client_get_timezone (cal_client, TZID_NEW, NULL, async_read_result_ready, user_data);
}

static void
test_add_timezone_async (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	icalproperty *property;
	icalcomponent *component;
	icaltimezone *zone;
	AsyncData data;

	/* Build up new timezone */
	component = icalcomponent_new_vtimezone ();
	property = icalproperty_new_tzid (TZID_NEW);
	icalcomponent_add_property (component, property);
	property = icalproperty_new_tzname (TZNAME_NEW);
	icalcomponent_add_property (component, property);
	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, component);

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	data.zone = zone;
	data.loop = fixture->loop;
	e_cal_client_add_timezone (cal_client, zone, NULL, async_write_result_ready, &data);
	g_main_loop_run (fixture->loop);

	icaltimezone_free (zone, TRUE);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/AddTimezone/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_add_timezone_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/AddTimezone/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_add_timezone_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
