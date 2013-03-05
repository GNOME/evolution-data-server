/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Mathias Hasselmann <mathias@openismus.com>
 */

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>

#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

#define TEST_SOURCE_UID "change-country-code-test"

typedef struct {
	ETestServerClosure	 closure;
	const gchar		*exec_path;
} TestData;

static gboolean
query_service_pid (const char  *name,
                   GPid        *pid,
                   GError     **error)
{
	GDBusConnection *connection;
	GVariant *rv;

	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

	if (connection == NULL)
		return FALSE;

	rv = g_dbus_connection_call_sync (connection,
	                                  "org.freedesktop.DBus",
	                                  "/org/freedesktop/DBus",
	                                  "org.freedesktop.DBus",
	                                  "GetConnectionUnixProcessID",
	                                  g_variant_new ("(s)", name),
	                                  NULL, G_DBUS_CALL_FLAGS_NONE, -1,
	                                  NULL, error);

	g_object_unref (connection);

	if (rv == NULL)
		return FALSE;

	g_variant_get (rv, "(u)", pid);
	g_variant_unref (rv);

	return TRUE;
}

static void
run_forked_test (const TestData *data,
                 const gchar *path)
{
	GTestTrapFlags test_flags = 0;
	GPid pid = 0;

	if (!g_test_verbose ())
		test_flags |= G_TEST_TRAP_SILENCE_STDOUT;

	if (g_test_trap_fork (0, test_flags)) {
		execl (data->exec_path, data->exec_path, "run-forked", path, g_test_verbose () ? "--verbose" : NULL, NULL);
		g_error ("execl failed: %s", g_strerror (errno));
		exit (1);
	}

	g_test_trap_assert_passed ();

	if (query_service_pid (ADDRESS_BOOK_DBUS_SERVICE_NAME, &pid, NULL))
              kill (pid, SIGTERM);
}

static gchar *
make_path (const TestData *data,
           const gchar *suffix)
{
	return g_strdup_printf (
		"/EBookClient/%s%s/PhoneNumber/ChangeCountryCode/%s",
		data->closure.customize ? "Custom" : "Default",
		data->closure.type == E_TEST_SERVER_DIRECT_ADDRESS_BOOK ? "/DirectAccess" : "",
		suffix);
}

static void
test_change_country_code (gconstpointer user_data)
{
	const TestData *data = user_data;
	gchar *region;
	gchar *path;

	/* Starting with U.S. locale */
	g_setenv ("LC_ADDRESS", "en_US.UTF-8", TRUE);
	setlocale (LC_ADDRESS, "");

	region = e_phone_number_get_default_region ();
	g_assert_cmpstr (region, ==, "US");
	g_free (region);

	/* Create the addressbook with U.S. locale */
	path = make_path (data, "Setup");
	run_forked_test (data, path);
	g_free (path);

	/* Test the addressbook with U.S. locale */
	path = make_path (data, "UnitedStates");
	run_forked_test (data, path);
	g_free (path);

	/* Switching to British locale */
	g_setenv ("LC_ADDRESS", "en_GB.UTF-8", TRUE);
	setlocale (LC_ADDRESS, "");

	region = e_phone_number_get_default_region ();
	g_assert_cmpstr (region, ==, "GB");
	g_free (region);

	/* Test the addressbook with British locale */
	path = make_path (data, "GreatBritain");
	run_forked_test (data, path);
	g_free (path);
}

static void
setup_custom_book (ESource            *scratch,
		   ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_class_unref (g_type_class_ref (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP));
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup, E_CONTACT_TEL, 0);
	e_source_backend_summary_setup_set_indexed_fields (setup, E_CONTACT_TEL, E_BOOK_INDEX_PHONE, 0);
}

static TestData *
test_data_new (ETestSourceCustomizeFunc  custom_summary,
               ETestServiceType          service_type,
               const gchar              *exec_path)
{
	TestData *data = g_new0 (TestData, 1);

	data->closure.keep_work_directory = TRUE;
	data->closure.customize = custom_summary;
	data->closure.type = service_type;
	data->exec_path = exec_path;

	return data;
}

