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
#include <libecal/libecal.h>

#include "e-test-server-utils.h"
#include "test-cal-cache-utils.h"

static void
test_fill_cache (TCUFixture *fixture,
		 ECalComponent **out_component)
{
	tcu_add_component_from_test_case (fixture, "event-1", out_component);
	tcu_add_component_from_test_case (fixture, "event-2", NULL);
	tcu_add_component_from_test_case (fixture, "event-5", NULL);
}

enum {
	EXPECT_DEFAULT		= (0),
	EXPECT_EVENT_1		= (1 << 0),
	EXPECT_EVENT_2		= (1 << 1),
	EXPECT_EVENT_3		= (1 << 2),
	EXPECT_EVENT_4		= (1 << 3),
	HAS_SEARCH_DATA		= (1 << 4),
	SKIP_COMPONENT_PUT	= (1 << 5)
};

static void
test_check_search_result (const GSList *list,
			  guint32 flags)
{
	gboolean expect_event_1 = (flags & EXPECT_EVENT_1) != 0;
	gboolean expect_event_2 = (flags & EXPECT_EVENT_2) != 0;
	gboolean expect_event_3 = (flags & EXPECT_EVENT_3) != 0;
	gboolean expect_event_4 = (flags & EXPECT_EVENT_4) != 0;
	gboolean has_search_data = (flags & HAS_SEARCH_DATA) != 0;
	gboolean have_event_1 = FALSE;
	gboolean have_event_2 = FALSE;
	gboolean have_event_3 = FALSE;
	gboolean have_event_4 = FALSE;
	gboolean have_event_5 = FALSE;
	const GSList *link;

	for (link = list; link; link = g_slist_next (link)) {
		const gchar *uid;

		if (has_search_data) {
			ECalCacheSearchData *sd = link->data;
			ECalComponent *component;

			g_assert_true (sd != NULL);
			g_assert_true (sd->uid != NULL);
			g_assert_true (sd->object != NULL);

			uid = sd->uid;

			component = e_cal_component_new_from_string (sd->object);
			g_assert_true (E_IS_CAL_COMPONENT (component));
			g_assert_cmpstr (uid, ==, e_cal_component_get_uid (component));
			g_assert_nonnull (i_cal_component_get_summary (e_cal_component_get_icalcomponent (component)));

			g_clear_object (&component);
		} else {
			const ECalComponentId *id = link->data;

			g_assert_true (id != NULL);
			g_assert_true (e_cal_component_id_get_uid (id) != NULL);

			uid = e_cal_component_id_get_uid (id);
		}

		g_assert_nonnull (uid);

		if (g_str_equal (uid, "event-1")) {
			g_assert_true (expect_event_1);
			g_assert_true (!have_event_1);
			have_event_1 = TRUE;
		} else if (g_str_equal (uid, "event-2")) {
			g_assert_true (!have_event_2);
			have_event_2 = TRUE;
		} else if (g_str_equal (uid, "event-3")) {
			g_assert_true (expect_event_3);
			g_assert_true (!have_event_3);
			have_event_3 = TRUE;
		} else if (g_str_equal (uid, "event-4")) {
			g_assert_true (expect_event_4);
			g_assert_true (!have_event_4);
			have_event_4 = TRUE;
		} else if (g_str_equal (uid, "event-5")) {
			g_assert_true (!have_event_5);
			have_event_5 = TRUE;
		} else {
			/* It's not supposed to be NULL, but it will print the value of 'uid' */
			g_assert_cmpstr (uid, ==, NULL);
		}
	}

	g_assert_true ((expect_event_1 && have_event_1) || (!expect_event_1 && !have_event_1));
	g_assert_true ((expect_event_2 && have_event_2) || (!expect_event_2 && !have_event_2));
	g_assert_true ((expect_event_3 && have_event_3) || (!expect_event_3 && !have_event_3));
	g_assert_true ((expect_event_4 && have_event_4) || (!expect_event_4 && !have_event_4));
	g_assert_true (have_event_5);
}

