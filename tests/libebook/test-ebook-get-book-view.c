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

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
print_contact (EContact *contact)
{
	GList *emails, *e;

	test_print ("Contact: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_FULL_NAME));
	test_print ("UID: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_UID));
	test_print ("Email addresses:\n");

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		test_print ("\t%s\n",  (gchar *) e->data);
	}
	g_list_foreach (emails, (GFunc) g_free, NULL);
	g_list_free (emails);

	test_print ("\n");
}

static void
contacts_added (EBookView *book_view,
                const GList *contacts)
{
	GList *l;

	for (l = (GList *) contacts; l; l = l->next) {
		print_contact (l->data);
	}
}

static void
contacts_removed (EBookView *book_view,
                  const GList *ids)
{
	GList *l;

	for (l = (GList *) ids; l; l = l->next) {
		test_print ("Removed contact: %s\n", (gchar *) l->data);
	}
}

static void
view_complete (EBookView *book_view,
               EBookViewStatus status,
               const gchar *error_msg,
               GMainLoop *loop)
{
	e_book_view_stop (book_view);
	g_object_unref (book_view);
	g_main_loop_quit (loop);
}

static void
setup_and_start_view (EBookView *view,
                      GMainLoop *loop)
{
	g_signal_connect (view, "contacts_added", G_CALLBACK (contacts_added), NULL);
	g_signal_connect (view, "contacts_removed", G_CALLBACK (contacts_removed), NULL);
	g_signal_connect (view, "view_complete", G_CALLBACK (view_complete), loop);

	e_book_view_start (view);
}

static void
get_book_view_cb (EBookTestClosure *closure)
{
	GMainLoop *loop = closure->user_data;
	g_assert_true (closure->view);

	setup_and_start_view (closure->view, loop);
}

static void
setup_book (EBook *book)
{
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-2", NULL);
	ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", NULL);
}

static void
test_get_book_view_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	EBook *book;
	EBookQuery *query;
	EBookView *view;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);
	setup_book (book);

	query = e_book_query_any_field_contains ("");
	ebook_test_utils_book_get_book_view (book, query, &view);
	setup_and_start_view (view, fixture->loop);

	test_print ("successfully set up the book view\n");

	g_main_loop_run (fixture->loop);

	e_book_query_unref (query);
}

static gboolean
main_loop_fail_timeout (gpointer unused)
{
	g_error ("Failed to get book view, async call timed out");
	return FALSE;
}

static void
test_get_book_view_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBook *book;
	EBookQuery *query;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);
	setup_book (book);

	query = e_book_query_any_field_contains ("");

	ebook_test_utils_book_async_get_book_view (
		book, query,
			(GSourceFunc) get_book_view_cb, fixture->loop);

	g_timeout_add_seconds (5, (GSourceFunc) main_loop_fail_timeout, NULL);
	g_main_loop_run (fixture->loop);
	e_book_query_unref (query);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	ebook_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBook/GetBookView/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_book_view_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBook/GetBookView/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_book_view_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
