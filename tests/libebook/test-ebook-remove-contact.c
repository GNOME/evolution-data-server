/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/libebook.h>

#include "ebook-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
test_remove_contact_sync (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBook *book;
	EContact *contact_final = NULL;
	gchar *uid;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);

	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", NULL);
	ebook_test_utils_book_remove_contact (book, uid);
	e_book_get_contact (book, uid, &contact_final, NULL);

	g_assert (contact_final == NULL);
	test_print ("successfully added and removed contact '%s'\n", uid);

	g_free (uid);
}

static void
test_remove_contact_async (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	EBook *book;
	EContact *contact_final = NULL;
	gchar *uid;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);

	/* contact_final has 2 refs by the end of this */
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "simple-1", &contact_final);

	/* contact_final is unref'd by e_book_remove_contact() here */
	ebook_test_utils_book_async_remove_contact (
		book, contact_final, ebook_test_utils_callback_quit, fixture->loop);

	g_main_loop_run (fixture->loop);
	g_object_unref (contact_final);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBook/RemoveContact/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_remove_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBook/RemoveContact/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_remove_contact_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
