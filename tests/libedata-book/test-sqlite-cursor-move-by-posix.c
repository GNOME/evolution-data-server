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
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	data_test_utils_read_args (argc, argv);

	for (i = 0; i < G_N_ELEMENTS (params); i++) {

		data = step_test_new (
			params[i].path, "/POSIX/Move/Forward", "POSIX",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, 5, 11, 2, 6, 3, 8);
		step_test_add_assertion (data, 6, 1,  5,  4,  7,  15, 17);
		step_test_add (data, FALSE);

		data = step_test_new (
			params[i].path, "/POSIX/Move/ForwardOnNameless", "POSIX",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, 1, 11);
		step_test_add_assertion (data, 3, 2, 6, 3);
		step_test_add (data, FALSE);

		data = step_test_new (
			params[i].path, "/POSIX/Move/Backwards", "POSIX",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, -5, 20, 19, 9, 13, 12);
		step_test_add_assertion (data, -12, 14, 10, 18, 16, 17, 15, 7, 4, 5, 1, 8, 3);
		step_test_add (data, FALSE);

		data = step_test_new (
			params[i].path, "/POSIX/Filtered/Move/Forward", "POSIX",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, 5, 11, 2, 3, 8, 1);
		step_test_add_assertion (data, 8, 5, 17, 16, 18, 10, 14, 12, 9);
		step_test_add (data, TRUE);

		data = step_test_new (
			params[i].path, "/POSIX/Filtered/Move/Backwards", "POSIX",
			params[i].store_vcards, params[i].empty_book);
		step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
		step_test_add_assertion (data, -8, 16, 17, 5, 1, 8, 3, 2, 11);
		step_test_add (data, TRUE);
	}

	return e_test_server_utils_run_full (argc, argv, 0);
}
