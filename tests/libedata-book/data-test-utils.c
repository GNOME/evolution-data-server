/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013, Openismus GmbH
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "evolution-data-server-config.h"

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "data-test-utils.h"

static const gchar *args_data_dir = NULL;

void
data_test_utils_read_args (gint argc,
			   gchar **argv)
{
	gint ii;

	for (ii = 0; ii < argc; ii++) {
		if (g_strcmp0 (argv[ii], "--data-dir") == 0) {
			if (ii + 1 < argc)
				args_data_dir = argv[ii + 1];
			break;
		}
	}
}

gchar *
new_vcard_from_test_case (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;
	GFile * file;
	GError *error = NULL;
	gchar *vcard;

	case_filename = g_strdup_printf ("%s.vcf", case_name);

	/* In the case of installed tests, they run in ${pkglibexecdir}/installed-tests
	 * and the vcards are installed in ${pkglibexecdir}/installed-tests/vcards
	 */
	if (g_getenv ("TEST_INSTALLED_SERVICES") != NULL) {
		filename = g_build_filename (INSTALLED_TEST_DIR, "vcards", case_filename, NULL);
	} else {
		if (!args_data_dir) {
			g_warning ("Data directory not set, pass it with `--data-dir PATH`");
			exit(1);
		}

		filename = g_build_filename (args_data_dir, case_filename, NULL);
	}

	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error))
		g_error (
			"failed to read test contact file '%s': %s",
			filename, error->message);

	g_free (case_filename);
	g_free (filename);
	g_object_unref (file);

	return vcard;
}

EContact *
new_contact_from_test_case (const gchar *case_name)
{
	gchar *vcard;
	EContact *contact = NULL;

	vcard = new_vcard_from_test_case (case_name);
	if (vcard)
		contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	if (!contact)
		g_error (
			"failed to construct contact from test case '%s'",
			case_name);

	return contact;
}

void
add_contact_from_test_case (EbSqlFixture *fixture,
                            const gchar *case_name,
                            EContact **ret_contact)
{
	EContact *contact;
	GError *error = NULL;

	contact = new_contact_from_test_case (case_name);

	if (!e_book_sqlite_add_contact (fixture->ebsql,
					contact, case_name,
					FALSE, NULL, &error))
		g_error ("Failed to add contact: %s", error->message);

	if (ret_contact)
		*ret_contact = g_object_ref (contact);

	/* Hold on to this so we can load vcards */
	g_hash_table_insert (fixture->contacts, g_strdup (case_name), contact);
}

static void
delete_work_directory (const gchar *filename)
{
	/* XXX Instead of complex error checking here, we should ideally use
	 * a recursive GDir / g_unlink() function.
	 *
	 * We cannot use GFile and the recursive delete function without
	 * corrupting our contained D-Bus environment with service files
	 * from the OS.
	 */
	const gchar *argv[] = { "/bin/rm", "-rf", filename, NULL };
	gboolean spawn_succeeded;
	gint exit_status;

	spawn_succeeded = g_spawn_sync (
		NULL, (gchar **) argv, NULL, 0, NULL, NULL,
					NULL, NULL, &exit_status, NULL);

	g_assert (spawn_succeeded);
	#ifndef G_OS_WIN32
	g_assert (WIFEXITED (exit_status));
	g_assert_cmpint (WEXITSTATUS (exit_status), ==, 0);
	#else
	g_assert_cmpint (exit_status, ==, 0);
	#endif
}

ESourceBackendSummarySetup *
setup_empty_book (void)
{
	ESourceBackendSummarySetup *setup;
	ESource *scratch;
	GError *error = NULL;

	scratch = e_source_new_with_uid ("test-source", NULL, &error);
	if (!scratch)
		g_error ("Error creating scratch source");

	/* This is a bit of a cheat */
	setup = g_object_new (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP, "source", scratch, NULL);
	e_source_backend_summary_setup_set_summary_fields (
		setup,
							   /* We don't use this field in our tests anyway */
		E_CONTACT_FILE_AS,
		0);

	g_object_unref (scratch);

	return setup;
}

