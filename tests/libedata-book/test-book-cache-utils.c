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

#include "test-book-cache-utils.h"

static const gchar *args_data_dir = NULL;

void
tcu_read_args (gint argc,
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
tcu_new_vcard_from_test_case (const gchar *case_name)
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
tcu_new_contact_from_test_case (const gchar *case_name)
{
	gchar *vcard;
	EContact *contact = NULL;

	vcard = tcu_new_vcard_from_test_case (case_name);
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
tcu_add_contact_from_test_case (TCUFixture *fixture,
				const gchar *case_name,
				EContact **ret_contact)
{
	EContact *contact;
	GError *error = NULL;

	contact = tcu_new_contact_from_test_case (case_name);

	if (!e_book_cache_put_contact (fixture->book_cache, contact, case_name, 0, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to add contact: %s", error->message);

	if (ret_contact)
		*ret_contact = g_object_ref (contact);

	g_clear_object (&contact);
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
tcu_setup_empty_book (void)
{
	ESourceBackendSummarySetup *setup;
	ESource *scratch;
	GError *error = NULL;

	scratch = e_source_new_with_uid ("test-source", NULL, &error);
	if (!scratch)
		g_error ("Error creating scratch source: %s", error ? error->message : "Unknown error");

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

static void
e164_changed_cb (EBookCache *book_cache,
		 EContact *contact,
		 gboolean is_replace,
		 gpointer user_data)
{
	TCUFixture *fixture = user_data;

	if (is_replace)
		fixture->n_locale_changes++;
	else
		fixture->n_add_changes++;
}

void
tcu_fixture_setup (TCUFixture *fixture,
		   gconstpointer user_data)
{
	TCUClosure *closure = (TCUClosure *) user_data;
	ESourceBackendSummarySetup *setup = NULL;
	gchar *filename, *directory;
	GError *error = NULL;

	/* Cleanup from last test */
	directory = g_build_filename (g_get_tmp_dir (), "test-book-cache", NULL);
	delete_work_directory (directory);
	g_free (directory);
	filename = g_build_filename (g_get_tmp_dir (), "test-book-cache", "cache.db", NULL);

	if (closure->setup_summary)
		setup = closure->setup_summary ();

	fixture->book_cache = e_book_cache_new_full (filename, NULL, setup, NULL, &error);

	g_clear_object (&setup);

	if (!fixture->book_cache)
		g_error ("Failed to create the EBookCache: %s", error->message);

	g_free (filename);

	g_signal_connect (fixture->book_cache, "e164-changed",
		G_CALLBACK (e164_changed_cb), fixture);
}

void
tcu_fixture_teardown (TCUFixture *fixture,
		      gconstpointer user_data)
{
	g_object_unref (fixture->book_cache);
}

void
tcu_cursor_fixture_setup (TCUCursorFixture *fixture,
			  gconstpointer user_data)
{
	TCUFixture *base_fixture = (TCUFixture   *) fixture;
	TCUCursorClosure *data = (TCUCursorClosure *) user_data;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { data->sort_type, data->sort_type };
	GSList *contacts = NULL;
	GSList *extra_list = NULL;
	GError *error = NULL;
	gint ii;
	gchar *sexp = NULL;

	tcu_fixture_setup (base_fixture, user_data);

	if (data->locale)
		tcu_cursor_fixture_set_locale (fixture, data->locale);
	else
		tcu_cursor_fixture_set_locale (fixture, "en_US.UTF-8");

	for (ii = 0; ii < N_SORTED_CONTACTS; ii++) {
		gchar *case_name = g_strdup_printf ("sorted-%d", ii + 1);
		gchar *vcard;
		EContact *contact;

		vcard = tcu_new_vcard_from_test_case (case_name);
		contact = e_contact_new_from_vcard (vcard);
		contacts = g_slist_prepend (contacts, contact);
		extra_list = g_slist_prepend (extra_list, case_name);

		g_free (vcard);

		fixture->contacts[ii] = g_object_ref (contact);
	}

	if (!e_book_cache_put_contacts (base_fixture->book_cache, contacts, extra_list, NULL, E_CACHE_IS_ONLINE, NULL, &error)) {
		/* Dont complain here, we re-use the same addressbook for multiple tests
		 * and we can't add the same contacts twice
		 */
		if (g_error_matches (error, E_CACHE_ERROR, E_CACHE_ERROR_CONSTRAINT))
			g_clear_error (&error);
		else
			g_error ("Failed to add test contacts: %s", error->message);
	}

	g_slist_free_full (contacts, g_object_unref);
	g_slist_free_full (extra_list, g_free);

	/* Allow a surrounding fixture setup to add a query here */
	if (fixture->query) {
		sexp = e_book_query_to_string (fixture->query);
		e_book_query_unref (fixture->query);
		fixture->query = NULL;
	}

	fixture->cursor = e_book_cache_cursor_new (
		base_fixture->book_cache, sexp,
		sort_fields, sort_types, 2, &error);

	if (!fixture->cursor)
		g_error ("Failed to create cursor: %s\n", error->message);

	g_free (sexp);
}

void
tcu_cursor_fixture_filtered_setup (TCUCursorFixture *fixture,
				   gconstpointer user_data)
{
	fixture->query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");

	tcu_cursor_fixture_setup (fixture, user_data);
}

void
tcu_cursor_fixture_teardown (TCUCursorFixture *fixture,
			     gconstpointer user_data)
{
	TCUFixture *base_fixture = (TCUFixture   *) fixture;
	gint ii;

	for (ii = 0; ii < N_SORTED_CONTACTS; ii++) {
		if (fixture->contacts[ii])
			g_object_unref (fixture->contacts[ii]);
	}

	e_book_cache_cursor_free (base_fixture->book_cache, fixture->cursor);
	tcu_fixture_teardown (base_fixture, user_data);
}

void
tcu_cursor_fixture_set_locale (TCUCursorFixture *fixture,
			       const gchar *locale)
{
	TCUFixture *base_fixture = (TCUFixture   *) fixture;
	GError *error = NULL;

	if (!e_book_cache_set_locale (base_fixture->book_cache, locale, NULL, &error))
		g_error ("Failed to set locale: %s", error->message);
}

static gint
find_contact_data (EBookCacheSearchData *data,
                   const gchar *uid)
{
	return g_strcmp0 (data->uid, uid);
}

void
tcu_assert_contacts_order_slist (GSList *results,
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
tcu_assert_contacts_order (GSList *results,
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

	tcu_assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

void
tcu_print_results (const GSList *results)
{
	const GSList *link;

	if (g_getenv ("TEST_DEBUG") == NULL)
		return;

	g_print ("\nPRINTING RESULTS:\n");

	for (link = results; link; link = link->next) {
		EBookCacheSearchData *data = link->data;

		g_print ("\n%s\n", data->vcard);
	}

	g_print ("\nRESULT LIST_FINISHED\n");
}

/********************************************
 *           Move By Test Helpers
 ********************************************/
#define DEBUG_FIXTURE        0

static TCUStepData *
step_test_new_internal (const gchar *test_path,
                        const gchar *locale,
                        gboolean empty_book)
{
	TCUStepData *data;

	data = g_slice_new0 (TCUStepData);

	data->parent.locale = g_strdup (locale);
	data->parent.sort_type = E_BOOK_CURSOR_SORT_ASCENDING;

	if (empty_book)
		data->parent.parent.setup_summary = tcu_setup_empty_book;

	data->path = g_strdup (test_path);

	return data;
}

static void
step_test_free (TCUStepData *data)
{
	GList *l;

	g_free (data->path);
	g_free ((gchar *) data->parent.locale);

	for (l = data->assertions; l; l = l->next) {
		TCUStepAssertion *assertion = l->data;

		g_free (assertion->locale);
		g_slice_free (TCUStepAssertion, assertion);
	}

	g_list_free (data->assertions);

	g_slice_free (TCUStepData, data);
}

TCUStepData *
tcu_step_test_new (const gchar *test_prefix,
		   const gchar *test_path,
		   const gchar *locale,
		   gboolean empty_book)
{
	TCUStepData *data;
	gchar *path;

	path = g_strconcat (test_prefix, test_path, NULL);
	data = step_test_new_internal (path, locale, empty_book);
	g_free (path);

	return data;
}

TCUStepData *
tcu_step_test_new_full (const gchar *test_prefix,
			const gchar *test_path,
			const gchar *locale,
			gboolean empty_book,
			EBookCursorSortType sort_type)
{
	TCUStepData *data;
	gchar *path;

	path = g_strconcat (test_prefix, test_path, NULL);
	data = step_test_new_internal (path, locale, empty_book);
	data->parent.sort_type = sort_type;
	g_free (path);

	return data;
}

static void
test_cursor_move_teardown (TCUCursorFixture *fixture,
			   gconstpointer user_data)
{
	TCUStepData *data = (TCUStepData *) user_data;

	tcu_cursor_fixture_teardown (fixture, user_data);
	step_test_free (data);
}

static void
assert_step (TCUCursorFixture *fixture,
	     TCUStepData *data,
	     TCUStepAssertion *assertion,
	     GSList *results,
	     gint n_results,
	     gboolean expect_results)
{
	GSList *uids = NULL;
	gint ii, expected = 0;

	/* Count the number of really expected results */
	for (ii = 0; ii < ABS (assertion->count); ii++) {
		gint index = assertion->expected[ii];

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
	for (ii = 0; ii < ABS (assertion->count); ii++) {
		gint index = assertion->expected[ii];
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

	tcu_assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

static void
test_step (TCUCursorFixture *fixture,
	   gconstpointer user_data)
{
	TCUFixture *base_fixture = (TCUFixture   *) fixture;
	TCUStepData *data = (TCUStepData *) user_data;
	GSList *results = NULL;
	GError *error = NULL;
	gint n_results;
	EBookCacheCursorOrigin origin;
	GList *l;
	gboolean reset = TRUE;

	for (l = data->assertions; l; l = l->next) {
		TCUStepAssertion *assertion = l->data;

		if (assertion->locale) {
			gint n_locale_changes = base_fixture->n_locale_changes;

			if (!e_book_cache_set_locale (base_fixture->book_cache, assertion->locale, NULL, &error))
				g_error ("Failed to set locale: %s", error->message);

			n_locale_changes = (base_fixture->n_locale_changes - n_locale_changes);

			/* Only check for contact changes is phone numbers are supported,
			 * contact changes only happen because of e164 number interpretations.
			 */
			if (e_phone_number_is_supported () &&
			    assertion->count != -1 &&
			    assertion->count != n_locale_changes)
				g_error ("Expected %d e164 numbers to change, %d actually changed.",
					assertion->count, n_locale_changes);

			reset = TRUE;
			continue;
		}

               /* For the first call to e_book_cache_cursor_step(),
		* or the first reset after locale change, set the origin accordingly.
                */
	       if (reset) {
		       if (assertion->count < 0)
			       origin = E_BOOK_CACHE_CURSOR_ORIGIN_END;
		       else
			       origin = E_BOOK_CACHE_CURSOR_ORIGIN_BEGIN;

		       reset = FALSE;
	       } else {
		       origin = E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT;
	       }

		/* Try only fetching the contacts but not moving the cursor */
		n_results = e_book_cache_cursor_step (
			base_fixture->book_cache,
			fixture->cursor,
			E_BOOK_CACHE_CURSOR_STEP_FETCH,
			origin,
			assertion->count,
			&results,
			NULL, &error);
		if (n_results < 0)
			g_error ("Error fetching cursor results: %s", error->message);

		tcu_print_results (results);
		assert_step (fixture, data, assertion, results, n_results, TRUE);
		g_slist_free_full (results, e_book_cache_search_data_free);
		results = NULL;

		/* Do it again, this time only moving the cursor */
		n_results = e_book_cache_cursor_step (
			base_fixture->book_cache,
			fixture->cursor,
			E_BOOK_CACHE_CURSOR_STEP_MOVE,
			origin,
			assertion->count,
			&results,
			NULL, &error);
		if (n_results < 0)
			g_error ("Error fetching cursor results: %s", error->message);

		tcu_print_results (results);
		assert_step (fixture, data, assertion, results, n_results, FALSE);
		g_slist_free_full (results, e_book_cache_search_data_free);
		results = NULL;
	}
}

static void
step_test_add_assertion_va_list (TCUStepData *data,
				 gint count,
				 va_list args)
{
	TCUStepAssertion *assertion = g_slice_new0 (TCUStepAssertion);
	gint expected, ii = 0;

	assertion->count = count;

#if DEBUG_FIXTURE
	g_print ("Adding assertion to test %d: %s\n", ii + 1, data->path);
	g_print ("  Test will move by %d and expect: ", count);
#endif
	for (ii = 0; ii < ABS (count); ii++) {
		expected = va_arg (args, gint);

#if DEBUG_FIXTURE
		g_print ("%d ", expected);
#endif
		assertion->expected[ii] = expected - 1;
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
tcu_step_test_add_assertion (TCUStepData *data,
			     gint count,
			     ...)
{
	va_list args;

	va_start (args, count);
	step_test_add_assertion_va_list (data, count, args);
	va_end (args);
}

void
tcu_step_test_change_locale (TCUStepData *data,
			     const gchar *locale,
			     gint expected_changes)
{
	TCUStepAssertion *assertion = g_slice_new0 (TCUStepAssertion);

	assertion->locale = g_strdup (locale);
	assertion->count = expected_changes;
	data->assertions = g_list_append (data->assertions, assertion);
}

void
tcu_step_test_add (TCUStepData *data,
		   gboolean filtered)
{
	data->filtered = filtered;

	g_test_add (
		data->path, TCUCursorFixture, data,
		filtered ?
		tcu_cursor_fixture_filtered_setup :
		tcu_cursor_fixture_setup,
		test_step,
		test_cursor_move_teardown);
}