static void
save_test_contact (EBookClient *client,
                   const gchar *vcard_string,
                   const gchar *national_number)
{
	GError          *error = NULL;
	gchar           *contact_uid;
	EContact        *contact;
	gboolean         success;
	EVCardAttribute *attr;
	GList           *params;

	/* Add test contact */
	contact = e_contact_new_from_vcard (vcard_string);
	success = e_book_client_add_contact_sync (client, contact, &contact_uid, NULL, &error);
	g_object_unref (contact);

	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	/* Fetch the contact by UID and check EVC_TEL attribute and its EVC_X_E164 parameter */
	success = e_book_client_get_contact_sync (client, contact_uid, &contact, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_TEL);

	g_assert (attr != NULL);
	g_assert_cmpstr (e_vcard_attribute_get_value (attr), ==, "123.456");

	params = e_vcard_attribute_get_param (attr, EVC_X_E164);

	g_assert (params != NULL);
	g_assert (params->next == NULL);
	g_assert_cmpstr (params->data, ==, national_number);

	g_object_unref (contact);
}

static void
find_contacts (EBookClient *client,
               const gchar *query,
               const gchar *national_number,
               gint         n_names,
               ...)
{
	GHashTable *expected_names = g_hash_table_new (g_str_hash, g_str_equal);
	GError     *error = NULL;
	GSList     *contacts, *l;
	gboolean    success;
	va_list     args;
	gint        i;

	g_test_message ("the query is: %s", query);

	va_start (args, n_names);

	for (i = 0; i < n_names; ++i) {
		gchar *name = va_arg (args, gchar *);
		g_hash_table_add (expected_names, name);
	}

	va_end (args);

	success = e_book_client_get_contacts_sync (client, query, &contacts, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	for (l = contacts; l; l = l->next) {
		const gchar *name;
		gboolean name_found;
		EVCardAttribute *attr;
		GList *params;

		name = e_contact_get_const (l->data, E_CONTACT_FULL_NAME);
		name_found = g_hash_table_remove (expected_names, name);
		g_assert_cmpstr (name_found ? name : NULL, ==, name);

		attr = e_vcard_get_attribute (l->data, EVC_TEL);
		g_assert (attr != NULL);
		g_assert_cmpstr (e_vcard_attribute_get_value (attr), ==, "123.456");

		params = e_vcard_attribute_get_param (attr, EVC_X_E164);

		g_assert (params != NULL);
		g_assert (params->next == NULL);
		g_assert_cmpstr (params->data, ==, national_number);
	}

	g_assert_cmpint (g_slist_length (contacts), ==, n_names);
	g_assert_cmpint (g_hash_table_size (expected_names), ==, 0);
}

static void
test_setup_addressbook (ETestServerFixture *fixture,
                        gconstpointer       user_data)
{
}

static void
test_change_country_code_setup (ETestServerFixture *fixture,
                                gconstpointer       user_data)
{
	fixture->source_name = g_strdup (TEST_SOURCE_UID);
	e_test_server_utils_setup (fixture, user_data);
	g_assert_cmpstr (fixture->source_name, ==, TEST_SOURCE_UID);
}

static void
test_change_country_code_setup_reopen (ETestServerFixture *fixture,
                                       gconstpointer       user_data)
{
	const ETestServerClosure *closure = user_data;
	ESource *source = NULL;
	GError *error = NULL;
	gboolean success;

	fixture->registry = e_source_registry_new_sync (NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (E_IS_SOURCE_REGISTRY (fixture->registry));

	source = e_source_new_with_uid (TEST_SOURCE_UID, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (E_IS_SOURCE (source));

	switch (closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
		fixture->service.book_client = e_book_client_new (source, &error);
		break;

	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
		fixture->service.book_client = e_book_client_new_direct (fixture->registry, source, &error);
		break;

	default:
		g_assert_not_reached ();
		break;
	}

	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (E_IS_BOOK_CLIENT (fixture->service.book_client));

	success = e_client_open_sync (E_CLIENT (fixture->service.book_client), TRUE, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	g_object_unref (source);
}

static void
test_change_country_code_teardown (ETestServerFixture *fixture,
                                   gconstpointer       user_data)
{
	if (fixture->service.book_client)
		g_object_unref (fixture->service.book_client);
}

static void
test_united_states (ETestServerFixture *fixture,
                    gconstpointer       user_data)
{
	EBookClient *const client = fixture->service.book_client;
	gchar *region;

	region = e_phone_number_get_default_region ();
	g_assert_cmpstr (region, ==, "US");
	g_free (region);

	save_test_contact (client, "BEGIN:VCARD\nFN:Joe Smith\nTEL:123.456\nEND:VCARD", "23456");

	find_contacts (client, "(eqphone_national \"phone\" \"123456\")", "23456", 1, "Joe Smith");
	find_contacts (client, "(eqphone_national \"phone\" \"23456\")", "23456", 1, "Joe Smith");
	find_contacts (client, "(eqphone_national \"phone\" \"+123456\")", "23456", 1, "Joe Smith");
	find_contacts (client, "(eqphone_national \"phone\" \"+44123456\")", "23456", 0);
}

static void
test_great_britain (ETestServerFixture *fixture,
                    gconstpointer       user_data)
{
	EBookClient *const client = fixture->service.book_client;
	gchar *region;

	/* FIXME: e_book_backend_file_create_unique_id() is broken:
	 * The generated id consists of a second-resolution timestamp and
	 * a process local static counter. It therefore will generate equal
	 * ids if the addressbook factory is respawning quickly. Let's
	 * work around for now by sleeping a bit. */
	sleep (1);

	region = e_phone_number_get_default_region ();
	g_assert_cmpstr (region, ==, "GB");
	g_free (region);

	save_test_contact (client, "BEGIN:VCARD\nFN:Joe Black\nTEL:123.456\nEND:VCARD", "123456");

	find_contacts (client, "(contains \"x-evolution-any-field\" \"\")", "123456", 2, "Joe Smith", "Joe Black");
	find_contacts (client, "(eqphone_national \"phone\" \"123456\")", "123456", 2, "Joe Smith", "Joe Black");
	find_contacts (client, "(eqphone_national \"phone\" \"23456\")", "123456", 0);
	find_contacts (client, "(eqphone_national \"phone\" \"+123456\")", "123456", 0);
	find_contacts (client, "(eqphone_national \"phone\" \"+44123456\")", "123456", 2, "Joe Smith", "Joe Black");
}

gint
main (gint    argc,
      gchar **argv)
{
	setlocale (LC_ALL, "");

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	if (argc > 2 && strcmp (argv[1], "run-forked") == 0) {
		ETestServerFlags flags = E_TEST_SERVER_KEEP_WORK_DIRECTORY;
		const gchar *const path = argv[2];

		TestData *data = test_data_new (
			strstr (path, "/Custom/") ? setup_custom_book : NULL,
			strstr (path, "/DirectAccess/")
				? E_TEST_SERVER_DIRECT_ADDRESS_BOOK
				: E_TEST_SERVER_ADDRESS_BOOK,
			NULL);

		if (g_str_has_suffix (path, "/Setup")) {
			g_test_add (
				path, ETestServerFixture, data,
				test_change_country_code_setup, test_setup_addressbook,
				e_test_server_utils_teardown);

			flags &= ~E_TEST_SERVER_KEEP_WORK_DIRECTORY;
		} else if (g_str_has_suffix (path, "/UnitedStates")) {
			g_test_add (
				path, ETestServerFixture, data,
				test_change_country_code_setup_reopen, test_united_states,
				test_change_country_code_teardown);
		} else if (g_str_has_suffix (path, "/GreatBritain")) {
			g_test_add (
				path, ETestServerFixture, data,
				test_change_country_code_setup_reopen, test_great_britain,
				test_change_country_code_teardown);
		}

		return e_test_server_utils_run_full (flags);
	}

#ifdef ENABLE_PHONENUMBER

	g_test_add_data_func (
		"/EBookClient/Default/PhoneNumber/ChangeCountryCode",
		test_data_new (NULL, E_TEST_SERVER_ADDRESS_BOOK, argv[0]),
		test_change_country_code);
	g_test_add_data_func (
		"/EBookClient/Default/DirectAccess/PhoneNumber/ChangeCountryCode",
		test_data_new (NULL, E_TEST_SERVER_DIRECT_ADDRESS_BOOK, argv[0]),
		test_change_country_code);
	g_test_add_data_func (
		"/EBookClient/Custom/PhoneNumber/ChangeCountryCode",
		test_data_new (setup_custom_book, E_TEST_SERVER_ADDRESS_BOOK, argv[0]),
		test_change_country_code);
	g_test_add_data_func (
		"/EBookClient/Custom/DirectAccess/PhoneNumber/ChangeCountryCode",
		test_data_new (setup_custom_book, E_TEST_SERVER_DIRECT_ADDRESS_BOOK, argv[0]),
		test_change_country_code);

#endif /* ENABLE_PHONENUMBER */

	/* Call without flags to get the working directory wiped. */
	return e_test_server_utils_run ();
}
