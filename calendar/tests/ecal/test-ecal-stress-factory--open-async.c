/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

#define OPEN_ASYNC_TIMEOUT 200
#define NUM_CALS 200

static void open_timeout_cb (gpointer user_data) __attribute__ ((noreturn));

static guint open_timeout_id = 0;
static ECal *cals[NUM_CALS];
static gint cals_processed = 0;

static void
open_complete_cb (ECalTestClosure *closure)
{
	g_source_remove (open_timeout_id);
	ecal_test_utils_cal_remove (closure->cal);

	cals_processed++;

	if (cals_processed == NUM_CALS) {
		test_print ("asynchronously opened all calendars successfully\n");
		g_main_loop_quit ((GMainLoop*) closure->user_data);
	}
}

static void
open_timeout_cb (gpointer user_data)
{
	g_error ("failed to get a response for the async 'open' within a "
			"reasonable time frame");
}

gint
main (gint argc, gchar **argv)
{
	gchar *uri = NULL;
	GMainLoop *loop;
	gint i;

	g_type_init ();

	open_timeout_id = g_timeout_add_seconds (OPEN_ASYNC_TIMEOUT,
			(GSourceFunc) open_timeout_cb, NULL);

	loop = g_main_loop_new (NULL, TRUE);

        /* open and close many calendars in parallel */
        for (i = 0; i < NUM_CALS; i++) {
                cals[i] = ecal_test_utils_cal_new_temp (&uri,
                                E_CAL_SOURCE_TYPE_EVENT);
		ecal_test_utils_cal_async_open (cals[i], FALSE,
				(GSourceFunc) open_complete_cb, loop);

		g_free (uri);
        }

	g_main_loop_run (loop);

	return 0;
}
