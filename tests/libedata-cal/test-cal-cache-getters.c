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

static ECalComponentId *
extract_id_from_component (ECalComponent *component)
{
	ECalComponentId *id;

	g_assert_true (component != NULL);

	id = e_cal_component_get_id (component);
	g_assert_true (id != NULL);
	g_assert_true (e_cal_component_id_get_uid (id) != NULL);

	return id;
}

static ECalComponentId *
extract_id_from_string (const gchar *icalstring)
{
	ECalComponent *component;
	ECalComponentId *id;

	g_assert_true (icalstring != NULL);

	component = e_cal_component_new_from_string (icalstring);
	g_assert_true (component != NULL);

	id = extract_id_from_component (component);

	g_object_unref (component);

	return id;
}

static void
test_get_one (ECalCache *cal_cache,
	      const gchar *uid,
	      const gchar *rid,
	      gboolean expect_failure)
{
	ECalComponent *component = NULL;
	ECalComponentId *id;
	gchar *icalstring = NULL;
	gboolean success;
	GError *error = NULL;

	success = e_cal_cache_get_component (cal_cache, uid, rid, &component, NULL, &error);
	if (expect_failure) {
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_assert_true (!success);
		g_assert_true (!component);

		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (component);

		id = extract_id_from_component (component);

		g_assert_cmpstr (e_cal_component_id_get_uid (id), ==, uid);
		g_assert_cmpstr (e_cal_component_id_get_rid (id), ==, rid && *rid ? rid : NULL);

		e_cal_component_id_free (id);
		g_object_unref (component);
	}

	success = e_cal_cache_get_component_as_string (cal_cache, uid, rid, &icalstring, NULL, &error);
	if (expect_failure) {
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_assert_true (!success);
		g_assert_true (!icalstring);

		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (icalstring);

		id = extract_id_from_string (icalstring);

		g_assert_cmpstr (e_cal_component_id_get_uid (id), ==, uid);
		g_assert_cmpstr (e_cal_component_id_get_rid (id), ==, rid && *rid ? rid : NULL);

		e_cal_component_id_free (id);
		g_free (icalstring);
	}
}

static void
test_getters_one (TCUFixture *fixture,
		  gconstpointer user_data)
{
	test_get_one (fixture->cal_cache, "unexistent-event", NULL, TRUE);
	test_get_one (fixture->cal_cache, "unexistent-event", "", TRUE);
	test_get_one (fixture->cal_cache, "event-2", NULL, FALSE);
	test_get_one (fixture->cal_cache, "event-2", "", FALSE);
	test_get_one (fixture->cal_cache, "event-5", NULL, FALSE);
	test_get_one (fixture->cal_cache, "event-5", "", FALSE);
	test_get_one (fixture->cal_cache, "event-5", "20131231T000000Z", TRUE);
	test_get_one (fixture->cal_cache, "event-6", NULL, FALSE);
	test_get_one (fixture->cal_cache, "event-6", "", FALSE);
	test_get_one (fixture->cal_cache, "event-6", "20170225T134900", FALSE);
}

/* NULL-terminated list of pairs <uid, rid>, what to expect */
static void
test_get_all (ECalCache *cal_cache,
	      const gchar *uid,
	      ...)
{
	ECalComponentId *id;
	GSList *items, *link;
	va_list va;
	const gchar *tmp;
	GHashTable *expects;
	gboolean success;
	GError *error = NULL;

	expects = g_hash_table_new_full ((GHashFunc) e_cal_component_id_hash, (GEqualFunc) e_cal_component_id_equal,
		(GDestroyNotify) e_cal_component_id_free, NULL);

	va_start (va, uid);
	tmp = va_arg (va, const gchar *);
	while (tmp) {
		const gchar *rid = va_arg (va, const gchar *);
		id = e_cal_component_id_new (tmp, rid);

		g_hash_table_insert (expects, id, NULL);

		tmp = va_arg (va, const gchar *);
	}
	va_end (va);

	items = NULL;

	success = e_cal_cache_get_components_by_uid (cal_cache, uid, &items, NULL, &error);
	if (!g_hash_table_size (expects)) {
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_assert_true (!success);
		g_assert_true (!items);

		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (items);

		g_assert_cmpint (g_hash_table_size (expects), ==, g_slist_length (items));

		for (link = items; link; link = g_slist_next (link)) {
			id = extract_id_from_component (link->data);

			g_assert_cmpstr (e_cal_component_id_get_uid (id), ==, uid);
			g_assert_true (g_hash_table_contains (expects, id));

			e_cal_component_id_free (id);
		}

		g_slist_free_full (items, g_object_unref);
	}

	items = NULL;

	success = e_cal_cache_get_components_by_uid_as_string (cal_cache, uid, &items, NULL, &error);
	if (!g_hash_table_size (expects)) {
		g_assert_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND);
		g_assert_true (!success);
		g_assert_true (!items);

		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (items);

		g_assert_cmpint (g_hash_table_size (expects), ==, g_slist_length (items));

		for (link = items; link; link = g_slist_next (link)) {
			id = extract_id_from_string (link->data);

			g_assert_cmpstr (e_cal_component_id_get_uid (id), ==, uid);
			g_assert_true (g_hash_table_contains (expects, id));

			e_cal_component_id_free (id);
		}

		g_slist_free_full (items, g_free);
	}

	g_hash_table_destroy (expects);
}

static void
test_getters_all (TCUFixture *fixture,
		  gconstpointer user_data)
{
	test_get_all (fixture->cal_cache, "unexistent-event", NULL);
	test_get_all (fixture->cal_cache, "unexistent-event", NULL);
	test_get_all (fixture->cal_cache, "event-2", "event-2", NULL, NULL);
	test_get_all (fixture->cal_cache, "event-5", "event-5", NULL, NULL);
	test_get_all (fixture->cal_cache, "event-6", "event-6", NULL, "event-6", "20170225T134900", NULL);
}

gint
main (gint argc,
      gchar **argv)
{
	TCUClosure closure_events = { TCU_LOAD_COMPONENT_SET_EVENTS };

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	/* Ensure that the client and server get the same locale */
	g_assert_true (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/ECalCache/Getters/One", TCUFixture, &closure_events,
		tcu_fixture_setup, test_getters_one, tcu_fixture_teardown);
	g_test_add ("/ECalCache/Getters/All", TCUFixture, &closure_events,
		tcu_fixture_setup, test_getters_all, tcu_fixture_teardown);

	return e_test_server_utils_run_full (argc, argv, 0);
}
