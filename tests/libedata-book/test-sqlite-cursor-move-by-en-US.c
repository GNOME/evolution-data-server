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

	data = move_by_test_new ("/EbSdbCursor/en_US/Move/Forward", "en_US.UTF-8");
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
	move_by_test_add_assertion (data, 6, 4, 3, 7, 8, 15, 17);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/en_US/Move/ForwardOnNameless", "en_US.UTF-8");
	move_by_test_add_assertion (data, 1, 11);
	move_by_test_add_assertion (data, 3, 1, 2, 5);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/en_US/Move/Backwards", "en_US.UTF-8");
	move_by_test_add_assertion (data, -5, 20, 19, 9, 13, 12);
	move_by_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 8, 7);
	move_by_test_add (data, FALSE);

	data = move_by_test_new ("/EbSdbCursor/en_US/Filtered/Move/Forward", "en_US.UTF-8");
	move_by_test_add_assertion (data, 5, 11, 1, 2, 5, 3);
	move_by_test_add_assertion (data, 8, 8, 17, 16, 18, 10, 14, 12, 9);
	move_by_test_add (data, TRUE);

	data = move_by_test_new ("/EbSdbCursor/en_US/Filtered/Move/Backwards", "en_US.UTF-8");
	move_by_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
	move_by_test_add_assertion (data, -8, 16, 17, 8, 3, 5, 2, 1, 11);
	move_by_test_add (data, TRUE);

	data = move_by_test_new_full ("/EbSdbCursor/en_US/Move/Descending/Forward", "en_US.UTF-8",
				      E_BOOK_SORT_DESCENDING);
	move_by_test_add_assertion (data, 5, 20, 19, 9,  13, 12);
	move_by_test_add_assertion (data, 5, 14, 10, 18, 16, 17);
	move_by_test_add_assertion (data, 5, 15, 8,  7,  3,  4);
	move_by_test_add_assertion (data, 5, 6,  5,  2,  1,  11);
	move_by_test_add (data, FALSE);

	data = move_by_test_new_full ("/EbSdbCursor/en_US/Move/Descending/Forward/Loop", "en_US.UTF-8",
				      E_BOOK_SORT_DESCENDING);
	move_by_test_add_assertion (data, 10, 20, 19, 9,  13, 12, 14, 10, 18, 16, 17);
	move_by_test_add_assertion (data, 11, 15, 8,  7,  3,  4, 6,  5,  2,  1,  11, 0);

	move_by_test_add_assertion (data, 10, 20, 19, 9,  13, 12, 14, 10, 18, 16, 17);
	move_by_test_add_assertion (data, 10, 15, 8,  7,  3,  4, 6,  5,  2,  1,  11);
	move_by_test_add (data, FALSE);

	return e_test_server_utils_run ();
}
