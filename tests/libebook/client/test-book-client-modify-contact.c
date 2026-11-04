/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

#define EMAIL_ADD "foo@bar.com"

static void
verify_premodify_and_prepare_contact (EContact *contact)
{
	EVCardAttribute *attr;

	/* ensure there is no email address to begin with, then add one */
	g_assert_true (!e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL));
	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, EMAIL_ADD);
}

static void
verify_modify (EContact *contact)
{
	EVCardAttribute *attr;
	gchar *email_value;

	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL);
	g_assert_true (attr != NULL);
	g_assert_true (e_vcard_attribute_is_single_valued (attr));
	email_value = e_vcard_attribute_get_value (attr);
	g_assert_true (!g_strcmp0 (email_value, EMAIL_ADD));
	g_free (email_value);
}

static void
test_modify_contact_sync (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact, *book_contact;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "name-only", &contact))
		g_error ("Failed to add contact");

	verify_premodify_and_prepare_contact (contact);

	if (!e_book_client_modify_contact_sync (book_client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	if (!e_book_client_get_contact_sync (book_client, e_contact_get_const (contact, E_CONTACT_UID),
					     &book_contact, NULL, &error))
		g_error ("get contact sync: %s", error->message);

	verify_modify (book_contact);
	g_object_unref (book_contact);
	g_object_unref (contact);
}

typedef struct {
	EContact *contact;
	GMainLoop *loop;
} ModifyData;

static void
contact_ready_cb (GObject *source_object,
                  GAsyncResult *result,
                  gpointer user_data)
{
	EContact *contact;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	if (!e_book_client_get_contact_finish (E_BOOK_CLIENT (source_object), result, &contact, &error))
		g_error ("get contact finish: %s", error->message);

	verify_modify (contact);

	g_object_unref (contact);
	g_main_loop_quit (loop);
}

static void
contact_modified_cb (GObject *source_object,
                     GAsyncResult *result,
                     gpointer user_data)
{
	ModifyData *data = (ModifyData *) user_data;
	GError *error = NULL;

	if (!e_book_client_modify_contact_finish (E_BOOK_CLIENT (source_object), result, &error))
		g_error ("modify contact finish: %s", error->message);

	e_book_client_get_contact (
		E_BOOK_CLIENT (source_object),
		e_contact_get_const (data->contact, E_CONTACT_UID),
		NULL, contact_ready_cb, data->loop);
}

static void
test_modify_contact_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact;
	ModifyData data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "name-only", &contact))
		g_error ("Failed to add contact");

	verify_premodify_and_prepare_contact (contact);

	data.contact = contact;
	data.loop = fixture->loop;
	e_book_client_modify_contact (book_client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, contact_modified_cb, &data);

	g_main_loop_run (fixture->loop);

	g_object_unref (contact);
}

typedef struct {
	guint simple1;
	guint simple2;
	guint other;
} ModifiedCounts;

static void
objects_modified_cb (EBookClientView *view,
                     const GSList *contacts,
                     gpointer user_data)
{
	ModifiedCounts *counts = user_data;
	const GSList *link;

	for (link = contacts; link; link = g_slist_next (link)) {
		const gchar *uid = e_contact_get_const (link->data, E_CONTACT_UID);

		if (!g_strcmp0 (uid, "simple-1"))
			counts->simple1++;
		else if (!g_strcmp0 (uid, "simple-2"))
			counts->simple2++;
		else
			counts->other++;
	}
}

static void
view_complete_cb (EBookClientView *view,
                  const GError *error,
                  gpointer user_data)
{
	g_assert_no_error (error);
	g_main_loop_quit (user_data);
}

static void
test_modify_contacts_notify_view (ETestServerFixture *fixture,
                                  gconstpointer user_data)
{
	EBookClient *book_client;
	EBookClientView *view;
	EBookQuery *query;
	EContact *contact1, *contact2;
	ModifiedCounts counts = { 0, 0, 0 };
	GSList *contacts;
	gchar *sexp;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact1) ||
	    !add_contact_from_test_case_verify (book_client, "simple-2", &contact2))
		g_error ("Failed to add contacts");

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	if (!e_book_client_get_view_sync (book_client, sexp, &view, NULL, &error))
		g_error ("get book view sync: %s", error->message);
	g_free (sexp);

	g_signal_connect (view, "objects-modified", G_CALLBACK (objects_modified_cb), &counts);
	g_signal_connect (view, "complete", G_CALLBACK (view_complete_cb), fixture->loop);

	e_book_client_view_start (view, &error);
	if (error)
		g_error ("start view: %s", error->message);

	/* wait for the initial population of the view */
	g_main_loop_run (fixture->loop);

	e_contact_set (contact1, E_CONTACT_FULL_NAME, "Foo Bar Modified");
	e_contact_set (contact2, E_CONTACT_FULL_NAME, "Baz Qux Modified");
	contacts = g_slist_append (g_slist_append (NULL, contact1), contact2);

	if (!e_book_client_modify_contacts_sync (book_client, contacts, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("modify contacts sync: %s", error->message);

	/* the backend completes the view after notifying about the changes */
	g_main_loop_run (fixture->loop);

	g_assert_cmpuint (counts.simple1, ==, 1);
	g_assert_cmpuint (counts.simple2, ==, 1);
	g_assert_cmpuint (counts.other, ==, 0);

	e_book_client_view_stop (view, NULL);
	g_object_unref (view);
	g_slist_free (contacts);
	g_object_unref (contact1);
	g_object_unref (contact2);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/ModifyContact/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_modify_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/ModifyContact/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_modify_contact_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/ModifyContact/NotifyView",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_modify_contacts_notify_view,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
