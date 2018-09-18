/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "data-test-utils.h"

struct {
	gboolean empty_book;
	gboolean store_vcards;
	const gchar *path;
} params[] = {
	{ FALSE, TRUE,  "/EbSdbCursor/DefaultSummary/StoreVCards" },
	{ FALSE, FALSE, "/EbSdbCursor/DefaultSummary/NoVCards" },
	{ FALSE, TRUE,  "/EbSdbCursor/EmptySummary/StoreVCards" },
	{ FALSE, FALSE, "/EbSdbCursor/EmptySummary/NoVCards" }
};

gint
main (gint argc,
      gchar **argv)
{
	StepData *data;
	gint i;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	for (i = 0; i < G_N_ELEMENTS (params); i++) {

		data = step_test_new (
			params[i].path, "/en_US/Move/Forward", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, 5, 11, 1, 2, 5, 6);
		step_test_add_assertion (data, 6, 4, 3, 7, 8, 15, 17);
		step_test_add (data, FALSE);

		data = step_test_new (
			params[i].path, "/en_US/Move/ForwardOnNameless", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, 1, 11);
		step_test_add_assertion (data, 3, 1, 2, 5);
		step_test_add (data, FALSE);

		data = step_test_new (
			params[i].path, "/en_US/Move/Backwards", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, -5, 20, 19, 9, 13, 12);
		step_test_add_assertion (data, -8, 14, 10, 18, 16, 17, 15, 8, 7);
		step_test_add (data, FALSE);

		data = step_test_new (
			params[i].path, "/en_US/Filtered/Move/Forward", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, 5, 11, 1, 2, 5, 3);
		step_test_add_assertion (data, 8, 8, 17, 16, 18, 10, 14, 12, 9);
		step_test_add (data, TRUE);

		data = step_test_new (
			params[i].path, "/en_US/Filtered/Move/Backwards", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
		step_test_add_assertion (data, -8, 16, 17, 8, 3, 5, 2, 1, 11);
		step_test_add (data, TRUE);

		data = step_test_new_full (
			params[i].path, "/en_US/Move/Descending/Forward", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book,
			E_BOOK_CURSOR_SORT_DESCENDING);
		step_test_add_assertion (data, 5, 20, 19, 9,  13, 12);
		step_test_add_assertion (data, 5, 14, 10, 18, 16, 17);
		step_test_add_assertion (data, 5, 15, 8,  7,  3,  4);
		step_test_add_assertion (data, 5, 6,  5,  2,  1,  11);
		step_test_add (data, FALSE);

		data = step_test_new_full (
			params[i].path, "/en_US/Move/Descending/Backwards", "en_US.UTF-8",
			params[i].store_vcards, params[i].empty_book,
			E_BOOK_CURSOR_SORT_DESCENDING);
		step_test_add_assertion (data, -10, 11, 1,  2,  5,  6,  4,  3,  7,  8, 15);
		step_test_add_assertion (data, -10, 17, 16, 18, 10, 14, 12, 13, 9, 19, 20);
		step_test_add (data, FALSE);
	}

	return e_test_server_utils_run_full (0);
}
