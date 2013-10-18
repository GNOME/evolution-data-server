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

	g_assert (g_setenv ("MIGRATION_TEST_SOURCE_NAME", "migration-test-source", TRUE));

	data = step_test_new ("/EbSdbCursor/ChangeLocale/POSIX", "POSIX");
	step_test_add_assertion (data, 5, 11, 2,  6,  3,  8);
	step_test_add_assertion (data, 5, 1,  5,  4,  7,  15);
	step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
	step_test_add_assertion (data, 5, 12, 13, 9,  19, 20);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/ChangeLocale/en_US", "en_US.UTF-8");
	step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
	step_test_add_assertion (data, 5, 4,  3,  7,  8,  15);
	step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
	step_test_add_assertion (data, 5, 12, 13, 9,  19, 20);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/ChangeLocale/fr_CA", "fr_CA.UTF-8");
	step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
	step_test_add_assertion (data, 5, 4,  3,  7,  8,  15);
	step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
	step_test_add_assertion (data, 5, 13, 12, 9,  19, 20);
	step_test_add (data, FALSE);

	data = step_test_new ("/EbSdbCursor/ChangeLocale/de_DE", "de_DE.UTF-8");
	step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
	step_test_add_assertion (data, 5, 7,  8,  4,  3,  15);
	step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
	step_test_add_assertion (data, 5, 12, 13, 9,  20, 19);
	step_test_add (data, FALSE);

	/* On this case, we want to delete the work directory and start afresh */
	return e_test_server_utils_run ();
}
