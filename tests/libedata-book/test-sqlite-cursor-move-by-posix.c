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

	data = step_test_new ("/EbSdbCursor/POSIX/Move/Forward", "POSIX");
	step_test_add_assertion (data, 5, 11, 2, 6, 3, 8);
	step_test_add_assertion (data, 6, 1,  5,  4,  7,  15, 17);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/POSIX/Move/ForwardOnNameless", "POSIX");
	step_test_add_assertion (data, 1, 11);
	step_test_add_assertion (data, 3, 2, 6, 3);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/POSIX/Move/Backwards", "POSIX");
	step_test_add_assertion (data, -5, 20, 19, 9, 13, 12);
	step_test_add_assertion (data, -12, 14, 10, 18, 16, 17, 15, 7, 4, 5, 1, 8, 3);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/POSIX/Filtered/Move/Forward", "POSIX");
	step_test_add_assertion (data, 5, 11, 2, 3, 8, 1);
	step_test_add_assertion (data, 8, 5, 17, 16, 18, 10, 14, 12, 9);
	step_test_add (data, TRUE);

	data = step_test_new ("/EbSdbCursor/POSIX/Filtered/Move/Backwards", "POSIX");
	step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	step_test_add_assertion (data, -8, 16, 17, 5, 1, 8, 3, 2, 11);
	step_test_add (data, TRUE);

	return e_test_server_utils_run ();
}
