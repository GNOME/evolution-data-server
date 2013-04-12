/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "data-test-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static void
test_get_contact (ESqliteDBFixture *fixture,
		  gconstpointer     user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	EContact *other;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact)) {
		g_error ("Failed to get contact");
	}

	other = e_book_backend_sqlitedb_get_contact (fixture->ebsdb, SQLITEDB_FOLDER_ID,
						     (const gchar *)e_contact_get_const (contact, E_CONTACT_UID),
						     NULL, NULL, &error);

	if (!other)
		g_error ("Failed to get contact with uid '%s': %s",
			 (const gchar *)e_contact_get_const (contact, E_CONTACT_UID),
			 error->message);

	g_object_unref (contact);
	g_object_unref (other);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/EBookBackendSqliteDB/GetContact", ESqliteDBFixture, &book_closure,
		    e_sqlitedb_fixture_setup, test_get_contact, e_sqlitedb_fixture_teardown);

	return e_test_server_utils_run ();
}
