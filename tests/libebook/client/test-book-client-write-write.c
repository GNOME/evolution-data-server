/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <stdlib.h>
#include <locale.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static void setup_custom_book (ESource            *scratch,
			       ETestServerClosure *closure);

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, setup_custom_book, 0 };

static void
setup_custom_book (ESource *scratch,
                   ETestServerClosure *closure)
{
	ESourceRevisionGuards *guards;

	g_type_ensure (E_TYPE_SOURCE_REVISION_GUARDS);
	guards = e_source_get_extension (scratch, E_SOURCE_EXTENSION_REVISION_GUARDS);
	e_source_revision_guards_set_enabled (guards, TRUE);
}

typedef struct {
	EContactField field;
	const gchar  *value;
} TestData;

typedef struct {
	ESourceRegistry *registry;
	GThread       *thread;
	const gchar   *book_uid;
	const gchar   *contact_uid;
	EContactField  field;
	const gchar   *value;
	EBookClient   *client;
	GMainLoop     *loop;
} ThreadData;

/* Special attention needed for this array:
 *
 * Some contact fields cannot be used together, for instance
 * E_CONTACT_PHONE_OTHER will conflict with E_CONTACT_PHONE_HOME and others,
 * E_CONTACT_EMAIL_[1-4] can get mixed up if not set in proper sequence.
 *
 * For this test case to work properly, all fields must not conflict with eachother.
 */
static const TestData field_tests[] = {
	{ E_CONTACT_GIVEN_NAME,          "Elvis" },
	{ E_CONTACT_FAMILY_NAME,         "Presley" },
	{ E_CONTACT_NICKNAME,            "The King" },
	{ E_CONTACT_EMAIL_1,             "elvis@presley.com" },
	{ E_CONTACT_ADDRESS_LABEL_HOME,  "3764 Elvis Presley Boulevard, Graceland" },
	{ E_CONTACT_ADDRESS_LABEL_WORK,  "Workin on the road again..." },
	{ E_CONTACT_ADDRESS_LABEL_OTHER, "Another address to reach the king" },
	{ E_CONTACT_PHONE_ASSISTANT,     "+1234567890" },
	{ E_CONTACT_PHONE_BUSINESS,      "+99-123-4352-9943" },
	{ E_CONTACT_PHONE_BUSINESS_FAX,  "+44-123456789" },
	{ E_CONTACT_PHONE_CALLBACK,      "+11-222-3333-4444" },
	{ E_CONTACT_PHONE_CAR,           "555-123-4567" },
	{ E_CONTACT_PHONE_COMPANY,       "666-666-6666" },
	{ E_CONTACT_PHONE_HOME,          "333-4444-5678" },
	{ E_CONTACT_PHONE_HOME_FAX,      "+993355556666" },
	{ E_CONTACT_PHONE_ISDN,          "+88-777-6666-5555" },
	{ E_CONTACT_PHONE_MOBILE,        "333-3333" }
};

static gboolean try_write_field_thread_idle (ThreadData *data);

static void
test_write_thread_contact_modified (GObject *source_object,
                                    GAsyncResult *res,
                                    ThreadData *data)
{
	GError   *error = NULL;
	gboolean  retry = FALSE;

	if (!e_book_client_modify_contact_finish (E_BOOK_CLIENT (source_object), res, &error)) {

		/* For bad revision errors, retry the transaction after fetching the
		 * contact again first: The backend is telling us that this commit would have
		 * caused some data loss since we dont have the right contact in the first place.
		 */
		if (g_error_matches (error, E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC))
			retry = TRUE;
		else
			g_error (
				"Error updating '%s' field: %s\n",
				e_contact_field_name (data->field),
				error->message);

		g_error_free (error);
	}

	if (retry)
		try_write_field_thread_idle (data);
	else
		g_main_loop_quit (data->loop);
}

static void
test_write_thread_contact_fetched (GObject *source_object,
                                   GAsyncResult *res,
                                   ThreadData *data)
{
	EContact *contact = NULL;
	GError   *error = NULL;

	if (!e_book_client_get_contact_finish (E_BOOK_CLIENT (source_object), res, &contact, &error))
		g_error (
			"Failed to fetch contact in thread '%s': %s",
			e_contact_field_name (data->field), error->message);

	e_contact_set (contact, data->field, data->value);

	e_book_client_modify_contact (
		data->client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL,
		(GAsyncReadyCallback) test_write_thread_contact_modified, data);

	g_object_unref (contact);
}

static gboolean
try_write_field_thread_idle (ThreadData *data)
{
	e_book_client_get_contact (
		data->client, data->contact_uid, NULL,
		(GAsyncReadyCallback) test_write_thread_contact_fetched, data);

	return FALSE;
}

