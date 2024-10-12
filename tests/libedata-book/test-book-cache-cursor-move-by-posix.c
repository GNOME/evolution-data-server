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

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	for (ii = 0; ii < G_N_ELEMENTS (params); ii++) {

		data = tcu_step_test_new (
			params[ii].path, "/POSIX/Move/Forward", "POSIX",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 2, 6, 3, 8);
		tcu_step_test_add_assertion (data, 6, 1,  5,  4,  7,  15, 17);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/POSIX/Move/ForwardOnNameless", "POSIX",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 1, 11);
		tcu_step_test_add_assertion (data, 3, 2, 6, 3);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/POSIX/Move/Backwards", "POSIX",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, -5, 20, 19, 9, 13, 12);
		tcu_step_test_add_assertion (data, -12, 14, 10, 18, 16, 17, 15, 7, 4, 5, 1, 8, 3);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/POSIX/Filtered/Move/Forward", "POSIX",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 2, 3, 8, 1);
		tcu_step_test_add_assertion (data, 8, 5, 17, 16, 18, 10, 14, 12, 9);
		tcu_step_test_add (data, TRUE);

		data = tcu_step_test_new (
			params[ii].path, "/POSIX/Filtered/Move/Backwards", "POSIX",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, -5, 9, 12, 14, 10, 18);
		tcu_step_test_add_assertion (data, -8, 16, 17, 5, 1, 8, 3, 2, 11);
		tcu_step_test_add (data, TRUE);
	}

	return e_test_server_utils_run_full (argc, argv, 0);
}
