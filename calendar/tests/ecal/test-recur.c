/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libecal/e-cal.h>

int
main (int argc, char **argv)
{
	ECal *ecal;
	GList *l, *objects;

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	if (argc < 2) {
		printf ("usage: test-recur <uid>\n");
		exit (0);
	}

	ecal = e_cal_new_from_uri (argv[1], E_CAL_SOURCE_TYPE_EVENT);

	if (!e_cal_open (ecal, TRUE, NULL)) {
		printf ("failed to open calendar\n");
		exit(0);
	}

	g_object_unref (ecal);

	return 0;
}