static void
test_write_thread_client_opened (GObject *source_object,
                                 GAsyncResult *res,
                                 ThreadData *data)
{
	GMainContext *context;
	GSource      *gsource;
	GError       *error = NULL;

	if (!e_client_open_finish (E_CLIENT (source_object), res, &error))
		g_error (
			"Error opening client for thread '%s': %s",
			e_contact_field_name (data->field),
			error->message);

	context = g_main_loop_get_context (data->loop);
	gsource = g_idle_source_new ();
	g_source_set_callback (gsource, (GSourceFunc) try_write_field_thread_idle, data, NULL);
	g_source_attach (gsource, context);
}

static gboolean
test_write_thread_open_idle (ThreadData *data)
{
	/* Open the book client, only if it exists, it should be the same book created by the main thread */
	e_client_open (E_CLIENT (data->client), TRUE, NULL, (GAsyncReadyCallback) test_write_thread_client_opened, data);

	return FALSE;
}

static gpointer
test_write_thread (ThreadData *data)
{
	GMainContext    *context;
	GSource         *gsource;
	ESource         *source;
	GError          *error = NULL;

	context = g_main_context_new ();
	data->loop = g_main_loop_new (context, FALSE);
	g_main_context_push_thread_default (context);

	/* Open the test book client in this thread */
	source = e_source_registry_ref_source (data->registry, data->book_uid);
	if (!source)
		g_error ("Unable to fetch source uid '%s' from the registry", data->book_uid);

	data->client = e_book_client_new (source, &error);
	if (!data->client)
		g_error ("Unable to create EBookClient for uid '%s': %s", data->book_uid, error->message);

	/* Retry setting the contact field until we succeed setting the field
	 */
	gsource = g_idle_source_new ();
	g_source_set_callback (gsource, (GSourceFunc) test_write_thread_open_idle, data, NULL);
	g_source_attach (gsource, context);
	g_main_loop_run (data->loop);

	g_object_unref (source);

	g_object_unref (data->client);
	g_main_context_pop_thread_default (context);
	g_main_loop_unref (data->loop);
	g_main_context_unref (context);

	return NULL;
}

static ThreadData *
create_test_thread (ESourceRegistry *registry,
		    const gchar *book_uid,
                    const gchar *contact_uid,
                    EContactField field,
                    const gchar *value)
{
	ThreadData  *data = g_slice_new0 (ThreadData);
	const gchar *name = e_contact_field_name (field);

	g_assert_nonnull (registry);

	data->registry = registry;
	data->book_uid = book_uid;
	data->contact_uid = contact_uid;
	data->field = field;
	data->value = value;

	data->thread = g_thread_new (name, (GThreadFunc) test_write_thread, data);

	return data;
}

static void
wait_thread_test (ThreadData *data)
{
	g_thread_join (data->thread);
	g_slice_free (ThreadData, data);
}

static void
test_concurrent_writes (ETestServerFixture *fixture,
                       gconstpointer user_data)
{
	EBookClient *main_client;
	ESource *source;
	EContact *contact;
	GError *error = NULL;
	const gchar *book_uid = NULL;
	gchar *contact_uid = NULL;
	ThreadData **tests;
	gint i;

	main_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	source = e_client_get_source (E_CLIENT (main_client));
	book_uid = e_source_get_uid (source);

	/* Create out test contact */
	if (!add_contact_from_test_case_verify (main_client, "simple-1", &contact))
		g_error ("Failed to add the test contact");

	contact_uid = e_contact_get (contact, E_CONTACT_UID);
	g_object_unref (contact);

	/* Create all concurrent threads accessing the same addressbook */
	tests = g_new0 (ThreadData *, G_N_ELEMENTS (field_tests));
	for (i = 0; i < G_N_ELEMENTS (field_tests); i++)
		tests[i] = create_test_thread (
			fixture->registry,
			book_uid, contact_uid,
			field_tests[i].field,
			field_tests[i].value);

	/* Wait for all threads to complete */
	for (i = 0; i < G_N_ELEMENTS (field_tests); i++)
		wait_thread_test (tests[i]);

	/* Fetch the updated contact */
	if (!e_book_client_get_contact_sync (main_client, contact_uid, &contact, NULL, &error))
		g_error ("Failed to fetch test contact after updates: %s", error->message);

	/* Ensure that every value written to the contact concurrently was actually updated in
	 * the final contact
	 */
	for (i = 0; i < G_N_ELEMENTS (field_tests); i++) {
		gchar *value = e_contact_get (contact, field_tests[i].field);

		if (g_strcmp0 (field_tests[i].value, value) != 0) {
			gchar *vcard;

			vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

			g_error (
				"Lost data in concurrent writes, expected "
				"value for field '%s' was '%s', actual value "
				"is '%s', vcard:\n%s\n",
				e_contact_field_name (field_tests[i].field),
				field_tests[i].value, value, vcard);
		}

		g_free (value);
	}
	g_object_unref (contact);

	g_free (contact_uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	setlocale (LC_ALL, "en_US.UTF-8");

	g_test_add (
		"/EBookClient/ConcurrentWrites",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_concurrent_writes,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
