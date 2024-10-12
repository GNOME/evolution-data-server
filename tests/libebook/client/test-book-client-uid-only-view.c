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

typedef struct {
	ETestServerClosure closure;
	gboolean uids_only;
} UIDOnlyClosure;

static UIDOnlyClosure book_closure_all_data_sync = { { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE }, FALSE };
static UIDOnlyClosure book_closure_all_data_async = { { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE },  FALSE };
static UIDOnlyClosure book_closure_uids_only_sync = { { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE }, TRUE };
static UIDOnlyClosure book_closure_uids_only_async = { { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE },  TRUE };

#define N_TEST_CONTACTS 4

/* If COMPARE_PERFORMANCE is set, only print a performance comparison, otherwise print contacts */
#define COMPARE_PERFORMANCE 0
#define BEEFY_VCARDS        1

#if COMPARE_PERFORMANCE
#  define SETUP_TIMER(timer)  GTimer *timer = g_timer_new ();
#  define START_TIMER(timer)  g_timer_start (timer);
#  define STOP_TIMER(timer)   g_timer_stop (timer);
#  define PRINT_TIMER(timer, activity) \
	printf ("%s finished in %02.6f seconds\n", activity, g_timer_elapsed (timer, NULL));
#else
#  define SETUP_TIMER(timer)
#  define START_TIMER(timer)
#  define STOP_TIMER(timer)
#  define PRINT_TIMER(timer, activity)
#endif

static gboolean uids_only = FALSE;

/****************************************************************
 *                     Modify/Setup the EBook                   *
 ****************************************************************/
static gboolean
setup_book (EBookClient *book_client)
{
	gint   i, j;

	for (i = 0; i < N_TEST_CONTACTS; i++)
	{
		EContact *contact = e_contact_new ();
		gchar    *name = g_strdup_printf ("Contact #%d", i + 1);
		gchar    *emails[5] = {
			g_strdup_printf ("contact%d@first.email.com", i),
			g_strdup_printf ("contact%d@second.email.com", i),
			g_strdup_printf ("contact%d@third.email.com", i),
			g_strdup_printf ("contact%d@fourth.email.com", i),
			NULL
		};

		e_contact_set (contact, E_CONTACT_FULL_NAME, name);
		e_contact_set (contact, E_CONTACT_NICKNAME, name);

		/* Fill some emails */
		for (j = E_CONTACT_EMAIL_1; j < (E_CONTACT_EMAIL_4 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_EMAIL_1]);

#if BEEFY_VCARDS
		/* Fill some other random stuff */
		for (j = E_CONTACT_IM_AIM_HOME_1; j < (E_CONTACT_IM_AIM_HOME_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_AIM_HOME_1]);
		for (j = E_CONTACT_IM_AIM_WORK_1; j < (E_CONTACT_IM_AIM_WORK_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_AIM_WORK_1]);
		for (j = E_CONTACT_IM_GROUPWISE_HOME_1; j < (E_CONTACT_IM_GROUPWISE_HOME_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_GROUPWISE_HOME_1]);
		for (j = E_CONTACT_IM_GROUPWISE_WORK_1; j < (E_CONTACT_IM_GROUPWISE_WORK_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_GROUPWISE_WORK_1]);
		for (j = E_CONTACT_IM_JABBER_HOME_1; j < (E_CONTACT_IM_JABBER_HOME_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_JABBER_HOME_1]);
		for (j = E_CONTACT_IM_JABBER_WORK_1; j < (E_CONTACT_IM_JABBER_WORK_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_JABBER_WORK_1]);
		for (j = E_CONTACT_IM_YAHOO_HOME_1; j < (E_CONTACT_IM_YAHOO_HOME_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_YAHOO_HOME_1]);
		for (j = E_CONTACT_IM_YAHOO_WORK_1; j < (E_CONTACT_IM_YAHOO_WORK_3 + 1); j++)
			e_contact_set (contact, j, emails[j - E_CONTACT_IM_YAHOO_WORK_1]);
#endif

		/* verify the contact was added "successfully" (not thorough) */
		if (!add_contact_verify (book_client, contact))
			g_error ("Failed to add contact");

		g_free (name);
		for (j = E_CONTACT_EMAIL_1; j < (E_CONTACT_EMAIL_4 + 1); j++)
			g_free (emails[j - E_CONTACT_EMAIL_1]);

		g_object_unref (contact);
	}

	return TRUE;
}

/****************************************************************
 *                 Handle EClientBookView notifications               *
 ****************************************************************/
#if !COMPARE_PERFORMANCE
static void
print_contact (EContact *contact)
{
	GList *emails, *e;

	g_print ("Contact: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_FULL_NAME));
	g_print ("UID: %s\n", (gchar *) e_contact_get_const (contact, E_CONTACT_UID));
	g_print ("Email addresses:\n");

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		g_print ("\t%s\n",  (gchar *) e->data);
	}
	g_list_foreach (emails, (GFunc) g_free, NULL);
	g_list_free (emails);

	g_print ("\n");
}
#endif

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

#if !COMPARE_PERFORMANCE
		print_contact (contact);
#endif

		if (uids_only && e_contact_get_const (contact, E_CONTACT_FULL_NAME) != NULL)
			g_error (
				"received contact name `%s' when only the uid was requested",
				(gchar *) e_contact_get_const (contact, E_CONTACT_FULL_NAME));
		else if (!uids_only && e_contact_get_const (contact, E_CONTACT_FULL_NAME) == NULL)
			g_error ("expected contact name missing");
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
	GSList  uid_field_list = { 0, };

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), loop);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), loop);
	g_signal_connect (view, "complete", G_CALLBACK (complete), loop);

	uid_field_list.data = (gpointer) e_contact_field_name (E_CONTACT_UID);

	if (uids_only)
		e_book_client_view_set_fields_of_interest (view, &uid_field_list, &error);
	else
		e_book_client_view_set_fields_of_interest (view, NULL, &error);

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

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), result, &view, &error)) {
		g_error ("get view finish: %s", error->message);
	}

	setup_and_start_view (view, loop);
}

static void
test_get_view_sync (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	EBookClientView *view;
	gchar *sexp;
	GError *error = NULL;
	UIDOnlyClosure *closure = (UIDOnlyClosure *) user_data;

	uids_only = closure->uids_only;

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
test_get_view_async (ETestServerFixture *fixture,
                     gconstpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	gchar *sexp;
	UIDOnlyClosure *closure = (UIDOnlyClosure *) user_data;

	uids_only = closure->uids_only;

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
		"/EBookClient/UidOnlyView/Sync/AllData",
		ETestServerFixture,
		&book_closure_all_data_sync,
		e_test_server_utils_setup,
		test_get_view_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/UidOnlyView/Sync/UidsOnly",
		ETestServerFixture,
		&book_closure_uids_only_sync,
		e_test_server_utils_setup,
		test_get_view_sync,
		e_test_server_utils_teardown);

	g_test_add (
		"/EBookClient/UidOnlyView/Async/AllData",
		ETestServerFixture,
		&book_closure_all_data_async,
		e_test_server_utils_setup,
		test_get_view_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/UidOnlyView/Async/UidsOnly",
		ETestServerFixture,
		&book_closure_uids_only_async,
		e_test_server_utils_setup,
		test_get_view_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
