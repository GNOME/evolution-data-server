/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libecal/e-cal.h>

int
main (int argc, char **argv)
{
	ECal *ecal;
	GList *l, *objects;

	gnome_program_init("test-search", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	if (argc < 3) {
		printf ("usage: test-search <uid> <query>\n");
		exit (0);
	}

	ecal = e_cal_new_from_uri (argv[1], E_CAL_SOURCE_TYPE_EVENT);

	if (!e_cal_open (ecal, TRUE, NULL)) {
		printf ("failed to open calendar\n");
		exit(0);
	}

	if (!e_cal_get_object_list_as_comp (ecal, argv[2], &objects, NULL)) {
		printf ("failed to get objects\n");
		exit(0);
	}

	printf ("Received %d objects\n", g_list_length (objects));
	for (l = objects; l; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);
		char *str;
		
		str = e_cal_component_get_as_string (comp);
		printf ("%s\n", str);

		g_free (str);
		g_object_unref (comp);
	}

	g_list_free (objects);

	g_object_unref (ecal);

	return 0;
}
