/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static void
print_all_uids_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	EBookClient *book_client;
	GSList *uids = NULL, *u;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contacts_uids_finish (book_client, result, &uids, &error))
		g_error ("get contacts uids finish: %s", error->message);

	for (u = uids; u; u = u->next) {
		const gchar *uid = u->data;

		g_print ("   uid:'%s'\n", uid);
	}

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);

	g_main_loop_quit (loop);
}

static void
print_all_emails_cb (GObject *source_object,
                     GAsyncResult *result,
                     gpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	gchar *sexp;
	GSList *contacts = NULL, *c;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contacts_finish (book_client, result, &contacts, &error))
		g_error ("get contacts finish: %s", error->message);

	for (c = contacts; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		print_email (contact);
	}

	g_slist_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_slist_free (contacts);

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_contacts_uids (book_client, sexp, NULL, print_all_uids_cb, loop);

	g_free (sexp);
}

static void
print_all_emails (EBookClient *book_client,
                  GMainLoop *loop)
{
	EBookQuery *query;
	gchar *sexp;

	query = e_book_query_field_exists (E_CONTACT_FULL_NAME);
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	e_book_client_get_contacts (book_client, sexp, NULL, print_all_emails_cb, loop);

	g_free (sexp);
}

static void
print_email_cb (GObject *source_object,
                GAsyncResult *result,
                gpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);
	g_return_if_fail (book_client != NULL);

	if (!e_book_client_get_contact_finish (book_client, result, &contact, &error)) {
		g_error ("get contact finish: %s", error->message);
	} else {
		print_email (contact);
		g_object_unref (contact);
	}

	printf ("printing all contacts\n");
	print_all_emails (book_client, loop);
}

static void
print_one_email (EBookClient *book_client,
                 GSList *uids,
                 GMainLoop *loop)
{
	const gchar *uid = uids->data;

	e_book_client_get_contact (book_client, uid, NULL, print_email_cb, loop);

	e_util_free_string_slist (uids);
}

static void
contacts_added_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	GSList *uids = NULL, *l;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);

	if (!e_book_client_add_contacts_finish (book_client, result, &uids, &error))
		g_error ("client open finish: %s", error->message);

	printf ("Added contacts uids are:\n");
	for (l = uids; l; l = l->next) {
		const gchar *uid = l->data;

		printf ("\t%s\n", uid);
	}
	printf ("\n");

	printf ("printing one contact\n");
	print_one_email (book_client, uids, loop);
}

static void
add_contacts (EBookClient *book_client,
              GMainLoop *loop)
{
	GSList *contacts = NULL;
	EContact *contact;
	gchar *vcard;

	vcard = new_vcard_from_test_case ("custom-1");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-2");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-3");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-4");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	vcard = new_vcard_from_test_case ("custom-5");
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	contacts = g_slist_prepend (contacts, contact);

	e_book_client_add_contacts (book_client, contacts, NULL, contacts_added_cb, loop);

	e_util_free_object_slist (contacts);
}

static void
test_async (ETestServerFixture *fixture,
            gconstpointer user_data)
{
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	printf ("Adding contacts\n");
	add_contacts (book_client, fixture->loop);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add (
		"/EBookClient/AsyncTest", ETestServerFixture, &book_closure,
		e_test_server_utils_setup, test_async, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
