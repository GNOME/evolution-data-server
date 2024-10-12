/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "data-test-utils.h"

static void
test_sqlite_query (EbSqlFixture *fixture,
		   gconstpointer user_data)
{
	const gchar *sexp = "(contains \"file_as\" \"X\")";
	GPtrArray *uids = NULL, *values = NULL, *contacts = NULL;
	guint n_total = G_MAXUINT, ii;
	gboolean success;
	GError *error = NULL;

	add_contact_from_test_case (fixture, "file-as-2", NULL);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 1);

	add_contact_from_test_case (fixture, "file-as-1", NULL);
	add_contact_from_test_case (fixture, "file-as-3", NULL);
	add_contact_from_test_case (fixture, "file-as-4", NULL);
	add_contact_from_test_case (fixture, "file-as-5", NULL);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 3);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FILE_AS, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 3);
	g_assert_cmpuint (values->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "file-as-2");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "file-as-5");
	g_assert_cmpstr (g_ptr_array_index (uids, 2), ==, "file-as-3");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "andyx b");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "lastx man");
	g_assert_cmpstr (g_ptr_array_index (values, 2), ==, "zoe x");

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (uids, ii);
		gchar *value = NULL;

		success = e_book_sqlite_dup_summary_field (fixture->ebsql, E_CONTACT_FILE_AS, uid, &value, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (value);
		g_assert_cmpstr (value, ==, g_ptr_array_index (values, ii));

		g_free (value);
	}

	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FILE_AS, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_DESCENDING, 0, 10, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 3);
	g_assert_cmpuint (values->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "file-as-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "file-as-5");
	g_assert_cmpstr (g_ptr_array_index (uids, 2), ==, "file-as-2");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "zoe x");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "lastx man");
	g_assert_cmpstr (g_ptr_array_index (values, 2), ==, "andyx b");
	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FILE_AS, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING, 1, 2, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 2);
	g_assert_cmpuint (values->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "file-as-5");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "file-as-3");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "lastx man");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "zoe x");
	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FILE_AS, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING, 2, 1, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 1);
	g_assert_cmpuint (values->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "file-as-3");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "zoe x");
	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FILE_AS, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_DESCENDING, 2, 1, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 1);
	g_assert_cmpuint (values->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "file-as-2");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "andyx b");
	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "file-as-5");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "file-as-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 3);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "file-as-3");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "file-as-5");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 2), E_CONTACT_UID), ==, "file-as-2");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_DESCENDING, 2, 1, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 1);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "file-as-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_FULL_NAME), ==, "AndyX B");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING, 1, 2, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "file-as-5");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "file-as-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);
}

static void
test_sqlite_query_aux (EbSqlFixture *fixture,
		       gconstpointer user_data)
{
	const gchar *sexp = "(exists \"phone\")";
	GPtrArray *uids = NULL, *values = NULL, *contacts = NULL;
	guint n_total = G_MAXUINT, ii;
	gboolean success;
	GError *error = NULL;

	add_contact_from_test_case (fixture, "custom-1", NULL);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 1);

	add_contact_from_test_case (fixture, "file-as-1", NULL);
	add_contact_from_test_case (fixture, "file-as-2", NULL);
	add_contact_from_test_case (fixture, "custom-3", NULL);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 2);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 2);
	g_assert_cmpuint (values->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "bobby brown");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "micheal jackson");

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (uids, ii);
		gchar *value = NULL;

		success = e_book_sqlite_dup_summary_field (fixture->ebsql, E_CONTACT_FULL_NAME, uid, &value, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (value);
		g_assert_cmpstr (value, ==, g_ptr_array_index (values, ii));

		g_free (value);
	}

	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, 10, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 2);
	g_assert_cmpuint (values->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "micheal jackson");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "bobby brown");
	g_clear_pointer (&uids, g_ptr_array_unref);
	g_clear_pointer (&values, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 1);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 1, 1, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 1);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-3");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_FULL_NAME), ==, "Bobby Brown");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, 1, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 1);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);
}

static void
test_sqlite_query_contains_nickname (EbSqlFixture *fixture,
				     gconstpointer user_data)
{
	const gchar *sexp_exists = "(exists \"nickname\")";
	const gchar *sexp_contains_empty = "(contains \"nickname\" \"\")";
	const gchar *sexp_contains_jackson = "(contains \"nickname\" \"jackson\")";
	GPtrArray *uids = NULL, *values = NULL, *contacts = NULL;
	guint n_total = G_MAXUINT;
	gboolean success;
	GError *error = NULL;

	add_contact_from_test_case (fixture, "custom-1", NULL);
	add_contact_from_test_case (fixture, "custom-2", NULL);
	add_contact_from_test_case (fixture, "custom-3", NULL);
	add_contact_from_test_case (fixture, "sorted-1", NULL);
	add_contact_from_test_case (fixture, "sorted-2", NULL);
	add_contact_from_test_case (fixture, "sorted-3", NULL);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp_exists, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 3);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp_exists,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 3);
	g_assert_cmpuint (values->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-2");
	g_assert_cmpstr (g_ptr_array_index (uids, 2), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "bobby brown");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "janet jackson");
	g_assert_cmpstr (g_ptr_array_index (values, 2), ==, "micheal jackson");

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_exists,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_exists,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 3);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 2), E_CONTACT_UID), ==, "custom-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp_contains_empty, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 3);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp_contains_empty,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 3);
	g_assert_cmpuint (values->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-2");
	g_assert_cmpstr (g_ptr_array_index (uids, 2), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "bobby brown");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "janet jackson");
	g_assert_cmpstr (g_ptr_array_index (values, 2), ==, "micheal jackson");

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_empty,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_empty,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 3);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 2), E_CONTACT_UID), ==, "custom-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp_contains_jackson, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 2);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp_contains_jackson,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 2);
	g_assert_cmpuint (values->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-2");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "janet jackson");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "micheal jackson");

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_jackson,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 1);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_jackson,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-2");
	g_clear_pointer (&contacts, g_ptr_array_unref);
}

