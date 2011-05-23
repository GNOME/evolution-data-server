/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal-client.h>

#include "client-test-utils.h"

#define NUM_CLIENTS 200

gint
main (gint argc, gchar **argv)
{
	ECalClientSourceType source_type = E_CAL_CLIENT_SOURCE_TYPE_EVENTS;
	ECalClient *cal_clients[NUM_CLIENTS];
	GError *error = NULL;
	gint ii;

	main_initialize ();

	/* Create and open many cals; then remove each of them */

	for (ii = 0; ii < NUM_CLIENTS; ii++) {
		cal_clients[ii] = new_temp_client (source_type, NULL);
		g_return_val_if_fail (cal_clients[ii] != NULL, 1);

		if (!e_client_open_sync (E_CLIENT (cal_clients[ii]), FALSE, NULL, &error)) {
			report_error ("client open sync", &error);
			while (ii >= 0) {
				g_object_unref (cal_clients[ii]);
				ii--;
			}

			return 1;
		}
	}

	for (ii = 0; ii < NUM_CLIENTS; ii++) {
		if (!e_client_remove_sync (E_CLIENT (cal_clients[ii]), NULL, &error)) {
			report_error ("client remove sync", &error);
			while (ii < NUM_CLIENTS) {
				g_object_unref (cal_clients[ii]);
				ii++;
			}
			return 1;
		}

		g_object_unref (cal_clients[ii]);
	}

	return 0;
}
