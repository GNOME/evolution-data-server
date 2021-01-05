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

#include <libebook/libebook.h>

#include "ebook-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
test_remove_contact_by_id_async (ETestServerFixture *fixture,
                                 gconstpointer user_data)
{
	EBook *book;
	gchar *uid;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);

	ebook_test_utils_book_async_remove_contact_by_id (
		book, uid, ebook_test_utils_callback_quit, fixture->loop);

	g_main_loop_run (fixture->loop);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	ebook_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBook/RemoveContactById/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_remove_contact_by_id_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