static void
test_sqlite_query_contains_phone (EbSqlFixture *fixture,
				  gconstpointer user_data)
{
	const gchar *sexp_exists = "(exists \"phone\")";
	const gchar *sexp_contains_empty = "(contains \"phone\" \"\")";
	const gchar *sexp_contains_9 = "(contains \"phone\" \"9\")";
	GPtrArray *uids = NULL, *values = NULL, *contacts = NULL;
	guint n_total = G_MAXUINT;
	gboolean success;
	GError *error = NULL;

	add_contact_from_test_case (fixture, "custom-1", NULL);
	add_contact_from_test_case (fixture, "custom-2", NULL);
	add_contact_from_test_case (fixture, "custom-3", NULL);
	add_contact_from_test_case (fixture, "file-as-1", NULL);
	add_contact_from_test_case (fixture, "file-as-2", NULL);
	add_contact_from_test_case (fixture, "file-as-3", NULL);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp_exists, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 3);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp_exists,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 3);
	g_assert_cmpuint (values->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-2");
	g_assert_cmpstr (g_ptr_array_index (uids, 2), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "bobby brown");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "janet jackson");
	g_assert_cmpstr (g_ptr_array_index (values, 2), ==, "micheal jackson");

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_exists,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_exists,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 3);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 2), E_CONTACT_UID), ==, "custom-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp_contains_empty, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 3);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp_contains_empty,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 3);
	g_assert_cmpuint (values->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-2");
	g_assert_cmpstr (g_ptr_array_index (uids, 2), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "bobby brown");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "janet jackson");
	g_assert_cmpstr (g_ptr_array_index (values, 2), ==, "micheal jackson");

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_empty,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_empty,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 3);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-2");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 2), E_CONTACT_UID), ==, "custom-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_count_query (fixture->ebsql, sexp_contains_9, &n_total, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpuint (n_total, ==, 2);

	success = e_book_sqlite_dup_query_field (fixture->ebsql, E_CONTACT_FULL_NAME, sexp_contains_9,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 0, 0, &uids, &values, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uids);
	g_assert_nonnull (values);
	g_assert_cmpuint (uids->len, ==, 2);
	g_assert_cmpuint (values->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (uids, 0), ==, "custom-3");
	g_assert_cmpstr (g_ptr_array_index (uids, 1), ==, "custom-1");
	g_assert_cmpstr (g_ptr_array_index (values, 0), ==, "bobby brown");
	g_assert_cmpstr (g_ptr_array_index (values, 1), ==, "micheal jackson");

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_9,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_ASCENDING, 1, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 1);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_clear_pointer (&contacts, g_ptr_array_unref);

	success = e_book_sqlite_dup_query_contacts (fixture->ebsql, sexp_contains_9,
		E_CONTACT_FULL_NAME, E_BOOK_CURSOR_SORT_DESCENDING, 0, G_MAXUINT, &contacts, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (contacts);
	g_assert_cmpuint (contacts->len, ==, 2);
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 0), E_CONTACT_UID), ==, "custom-1");
	g_assert_cmpstr (e_contact_get_const (g_ptr_array_index (contacts, 1), E_CONTACT_UID), ==, "custom-3");
	g_clear_pointer (&contacts, g_ptr_array_unref);
}

gint
main (gint argc,
      gchar **argv)
{
	static EbSqlClosure closure_with_cards = { FALSE, NULL };
	static EbSqlClosure closure_without_cards = { TRUE, NULL };

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	data_test_utils_read_args (argc, argv);

	/* Ensure that the client and server get the same locale */
	g_assert_true (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/EBookSqlite/QueryWithCards", EbSqlFixture, &closure_with_cards,
		e_sqlite_fixture_setup, test_sqlite_query, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryWithoutCards", EbSqlFixture, &closure_without_cards,
		e_sqlite_fixture_setup, test_sqlite_query, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryAuxWithCards", EbSqlFixture, &closure_with_cards,
		e_sqlite_fixture_setup, test_sqlite_query_aux, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryAuxWithoutCards", EbSqlFixture, &closure_without_cards,
		e_sqlite_fixture_setup, test_sqlite_query_aux, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryContainsNicknameWithCards", EbSqlFixture, &closure_with_cards,
		e_sqlite_fixture_setup, test_sqlite_query_contains_nickname, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryContainsNicknameWithoutCards", EbSqlFixture, &closure_without_cards,
		e_sqlite_fixture_setup, test_sqlite_query_contains_nickname, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryContainsPhoneWithCards", EbSqlFixture, &closure_with_cards,
		e_sqlite_fixture_setup, test_sqlite_query_contains_phone, e_sqlite_fixture_teardown);
	g_test_add ("/EBookSqlite/QueryContainsPhoneWithoutCards", EbSqlFixture, &closure_without_cards,
		e_sqlite_fixture_setup, test_sqlite_query_contains_phone, e_sqlite_fixture_teardown);

	return e_test_server_utils_run_full (argc, argv, 0);
}
