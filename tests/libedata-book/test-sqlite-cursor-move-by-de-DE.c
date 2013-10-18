/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

gint
main (gint argc,
      gchar **argv)
{
	StepData *data;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	data = step_test_new ("/EbSdbCursor/de_DE/Move/Forward", "de_DE.UTF-8");
	step_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
	step_test_add_assertion (data, 6, 7, 8, 4, 3, 15, 17);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/de_DE/Move/ForwardOnNameless", "de_DE.UTF-8");
	step_test_add_assertion (data, 1, 11);
	step_test_add_assertion (data, 3, 1, 2, 5);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/de_DE/Move/Backwards", "de_DE.UTF-8");
	step_test_add_assertion (data, -5, 19, 20, 9, 13, 12);
	step_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 3, 4);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/de_DE/Filtered/Move/Forward", "de_DE.UTF-8");
	step_test_add_assertion (data, 5, 11, 1, 2, 5, 8);
	step_test_add_assertion (data, 8, 3, 17, 16, 18, 10, 14, 12, 9);
	step_test_add (data, TRUE);

	data = step_test_new ("/EbSdbCursor/de_DE/Filtered/Move/Backwards", "de_DE.UTF-8");
	step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	step_test_add_assertion (data, -8, 16, 17, 3, 8, 5, 2, 1, 11);
	step_test_add (data, TRUE);

	return e_test_server_utils_run ();
}
