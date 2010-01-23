/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

#define OPEN_ASYNC_TIMEOUT 30

static void open_timeout_cb (gpointer user_data) __attribute__ ((noreturn));

static guint open_timeout_id = 0;

static void
open_complete_cb (ECalTestClosure *closure)
{
	g_source_remove (open_timeout_id);

        g_main_loop_quit ((GMainLoop*) closure->user_data);
}

static void
open_timeout_cb (gpointer user_data)
{
	g_warning ("failed to get a response for the async 'open' within a "
			"reasonable time frame");
	exit (1);
}

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	gchar *uri = NULL;
	GMainLoop *loop;

	g_type_init ();

	/* Sync version */
	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);
	ecal_test_utils_cal_remove (cal);

	/* Async version */
	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	open_timeout_id = g_timeout_add_seconds (OPEN_ASYNC_TIMEOUT,
			(GSourceFunc) open_timeout_cb, cal);

	loop = g_main_loop_new (NULL, TRUE);
	ecal_test_utils_cal_async_open (cal, FALSE,
			(GSourceFunc) open_complete_cb, loop);
	g_main_loop_run (loop);

	ecal_test_utils_cal_remove (cal);

	return 0;
}
