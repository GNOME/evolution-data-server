/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

gint
main (gint argc,
      gchar **argv)
{
	MoveByData *data;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("MIGRATION_TEST_SOURCE_NAME", "migration-test-source", TRUE));

	data = move_by_test_new ("/EbSdbCursor/Locale/de_DE/Migrated", "de_DE.UTF-8");
	move_by_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
	move_by_test_add_assertion (data, 5, 7,  8,  4,  3,  15);
	move_by_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
	move_by_test_add_assertion (data, 5, 12, 13, 9,  20, 19);
	move_by_test_add (data, FALSE);

	/* On this case, we are using the migrated addressbook, don't delete it first */
	return e_test_server_utils_run_full (E_TEST_SERVER_KEEP_WORK_DIRECTORY);
}
