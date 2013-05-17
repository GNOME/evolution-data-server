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
	g_assert (g_setenv ("EDS_COLLATE", "de_DE.UTF-8", TRUE));

	data = move_by_test_new ("/EbSdbCursor/de_DE/Move/Forward");
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
	move_by_test_add_assertion (data, 6, 7, 8, 4, 3, 15, 17);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/de_DE/Move/ForwardOnNameless");
	move_by_test_add_assertion (data, 1, 11);
	move_by_test_add_assertion (data, 3, 1, 2, 5);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/de_DE/Move/Backwards");
	move_by_test_add_assertion (data, -5, 19, 20, 9, 13, 12);
	move_by_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 3, 4);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/de_DE/Filtered/Move/Forward");
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 8);
	move_by_test_add_assertion (data, 8, 3, 17, 16, 18, 10, 14, 12, 9);
	move_by_test_add (data, TRUE);

	data = move_by_test_new ("/EbSdbCursor/de_DE/Filtered/Move/Backwards");
	move_by_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	move_by_test_add_assertion (data, -8, 16, 17, 3, 8, 5, 2, 1, 11);
	move_by_test_add (data, TRUE);

	return e_test_server_utils_run ();
}
