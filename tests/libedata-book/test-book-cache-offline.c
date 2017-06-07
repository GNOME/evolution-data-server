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
#include <locale.h>
#include <libebook/libebook.h>

#include "test-book-cache-utils.h"

static void
test_fill_cache (TCUFixture *fixture,
		 EContact **out_contact)
{
	tcu_add_contact_from_test_case (fixture, "custom-1", out_contact);
	tcu_add_contact_from_test_case (fixture, "custom-3", NULL);
	tcu_add_contact_from_test_case (fixture, "custom-9", NULL);
}

enum {
	EXPECT_DEFAULT		= (0),
	EXPECT_CUSTOM_1		= (1 << 0),
	EXPECT_CUSTOM_9		= (1 << 1),
	EXPECT_SIMPLE_1		= (1 << 2),
	EXPECT_SIMPLE_2		= (1 << 3),
	HAS_SEARCH_DATA		= (1 << 4),
	HAS_META_CONTACTS	= (1 << 5),
	SKIP_CONTACT_PUT	= (1 << 6)
};

static void
test_check_search_result (const GSList *list,
			  guint32 flags)
{
	gboolean expect_custom_1 = (flags & EXPECT_CUSTOM_1) != 0;
	gboolean expect_custom_9 = (flags & EXPECT_CUSTOM_9) != 0;
	gboolean expect_simple_1 = (flags & EXPECT_SIMPLE_1) != 0;
	gboolean expect_simple_2 = (flags & EXPECT_SIMPLE_2) != 0;
	gboolean has_search_data = (flags & HAS_SEARCH_DATA) != 0;
	gboolean has_meta_contacts = (flags & HAS_META_CONTACTS) != 0;
	gboolean have_custom_1 = FALSE;
	gboolean have_custom_3 = FALSE;
	gboolean have_custom_9 = FALSE;
	gboolean have_simple_1 = FALSE;
	gboolean have_simple_2 = FALSE;
	const GSList *link;

	for (link = list; link; link = g_slist_next (link)) {
		const gchar *uid;

		if (has_search_data) {
			EBookCacheSearchData *sd = link->data;
			EContact *contact;

			g_assert (sd != NULL);
			g_assert (sd->uid != NULL);
			g_assert (sd->vcard != NULL);

			uid = sd->uid;

			contact = e_contact_new_from_vcard (sd->vcard);
			g_assert (E_IS_CONTACT (contact));
			g_assert_cmpstr (uid, ==, e_contact_get_const (contact, E_CONTACT_UID));

			if (has_meta_contacts) {
				g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_REV));
				g_assert_null (e_contact_get_const (contact, E_CONTACT_EMAIL_1));
			} else {
				g_assert_nonnull (e_contact_get_const (contact, E_CONTACT_EMAIL_1));
			}

			g_clear_object (&contact);
		} else {
			uid = link->data;
		}

		g_assert_nonnull (uid);

		if (g_str_equal (uid, "custom-1")) {
			g_assert (expect_custom_1);
			g_assert (!have_custom_1);
			have_custom_1 = TRUE;
		} else if (g_str_equal (uid, "custom-3")) {
			g_assert (!have_custom_3);
			have_custom_3 = TRUE;
		} else if (g_str_equal (uid, "custom-9")) {
			g_assert (expect_custom_9);
			g_assert (!have_custom_9);
			have_custom_9 = TRUE;
		} else if (g_str_equal (uid, "simple-1")) {
			g_assert (expect_simple_1);
			g_assert (!have_simple_1);
			have_simple_1 = TRUE;
		} else if (g_str_equal (uid, "simple-2")) {
			g_assert (expect_simple_2);
			g_assert (!have_simple_2);
			have_simple_2 = TRUE;
		} else {
			/* It's not supposed to be NULL, but it will print the value of 'uid' */
			g_assert_cmpstr (uid, ==, NULL);
		}
	}

	g_assert ((expect_custom_1 && have_custom_1) || (!expect_custom_1 && !have_custom_1));
	g_assert ((expect_custom_9 && have_custom_9) || (!expect_custom_9 && !have_custom_9));
	g_assert ((expect_simple_1 && have_simple_1) || (!expect_simple_1 && !have_simple_1));
	g_assert ((expect_simple_2 && have_simple_2) || (!expect_simple_2 && !have_simple_2));
	g_assert (have_custom_3);
}

