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

#define N_TEST_CONTACTS 4

/****************************************************************
 *                     Modify/Setup the EBook                   *
 ****************************************************************/
static void
setup_book (EBookClient *book_client)
{
	gint    i;

	for (i = 0; i < N_TEST_CONTACTS; i++)
	{
		EContact *contact = e_contact_new ();
		gchar    *name = g_strdup_printf ("Contact #%d", i + 1);

		e_contact_set (contact, E_CONTACT_FULL_NAME, name);
		e_contact_set (contact, E_CONTACT_NICKNAME, name);

		/* verify the contact was added "successfully" (not thorough) */
		if (!add_contact_verify (book_client, contact))
			g_error ("Failed to add contact");

		g_free (name);
		g_object_unref (contact);
	}
}

/****************************************************************
 *                 Handle EClientBookView notifications         *
 ****************************************************************/
static void
print_contact (EContact *contact)
{
	g_print ("Contact: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_FULL_NAME));
	g_print ("UID: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_UID));
	g_print ("REV: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_REV));
	g_print ("\n");
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

	for (l = contacts; l; l = l->next) {
		EContact *contact = l->data;

		print_contact (contact);

		if (e_contact_get_const (contact, E_CONTACT_FULL_NAME) != NULL)
			g_error (
				"received contact name `%s' when only the uid and revision was requested",
				(gchar *) e_contact_get_const (contact, E_CONTACT_FULL_NAME));
	}

}

static void
objects_removed (EBookClientView *view,
                 const GSList *ids)
{
	const GSList *l;

	for (l = ids; l; l = l->next) {
		printf ("Removed contact: %s\n", (gchar *) l->data);
	}
}

static void
complete (EBookClientView *view,
          const GError *error,
          gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	finish_test (view, loop);
}

static void
setup_and_start_view (EBookClientView *view,
                      GMainLoop *loop)
{
	GError *error = NULL;
	GSList *field_list;

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), loop);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), loop);
	g_signal_connect (view, "complete", G_CALLBACK (complete), loop);

	field_list = g_slist_prepend (NULL, (gpointer) e_contact_field_name (E_CONTACT_UID));
	field_list = g_slist_prepend (field_list, (gpointer) e_contact_field_name (E_CONTACT_REV));

	e_book_client_view_set_fields_of_interest (view, field_list, &error);
	g_slist_free (field_list);

	if (error)
		g_error ("set fields of interest: %s", error->message);

	e_book_client_view_start (view, &error);
	if (error)
		g_error ("start view: %s", error->message);

}

static void
get_view_cb (GObject *source_object,
             GAsyncResult *result,
             gpointer user_data)
{
	EBookClientView *view;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), result, &view, &error))
		g_error ("get view finish: %s", error->message);

	setup_and_start_view (view, loop);
}

static void
test_revision_view_sync (ETestServerFixture *fixture,
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
test_revision_view_async (ETestServerFixture *fixture,
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
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBookClient/RevisionView/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_revision_view_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/RevisionView/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_revision_view_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
