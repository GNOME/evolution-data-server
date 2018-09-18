/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

#include <stdlib.h>
#include <locale.h>
#include <libecal/libecal.h>

#include "e-test-server-utils.h"
#include "test-cal-cache-utils.h"

#define dd(x)

static GHashTable *
test_search_manual (ECalCache *cal_cache,
		    const gchar *expr)
{
	GSList *components = NULL, *link;
	GHashTable *res;
	ECalBackendSExp *sexp;
	ETimezoneCache *zone_cache;
	gboolean success;
	GError *error = NULL;

	res = g_hash_table_new_full ((GHashFunc) e_cal_component_id_hash, (GEqualFunc) e_cal_component_id_equal,
		(GDestroyNotify) e_cal_component_free_id, g_object_unref);

	zone_cache = E_TIMEZONE_CACHE (cal_cache);

	/* Get all the components stored in the summary. */
	success = e_cal_cache_search_components	(cal_cache, NULL, &components, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (components);

	sexp = e_cal_backend_sexp_new (expr);
	g_assert (sexp != NULL);

	for (link = components; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;

		if (e_cal_backend_sexp_match_comp (sexp, comp, zone_cache)) {
			ECalComponentId *id = NULL;

			id = e_cal_component_get_id (comp);
			g_assert_nonnull (id);

			g_hash_table_insert (res, id, g_object_ref (comp));
		}
	}

	g_slist_free_full (components, g_object_unref);
	g_object_unref (sexp);

	return res;
}

#if dd(1)+0
static void
test_search_dump_results (GSList *search_data,
			  GHashTable *should_be)
{
	GSList *link;
	GHashTableIter iter;
	gpointer key;
	gint ii;

	printf ("   Found %d in ECalCache:\n", g_slist_length (search_data));
	for (ii = 0, link = search_data; link; link = g_slist_next (link), ii++) {
		ECalCacheSearchData *sd = link->data;

		printf ("      [%d]: %s%s%s\n", ii, sd->uid, sd->rid ? ", " : "", sd->rid ? sd->rid : "");
	}

	printf ("\n");
	printf ("   Found %d in traverse:\n", g_hash_table_size (should_be));

	ii = 0;
	g_hash_table_iter_init (&iter, should_be);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		ECalComponentId *id = key;

		printf ("      [%d]: %s%s%s\n", ii, id->uid, id->rid ? ", " : "", id->rid ? id->rid : "");
		ii++;
	}

	printf ("\n");
}
#endif

static void
test_search_result_equal (GSList *items,
			  GHashTable *should_be,
			  gboolean (* check_cb) (GHashTable *should_be, gpointer item_data))
{
	GSList *link;

	g_assert_cmpint (g_slist_length (items), ==, g_hash_table_size (should_be));

	for (link = items; link; link = g_slist_next (link)) {
		g_assert (check_cb (should_be, link->data));
	}
}

static gboolean
search_data_check_cb (GHashTable *should_be,
		      gpointer item_data)
{
	ECalCacheSearchData *sd = item_data;
	ECalComponentId id;

	g_assert (sd != NULL);
	g_assert (sd->uid != NULL);

	id.uid = sd->uid;
	id.rid = sd->rid;

	return g_hash_table_contains (should_be, &id);
}

static gboolean
component_check_cb (GHashTable *should_be,
		    gpointer item_data)
{
	ECalComponent *comp = item_data;
	ECalComponentId *id;
	gboolean contains;

	g_assert (comp != NULL);

	id = e_cal_component_get_id (comp);

	g_assert (id != NULL);
	g_assert (id->uid != NULL);

	contains = g_hash_table_contains (should_be, id);

	e_cal_component_free_id (id);

	return contains;
}

static gboolean
id_check_cb (GHashTable *should_be,
	     gpointer item_data)
{
	ECalComponentId *id = item_data;

	g_assert (id != NULL);
	g_assert (id->uid != NULL);

	return g_hash_table_contains (should_be, id);
}

