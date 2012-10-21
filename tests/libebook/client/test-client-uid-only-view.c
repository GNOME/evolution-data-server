/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"

#define N_TEST_CONTACTS 4

/* If COMPARE_PERFORMANCE is set, only print a performance comparison, otherwise print contacts */
#define COMPARE_PERFORMANCE 0
#define BEEFY_VCARDS        1

#if COMPARE_PERFORMANCE
#  define SETUP_TIMER(timer)  GTimer *timer = g_timer_new ();
#  define START_TIMER(timer)  g_timer_start (timer);
#  define STOP_TIMER(timer)   g_timer_stop (timer);
#  define PRINT_TIMER(timer, activity)  \
	printf ("%s finished in %02.6f seconds\n", activity, g_timer_elapsed (timer, NULL));
#else
#  define SETUP_TIMER(timer)
#  define START_TIMER(timer)
#  define STOP_TIMER(timer)
#  define PRINT_TIMER(timer, activity)
#endif

static gboolean loading_view = FALSE;
static gboolean uids_only    = FALSE;

/****************************************************************
 *                     Modify/Setup the EBook                   *
 ****************************************************************/
static void
add_contact (EBookClient *client)
{
	EContact *contact = e_contact_new ();

	e_contact_set (contact, E_CONTACT_FULL_NAME, "Micheal Jackson");

	if (!add_contact_verify (client, contact))
		stop_main_loop (1);

	g_object_unref (contact);
}

static gboolean
setup_book (EBookClient **book_out)
{
	GError *error = NULL;
	gint   i, j;

	g_return_val_if_fail (book_out != NULL, FALSE);

	*book_out = new_temp_client (NULL);
	g_return_val_if_fail (*book_out != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (*book_out), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (*book_out);
		return FALSE;
	}

	for (i = 0; i < N_TEST_CONTACTS; i++)
	{
		EContact *contact = e_contact_new ();
		gchar    *name      = g_strdup_printf ("Contact #%d", i + 1);
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
		if (!add_contact_verify (*book_out, contact))
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
finish_test (EBookClientView *view)
{
	e_book_client_view_stop (view, NULL);
	g_object_unref (view);

	stop_main_loop (0);
}

static void
objects_added (EBookClientView *view,
               const GSList *contacts)
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

	if (!loading_view)
		finish_test (view);

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
          const GError *error)
{
	/* Now add a contact and assert that we received notification */
	loading_view = FALSE;
	add_contact (e_book_client_view_get_client (view));
}

static void
setup_and_start_view (EBookClientView *view)
{
	GError *error = NULL;
	GSList  uid_field_list = { 0, };

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), NULL);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), NULL);
	g_signal_connect (view, "complete", G_CALLBACK (complete), NULL);

	uid_field_list.data = (gpointer) e_contact_field_name (E_CONTACT_UID);

	if (uids_only)
		e_book_client_view_set_fields_of_interest (view, &uid_field_list, &error);
	else
		e_book_client_view_set_fields_of_interest (view, NULL, &error);

	if (error)
		report_error ("set fields of interest", &error);

	loading_view = TRUE;

	e_book_client_view_start (view, &error);
	if (error)
		report_error ("start view", &error);

}

static void
get_view_cb (GObject *source_object,
             GAsyncResult *result,
             gpointer user_data)
{
	EBookClientView *view;
	GError *error = NULL;

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), result, &view, &error)) {
		report_error ("get view finish", &error);
		stop_main_loop (1);

		return;
	}

	setup_and_start_view (view);
}

static gpointer
call_get_view (gpointer user_data)
{
	EBookQuery *query;
	EBookClient *book_client = user_data;
	gchar *sexp;

	g_return_val_if_fail (book_client != NULL, NULL);
	g_return_val_if_fail (E_IS_BOOK_CLIENT (book_client), NULL);

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_view (book_client, sexp, NULL, get_view_cb, NULL);

	g_free (sexp);

	return NULL;
}

gint
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	EBookQuery *query;
	EBookClientView *view;
	gchar *sexp;
	GError *error = NULL;
	SETUP_TIMER (timer);

	main_initialize ();

	/*
	 * Sync version all data
	 */
	uids_only = FALSE;

	if (!setup_book (&book_client))
		return 1;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	if (!e_book_client_get_view_sync (book_client, sexp, &view, NULL, &error)) {
		report_error ("get book view sync", &error);
		g_free (sexp);
		g_object_unref (book_client);

		return 1;
	}

	g_free (sexp);

	START_TIMER (timer);
	setup_and_start_view (view);
	start_main_loop (NULL, NULL);
	STOP_TIMER (timer);
	PRINT_TIMER (timer, "Loading all data from book view synchronously");

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}
	g_object_unref (book_client);

	/*
	 * Sync version uids only
	 */
	uids_only = TRUE;

	if (!setup_book (&book_client))
		return 1;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	if (!e_book_client_get_view_sync (book_client, sexp, &view, NULL, &error)) {
		report_error ("get book view sync", &error);
		g_free (sexp);
		g_object_unref (book_client);

		return 1;
	}

	g_free (sexp);

	START_TIMER (timer);
	setup_and_start_view (view);
	start_main_loop (NULL, NULL);
	STOP_TIMER (timer);
	PRINT_TIMER (timer, "Loading uids only from book view synchronously");

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}
	g_object_unref (book_client);

	/*
	 * Async version all data
	 */
	uids_only = FALSE;

	if (!setup_book (&book_client))
		return 1;

	START_TIMER (timer);
	start_in_idle_with_main_loop (call_get_view, book_client);
	STOP_TIMER (timer);
	PRINT_TIMER (timer, "Loading all data from book view asynchronously");

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}

	g_object_unref (book_client);

	/*
	 * Async version uids only
	 */
	uids_only = TRUE;

	if (!setup_book (&book_client))
		return 1;

	START_TIMER (timer);
	start_in_idle_with_main_loop (call_get_view, book_client);
	STOP_TIMER (timer);
	PRINT_TIMER (timer, "Loading uids only from book view asynchronously");

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}
