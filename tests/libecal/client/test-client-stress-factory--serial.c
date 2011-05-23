/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libecal/e-cal-client.h>

#include "client-test-utils.h"

#define NUM_CLIENTS 200

gint
main (gint argc, gchar **argv)
{
	ECalClientSourceType source_type = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
	GError *error = NULL;
	gint ii;

	main_initialize ();

	/* Serially create, open, (close), and remove many cals */
	for (ii = 0; ii < NUM_CLIENTS; ii++) {
		ECalClient *cal_client = new_temp_client (source_type, NULL);
		g_return_val_if_fail (cal_client != NULL, 1);

		if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
			report_error ("client open sync", &error);
			return 1;
		}

		if (!e_client_remove_sync (E_CLIENT (cal_client), NULL, &error)) {
			report_error ("client remove sync", &error);
			g_object_unref (cal_client);
			return 1;
		}

		g_object_unref (cal_client);
	}

	return 0;
}
