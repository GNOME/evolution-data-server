/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "ebook-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
test_get_static_capabilities_sync (ETestServerFixture *fixture,
                                   gconstpointer user_data)
{
	EBook *book;
	const gchar *caps;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);

	caps = ebook_test_utils_book_get_static_capabilities (book);
	test_print ("successfully retrieved static capabilities: '%s'\n", caps);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBook/GetStaticCapabilities/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_get_static_capabilities_sync,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
