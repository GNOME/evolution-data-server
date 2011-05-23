/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>

#include "client-test-utils.h"

#define NUM_VIEWS 200

static void
objects_added (ECalClientView *cal_view, const GSList *objects)
{
	const GSList *l;

	for (l = objects; l; l = l->next) {
		print_icomp (l->data);
	}
}

static void
objects_removed (ECalClientView *view, const GSList *ids)
{
	const GSList *l;

	for (l = ids; l; l = l->next) {
		printf ("   Removed contact: %s\n", (gchar *) l->data);
	}
}

static void
complete (ECalClientView *view, const GError *error)
{
	printf ("view_complete (status == %d, error_msg == %s%s%s)\n", error ? error->code : 0, error ? "'" : "", error ? error->message : "NULL", error ? "'" : "");
}

static gint
stress_cal_views (ECalClient *cal_client, gboolean in_thread)
{
	ECalClientView *view = NULL;
	ECalClientView *new_view;
	gint i;

	g_return_val_if_fail (cal_client != NULL, -1);
	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), -1);

	for (i = 0; i < NUM_VIEWS; i++) {
		GError *error = NULL;

		if (!e_cal_client_get_view_sync (cal_client, "#t", &new_view, NULL, &error)) {
			report_error ("get cal view sync", &error);
			g_object_unref (view);
			return 1;
		}

		g_signal_connect (new_view, "objects_added", G_CALLBACK (objects_added), NULL);
		g_signal_connect (new_view, "objects_removed", G_CALLBACK (objects_removed), NULL);
		g_signal_connect (new_view, "complete", G_CALLBACK (complete), NULL);

		e_cal_client_view_start (new_view, NULL);

		if (view) {
			/* wait 100 ms when in a thread */
			if (in_thread)
				g_usleep (100000);

			e_cal_client_view_stop (view, NULL);
			g_object_unref (view);
		}

		view = new_view;
	}

	e_cal_client_view_stop (view, NULL);
	g_object_unref (view);

	return 0;
}

static gpointer
stress_cal_views_thread (gpointer user_data)
{
	stop_main_loop (stress_cal_views (user_data, TRUE));

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	ECalClient *cal_client;
	GError *error = NULL;

	main_initialize ();

	cal_client = e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, &error);
	if (!cal_client) {
		report_error ("create local calendar", &error);
		return 1;
	}

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		g_object_unref (cal_client);
		report_error ("open client sync", &error);
		return 1;
	}

	/* test from main thread */
	stress_cal_views (cal_client, FALSE);

	/* test from dedicated thread */
	start_in_thread_with_main_loop (stress_cal_views_thread, cal_client);

	g_object_unref (cal_client);

	return get_main_loop_stop_result ();
}
