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

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };
static ETestServerClosure book_closure_direct_sync = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_async = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

#define NOTIFICATION_WAIT 2000

static gboolean loading_view;

static void
add_contact (EBookClient *book_client)
{
	g_return_if_fail (add_contact_from_test_case_verify (book_client, "name-only", NULL));
}

static void
setup_book (EBookClient *book_client)
{
	if (!add_contact_from_test_case_verify (book_client, "simple-1", NULL) ||
	    !add_contact_from_test_case_verify (book_client, "simple-2", NULL))
		g_error ("Failed to add contacts");
}

static void
finish_test (EBookClientView *view,
             GMainLoop *loop)
{
	e_book_client_view_stop (view, NULL);
	g_object_unref (view);

	g_main_loop_quit (loop);
}

static void
objects_added (EBookClientView *view,
               const GSList *contacts,
               gpointer user_data)
{
	const GSList *l;
	GMainLoop *loop = (GMainLoop *) user_data;

	/* We quit the mainloop and the test succeeds if we get the notification
	 * for the contact we add after loading the view completes */
	for (l = contacts; l; l = l->next) {
		print_email (l->data);
	}

	if (loading_view)
		g_error ("Expected no contact additions while loading the view");
	else {
		finish_test (view, loop);
	}

}

static void
objects_removed (EBookClientView *view,
                 const GSList *ids)
{
	const GSList *l;

	if (loading_view)
		g_error ("Expected no contact removals while loading the view");

	for (l = ids; l; l = l->next) {
		printf ("   Removed contact: %s\n", (gchar *) l->data);
	}
}

static void
complete (EBookClientView *view,
          const GError *error)
{
	EBookClient *client;

	client = e_book_client_view_ref_client (view);

	/* Now add a contact and assert that we received notification */
	loading_view = FALSE;
	add_contact (client);

	g_object_unref (client);
}

static void
setup_and_start_view (EBookClientView *view,
                      GMainLoop *loop)
{
	GError *error = NULL;

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), loop);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), loop);
	g_signal_connect (view, "complete", G_CALLBACK (complete), loop);

	e_book_client_view_set_fields_of_interest (view, NULL, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);

	/* Set flags to 0, i.e. unflag E_BOOK_VIEW_NOTIFY_INITIAL */
	e_book_client_view_set_flags (view, 0, &error);
	if (error)
		g_error ("set view flags: %s", error->message);
	loading_view = TRUE;

	e_book_client_view_start (view, &error);
	if (error)
		g_error ("start view: %s", error->message);

}

static void
get_view_cb (GObject *source_object,
             GAsyncResult *result,
             gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	EBookClientView *view;
	GError *error = NULL;

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), result, &view, &error)) {
		g_error ("get view finish: %s", error->message);
	}

	setup_and_start_view (view, loop);
}

static void
test_suppress_notifications_sync (ETestServerFixture *fixture,
                                  gconstpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	EBookClientView *view;
	gchar *sexp;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	setup_book (book_client);

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	if (!e_book_client_get_view_sync (book_client, sexp, &view, NULL, &error)) {
		g_error ("get book view sync: %s", error->message);
		g_free (sexp);
		g_object_unref (book_client);
	}

	g_free (sexp);

	setup_and_start_view (view, fixture->loop);
	g_main_loop_run (fixture->loop);
}

static void
test_suppress_notifications_async (ETestServerFixture *fixture,
                                   gconstpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	gchar *sexp;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	setup_book (book_client);
	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_view (book_client, sexp, NULL, get_view_cb, fixture->loop);

	g_free (sexp);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/SuppressNotifications/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_suppress_notifications_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/SuppressNotifications/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_suppress_notifications_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/SuppressNotifications/Sync",
		ETestServerFixture,
		&book_closure_direct_sync,
		e_test_server_utils_setup,
		test_suppress_notifications_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/SuppressNotifications/Async",
		ETestServerFixture,
		&book_closure_direct_async,
		e_test_server_utils_setup,
		test_suppress_notifications_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