static void
test_basic_search (TCUFixture *fixture,
		   guint32 flags)
{
	GSList *list = NULL;
	const gchar *sexp;
	gint expect_total;
	GError *error = NULL;

	expect_total = 2 +
		((flags & EXPECT_EVENT_1) != 0 ? 1 : 0) +
		((flags & EXPECT_EVENT_3) != 0 ? 1 : 0) +
		((flags & EXPECT_EVENT_4) != 0 ? 1 : 0);

	/* All components first */
	g_assert_true (e_cal_cache_search (fixture->cal_cache, NULL, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, expect_total);
	test_check_search_result (list, flags | EXPECT_EVENT_2 | HAS_SEARCH_DATA);
	g_slist_free_full (list, e_cal_cache_search_data_free);
	list = NULL;

	g_assert_true (e_cal_cache_search_ids (fixture->cal_cache, NULL, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, expect_total);
	test_check_search_result (list, flags | EXPECT_EVENT_2);
	g_slist_free_full (list, e_cal_component_id_free);
	list = NULL;

	/* Only Party, aka event-5, as an in-summary query */
	sexp = "(has-categories? \"Holiday\")";

	g_assert_true (e_cal_cache_search (fixture->cal_cache, sexp, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, HAS_SEARCH_DATA);
	g_slist_free_full (list, e_cal_cache_search_data_free);
	list = NULL;

	g_assert_true (e_cal_cache_search_ids (fixture->cal_cache, sexp, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, EXPECT_DEFAULT);
	g_slist_free_full (list, e_cal_component_id_free);
	list = NULL;

	/* Only Party, aka event-5, as a non-summarised query */
	sexp = "(has-alarms-in-range? (make-time \"20091229T230000Z\") (make-time \"20091231T010000Z\"))";

	g_assert_true (e_cal_cache_search (fixture->cal_cache, sexp, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, HAS_SEARCH_DATA);
	g_slist_free_full (list, e_cal_cache_search_data_free);
	list = NULL;

	g_assert_true (e_cal_cache_search_ids (fixture->cal_cache, sexp, &list, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (list), ==, 1);
	test_check_search_result (list, EXPECT_DEFAULT);
	g_slist_free_full (list, e_cal_component_id_free);
	list = NULL;

	/* Invalid expression */
	g_assert_true (!e_cal_cache_search (fixture->cal_cache, "invalid expression here", &list, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_INVALID_QUERY);
	g_assert_null (list);
	g_clear_error (&error);

	g_assert_true (!e_cal_cache_search_ids (fixture->cal_cache, "invalid expression here", &list, NULL, &error));
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

	changes = e_cache_get_offline_changes (E_CACHE (fixture->cal_cache), NULL, &error);

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
		g_assert_true (g_hash_table_contains (expects, change->uid));

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

	offline_state = e_cache_get_offline_state (E_CACHE (fixture->cal_cache), uid, NULL, &error);
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
		       const gchar *summ_value)
{
	ECalComponent *component = NULL;
	GError *error = NULL;

	g_assert_true (e_cal_cache_get_component (fixture->cal_cache, uid, NULL, &component, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (component);
	g_assert_cmpstr (i_cal_component_get_summary (e_cal_component_get_icalcomponent (component)), ==, summ_value);

	g_clear_object (&component);
}

static void
test_verify_storage (TCUFixture *fixture,
		     const gchar *uid,
		     const gchar *expect_summ,
		     const gchar *expect_extra,
		     EOfflineState expect_offline_state)
{
	ECalComponent *component = NULL;
	EOfflineState offline_state;
	gchar *saved_extra = NULL;
	GError *error = NULL;

	if (expect_offline_state == E_OFFLINE_STATE_LOCALLY_DELETED ||
	    expect_offline_state == E_OFFLINE_STATE_UNKNOWN) {
		g_assert_true (!e_cal_cache_get_component (fixture->cal_cache, uid, NULL, &component, NULL, &error));
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_assert_null (component);

		g_clear_error (&error);
	} else {
		g_assert_true (e_cal_cache_get_component (fixture->cal_cache, uid, NULL, &component, NULL, &error));
		g_assert_no_error (error);
		g_assert_nonnull (component);
	}

	offline_state = test_check_offline_state (fixture, uid, expect_offline_state);

	if (offline_state == E_OFFLINE_STATE_UNKNOWN) {
		g_assert_true (!e_cal_cache_contains (fixture->cal_cache, uid, NULL, E_CACHE_EXCLUDE_DELETED));
		g_assert_true (!e_cal_cache_contains (fixture->cal_cache, uid, NULL, E_CACHE_INCLUDE_DELETED));
		test_check_offline_changes (fixture, NULL);
		return;
	}

	g_assert_true (e_cal_cache_get_component_extra (fixture->cal_cache, uid, NULL, &saved_extra, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpstr (saved_extra, ==, expect_extra);
	g_assert_cmpstr (i_cal_component_get_summary (e_cal_component_get_icalcomponent (component)), ==, expect_summ);

	g_clear_object (&component);
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
	ECalComponent *component = NULL;
	gint ii;
	const gchar *uid;
	gchar *saved_extra = NULL, *tmp;
	guint32 custom_flags;
	GSList *ids = NULL;
	GError *error = NULL;

	/* Basic ECache stuff */
	e_cache_set_version (E_CACHE (fixture->cal_cache), 123);
	g_assert_cmpint (e_cache_get_version (E_CACHE (fixture->cal_cache)), ==, 123);

	e_cache_set_revision (E_CACHE (fixture->cal_cache), "rev-321");
	tmp = e_cache_dup_revision (E_CACHE (fixture->cal_cache));
	g_assert_cmpstr ("rev-321", ==, tmp);
	g_free (tmp);

	g_assert_true (e_cache_set_key (E_CACHE (fixture->cal_cache), "my-key-str", "key-str-value", &error));
	g_assert_no_error (error);

	tmp = e_cache_dup_key (E_CACHE (fixture->cal_cache), "my-key-str", &error);
	g_assert_no_error (error);
	g_assert_cmpstr ("key-str-value", ==, tmp);
	g_free (tmp);

	g_assert_true (e_cache_set_key_int (E_CACHE (fixture->cal_cache), "version", 567, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_key_int (E_CACHE (fixture->cal_cache), "version", &error), ==, 567);
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_version (E_CACHE (fixture->cal_cache)), ==, 123);

	/* Add in online */
	test_fill_cache (fixture, &component);
	g_assert_nonnull (component);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	g_assert_true (e_cal_cache_set_component_extra (fixture->cal_cache, uid, NULL, "extra-0", NULL, &error));
	g_assert_no_error (error);

	g_assert_true (e_cal_cache_get_component_extra (fixture->cal_cache, uid, NULL, &saved_extra, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (saved_extra, ==, "extra-0");

	g_free (saved_extra);
	saved_extra = NULL;

	g_assert_true (e_cal_cache_get_ids_with_extra (fixture->cal_cache, "extra-0", &ids, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (ids), ==, 1);
	g_assert_nonnull (ids->data);
	g_assert_cmpstr (e_cal_component_id_get_uid (ids->data), ==, uid);

	g_slist_free_full (ids, e_cal_component_id_free);
	ids = NULL;

	i_cal_component_set_summary (e_cal_component_get_icalcomponent (component), "summ-0");

	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	test_check_offline_changes (fixture, NULL);

	/* Try change status */
	for (ii = 0; ii < G_N_ELEMENTS (states); ii++) {
		g_assert_true (e_cache_set_offline_state (E_CACHE (fixture->cal_cache), uid, states[ii], NULL, &error));
		g_assert_no_error (error);

		test_check_offline_state (fixture, uid, states[ii]);

		if (states[ii] != E_OFFLINE_STATE_SYNCED)
			test_check_offline_changes (fixture, uid, states[ii], NULL);

		if (states[ii] == E_OFFLINE_STATE_LOCALLY_DELETED) {
			g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
			g_assert_no_error (error);

			g_assert_true (!e_cal_cache_contains (fixture->cal_cache, uid, NULL, E_CACHE_EXCLUDE_DELETED));

			g_assert_true (e_cal_cache_set_component_extra (fixture->cal_cache, uid, NULL, "extra-1", NULL, &error));
			g_assert_no_error (error);

			g_assert_true (e_cal_cache_get_component_extra (fixture->cal_cache, uid, NULL, &saved_extra, NULL, &error));
			g_assert_no_error (error);
			g_assert_cmpstr (saved_extra, ==, "extra-1");

			g_free (saved_extra);
			saved_extra = NULL;

			g_assert_true (e_cal_cache_set_component_custom_flags (fixture->cal_cache, uid, NULL, 123, NULL, &error));
			g_assert_no_error (error);

			custom_flags = 0;
			g_assert_true (e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
			g_assert_no_error (error);
			g_assert_cmpint (custom_flags, ==, 123);

			/* Search when locally deleted */
			test_basic_search (fixture, EXPECT_DEFAULT);
		} else {
			g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
			g_assert_no_error (error);

			g_assert_true (e_cal_cache_contains (fixture->cal_cache, uid, NULL, E_CACHE_EXCLUDE_DELETED));

			/* Search when locally available */
			test_basic_search (fixture, EXPECT_EVENT_1);
		}

		g_assert_true (e_cal_cache_contains (fixture->cal_cache, uid, NULL, E_CACHE_INCLUDE_DELETED));

		g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
		g_assert_no_error (error);
	}

	test_check_offline_changes (fixture, NULL);

	/* Edit in online */
	i_cal_component_set_summary (e_cal_component_get_icalcomponent (component), "summ-1");

	g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, NULL, 0, E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	test_verify_storage (fixture, uid, "summ-1", NULL, E_OFFLINE_STATE_SYNCED);
	test_check_offline_changes (fixture, NULL);

	i_cal_component_set_summary (e_cal_component_get_icalcomponent (component), "summ-2");

	g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, "extra-2", 0, E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	test_verify_storage (fixture, uid, "summ-2", "extra-2", E_OFFLINE_STATE_SYNCED);
	test_check_offline_changes (fixture, NULL);

	g_assert_true (e_cal_cache_get_ids_with_extra (fixture->cal_cache, "extra-2", &ids, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (ids), ==, 1);
	g_assert_nonnull (ids->data);
	g_assert_cmpstr (e_cal_component_id_get_uid (ids->data), ==, uid);

	g_slist_free_full (ids, e_cal_component_id_free);
	ids = NULL;

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	g_assert_true (e_cal_cache_set_component_custom_flags (fixture->cal_cache, uid, NULL, 234, NULL, &error));
	g_assert_no_error (error);

	custom_flags = 0;
	g_assert_true (e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (custom_flags, ==, 234);

	/* Search before delete */
	test_basic_search (fixture, EXPECT_EVENT_1);

	/* Delete in online */
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 0, E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_true (!e_cache_set_offline_state (E_CACHE (fixture->cal_cache), uid, E_OFFLINE_STATE_LOCALLY_MODIFIED, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	test_verify_storage (fixture, uid, NULL, NULL, E_OFFLINE_STATE_UNKNOWN);
	test_check_offline_changes (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_no_error (error);

	g_assert_true (!e_cal_cache_set_component_extra (fixture->cal_cache, uid, NULL, "extra-3", NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	g_assert_true (!e_cal_cache_get_component_extra (fixture->cal_cache, uid, NULL, &saved_extra, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_assert_null (saved_extra);
	g_clear_error (&error);

	g_assert_true (!e_cal_cache_get_ids_with_extra (fixture->cal_cache, "extra-3", &ids, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_assert_null (ids);
	g_clear_error (&error);

	g_assert_true (!e_cal_cache_set_component_custom_flags (fixture->cal_cache, uid, NULL, 456, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	g_assert_true (!e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	g_clear_object (&component);

	/* Search after delete */
	test_basic_search (fixture, EXPECT_DEFAULT);
}

static void
test_offline_add_one (TCUFixture *fixture,
		      const gchar *case_name,
		      gint expect_total,
		      guint32 flags,
		      ECalComponent **out_component)
{
	ECalComponent *component = NULL;
	const gchar *uid;
	GError *error = NULL;

	if (!(flags & SKIP_COMPONENT_PUT)) {
		component = tcu_new_component_from_test_case (case_name);
		g_assert_nonnull (component);

		uid = e_cal_component_get_uid (component);
		g_assert_nonnull (uid);

		test_check_offline_state (fixture, uid, E_OFFLINE_STATE_UNKNOWN);

		/* Add a component in offline */
		g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
		g_assert_no_error (error);
	} else {
		uid = case_name;
	}

	if ((flags & EXPECT_EVENT_3) != 0) {
		test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_CREATED);
	} else {
		test_check_offline_state (fixture, uid, E_OFFLINE_STATE_UNKNOWN);
	}

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, expect_total);
	g_assert_no_error (error);

	test_basic_search (fixture, flags);

	if (out_component)
		*out_component = component;
	else
		g_clear_object (&component);
}

static void
test_offline_add (TCUFixture *fixture,
		  gconstpointer user_data)
{
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add the first in offline */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, NULL);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	/* Add the second in offline */
	test_offline_add_one (fixture, "event-4", 5, EXPECT_EVENT_3 | EXPECT_EVENT_4 | EXPECT_EVENT_1, NULL);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		"event-4", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);
}

static void
test_offline_add_edit (TCUFixture *fixture,
		       gconstpointer user_data)
{
	ECalComponent *component = NULL;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, &component);
	g_assert_nonnull (component);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	/* Modify added in offline */
	i_cal_component_set_summary (e_cal_component_get_icalcomponent (component), "summ-2");

	g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1 | SKIP_COMPONENT_PUT, NULL);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	test_check_edit_saved (fixture, "event-3", "summ-2");

	g_clear_object (&component);
}

static void
test_offline_add_delete (TCUFixture *fixture,
			 gconstpointer user_data)
{
	ECalComponent *component = NULL;
	guint32 custom_flags = 0;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, &component);
	g_assert_nonnull (component);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	/* Delete added in offline */

	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 1, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_true (!e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_clear_error (&error);

	test_offline_add_one (fixture, "event-3", 3, EXPECT_EVENT_1 | SKIP_COMPONENT_PUT, NULL);

	test_check_offline_changes (fixture, NULL);

	/* Add in online */

	g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, NULL, 333, E_CACHE_IS_ONLINE, NULL, &error));
	g_assert_no_error (error);

	custom_flags = 0;
	g_assert_true (e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (custom_flags, ==, 333);

	/* Delete in offline */

	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 246, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	custom_flags = 0;
	g_assert_true (e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (custom_flags, ==, 246);

	g_clear_object (&component);
}

static void
test_offline_add_delete_add (TCUFixture *fixture,
			     gconstpointer user_data)
{
	ECalComponent *component = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, &component);
	g_assert_nonnull (component);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	/* Delete added in offline */
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	test_offline_add_one (fixture, "event-3", 3, EXPECT_EVENT_1 | SKIP_COMPONENT_PUT, NULL);

	test_check_offline_changes (fixture, NULL);

	g_clear_object (&component);

	/* Add in offline again */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, NULL);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);
}

static void
test_offline_add_resync (TCUFixture *fixture,
			 gconstpointer user_data)
{
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);

	/* Add in offline */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, NULL);

	test_check_offline_changes (fixture,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	/* Resync all offline changes */
	g_assert_true (e_cache_clear_offline_changes (E_CACHE (fixture->cal_cache), NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 4);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_EVENT_3 | EXPECT_EVENT_1);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, "event-3", E_OFFLINE_STATE_SYNCED);
}

static void
test_offline_edit_common (TCUFixture *fixture,
			  gchar **out_uid)
{
	ECalComponent *component = NULL;
	guint32 custom_flags = 0;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &component);
	g_assert_nonnull (component);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	custom_flags = 0;
	g_assert_true (e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (custom_flags, ==, 0);

	/* Modify in offline */
	i_cal_component_set_summary (e_cal_component_get_icalcomponent (component), "summ-2");

	g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, NULL, 369, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	custom_flags = 0;
	g_assert_true (e_cal_cache_get_component_custom_flags (fixture->cal_cache, uid, NULL, &custom_flags, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (custom_flags, ==, 369);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_edit_saved (fixture, uid, "summ-2");

	test_basic_search (fixture, EXPECT_EVENT_1);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_MODIFIED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_MODIFIED);

	if (out_uid)
		*out_uid = g_strdup (uid);

	g_clear_object (&component);
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
	ECalComponent *component = NULL;
	gchar *uid = NULL;
	GError *error = NULL;

	test_offline_edit_common (fixture, &uid);

	/* Delete the modified component in offline */
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	g_assert_true (!e_cal_cache_get_component (fixture->cal_cache, uid, FALSE, &component, NULL, &error));
	g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
	g_assert_null (component);

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
	g_assert_true (e_cache_clear_offline_changes (E_CACHE (fixture->cal_cache), NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_EVENT_1);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	g_free (uid);
}

static void
test_offline_delete (TCUFixture *fixture,
		     gconstpointer user_data)
{
	ECalComponent *component = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &component);
	g_assert_nonnull (component);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Delete in offline */
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	g_clear_object (&component);
}

static void
test_offline_delete_add (TCUFixture *fixture,
			 gconstpointer user_data)
{
	ECalComponent *component = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &component);
	g_assert_nonnull (component);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Delete locally created in offline */
	test_offline_add_one (fixture, "event-3", 4, EXPECT_EVENT_3 | EXPECT_EVENT_1, NULL);
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, "event-3", NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_EVENT_1);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);
	test_check_offline_state (fixture, "event-3", E_OFFLINE_STATE_UNKNOWN);

	/* Delete synced in offline */
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	/* Add one in offline */
	test_offline_add_one (fixture, "event-3", 3, EXPECT_EVENT_3, NULL);

	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);

	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);
	test_check_offline_state (fixture, "event-3", E_OFFLINE_STATE_LOCALLY_CREATED);

	/* Modify the previous component and add it again */
	i_cal_component_set_summary (e_cal_component_get_icalcomponent (component), "summ-3");

	g_assert_true (e_cal_cache_put_component (fixture->cal_cache, component, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 4);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 4);
	g_assert_no_error (error);

	test_check_edit_saved (fixture, uid, "summ-3");

	test_basic_search (fixture, EXPECT_EVENT_1 | EXPECT_EVENT_3);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_MODIFIED,
		"event-3", E_OFFLINE_STATE_LOCALLY_CREATED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_MODIFIED);
	test_check_offline_state (fixture, "event-3", E_OFFLINE_STATE_LOCALLY_CREATED);

	g_clear_object (&component);
}

static void
test_offline_delete_resync (TCUFixture *fixture,
			    gconstpointer user_data)
{
	ECalComponent *component = NULL;
	const gchar *uid;
	GError *error = NULL;

	/* Add in online */
	test_fill_cache (fixture, &component);
	g_assert_nonnull (component);

	uid = e_cal_component_get_uid (component);
	g_assert_nonnull (uid);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_SYNCED);

	/* Delete in offline */
	g_assert_true (e_cal_cache_remove_component (fixture->cal_cache, uid, NULL, 0, E_CACHE_IS_OFFLINE, NULL, &error));
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 3);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture,
		uid, E_OFFLINE_STATE_LOCALLY_DELETED,
		NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_LOCALLY_DELETED);

	/* Resync all offline changes */
	e_cache_clear_offline_changes (E_CACHE (fixture->cal_cache), NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 2);
	g_assert_no_error (error);

	test_basic_search (fixture, EXPECT_DEFAULT);
	test_check_offline_changes (fixture, NULL);
	test_check_offline_state (fixture, uid, E_OFFLINE_STATE_UNKNOWN);

	g_clear_object (&component);
}

gint
main (gint argc,
      gchar **argv)
{
	TCUClosure closure = { TCU_LOAD_COMPONENT_SET_NONE };

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	/* Ensure that the client and server get the same locale */
	g_assert_true (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/ECalCache/Offline/Basics", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_basics, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/Add", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/AddEdit", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_edit, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/AddDelete", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_delete, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/AddDeleteAdd", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_delete_add, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/AddResync", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_add_resync, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/Edit", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_edit, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/EditDelete", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_edit_delete, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/EditResync", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_edit_resync, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/Delete", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_delete, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/DeleteAdd", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_delete_add, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Offline/DeleteResync", TCUFixture, &closure,
		tcu_fixture_setup, test_offline_delete_resync, tcu_fixture_teardown);

	return e_test_server_utils_run_full (argc, argv, 0);
}
