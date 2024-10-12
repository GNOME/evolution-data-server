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
			params[ii].path, "/ChangeLocale/POSIX/en_US", "POSIX",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 2,  6,  3,  8);
		tcu_step_test_add_assertion (data, 5, 1,  5,  4,  7,  15);
		tcu_step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
		tcu_step_test_add_assertion (data, 5, 12, 13, 9,  19, 20);

		tcu_step_test_change_locale (data, "en_US.UTF-8", 0);
		tcu_step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
		tcu_step_test_add_assertion (data, 5, 4,  3,  7,  8,  15);
		tcu_step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
		tcu_step_test_add_assertion (data, 5, 12, 13, 9,  19, 20);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/ChangeLocale/en_US/fr_CA", "en_US.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
		tcu_step_test_add_assertion (data, 5, 4,  3,  7,  8,  15);
		tcu_step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
		tcu_step_test_add_assertion (data, 5, 12, 13, 9,  19, 20);

		tcu_step_test_change_locale (data, "fr_CA.UTF-8", -1);
		tcu_step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
		tcu_step_test_add_assertion (data, 5, 4,  3,  7,  8,  15);
		tcu_step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
		tcu_step_test_add_assertion (data, 5, 13, 12, 9,  19, 20);
		tcu_step_test_add (data, FALSE);

		data = tcu_step_test_new (
			params[ii].path, "/ChangeLocale/fr_CA/de_DE", "fr_CA.UTF-8",
			params[ii].empty_book);
		tcu_step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
		tcu_step_test_add_assertion (data, 5, 4,  3,  7,  8,  15);
		tcu_step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
		tcu_step_test_add_assertion (data, 5, 13, 12, 9,  19, 20);

		/* Depending on the libphonenumber, when changing from fr_CA to de_DE, two numbers change:
		 *
		 * sorted-5:
		 *    049-2459-4393 is now parsed with the national number as 4924594393
		 *
		 * sorted-4:
		 *    12 245999 is now parsed with national number 12245999 instead of 2245999
		 *
		 * or only one. Skip this check, because a lack of a way to get the libphonenumber version.
		 */
		tcu_step_test_change_locale (data, "de_DE.UTF-8", -1);
		tcu_step_test_add_assertion (data, 5, 11, 1,  2,  5,  6);
		tcu_step_test_add_assertion (data, 5, 7,  8,  4,  3,  15);
		tcu_step_test_add_assertion (data, 5, 17, 16, 18, 10, 14);
		tcu_step_test_add_assertion (data, 5, 12, 13, 9,  20, 19);
		tcu_step_test_add (data, FALSE);
	}

	/* On this case, we want to delete the work directory and start fresh */
	return e_test_server_utils_run_full (argc, argv, 0);
}
