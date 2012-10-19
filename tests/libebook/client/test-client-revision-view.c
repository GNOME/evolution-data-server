/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"

#define N_TEST_CONTACTS 4

static gboolean loading_view = FALSE;

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
	gint    i;

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

		e_contact_set (contact, E_CONTACT_FULL_NAME, name);
		e_contact_set (contact, E_CONTACT_NICKNAME, name);

		/* verify the contact was added "successfully" (not thorough) */
		if (!add_contact_verify (*book_out, contact))
			g_error ("Failed to add contact");

		g_free (name);
		g_object_unref (contact);
	}

	return TRUE;
}

/****************************************************************
 *                 Handle EClientBookView notifications               *
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

		print_contact (contact);

		if (e_contact_get_const (contact, E_CONTACT_FULL_NAME) != NULL)
			g_error (
				"received contact name `%s' when only the uid and revision was requested",
				(gchar *) e_contact_get_const (contact, E_CONTACT_FULL_NAME));
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
	GSList *field_list;

	g_signal_connect (view, "objects-added", G_CALLBACK (objects_added), NULL);
	g_signal_connect (view, "objects-removed", G_CALLBACK (objects_removed), NULL);
	g_signal_connect (view, "complete", G_CALLBACK (complete), NULL);

	field_list = g_slist_prepend (NULL, (gpointer) e_contact_field_name (E_CONTACT_UID));
	field_list = g_slist_prepend (field_list, (gpointer) e_contact_field_name (E_CONTACT_REV));

	e_book_client_view_set_fields_of_interest (view, field_list, &error);
	g_slist_free (field_list);

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

	main_initialize ();

	/*
	 * Sync version
	 */
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

	setup_and_start_view (view);
	start_main_loop (NULL, NULL);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}
	g_object_unref (book_client);

	/*
	 * Async version uids only
	 */
	if (!setup_book (&book_client))
		return 1;

	start_in_idle_with_main_loop (call_get_view, book_client);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);

		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}