static gchar *
fetch_vcard_from_hash (const gchar *uid,
                       const gchar *extra,
                       gpointer user_data)
{
	EbSqlFixture *fixture = user_data;
	EContact     *contact;

	g_assert (extra && extra[0]);

	/* vCards not stored in shallow addressbooks, instead loaded on the fly */
	contact = g_hash_table_lookup (fixture->contacts, extra);
	if (contact)
		return e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	return NULL;
}

static void
contact_change_cb (EbSqlChangeType change_type,
                   const gchar *uid,
                   const gchar *extra,
                   const gchar *vcard,
                   gpointer user_data)
{
	EbSqlFixture *fixture = user_data;
	EContact *contact;

	if (change_type == EBSQL_CHANGE_LOCALE_CHANGED)
		fixture->n_locale_changes++;
	else if (change_type == EBSQL_CHANGE_CONTACT_ADDED)
		fixture->n_add_changes++;

	/* Replace the current contact with the new one */
	contact = e_contact_new_from_vcard (vcard);
	g_hash_table_insert (fixture->contacts, g_strdup (extra), contact);
}

void
e_sqlite_fixture_setup (EbSqlFixture *fixture,
                        gconstpointer user_data)
{
	EbSqlClosure *closure = (EbSqlClosure *) user_data;
	ESourceBackendSummarySetup *setup = NULL;
	gchar  *filename, *directory;
	GError *error = NULL;

	fixture->contacts =
		g_hash_table_new_full (
			g_str_hash,
			g_str_equal,
			g_free,
			g_object_unref);

	/* Cleanup from last test */
	directory = g_build_filename (g_get_tmp_dir (), "test-sqlite-cache", NULL);
	delete_work_directory (directory);
	g_free (directory);
	filename = g_build_filename (g_get_tmp_dir (), "test-sqlite-cache", "contacts.db", NULL);

	if (closure->setup_summary)
		setup = closure->setup_summary ();

	if (closure->without_vcards)
		fixture->ebsql = e_book_sqlite_new_full (
			filename,
			NULL,
			setup,
			fetch_vcard_from_hash,
			contact_change_cb,
			fixture,
			NULL, NULL,
			&error);
	else
		fixture->ebsql = e_book_sqlite_new_full (
			filename,
			NULL,
			setup,
			NULL,
			contact_change_cb,
			fixture,
			NULL, NULL,
			&error);

	g_clear_object (&setup);

	if (!fixture->ebsql)
		g_error ("Failed to create the SQLite: %s", error->message);

	g_free (filename);
}

void
e_sqlite_fixture_teardown (EbSqlFixture *fixture,
                           gconstpointer user_data)
{
	g_object_unref (fixture->ebsql);
	g_hash_table_destroy (fixture->contacts);
}

void
e_sqlite_cursor_fixture_setup (EbSqlCursorFixture *fixture,
                               gconstpointer user_data)
{
	EbSqlFixture       *base_fixture = (EbSqlFixture   *) fixture;
	EbSqlCursorClosure *data = (EbSqlCursorClosure *) user_data;
	EContactField       sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { data->sort_type, data->sort_type };
	GSList *contacts = NULL;
	GSList *extra_list = NULL;
	GError *error = NULL;
	gint i;
	gchar *sexp = NULL;

	e_sqlite_fixture_setup (base_fixture, user_data);

	if (data->locale)
		e_sqlite_cursor_fixture_set_locale (fixture, data->locale);
	else
		e_sqlite_cursor_fixture_set_locale (fixture, "en_US.UTF-8");

	for (i = 0; i < N_SORTED_CONTACTS; i++) {
		gchar *case_name = g_strdup_printf ("sorted-%d", i + 1);
		gchar *vcard;
		EContact *contact;

		vcard = new_vcard_from_test_case (case_name);
		contact = e_contact_new_from_vcard (vcard);
		contacts = g_slist_prepend (contacts, contact);
		extra_list = g_slist_prepend (extra_list, case_name);

		g_free (vcard);

		fixture->contacts[i] = contact;

		/* These need to be added to the hash as well */
		g_hash_table_insert (
			base_fixture->contacts,
			g_strdup (case_name),
			g_object_ref (contact));
	}

	if (!e_book_sqlite_add_contacts (base_fixture->ebsql,
					 contacts, extra_list,
					 FALSE, NULL, &error)) {
		/* Dont complain here, we re-use the same addressbook for multiple tests
		 * and we can't add the same contacts twice
		 */
		if (g_error_matches (error, E_BOOK_SQLITE_ERROR,
				     E_BOOK_SQLITE_ERROR_CONSTRAINT))
			g_clear_error (&error);
		else
			g_error ("Failed to add test contacts: %s", error->message);
	}

	g_slist_free (contacts);
	g_slist_free_full (extra_list, g_free);

	/* Allow a surrounding fixture setup to add a query here */
	if (fixture->query) {
		sexp = e_book_query_to_string (fixture->query);
		e_book_query_unref (fixture->query);
		fixture->query = NULL;
	}

	fixture->cursor = e_book_sqlite_cursor_new (
		base_fixture->ebsql, sexp,
		sort_fields, sort_types, 2, &error);

	if (!fixture->cursor)
		g_error ("Failed to create cursor: %s\n", error->message);

	g_free (sexp);
}

