/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

#define SET_MODE_TIMEOUT 30
#define MODE_FINAL CAL_MODE_LOCAL

static void cal_set_mode_timeout_cb (gpointer user_data) __attribute__ ((noreturn));

static guint cal_set_mode_timeout_id = 0;

static void
cal_set_mode_cb (ECalTestClosure *closure)
{
	g_source_remove (cal_set_mode_timeout_id);

	if (closure->mode != MODE_FINAL) {
		g_warning ("set mode to %d, but we expected %d", closure->mode,
				MODE_FINAL);
	}

        g_main_loop_quit ((GMainLoop*) closure->user_data);
}

static void
cal_set_mode_timeout_cb (gpointer user_data)
{
	g_warning ("failed to get a confirmation for the new calendar mode we "
			"set (within a reasonable time frame)");
	exit (1);
}

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
	GMainLoop *loop;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	cal_set_mode_timeout_id = g_timeout_add_seconds (SET_MODE_TIMEOUT,
			(GSourceFunc) cal_set_mode_timeout_cb, cal);

	loop = g_main_loop_new (NULL, TRUE);
	ecal_test_utils_cal_set_mode (cal, MODE_FINAL,
			(GSourceFunc) cal_set_mode_cb, loop);

	g_main_loop_run (loop);

	ecal_test_utils_cal_remove (cal);

	return 0;
}