static void
test_search_expr (TCUFixture *fixture,
		  const gchar *expr,
		  const gchar *expects)
{
	GSList *items = NULL;
	GHashTable *should_be;
	gboolean success;
	GError *error = NULL;

	should_be = test_search_manual (fixture->cal_cache, expr);
	g_assert_nonnull (should_be);

	success = e_cal_cache_search (fixture->cal_cache, expr, &items, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	dd (test_search_dump_results (items, should_be));

	test_search_result_equal (items, should_be, search_data_check_cb);

	g_slist_free_full (items, e_cal_cache_search_data_free);
	items = NULL;

	success = e_cal_cache_search_components (fixture->cal_cache, expr, &items, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	test_search_result_equal (items, should_be, component_check_cb);

	g_slist_free_full (items, g_object_unref);
	items = NULL;

	success = e_cal_cache_search_ids (fixture->cal_cache, expr, &items, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	if (expects) {
		GSList *link;
		gboolean negate = *expects == '!';

		if (negate)
			expects++;

		for (link = items; link; link = g_slist_next (link)) {
			ECalComponentId *id = link->data;

			if (g_strcmp0 (id->uid, expects) == 0)
				break;
		}

		if (link && negate)
			g_error ("Found '%s' in result of '%s', though it should not be there", expects, expr);
		else if (!link && !negate)
			g_error ("Not found '%s' in result of '%s', though it should be there", expects, expr);
	}

	test_search_result_equal (items, should_be, id_check_cb);

	g_slist_free_full (items, (GDestroyNotify) e_cal_component_free_id);

	g_hash_table_destroy (should_be);
}

static void
test_search (TCUFixture *fixture,
	     const gchar *expr,
	     const gchar *expects)
{
	gchar *not_expr;

	test_search_expr (fixture, expr, expects);

	not_expr = g_strdup_printf ("(not (%s))", expr);
	test_search_expr (fixture, not_expr, NULL);
	g_free (not_expr);
}

static void
test_search_uid (TCUFixture *fixture,
		 gconstpointer user_data)
{
	test_search (fixture, "(uid? \"event-3\")", "event-3");
	test_search (fixture, "(uid? \"event-6\")", "event-6");
	test_search (fixture, "(or (uid? \"event-3\") (uid? \"event-6\"))", "event-3");
	test_search (fixture, "(and (uid? \"event-3\") (uid? \"event-6\"))", "!event-3");
}

static void
test_search_occur_in_time_range (TCUFixture *fixture,
				 gconstpointer user_data)
{
	test_search (fixture, "(occur-in-time-range? (make-time \"20010101T000000Z\") (make-time \"20010101T010000Z\"))", "!event-1");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170209T000000Z\") (make-time \"20170210T000000Z\"))", "event-1");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170209T020000Z\") (make-time \"20170209T023000Z\"))", "event-1");
	test_search (fixture, "(occur-in-time-range? (make-time \"20111231T000000Z\") (make-time \"20111231T595959Z\"))", "event-5");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170225T210100Z\") (make-time \"20170225T210200Z\") \"America/New_York\")", "event-8");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170225T150100Z\") (make-time \"20170225T150200Z\") \"Europe/Berlin\")", "event-8");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170225T160100Z\") (make-time \"20170225T160200Z\") \"UTC\")", "event-8");

	/* event-6 */
	test_search (fixture, "(occur-in-time-range? (make-time \"20170221T180000Z\") (make-time \"20170221T190000Z\"))", "event-6");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170221T180000Z\") (make-time \"20170221T190000Z\") \"America/New_York\")", "event-6");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170221T200000Z\") (make-time \"20170221T210000Z\") \"Europe/Berlin\")", "!event-6");
	test_search (fixture, "(occur-in-time-range? (make-time \"20170221T180000Z\") (make-time \"20170221T190000Z\") \"Europe/Berlin\")", "event-6");
}

static void
test_search_due_in_time_range (TCUFixture *fixture,
			       gconstpointer user_data)
{
	test_search (fixture, "(due-in-time-range? (make-time \"20170101T000000Z\") (make-time \"20170101T010000Z\"))", "!task-4");
	test_search (fixture, "(due-in-time-range? (make-time \"20170228T000000Z\") (make-time \"20170302T000000Z\"))", "task-4");
}