void
e_sqlite_cursor_fixture_filtered_setup (EbSqlCursorFixture *fixture,
                                        gconstpointer user_data)
{
	fixture->query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");

	e_sqlite_cursor_fixture_setup (fixture, user_data);
}

void
e_sqlite_cursor_fixture_teardown (EbSqlCursorFixture *fixture,
                                    gconstpointer user_data)
{
	EbSqlFixture *base_fixture = (EbSqlFixture   *) fixture;
	gint i;

	for (i = 0; i < N_SORTED_CONTACTS; i++) {
		if (fixture->contacts[i])
			g_object_unref (fixture->contacts[i]);
	}

	e_book_sqlite_cursor_free (base_fixture->ebsql, fixture->cursor);
	e_sqlite_fixture_teardown (base_fixture, user_data);
}

void
e_sqlite_cursor_fixture_set_locale (EbSqlCursorFixture *fixture,
                                    const gchar *locale)
{
	EbSqlFixture *base_fixture = (EbSqlFixture   *) fixture;
	GError *error = NULL;

	if (!e_book_sqlite_set_locale (base_fixture->ebsql,
				       locale, NULL, &error))
		g_error ("Failed to set locale: %s", error->message);
}

static gint
find_contact_data (EbSqlSearchData *data,
                   const gchar *uid)
{
	return g_strcmp0 (data->uid, uid);
}

void
assert_contacts_order_slist (GSList *results,
                             GSList *uids)
{
	gint position = -1;
	GSList *link, *l;

	/* Assert that all passed UIDs are found in the
	 * results, and that those UIDs are in the
	 * specified order.
	 */
	for (l = uids; l; l = l->next) {
		const gchar *uid = l->data;
		gint new_position;

		link = g_slist_find_custom (results, uid, (GCompareFunc) find_contact_data);
		if (!link)
			g_error ("Specified uid '%s' was not found in results", uid);

		new_position = g_slist_position (results, link);
		g_assert_cmpint (new_position, >, position);
		position = new_position;
	}

}