static void
test_basic_cursor (TCUFixture *fixture,
		   guint32 flags,
		   const gchar *sexp)
{
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };
	EBookCacheCursor *cursor;
	gint total = -1, position = -1, expect_total;
	GSList *list;
	GError *error = NULL;

	expect_total = 1 +
		(((flags & EXPECT_CUSTOM_1) != 0) ? 1 : 0) +
		(((flags & EXPECT_CUSTOM_9) != 0) ? 1 : 0) +
		(((flags & EXPECT_SIMPLE_1) != 0) ? 1 : 0) +
		(((flags & EXPECT_SIMPLE_2) != 0) ? 1 : 0);

	cursor = e_book_cache_cursor_new (fixture->book_cache, sexp, sort_fields, sort_types, 2, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cursor);

	g_assert (e_book_cache_cursor_calculate (fixture->book_cache, cursor, &total, &position, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (total, ==, expect_total);
	g_assert_cmpint (position, ==, 0);

	g_assert_cmpint (e_book_cache_cursor_step (fixture->book_cache, cursor, E_BOOK_CACHE_CURSOR_STEP_FETCH,
		E_BOOK_CACHE_CURSOR_ORIGIN_CURRENT, total, &list, NULL, &error), ==, total);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, total);

	test_check_search_result (list, flags | HAS_SEARCH_DATA);

	g_slist_free_full (list, e_book_cache_search_data_free);
	e_book_cache_cursor_free (fixture->book_cache, cursor);
}