static void
test_search_contains (TCUFixture *fixture,
		      gconstpointer user_data)
{
	const TCUClosure *closure = user_data;
	gboolean searches_events = closure && closure->load_set == TCU_LOAD_COMPONENT_SET_EVENTS;

	test_search (fixture, "(contains? \"any\" \"party\")", searches_events ? "event-5" : NULL);
	test_search (fixture, "(contains? \"comment\" \"mentar\")", searches_events ? "event-8" : "task-6");
	test_search (fixture, "(contains? \"description\" \"with\")", searches_events ? "event-1" : "task-3");
	test_search (fixture, "(contains? \"summary\" \"meet\")", searches_events ? "event-8" : NULL);
	test_search (fixture, "(contains? \"location\" \"kitchen\")", searches_events ? "event-3" : "task-5");
	test_search (fixture, "(contains? \"attendee\" \"CharLie\")", searches_events ? "event-9" : NULL);
	test_search (fixture, "(contains? \"organizer\" \"bOb\")", searches_events ? "event-8" : NULL);
	test_search (fixture, "(contains? \"classification\" \"Public\")", searches_events ? "event-4" : "task-4");
	test_search (fixture, "(contains? \"classification\" \"Private\")", searches_events ? "event-3" : "task-7");
	test_search (fixture, "(contains? \"classification\" \"Confidential\")", searches_events ? "event-2" : "task-6");
	test_search (fixture, "(contains? \"status\" \"NOT STARTED\")", searches_events ? NULL : "task-1");
	test_search (fixture, "(contains? \"status\" \"COMPLETED\")", searches_events ? NULL : "task-4");
	test_search (fixture, "(contains? \"status\" \"CANCELLED\")", searches_events ? NULL : "task-8");
	test_search (fixture, "(contains? \"status\" \"IN PROGRESS\")", searches_events ? NULL : "task-7");
	test_search (fixture, "(contains? \"priority\" \"HIGH\")", searches_events ? NULL : "task-7");
	test_search (fixture, "(contains? \"priority\" \"NORMAL\")", searches_events ? NULL : "task-6");
	test_search (fixture, "(contains? \"priority\" \"LOW\")", searches_events ? NULL : "task-5");
	test_search (fixture, "(contains? \"priority\" \"UNDEFINED\")", searches_events ? NULL : "task-1");
}

static void
test_search_has_start (TCUFixture *fixture,
		       gconstpointer user_data)
{
	const TCUClosure *closure = user_data;
	gboolean searches_events = closure && closure->load_set == TCU_LOAD_COMPONENT_SET_EVENTS;

	test_search (fixture, "(has-start?)", searches_events ? "event-1" : "task-9");
	test_search (fixture, "(has-start?)", searches_events ? "event-1" : "!task-8");
	test_search (fixture, "(not (has-start?))", searches_events ? "!event-1" : "!task-9");
	test_search (fixture, "(not (has-start?))", searches_events ? "!event-1" : "task-8");
}

static void
test_search_has_alarms (TCUFixture *fixture,
			gconstpointer user_data)
{
	const TCUClosure *closure = user_data;
	gboolean searches_events = closure && closure->load_set == TCU_LOAD_COMPONENT_SET_EVENTS;

	test_search (fixture, "(has-alarms?)", searches_events ? "event-1" : "task-7");
	test_search (fixture, "(has-alarms?)", searches_events ? "event-1" : "!task-6");
	test_search (fixture, "(not (has-alarms?))", searches_events ? "!event-1" : "!task-7");
	test_search (fixture, "(not (has-alarms?))", searches_events ? "!event-1" : "task-6");
}

static void
test_search_has_alarms_in_range (TCUFixture *fixture,
				 gconstpointer user_data)
{
	const TCUClosure *closure = user_data;
	gboolean searches_events = closure && closure->load_set == TCU_LOAD_COMPONENT_SET_EVENTS;

	test_search (fixture, "(has-alarms-in-range? (make-time \"20091229T230000Z\") (make-time \"20091231T010000Z\"))",
		searches_events ? "event-5" : "!task-7");
}

