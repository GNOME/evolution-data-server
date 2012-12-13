/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

#define SET_MODE_TIMEOUT 30
#define MODE_FINAL CAL_MODE_LOCAL

static void
cal_set_mode_cb (ECalTestClosure *closure)
{
	if (closure->mode != MODE_FINAL) {
		g_warning (
			"set mode to %d, but we expected %d",
			closure->mode, MODE_FINAL);
	}

	g_main_loop_quit ((GMainLoop *) closure->user_data);
}

static gboolean
cal_set_mode_timeout_cb (gpointer user_data)
{
	g_error (
		"failed to get a confirmation for the new calendar mode we "
		"set (within a reasonable time frame)");
	return FALSE;
}

static void
test_set_mode (ETestServerFixture *fixture,
	       gconstpointer       user_data)
{
	ECal *cal;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	g_timeout_add_seconds (SET_MODE_TIMEOUT, (GSourceFunc) cal_set_mode_timeout_cb, cal);

	ecal_test_utils_cal_set_mode (
		cal, MODE_FINAL, (GSourceFunc) cal_set_mode_cb, fixture->loop);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/ECal/SetMode", ETestServerFixture, &cal_closure,
		    e_test_server_utils_setup, test_set_mode, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