void
assert_contacts_order (GSList *results,
                       const gchar *first_uid,
                       ...)
{
	GSList *uids = NULL;
	gchar *uid;
	va_list args;

	g_assert (first_uid);

	uids = g_slist_append (uids, (gpointer) first_uid);

	va_start (args, first_uid);
	uid = va_arg (args, gchar *);
	while (uid) {
		uids = g_slist_append (uids, uid);
		uid = va_arg (args, gchar *);
	}
	va_end (args);

	assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

void
print_results (GSList *results)
{
	GSList *l;

	if (g_getenv ("TEST_DEBUG") == NULL)
		return;

	g_print ("\nPRINTING RESULTS:\n");

	for (l = results; l; l = l->next) {
		EbSqlSearchData *data = l->data;

		g_print ("\n%s\n", data->vcard);
	}

	g_print ("\nRESULT LIST_FINISHED\n");
}

/********************************************
 *           Move By Test Helpers
 ********************************************/
#define DEBUG_FIXTURE        0

static StepData *
step_test_new_internal (const gchar *test_path,
                        const gchar *locale,
                        gboolean store_vcards,
                        gboolean empty_book)
{
	StepData *data;

	data = g_slice_new0 (StepData);

	data->parent.locale = g_strdup (locale);
	data->parent.sort_type = E_BOOK_CURSOR_SORT_ASCENDING;

	data->parent.parent.without_vcards = (store_vcards == FALSE);
	if (empty_book)
		data->parent.parent.setup_summary = setup_empty_book;

	data->path = g_strdup (test_path);

	return data;
}

static void
step_test_free (StepData *data)
{
	GList *l;

	g_free (data->path);
	g_free ((gchar *) data->parent.locale);

	for (l = data->assertions; l; l = l->next) {
		StepAssertion *assertion = l->data;

		g_free (assertion->locale);
		g_slice_free (StepAssertion, assertion);
	}

	g_slice_free (StepData, data);
}

StepData *
step_test_new (const gchar *test_prefix,
               const gchar *test_path,
               const gchar *locale,
               gboolean store_vcards,
               gboolean empty_book)
{
	StepData *data;
	gchar *path;

	path = g_strconcat (test_prefix, test_path, NULL);
	data = step_test_new_internal (path, locale, store_vcards, empty_book);
	g_free (path);

	return data;
}

StepData *
step_test_new_full (const gchar *test_prefix,
                    const gchar *test_path,
                    const gchar *locale,
                    gboolean store_vcards,
                    gboolean empty_book,
                    EBookCursorSortType sort_type)
{
	StepData *data;
	gchar *path;

	path = g_strconcat (test_prefix, test_path, NULL);
	data = step_test_new_internal (path, locale, store_vcards, empty_book);
	data->parent.sort_type = sort_type;
	g_free (path);

	return data;
}

static void
test_cursor_move_teardown (EbSqlCursorFixture *fixture,
                           gconstpointer user_data)
{
	StepData *data = (StepData *) user_data;

	e_sqlite_cursor_fixture_teardown (fixture, user_data);
	step_test_free (data);
}

static void
assert_step (EbSqlCursorFixture *fixture,
             StepData *data,
             StepAssertion *assertion,
             GSList *results,
             gint n_results,
             gboolean expect_results)
{
	GSList *uids = NULL;
	gint i, expected = 0;

	/* Count the number of really expected results */
	for (i = 0; i < ABS (assertion->count); i++) {
		gint index = assertion->expected[i];

		if (index < 0)
			break;

		expected++;
	}

	g_assert_cmpint (n_results, ==, expected);
	if (!expect_results) {
		g_assert_cmpint (g_slist_length (results), ==, 0);
		return;
	}

	/* Assert the exact amount of requested results */
	g_assert_cmpint (g_slist_length (results), ==, expected);

#if DEBUG_FIXTURE
	g_print (
		"%s: Constructing expected result list for a fetch of %d: ",
		data->path, assertion->count);
#endif
	for (i = 0; i < ABS (assertion->count); i++) {
		gint index = assertion->expected[i];
		gchar *uid;

		if (index < 0)
			break;

		uid = (gchar *) e_contact_get_const (fixture->contacts[index], E_CONTACT_UID);
		uids = g_slist_append (uids, uid);

#if DEBUG_FIXTURE
		g_print ("%s ", uid);
#endif

	}
#if DEBUG_FIXTURE
	g_print ("\n");
#endif

	assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

static void
test_step (EbSqlCursorFixture *fixture,
           gconstpointer user_data)
{
	EbSqlFixture *base_fixture = (EbSqlFixture   *) fixture;
	StepData *data = (StepData *) user_data;
	GSList *results = NULL;
	GError *error = NULL;
	gint n_results;
	EbSqlCursorOrigin origin;
	GList *l;
	gboolean reset = TRUE;

	for (l = data->assertions; l; l = l->next) {
		StepAssertion *assertion = l->data;

		if (assertion->locale) {
			gint n_locale_changes = base_fixture->n_locale_changes;

			if (!e_book_sqlite_set_locale (base_fixture->ebsql,
						       assertion->locale,
						       NULL, &error))
				g_error ("Failed to set locale: %s", error->message);

			n_locale_changes = (base_fixture->n_locale_changes - n_locale_changes);

			/* Only check for contact changes is phone numbers are supported,
			 * contact changes only happen because of e164 number interpretations.
			 */
			if (e_phone_number_is_supported () &&
			    assertion->count != -1 &&
			    assertion->count != n_locale_changes)
				g_error (
					"Expected %d e164 numbers to change, %d actually changed.",
					assertion->count, n_locale_changes);

			reset = TRUE;
			continue;
		}

               /* For the first call to e_book_sqlite_cursor_step(),
		* or the first reset after locale change, set the origin accordingly.
                */
	       if (reset) {
		       if (assertion->count < 0)
			       origin = EBSQL_CURSOR_ORIGIN_END;
		       else
			       origin = EBSQL_CURSOR_ORIGIN_BEGIN;

		       reset = FALSE;
	       } else
		       origin = EBSQL_CURSOR_ORIGIN_CURRENT;

		/* Try only fetching the contacts but not moving the cursor */
		n_results = e_book_sqlite_cursor_step (
			base_fixture->ebsql,
			fixture->cursor,
			EBSQL_CURSOR_STEP_FETCH,
			origin,
			assertion->count,
			&results,
			NULL, &error);
		if (n_results < 0)
			g_error ("Error fetching cursor results: %s", error->message);

		print_results (results);
		assert_step (fixture, data, assertion, results, n_results, TRUE);
		g_slist_foreach (results, (GFunc) e_book_sqlite_search_data_free, NULL);
		g_slist_free (results);
		results = NULL;

		/* Do it again, this time only moving the cursor */
		n_results = e_book_sqlite_cursor_step (
			base_fixture->ebsql,
			fixture->cursor,
			EBSQL_CURSOR_STEP_MOVE,
			origin,
			assertion->count,
			&results,
			NULL, &error);
		if (n_results < 0)
			g_error ("Error fetching cursor results: %s", error->message);

		print_results (results);
		assert_step (fixture, data, assertion, results, n_results, FALSE);
		g_slist_foreach (results, (GFunc) e_book_sqlite_search_data_free, NULL);
		g_slist_free (results);
		results = NULL;
	}
}

static void
step_test_add_assertion_va_list (StepData *data,
                                 gint count,
                                 va_list args)
{
	StepAssertion *assertion = g_slice_new0 (StepAssertion);
	gint expected, i;

	assertion->count = count;

#if DEBUG_FIXTURE
	g_print ("Adding assertion to test %d: %s\n", i + 1, data->path);
	g_print ("  Test will move by %d and expect: ", count);
#endif
	for (i = 0; i < ABS (count); i++) {
		expected = va_arg (args, gint);

#if DEBUG_FIXTURE
		g_print ("%d ", expected);
#endif
		assertion->expected[i] = expected - 1;
	}
#if DEBUG_FIXTURE
	g_print ("\n");
#endif

	data->assertions = g_list_append (data->assertions, assertion);
}

/* A positive of negative 'count' value
 * followed by ABS (count) UID indexes.
 *
 * The indexes start at 1 so that they
 * are easier to match up with the chart
 * in data-test-utils.h
 */
void
step_test_add_assertion (StepData *data,
                         gint count,
                         ...)
{
	va_list args;

	va_start (args, count);
	step_test_add_assertion_va_list (data, count, args);
	va_end (args);
}

void
step_test_change_locale (StepData *data,
                         const gchar *locale,
                         gint expected_changes)
{
	StepAssertion *assertion = g_slice_new0 (StepAssertion);

	assertion->locale = g_strdup (locale);
	assertion->count = expected_changes;
	data->assertions = g_list_append (data->assertions, assertion);
}

void
step_test_add (StepData *data,
               gboolean filtered)
{
	data->filtered = filtered;

	g_test_add (
		data->path, EbSqlCursorFixture, data,
		filtered ?
		e_sqlite_cursor_fixture_filtered_setup :
		e_sqlite_cursor_fixture_setup,
		test_step,
		test_cursor_move_teardown);
}

