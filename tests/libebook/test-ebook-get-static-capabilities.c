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
#include <libebook/libebook.h>

#include "ebook-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
test_get_static_capabilities_sync (ETestServerFixture *fixture,
                                   gconstpointer user_data)
{
	EBook *book;
	const gchar *caps;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);

	caps = ebook_test_utils_book_get_static_capabilities (book);
	test_print ("successfully retrieved static capabilities: '%s'\n", caps);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	ebook_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBook/GetStaticCapabilities/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_static_capabilities_sync,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
