/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>

gint
main (gint argc,
      gchar **argv)
{
	ECal *ecal;
	GList *l, *objects;

	g_type_init ();

	if (argc < 3) {
		printf ("usage: test-search <uid> <query>\n");
		exit (0);
	}

	/* FIXME We don't build ECals from URIs anymore. */
	/* ecal = e_cal_new_from_uri (argv[1], E_CAL_SOURCE_TYPE_EVENT); */
	ecal = NULL;

	if (!e_cal_open (ecal, TRUE, NULL)) {
		printf ("failed to open calendar\n");
		exit (0);
	}

	if (!e_cal_get_object_list_as_comp (ecal, argv[2], &objects, NULL)) {
		printf ("failed to get objects\n");
		exit (0);
	}

	printf ("Received %d objects\n", g_list_length (objects));
	for (l = objects; l; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);
		gchar *str;

		str = e_cal_component_get_as_string (comp);
		printf ("%s\n", str);

		g_free (str);
		g_object_unref (comp);
	}

	g_list_free (objects);

	g_object_unref (ecal);

	return 0;
}
