/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>

#include "client-test-utils.h"

#define NUM_OPENS 200

gint
main (gint argc, gchar **argv)
{
	ECalClientSourceType source_type = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
	gchar *uri = NULL;
	ECalClient *cal_client;
	GError *error = NULL;
	gint ii;

	main_initialize ();

	cal_client = new_temp_client (source_type, &uri);
	g_return_val_if_fail (cal_client != NULL, 1);
	g_return_val_if_fail (uri != NULL, 1);

	g_object_unref (cal_client);

	/* open and close the same cal repeatedly */
	for (ii = 0; ii < NUM_OPENS; ii++) {
		cal_client = e_cal_client_new_from_uri (uri, source_type, &error);
		if (!cal_client) {
			report_error ("new from uri", &error);
			break;
		}

		if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
			report_error ("client open sync", &error);
			g_object_unref (cal_client);
			break;
		}

		g_object_unref (cal_client);
	}

	cal_client = e_cal_client_new_from_uri (uri, source_type, &error);
	if (!cal_client) {
		g_clear_error (&error);
	} else if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		g_free (uri);
		return 1;
	} else 	if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (cal_client);
		g_free (uri);
		return 1;
	}

	g_free (uri);
	g_object_unref (cal_client);

	return ii == NUM_OPENS ? 0 : 1;
}
