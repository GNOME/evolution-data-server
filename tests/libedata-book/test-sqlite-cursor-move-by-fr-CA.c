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
	g_assert (g_setenv ("EDS_COLLATE", "fr_CA.UTF-8", TRUE));

	/* Try them all once in the normal base locale */
	data = move_by_test_new ("/EbSdbCursor/fr_CA/Move/Forward", FALSE);
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
	move_by_test_add_assertion (data, 6, 4, 3, 7, 8, 15, 17);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA/Move/ForwardOnNameless", FALSE);
	move_by_test_add_assertion (data, 1, 11);
	move_by_test_add_assertion (data, 3, 1, 2, 5);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA/Move/Backwards", FALSE);
	move_by_test_add_assertion (data, -5, 20, 19, 9, 12, 13);
	move_by_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 8, 7);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA/Filtered/Move/Forward", FALSE);
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 3);
	move_by_test_add_assertion (data, 8, 8, 17, 16, 18, 10, 14, 12, 9);
	move_by_test_add (data, TRUE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA/Filtered/Move/Backwards", FALSE);
	move_by_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	move_by_test_add_assertion (data, -8, 16, 17, 8, 3, 5, 2, 1, 11);
	move_by_test_add (data, TRUE);


	/* Try them all again with collation = phonebook */
	data = move_by_test_new ("/EbSdbCursor/fr_CA@collation=phonebook/Move/Forward", TRUE);
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
	move_by_test_add_assertion (data, 6, 4, 3, 7, 8, 15, 17);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA@collation=phonebook/Move/ForwardOnNameless", TRUE);
	move_by_test_add_assertion (data, 1, 11);
	move_by_test_add_assertion (data, 3, 1, 2, 5);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA@collation=phonebook/Move/Backwards", TRUE);
	move_by_test_add_assertion (data, -5, 20, 19, 9, 12, 13);
	move_by_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 8, 7);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA@collation=phonebook/Filtered/Move/Forward", TRUE);
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 3);
	move_by_test_add_assertion (data, 8, 8, 17, 16, 18, 10, 14, 12, 9);
	move_by_test_add (data, TRUE);

	data = move_by_test_new ("/EbSdbCursor/fr_CA@collation=phonebook/Filtered/Move/Backwards", TRUE);
	move_by_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	move_by_test_add_assertion (data, -8, 16, 17, 8, 3, 5, 2, 1, 11);
	move_by_test_add (data, TRUE);

	return e_test_server_utils_run ();
}
