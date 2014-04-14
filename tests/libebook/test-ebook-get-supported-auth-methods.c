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
#include <libebook/libebook.h>

#include "ebook-test-utils.h"
#include "e-test-server-utils.h"

/* NOTE: This is an old test case testing unsupported old code paths.
 *
 * All this test case really does is ensure the callbacks are indeed
 * called (with empty lists).
 */

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
list_member_print_and_free (gchar *member,
                            gpointer user_data)
{
	test_print ("    %s\n", member);
	g_free (member);
}

static void
get_supported_auth_methods_cb (EBookTestClosure *closure)
{
	/* XXX: assuming an empty list is valid, we'll just print out anything
	 * we do get */
	if (closure->list) {
		EIterator *iter;
		const gchar *field;

		test_print ("supported auth methods:\n");
		iter = e_list_get_iterator (closure->list);
		while ((field = e_iterator_get (iter))) {
			test_print ("    %s\n", field);
			e_iterator_next (iter);
		}
		test_print ("----------------\n");
	}

	g_object_unref (closure->list);

	g_main_loop_quit ((GMainLoop *) (closure->user_data));
}

static void
test_get_supported_auth_methods_sync (ETestServerFixture *fixture,
                                      gconstpointer user_data)
{
	EBook *book;
	GList *auth_methods;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);

	auth_methods = ebook_test_utils_book_get_supported_auth_methods (book);

	test_print ("successfully retrieved supported auth methods:\n");
	g_list_foreach (auth_methods, (GFunc) list_member_print_and_free, NULL);
	test_print ("----------------\n");
	g_list_free (auth_methods);
}

static gboolean
main_loop_fail_timeout (gpointer unused)
{
	g_error ("Failed to get supported auth methods, async call timed out");
	return FALSE;
}

static gboolean
get_supported_auth_methods_async_in_idle (ETestServerFixture *fixture)
{
	EBook *book;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);

	ebook_test_utils_book_async_get_supported_auth_methods (
		book, (GSourceFunc) get_supported_auth_methods_cb, fixture->loop);

	return FALSE;
}

static void
test_get_supported_auth_methods_async (ETestServerFixture *fixture,
                                       gconstpointer user_data)
{
	g_idle_add ((GSourceFunc) get_supported_auth_methods_async_in_idle, fixture);
	g_timeout_add_seconds (5, (GSourceFunc) main_loop_fail_timeout, NULL);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBook/GetSupportedAuthMethods/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_supported_auth_methods_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBook/GetSupportedAuthMethods/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_supported_auth_methods_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
