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

#include "ebook-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure =
	{ E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

#define EMAIL_ADD "foo@bar.com"

/* Global data */
static EBook *book = NULL;
static gchar *uid = NULL;

static void
verify_precommit_and_prepare_contact (EContact *contact)
{
	EVCardAttribute *attr;

	/* ensure there is no email address to begin with, then add one */
	g_assert (!e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL));
	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, EMAIL_ADD);
}

static void
verify_commit (EContact *contact)
{
	EVCardAttribute *attr;
	gchar *email_value;

	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL);
	g_assert (attr != NULL);
	g_assert (e_vcard_attribute_is_single_valued (attr));
	email_value = e_vcard_attribute_get_value (attr);
	g_assert (!g_strcmp0 (email_value, EMAIL_ADD));
}

static gboolean
commit_verify_cb (EBookTestClosure *closure)
{
	EContact *contact;

	contact = ebook_test_utils_book_get_contact (book, uid);
	verify_commit (contact);

	g_main_loop_quit ((GMainLoop *) (closure->user_data));

	return FALSE;
}

static void
test_commit_contact_sync (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EContact *contact;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", &contact);

	verify_precommit_and_prepare_contact (contact);
	ebook_test_utils_book_commit_contact (book, contact);
	verify_commit (contact);

	test_print ("successfully committed changes to contact contact '%s'\n", uid);
	g_object_unref (contact);
	g_free (uid);

	contact = NULL;
	uid = NULL;
}

static void
test_commit_contact_async (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	EContact *contact;

	book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBook);
	uid = ebook_test_utils_book_add_contact_from_test_case_verify (book, "name-only", &contact);

	verify_precommit_and_prepare_contact (contact);

	ebook_test_utils_book_async_commit_contact (
		book, contact, (GSourceFunc) commit_verify_cb, fixture->loop);

	g_main_loop_run (fixture->loop);

	g_object_unref (contact);
	g_free (uid);
	contact = NULL;
	uid = NULL;
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/EBook/CommitContact/Sync",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_commit_contact_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBook/CommitContact/Async",
		ETestServerFixture,
		&book_closure,
		e_test_server_utils_setup,
		test_commit_contact_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