static void
test_search_has_recurrences (TCUFixture *fixture,
			     gconstpointer user_data)
{
	test_search (fixture, "(has-recurrences?)", "event-6");
	test_search (fixture, "(not (has-recurrences?))", "!event-6");
}

static void
test_search_has_categories (TCUFixture *fixture,
			    gconstpointer user_data)
{
	test_search (fixture, "(has-categories? #f)", "!event-2");
	test_search (fixture, "(has-categories? \"Holiday\")", "event-7");
	test_search (fixture, "(has-categories? \"Hard\" \"Work\")", "event-2");
	test_search (fixture, "(has-categories? \"Hard\" \"Work\")", "!event-4");
}

static void
test_search_is_completed (TCUFixture *fixture,
			  gconstpointer user_data)
{
	test_search (fixture, "(is-completed?)", "task-4");
	test_search (fixture, "(is-completed?)", "!task-5");
	test_search (fixture, "(not (is-completed?))", "!task-4");
}

static void
test_search_completed_before (TCUFixture *fixture,
			      gconstpointer user_data)
{
	test_search (fixture, "(completed-before? (make-time \"20170221T000000Z\"))", "!task-4");
	test_search (fixture, "(completed-before? (make-time \"20170222T000000Z\"))", "task-4");
}

static void
test_search_has_attachments (TCUFixture *fixture,
			     gconstpointer user_data)
{
	test_search (fixture, "(has-attachments?)", "event-7");
	test_search (fixture, "(not (has-attachments?))", "!event-7");
}

static void
test_search_percent_complete (TCUFixture *fixture,
			      gconstpointer user_data)
{
	test_search (fixture, "(< (percent-complete?) 30)", "task-5");
	test_search (fixture, "(< (percent-complete?) 30)", "!task-7");
}

static void
test_search_occurrences_count (TCUFixture *fixture,
			       gconstpointer user_data)
{
	test_search (fixture, "(and (= (occurrences-count?) 1) (occur-in-time-range? (make-time \"20170209T000000Z\") (make-time \"20170210T000000Z\")))", "event-1");
	test_search (fixture, "(= (occurrences-count? (make-time \"20170209T000000Z\") (make-time \"20170210T000000Z\")) 1)", "event-1");
}

static void
test_search_complex (TCUFixture *fixture,
		     gconstpointer user_data)
{
	test_search (fixture,
		"(or "
			"(and (not (is-completed?)) (has-start?) (not (has-alarms?)))"
			"(contains? \"summary\" \"-on-\")"
			"(has-attachments?)"
		")", "event-3");
}

gint
main (gint argc,
      gchar **argv)
{
	TCUClosure closure_events = { TCU_LOAD_COMPONENT_SET_EVENTS };
	TCUClosure closure_tasks = { TCU_LOAD_COMPONENT_SET_TASKS };

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/ECalCache/Search/Uid", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_uid, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/OccurInTimeRange", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_occur_in_time_range, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/DueInTimeRange", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_due_in_time_range, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/Contains/Events", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_contains, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/Contains/Tasks", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_contains, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasStart/Events", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_has_start, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasStart/Tasks", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_has_start, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasAlarms/Events", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_has_alarms, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasAlarms/Tasks", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_has_alarms, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasAlarmsInRange/Events", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_has_alarms_in_range, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasAlarmsInRange/Tasks", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_has_alarms_in_range, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasRecurrences", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_has_recurrences, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasCategories", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_has_categories, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/IsCompleted", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_is_completed, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/CompletedBefore", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_completed_before, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/HasAttachments", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_has_attachments, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/PercentComplete", TCUFixture, &closure_tasks,
		tcu_fixture_setup, test_search_percent_complete, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/OccurrencesCount", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_occurrences_count, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Search/Complex", TCUFixture, &closure_events,
		tcu_fixture_setup, test_search_complex, tcu_fixture_teardown);

	return e_test_server_utils_run_full (0);
}