static void
test_basic_search (TCUFixture *fixture,
		   guint32 flags)
{
	EBookQuery *query;
	GSList *list = NULL;
	gchar *sexp;
	gint expect_total;
	GError *error = NULL;

	expect_total = 2 +
		((flags & EXPECT_CUSTOM_1) != 0 ? 1 : 0) +
		((flags & EXPECT_SIMPLE_1) != 0 ? 1 : 0) +
		((flags & EXPECT_SIMPLE_2) != 0 ? 1 : 0);

	/* All contacts first */
	g_assert (e_book_cache_search (fixture->book_cache, NULL, FALSE, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, expect_total);
	test_check_search_result (list, flags | EXPECT_CUSTOM_9 | HAS_SEARCH_DATA);
	g_slist_free_full (list, e_book_cache_search_data_free);
	list = NULL;

	g_assert (e_book_cache_search (fixture->book_cache, NULL, TRUE, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, expect_total);
	test_check_search_result (list, flags | EXPECT_CUSTOM_9 | HAS_SEARCH_DATA | HAS_META_CONTACTS);
	g_slist_free_full (list, e_book_cache_search_data_free);
	list = NULL;

	g_assert (e_book_cache_search_uids (fixture->book_cache, NULL, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, expect_total);
	test_check_search_result (list, flags | EXPECT_CUSTOM_9);
	g_slist_free_full (list, g_free);
	list = NULL;

	test_basic_cursor (fixture, flags | EXPECT_CUSTOM_9, NULL);

	/* Only Brown, aka custom-3, as an autocomplete query */
	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_CONTAINS, "Brown");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	g_assert (e_book_cache_search (fixture->book_cache, sexp, FALSE, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, HAS_SEARCH_DATA);
	g_slist_free_full (list, e_book_cache_search_data_free);
	list = NULL;

	g_assert (e_book_cache_search (fixture->book_cache, sexp, TRUE, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, HAS_SEARCH_DATA | HAS_META_CONTACTS);
	g_slist_free_full (list, e_book_cache_search_data_free);
	list = NULL;

	g_assert (e_book_cache_search_uids (fixture->book_cache, sexp, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, EXPECT_DEFAULT);
	g_slist_free_full (list, g_free);
	list = NULL;

	test_basic_cursor (fixture, EXPECT_DEFAULT, sexp);

	g_free (sexp);

	/* Only Brown, aka custom-3, as a regular query */
	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_CONTAINS, "brown");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	g_assert (e_book_cache_search (fixture->book_cache, sexp, FALSE, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, HAS_SEARCH_DATA);
	g_slist_free_full (list, e_book_cache_search_data_free);
	list = NULL;

	g_assert (e_book_cache_search (fixture->book_cache, sexp, TRUE, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, HAS_SEARCH_DATA | HAS_META_CONTACTS);
	g_slist_free_full (list, e_book_cache_search_data_free);
	list = NULL;

	g_assert (e_book_cache_search_uids (fixture->book_cache, sexp, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, EXPECT_DEFAULT);
	g_slist_free_full (list, g_free);
	list = NULL;

	test_basic_cursor (fixture, EXPECT_DEFAULT, sexp);

	g_free (sexp);

	/* Invalid expression */
	g_assert (!e_book_cache_search (fixture->book_cache, "invalid expression here", TRUE, &list, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_INVALID_QUERY);
	g_assert_null (list);
	g_clear_error (&error);

	g_assert (!e_book_cache_search_uids (fixture->book_cache, "invalid expression here", &list, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_INVALID_QUERY);
	g_assert_null (list);
	g_clear_error (&error);
}

/* Expects pairs of UID (gchar *) and EOfflineState (gint), terminated by NULL */
static void
test_check_offline_changes (TCUFixture *fixture,
			    ...) G_GNUC_NULL_TERMINATED;

static void
test_check_offline_changes (TCUFixture *fixture,
			    ...)
{
	GSList *changes, *link;
	va_list args;
	GHashTable *expects;
	const gchar *uid;
	GError *error = NULL;

	changes = e_cache_get_offline_changes (E_CACHE (fixture->book_cache), NULL, &error);

	g_assert_no_error (error);

	expects = g_hash_table_new (g_str_hash, g_str_equal);

	va_start (args, fixture);
	uid = va_arg (args, const gchar *);
	while (uid) {
		gint state = va_arg (args, gint);

		g_hash_table_insert (expects, (gpointer) uid, GINT_TO_POINTER (state));
		uid = va_arg (args, const gchar *);
	}
	va_end (args);

	g_assert_cmpint (g_slist_length (changes), ==, g_hash_table_size (expects));

	for (link = changes; link; link = g_slist_next (link)) {
		ECacheOfflineChange *change = link->data;
		gint expect_state;

		g_assert_nonnull (change);
		g_assert (g_hash_table_contains (expects, change->uid));

		expect_state = GPOINTER_TO_INT (g_hash_table_lookup (expects, change->uid));
		g_assert_cmpint (expect_state, ==, change->state);
	}

	g_slist_free_full (changes, e_cache_offline_change_free);
	g_hash_table_destroy (expects);
}

static EOfflineState
test_check_offline_state (TCUFixture *fixture,
			  const gchar *uid,
			  EOfflineState expect_offline_state)
{
	EOfflineState offline_state;
	GError *error = NULL;

	offline_state = e_cache_get_offline_state (E_CACHE (fixture->book_cache), uid, NULL, &error);
	g_assert_cmpint (offline_state, ==, expect_offline_state);

	if (offline_state == E_OFFLINE_STATE_UNKNOWN) {
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
	}

	return offline_state;
}

static void
test_check_edit_saved (TCUFixture *fixture,
		       const gchar *uid,
		       const gchar *rev_value)
{
	EContact *contact = NULL;
	GError *error = NULL;

	g_assert (e_book_cache_get_contact (fixture->book_cache, uid, FALSE, &contact, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (contact);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_REV), ==, rev_value);

	g_clear_object (&contact);

	g_assert (e_book_cache_get_contact (fixture->book_cache, uid, TRUE, &contact, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (contact);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_REV), ==, rev_value);

	g_clear_object (&contact);
}

static void
test_verify_storage (TCUFixture *fixture,
		     const gchar *uid,
		     const gchar *expect_rev,
		     const gchar *expect_extra,
		     EOfflineState expect_offline_state)
{
	EContact *contact = NULL;
	EOfflineState offline_state;
	gchar *vcard, *saved_rev = NULL, *saved_extra = NULL;
	GError *error = NULL;

	if (expect_offline_state == E_OFFLINE_STATE_LOCALLY_DELETED ||
	    expect_offline_state == E_OFFLINE_STATE_UNKNOWN) {
		g_assert (!e_book_cache_get_contact (fixture->book_cache, uid, FALSE, &contact, NULL, &error));
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_assert_null (contact);

		g_clear_error (&error);
	} else {
		g_assert (e_book_cache_get_contact (fixture->book_cache, uid, FALSE, &contact, NULL, &error));
		g_assert_no_error (error);
		g_assert_nonnull (contact);
	}

	offline_state = test_check_offline_state (fixture, uid, expect_offline_state);

	if (offline_state == E_OFFLINE_STATE_UNKNOWN) {
		g_assert (!e_cache_contains (E_CACHE (fixture->book_cache), uid, E_CACHE_EXCLUDE_DELETED));
		g_assert (!e_cache_contains (E_CACHE (fixture->book_cache), uid, E_CACHE_INCLUDE_DELETED));
		test_check_offline_changes (fixture, NULL);
		return;
	}

	g_assert (e_book_cache_get_contact_extra (fixture->book_cache, uid, &saved_extra, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpstr (saved_extra, ==, expect_extra);
	g_assert_cmpstr (e_contact_get_const (contact, E_CONTACT_REV), ==, expect_rev);

	g_clear_object (&contact);

	vcard = e_cache_get (E_CACHE (fixture->book_cache), uid, &saved_rev, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (vcard);
	g_assert_nonnull (saved_rev);

	g_assert_cmpstr (saved_rev, ==, expect_rev);

	g_free (vcard);
	g_free (saved_rev);
	g_free (saved_extra);

	if (expect_offline_state == E_OFFLINE_STATE_SYNCED)
		test_check_offline_changes (fixture, NULL);
	else
		test_check_offline_changes (fixture, uid, expect_offline_state, NULL);
}

static void
test_offline_basics (TCUFixture *fixture,
		     gconstpointer user_data)
{
	EOfflineState states[] = {
		E_OFFLINE_STATE_LOCALLY_CREATED,
		E_OFFLINE_STATE_LOCALLY_MODIFIED,
		E_OFFLINE_STATE_LOCALLY_DELETED,
		E_OFFLINE_STATE_SYNCED
	};
	EContact *contact = NULL;
	gint ii;
	const gchar *uid;
	gchar *saved_extra = NULL, *tmp;
	GSList *uids = NULL;
	GError *error = NULL;

	/* Basic ECache stuff */
	e_cache_set_version (E_CACHE (fixture->book_cache), 123);
	g_assert_cmpint (e_cache_get_version (E_CACHE (fixture->book_cache)), ==, 123);

	e_cache_set_revision (E_CACHE (fixture->book_cache), "rev-321");
	tmp = e_cache_dup_revision (E_CACHE (fixture->book_cache));
	g_assert_cmpstr ("rev-321", ==, tmp);
	g_free (tmp);

	g_assert (e_cache_set_key (E_CACHE (fixture->book_cache), "my-key-str", "key-str-value", &error));
	g_assert_no_error (error);

	tmp = e_cache_dup_key (E_CACHE (fixture->book_cache), "my-key-str", &error);
	g_assert_no_error (error);
	g_assert_cmpstr ("key-str-value", ==, tmp);
	g_free (tmp);

	g_assert (e_cache_set_key_int (E_CACHE (fixture->book_cache), "version", 567, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_key_int (E_CACHE (fixture->book_cache), "version", &error), ==, 567);
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_version (E_CACHE (fixture->book_cache)), ==, 123);

	/* Add in online */
	test_fill_cache (fixture, &contact);
	g_assert_nonnull (contact);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	g_assert (e_book_cache_set_contact_extra (fixture->book_cache, uid, "extra-0", NULL, &error));
	g_assert_no_error (error);

	g_assert (e_book_cache_get_contact_extra (fixture->book_cache, uid, &saved_extra, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (saved_extra, ==, "extra-0");

	g_free (saved_extra);
	saved_extra = NULL;

	g_assert (e_book_cache_get_uids_with_extra (fixture->book_cache, "extra-0", &uids, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (uids), ==, 1);
	g_assert_cmpstr (uids->data, ==, uid);

	g_slist_free_full (uids, g_free);
	uids = NULL;

	e_contact_set (contact, E_CONTACT_REV, "rev-0");

	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	test_check_offline_changes (fixture, NULL);

	/* Try change status */
	for (ii = 0; ii < G_N_ELEMENTS (states); ii++) {
		g_assert (e_cache_set_offline_state (E_CACHE (fixture->book_cache), uid, states[ii], NULL, &error));
		g_assert_no_error (error);

		test_check_offline_state (fixture, uid, states[ii]);

		if (states[ii] != E_OFFLINE_STATE_SYNCED)
			test_check_offline_changes (fixture, uid, states[ii], NULL);

		if (states[ii] == E_OFFLINE_STATE_LOCALLY_DELETED) {
			g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
			g_assert_no_error (error);

			g_assert (!e_cache_contains (E_CACHE (fixture->book_cache), uid, E_CACHE_EXCLUDE_DELETED));

			g_assert (e_book_cache_set_contact_extra (fixture->book_cache, uid, "extra-1", NULL, &error));
			g_assert_no_error (error);

			g_assert (e_book_cache_get_contact_extra (fixture->book_cache, uid, &saved_extra, NULL, &error));
			g_assert_no_error (error);
			g_assert_cmpstr (saved_extra, ==, "extra-1");

			g_free (saved_extra);
			saved_extra = NULL;

			/* Search when locally deleted */
			test_basic_search (fixture, EXPECT_DEFAULT);
		} else {
			g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
			g_assert_no_error (error);

			g_assert (e_cache_contains (E_CACHE (fixture->book_cache), uid, E_CACHE_EXCLUDE_DELETED));

			/* Search when locally available */
			test_basic_search (fixture, EXPECT_CUSTOM_1);
		}

		g_assert (e_cache_contains (E_CACHE (fixture->book_cache), uid, E_CACHE_INCLUDE_DELETED));

		g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
		g_assert_no_error (error);
	}

	test_check_offline_changes (fixture, NULL);

	/* Edit in online */
	e_contact_set (contact, E_CONTACT_REV, "rev-1");

	g_assert (e_book_cache_put_contact (fixture->book_cache, contact, NULL, E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	test_verify_storage (fixture, uid, "rev-1", NULL, E_OFFLINE_STATE_SYNCED);
	test_check_offline_changes (fixture, NULL);

	e_contact_set (contact, E_CONTACT_REV, "rev-2");

	g_assert (e_book_cache_put_contact (fixture->book_cache, contact, "extra-2", E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	test_verify_storage (fixture, uid, "rev-2", "extra-2", E_OFFLINE_STATE_SYNCED);
	test_check_offline_changes (fixture, NULL);

	g_assert (e_book_cache_get_uids_with_extra (fixture->book_cache, "extra-2", &uids, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (uids), ==, 1);
	g_assert_cmpstr (uids->data, ==, uid);

	g_slist_free_full (uids, g_free);
	uids = NULL;

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	/* Search before delete */
	test_basic_search (fixture, EXPECT_CUSTOM_1);

	/* Delete in online */
	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert (!e_cache_set_offline_state (E_CACHE (fixture->book_cache), uid, E_OFFLINE_STATE_LOCALLY_MODIFIED, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	test_verify_storage (fixture, uid, NULL, NULL, E_OFFLINE_STATE_UNKNOWN);
	test_check_offline_changes (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_no_error (error);

	g_assert (!e_book_cache_set_contact_extra (fixture->book_cache, uid, "extra-3", NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	g_assert (!e_book_cache_get_contact_extra (fixture->book_cache, uid, &saved_extra, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_assert_null (saved_extra);
	g_clear_error (&error);

	g_assert (!e_book_cache_get_uids_with_extra (fixture->book_cache, "extra-3", &uids, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_assert_null (uids);
	g_clear_error (&error);

	g_clear_object (&contact);

	/* Search after delete */
	test_basic_search (fixture, EXPECT_DEFAULT);
}

static void
test_offline_add_one (TCUFixture *fixture,
		      const gchar *case_name,
		      gint expect_total,
		      guint32 flags,
		      EContact **out_contact)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	if (!(flags & SKIP_CONTACT_PUT)) {
		contact = tcu_new_contact_from_test_case (case_name);
		g_assert_nonnull (contact);

		uid = e_contact_get_const (contact, E_CONTACT_UID);
		g_assert_nonnull (uid);

		test_check_offline_state (fixture, uid, E_OFFLINE_STATE_UNKNOWN);

		/* Add a contact in offline */
		g_assert (e_book_cache_put_contact (fixture->book_cache, contact, NULL, E_CACHE_IS_OFFLINE, NULL, &error));
		g_assert_no_error (error);
	} else {
		uid = case_name;
	}

	if ((flags & EXPECT_SIMPLE_1) != 0) {
		test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_CREATED);
	} else {
		test_check_offline_state (fixture, uid, E_OFFLINE_STATE_UNKNOWN);
	}

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, expect_total);
	g_assert_no_error (error);

	test_basic_search (fixture, flags);

	if (out_contact)
		*out_contact = contact;
	else
		g_clear_object (&contact);
}

static void
test_offline_add (TCUFixture *fixture,
		  gconstpointer user_data)
{
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add the first in offline */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, NULL);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	/* Add the second in offline */
	test_offline_add_one (fixture, "simple-2", 5, EXPECT_SIMPLE_1 | EXPECT_SIMPLE_2 | EXPECT_CUSTOM_1, NULL);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		"simple-2", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);
}

static void
test_offline_add_edit (TCUFixture *fixture,
		       gconstpointer user_data)
{
	EContact *contact = NULL;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, &contact);
	g_assert_nonnull (contact);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	/* Modify added in offline */
	e_contact_set (contact, E_CONTACT_REV, "rev-2");

	g_assert (e_book_cache_put_contact (fixture->book_cache, contact, NULL, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1 | SKIP_CONTACT_PUT, NULL);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	test_check_edit_saved (fixture, "simple-1", "rev-2");

	g_clear_object (&contact);
}

static void
test_offline_add_delete (TCUFixture *fixture,
			 gconstpointer user_data)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, &contact);
	g_assert_nonnull (contact);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	/* Delete added in offline */

	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	test_offline_add_one (fixture, "simple-1", 3, EXPECT_CUSTOM_1 | SKIP_CONTACT_PUT, NULL);

	test_check_offline_changes (fixture, NULL);

	g_clear_object (&contact);
}

static void
test_offline_add_delete_add (TCUFixture *fixture,
			     gconstpointer user_data)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, &contact);
	g_assert_nonnull (contact);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	/* Delete added in offline */
	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	test_offline_add_one (fixture, "simple-1", 3, EXPECT_CUSTOM_1 | SKIP_CONTACT_PUT, NULL);

	test_check_offline_changes (fixture, NULL);

	g_clear_object (&contact);

	/* Add in offline again */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, NULL);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);
}

static void
test_offline_add_resync (TCUFixture *fixture,
			 gconstpointer user_data)
{
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, NULL);

	test_check_offline_changes (fixture,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	/* Resync all offline changes */
	g_assert (e_cache_clear_offline_changes (E_CACHE (fixture->book_cache), NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 4);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, "simple-1", E_OFFLINE_STATE_SYNCED);
}

static void
test_offline_edit_common (TCUFixture *fixture,
			  gchar **out_uid)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &contact);
	g_assert_nonnull (contact);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Modify in offline */
	e_contact_set (contact, E_CONTACT_REV, "rev-2");

	g_assert (e_book_cache_put_contact (fixture->book_cache, contact, NULL, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_edit_saved (fixture, uid, "rev-2");

	test_basic_search (fixture, EXPECT_CUSTOM_1);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_MODIFIED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_MODIFIED);

	if (out_uid)
		*out_uid = g_strdup (uid);

	g_clear_object (&contact);
}

static void
test_offline_edit (TCUFixture *fixture,
		   gconstpointer user_data)
{
	test_offline_edit_common (fixture, NULL);
}

static void
test_offline_edit_delete (TCUFixture *fixture,
			  gconstpointer user_data)
{
	EContact *contact = NULL;
	gchar *uid = NULL;
	GError *error = NULL;

	test_offline_edit_common (fixture, &uid);

	/* Delete the modified contact in offline */
	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	g_assert (!e_book_cache_get_contact (fixture->book_cache, uid, FALSE, &contact, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_assert_null (contact);

	g_clear_error (&error);
	g_free (uid);
}

static void
test_offline_edit_resync (TCUFixture *fixture,
			  gconstpointer user_data)
{
	gchar *uid = NULL;
	GError *error = NULL;

	test_offline_edit_common (fixture, &uid);

	/* Resync all offline changes */
	g_assert (e_cache_clear_offline_changes (E_CACHE (fixture->book_cache), NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_CUSTOM_1);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	g_free (uid);
}

static void
test_offline_delete (TCUFixture *fixture,
		     gconstpointer user_data)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &contact);
	g_assert_nonnull (contact);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Delete in offline */
	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	g_clear_object (&contact);
}

static void
test_offline_delete_add (TCUFixture *fixture,
			 gconstpointer user_data)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &contact);
	g_assert_nonnull (contact);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Delete locally created in offline */
	test_offline_add_one (fixture, "simple-1", 4, EXPECT_SIMPLE_1 | EXPECT_CUSTOM_1, NULL);
	g_assert (e_book_cache_remove_contact (fixture->book_cache, "simple-1", E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_CUSTOM_1);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);
	test_check_offline_state (fixture, "simple-1", E_OFFLINE_STATE_UNKNOWN);

	/* Delete synced in offline */
	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	/* Add one in offline */
	test_offline_add_one (fixture, "simple-1", 3, EXPECT_SIMPLE_1, NULL);

	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);
	test_check_offline_state (fixture, "simple-1", E_OFFLINE_STATE_LOCALLY_CREATED);

	/* Modify the previous contact and add it again */
	e_contact_set (contact, E_CONTACT_REV, "rev-3");

	g_assert (e_book_cache_put_contact (fixture->book_cache, contact, NULL, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 4);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 4);
	g_assert_no_error (error);

	test_check_edit_saved (fixture, uid, "rev-3");

	test_basic_search (fixture, EXPECT_CUSTOM_1 | EXPECT_SIMPLE_1);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_MODIFIED,
		"simple-1", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_MODIFIED);
	test_check_offline_state (fixture, "simple-1", E_OFFLINE_STATE_LOCALLY_CREATED);

	g_clear_object (&contact);
}

static void
test_offline_delete_resync (TCUFixture *fixture,
			    gconstpointer user_data)
{
	EContact *contact = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &contact);
	g_assert_nonnull (contact);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Delete in offline */
	g_assert (e_book_cache_remove_contact (fixture->book_cache, uid, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	/* Resync all offline changes */
	e_cache_clear_offline_changes (E_CACHE (fixture->book_cache), NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->book_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_UNKNOWN);

	g_clear_object (&contact);
}

gint
main (gint argc,
      gchar **argv)
{
	TCUClosure closure = { NULL };

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/EBookCache/Offline/Basics", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_basics, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/Add", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/AddEdit", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_edit, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/AddDelete", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_delete, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/AddDeleteAdd", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_delete_add, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/AddResync", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_resync, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/Edit", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_edit, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/EditDelete", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_edit_delete, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/EditResync", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_edit_resync, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/Delete", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_delete, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/DeleteAdd", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_delete_add, tcu_fixture_teardown);
	g_test_add ("/EBookCache/Offline/DeleteResync", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_delete_resync, tcu_fixture_teardown);

	return g_test_run ();
}
