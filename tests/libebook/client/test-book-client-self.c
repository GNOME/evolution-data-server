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

#include "e-test-server-utils.h"
#include "client-test-utils.h"

static ETestServerClosure registry_closure = { E_TEST_SERVER_NONE, NULL, 0 };

static void
test_get_self (ETestServerFixture *fixture,
               gconstpointer user_data)
{
	EBookClient *client;
	EContact    *contact;
	GError      *error = NULL;

	if (!e_book_client_get_self (fixture->registry, &contact, &client, &error))
		g_error ("failed to get self contact: %s", error->message);

	if (!client)
		g_error ("e_book_client_get_self(): No client returned");

	if (!contact)
		g_error ("e_book_client_get_self(): No contact returned");

	g_object_unref (contact);
	g_object_unref (client);
}

static void
test_set_self (ETestServerFixture *fixture,
               gconstpointer user_data)
{
	ESource     *source;
	EBookClient *client;
	EContact    *loaded_contact = NULL;
	EContact    *self_contact = NULL;
	GError      *error = NULL;
	const gchar *added_uid, *self_uid;

	/* Open the system addressbook */
	source = e_source_registry_ref_builtin_address_book (fixture->registry);
	client = (EBookClient *) e_book_client_connect_sync (source, (guint32) -1, NULL, &error);
	g_object_unref (source);
	if (!client)
		g_error ("Error connecting to system addressbook: %s", error->message);

	/* Just a safety check which applies when running with installed services */
	if (e_book_client_get_contact_sync (client, "simple-1", &loaded_contact, NULL, NULL)) {
		g_clear_object (&loaded_contact);

		e_book_client_remove_contact_by_uid_sync (client, "simple-1", E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
	}

	/* Add contact to addressbook */
	g_assert_true (add_contact_from_test_case_verify (client, "simple-1", &loaded_contact));

	/* Set contact as self */
	if (!e_book_client_set_self (client, loaded_contact, &error))
		g_error ("Error setting self: %s", error->message);

	g_object_unref (client);
	client = NULL;

	if (!e_book_client_get_self (fixture->registry, &self_contact, &client, &error))
		g_error ("failed to get self contact: %s", error->message);

	if (!client)
		g_error ("e_book_client_get_self(): No client returned");

	if (!self_contact)
		g_error ("e_book_client_get_self(): No contact returned");

	/* Assert the fetched contact is the right one */
	added_uid = e_contact_get_const (loaded_contact, E_CONTACT_UID);
	self_uid = e_contact_get_const (self_contact, E_CONTACT_UID);
	g_assert_cmpstr (added_uid, ==, self_uid);

	g_object_unref (self_contact);
	g_object_unref (loaded_contact);
	g_object_unref (client);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/Self/Get",
		ETestServerFixture,
		&registry_closure,
		e_test_server_utils_setup,
		test_get_self,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/Self/Set",
		ETestServerFixture,
		&registry_closure,
		e_test_server_utils_setup,
		test_set_self,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
