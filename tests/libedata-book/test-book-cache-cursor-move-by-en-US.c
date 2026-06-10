/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "test-book-cache-utils.h"

struct {
	gboolean empty_book;
	const gchar *path;
} params[] = {
	{ FALSE, "/EBookCacheCursor/DefaultSummary" },
	{ TRUE,  "/EBookCacheCursor/EmptySummary" }
};

gint
main (gint argc,
      gchar **argv)
{
	TCUStepData *data;
	gint ii;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	for (ii = 0; ii < G_N_ELEMENTS (params); ii++) {

		data = tcu_step_test_new (
			params[ii].path, "/en_US/Move/Forward", "en_US.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
		tcu_step_test_add_assertion (data, 6, 4, 3, 7, 8, 15, 17);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/en_US/Move/ForwardOnNameless", "en_US.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 1, 11);
		tcu_step_test_add_assertion (data, 3, 1, 2, 5);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/en_US/Move/Backwards", "en_US.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, -5, 20, 19, 9, 13, 12);
		tcu_step_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 8, 7);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/en_US/Filtered/Move/Forward", "en_US.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 1, 2, 5, 3);
		tcu_step_test_add_assertion (data, 8, 8, 17, 16, 18, 10, 14, 12, 9);
		tcu_step_test_add (data, TRUE);

		data = tcu_step_test_new (
			params[ii].path, "/en_US/Filtered/Move/Backwards", "en_US.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
		tcu_step_test_add_assertion (data, -8, 16, 17, 8, 3, 5, 2, 1, 11);
		tcu_step_test_add (data, TRUE);

		data = tcu_step_test_new_full (
			params[ii].path, "/en_US/Move/Descending/Forward", "en_US.UTF-8",
			params[ii].empty_book,
			E_BOOK_CURSOR_SORT_DESCENDING);
		tcu_step_test_add_assertion (data, 5, 20, 19, 9,  13, 12);
		tcu_step_test_add_assertion (data, 5, 14, 10, 18, 16, 17);
		tcu_step_test_add_assertion (data, 5, 15, 8,  7,  3,  4);
		tcu_step_test_add_assertion (data, 5, 6,  5,  2,  1,  11);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new_full (
			params[ii].path, "/en_US/Move/Descending/Backwards", "en_US.UTF-8",
			params[ii].empty_book,
			E_BOOK_CURSOR_SORT_DESCENDING);
		tcu_step_test_add_assertion (data, -10, 11, 1,  2,  5,  6,  4,  3,  7,  8, 15);
		tcu_step_test_add_assertion (data, -10, 17, 16, 18, 10, 14, 12, 13, 9, 19, 20);
		tcu_step_test_add (data, FALSE);
	}

	return e_test_server_utils_run_full (argc, argv, 0);
}
