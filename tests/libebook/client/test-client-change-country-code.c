/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012,2013 Intel Corporation
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


static void
setup_custom_book (ESource            *scratch,
                   ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup,
							   E_CONTACT_TEL,
							   0);
	e_source_backend_summary_setup_set_indexed_fields (setup,
							   E_CONTACT_TEL, E_BOOK_INDEX_PHONE,
							   0);
}

static gboolean
query_service_pid (const gchar *name,
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

static gboolean
spawn_addressbook_factory (GPid *pid, GError **error)
{
	gint i;
	GPid child_pid = 0;
	GPid service_pid = 0;
	gboolean success;

	gchar *argv[] = {
		g_build_filename (BUILDDIR, "services/evolution-addressbook-factory/evolution-addressbook-factory", NULL),
		NULL
	};

	success = g_spawn_async (
		NULL, argv, NULL,
		g_test_verbose () ? 0 : G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		NULL, NULL, &child_pid, error);

	g_free (argv[0]);

	/* Wait a bit for the addressbook factory starting up so that it can claim its D-Bus name */
	if (success) {
		for (i = 0; service_pid == 0 && i < 10; ++i) {
			if (error && *error)
				g_clear_error (error);

			sleep_in_main_loop (150);
			success = query_service_pid (ADDRESS_BOOK_DBUS_SERVICE_NAME, &service_pid, error);
		}
	}

	if (success) {
		g_assert_cmpuint (service_pid, ==, child_pid);

		if (pid)
			*pid = child_pid;
	}

	return success;
}

static void
test_client_change_country_code (void)
{
	ETestServerFixture fixture;
	ETestServerClosure data;

	GError *error = NULL;
	EContact *contact;
	gchar *contact_uid;
	EVCardAttribute *attr;
	GList *e164_values;
	gboolean success;
	GPid factory_pid = 0;
	GSList *fetched_uids;

	/* Initialize e-test-server-utils structures */
	memset (&fixture, 0, sizeof fixture);

	memset (&data, 0, sizeof data);
	data.type = E_TEST_SERVER_ADDRESS_BOOK;
	data.customize = setup_custom_book;
	data.keep_work_directory = TRUE;

	/**************************************
	 * Create contacts within U.S. locale *
	 **************************************/

	/* Set with U.S. locale for addresses */
	g_setenv ("LC_ADDRESS", "en_US.UTF-8", TRUE);
	setlocale (LC_ADDRESS, "");
	g_assert_cmpstr (nl_langinfo (_NL_ADDRESS_COUNTRY_AB2), ==, "US");

	/* Launch addressbook factory on fake-dbus */
	success = spawn_addressbook_factory (&factory_pid, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpuint (factory_pid, !=, 0);
	g_assert (success);

	/* Create the test addressbook */
	e_test_server_utils_setup (&fixture, &data);
	g_assert (fixture.service.book_client != NULL);

	success = e_client_open_sync
		(E_CLIENT (fixture.service.book_client), FALSE, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	/* Add test contact */
	contact = e_contact_new_from_vcard ("BEGIN:VCARD\nTEL:221.542.3789\nEND:VCARD");
	success = e_book_client_add_contact_sync
		(fixture.service.book_client, contact, &contact_uid, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);
	g_object_unref (contact);

	/**************************************
	 * Verify contacts within U.S. locale *
	 **************************************/

	/* Fetch the contact by UID and check EVC_TEL attribute and its EVC_X_E164 parameter */
	success = e_book_client_get_contact_sync
		(fixture.service.book_client, contact_uid, &contact, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_TEL);

	g_assert (attr != NULL);
	g_assert_cmpstr (e_vcard_attribute_get_value (attr), ==, "221.542.3789");

	e164_values = e_vcard_attribute_get_param (attr, EVC_X_E164);

	g_assert (e164_values != NULL);
	g_assert_cmpstr (e164_values->data, ==, "+12215423789");

	/* Now resolve the contact via its phone number, assuming indexes are used */
	success = e_book_client_get_contacts_uids_sync (
		fixture.service.book_client, "(eqphone \"phone\" \"+1/221/5423789\")",
		&fetched_uids, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	g_assert_cmpuint (g_slist_length (fetched_uids), ==, 1);
	g_assert_cmpstr (fetched_uids->data, ==, contact_uid);
	g_slist_free_full (fetched_uids, g_free);

	success = e_book_client_get_contacts_uids_sync (
		fixture.service.book_client, "(eqphone \"phone\" \"+49 (221) 5423789\")",
		&fetched_uids, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	g_assert_cmpuint (g_slist_length (fetched_uids), ==, 0);
	g_slist_free_full (fetched_uids, g_free);

	/***************************
	 * Switch to German locale *
	 ***************************/

	/* Shutdown address book factory on fake D-Bus */
	e_test_server_utils_teardown (&fixture, &data);
	g_assert (fixture.service.book_client == NULL);
	data.keep_work_directory = g_test_verbose ();

	success = (kill (factory_pid, SIGTERM) == 0);
	g_assert (success);
	factory_pid = 0;

	/* Wait a bit to let GDBus notice what happened... */
	sleep_in_main_loop (1500);

	/* Switch to German locale */
	g_setenv ("LC_ADDRESS", "de_DE.UTF-8", TRUE);
	setlocale (LC_ADDRESS, "");

	g_assert_cmpstr (nl_langinfo (_NL_ADDRESS_COUNTRY_AB2), ==, "DE");

	/* Respawn the addressbook factory */
	success = spawn_addressbook_factory (&factory_pid, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert_cmpuint (factory_pid, !=, 0);
	g_assert (success);

	/* Reopen the book */
	e_test_server_utils_setup (&fixture, &data);
	g_assert (fixture.service.book_client != NULL);

	success = e_client_open_sync
		(E_CLIENT (fixture.service.book_client), FALSE, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	/****************************************
	 * Verify contacts within German locale *
	 ****************************************/

	/* Fetch the contact by UID and check EVC_TEL attribute and its EVC_X_E164 parameter */
	success = e_book_client_get_contact_sync
		(fixture.service.book_client, contact_uid, &contact, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_TEL);

	g_assert (attr != NULL);
	g_assert_cmpstr (e_vcard_attribute_get_value (attr), ==, "221.542.3789");

	e164_values = e_vcard_attribute_get_param (attr, EVC_X_E164);

	g_assert (e164_values != NULL);
	g_assert_cmpstr (e164_values->data, ==, "+492215423789");

	/* Now resolve the contact via its phone number, assuming indexes are used */
	success = e_book_client_get_contacts_uids_sync (
		fixture.service.book_client, "(eqphone \"phone\" \"+1/221/5423789\")",
		&fetched_uids, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	g_assert_cmpuint (g_slist_length (fetched_uids), ==, 0);
	g_slist_free_full (fetched_uids, g_free);

	success = e_book_client_get_contacts_uids_sync (
		fixture.service.book_client, "(eqphone \"phone\" \"+49 (221) 5423789\")",
		&fetched_uids, NULL, &error);
	g_assert_cmpstr (error ? error->message : NULL, ==, NULL);
	g_assert (success);

	g_assert_cmpuint (g_slist_length (fetched_uids), ==, 1);
	g_assert_cmpstr (fetched_uids->data, ==, contact_uid);
	g_slist_free_full (fetched_uids, g_free);

	/***********
	 * Cleanup *
	 ***********/

	e_test_server_utils_teardown (&fixture, &data);
	g_assert (fixture.service.book_client == NULL);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);

	main_initialize ();
	sleep_in_main_loop (500);

	g_test_add_func ("/client/e164/change-country-code", test_client_change_country_code);

	return e_test_server_utils_run ();
}
