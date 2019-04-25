/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* test-fixture.c - Test to ensure the server test fixture works.
 *
 * Copyright (C) 2012 Intel Corporation
 *
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "e-test-server-utils.h"

#define N_CYCLES 10

static ETestServerClosure registry_closure = { E_TEST_SERVER_NONE, NULL, 0 };
static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };
static ETestServerClosure calendar_closure = { E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };
static ETestServerClosure deprecated_book_closure = { E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };

static void
empty_test (ETestServerFixture *fixture,
            gconstpointer user_data)
{
	/* Basic Empty case just to run the fixture */
}

gint
main (gint argc,
      gchar *argv[])
{
	gchar **registry_keys;
	gchar **book_keys;
	gchar **calendar_keys;
	gchar **deprecated_book_keys;
	gint i;
	gint ret;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	registry_keys = g_new0 (gchar *, N_CYCLES);
	book_keys = g_new0 (gchar *, N_CYCLES);
	calendar_keys = g_new0 (gchar *, N_CYCLES);
	deprecated_book_keys = g_new0 (gchar *, N_CYCLES);

	for (i = 0; i < N_CYCLES; i++) {
		registry_keys[i] = g_strdup_printf ("/Fixture/Registry%d", i);
		g_test_add (
			registry_keys[i],
			ETestServerFixture,
			&registry_closure,
			e_test_server_utils_setup,
			empty_test,
			e_test_server_utils_teardown);
	}

	for (i = 0; i < N_CYCLES; i++) {
		book_keys[i] = g_strdup_printf ("/Fixture/Book%d", i);
		g_test_add (
			book_keys[i],
			ETestServerFixture,
			&book_closure,
			e_test_server_utils_setup,
			empty_test,
			e_test_server_utils_teardown);
	}

	for (i = 0; i < N_CYCLES; i++) {
		calendar_keys[i] = g_strdup_printf ("/Fixture/Calendar%d", i);
		g_test_add (
			calendar_keys[i],
			ETestServerFixture,
			&calendar_closure,
			e_test_server_utils_setup,
			empty_test,
			e_test_server_utils_teardown);
	}

	for (i = 0; i < N_CYCLES; i++) {
		deprecated_book_keys[i] = g_strdup_printf ("/Fixture/Deprecated/Book%d", i);
		g_test_add (
			deprecated_book_keys[i],
			ETestServerFixture,
			&deprecated_book_closure,
			e_test_server_utils_setup,
			empty_test,
			e_test_server_utils_teardown);
	}

	ret = e_test_server_utils_run ();

	for (i = 0; i < N_CYCLES; i++) {
		g_free (registry_keys[i]);
		g_free (book_keys[i]);
		g_free (calendar_keys[i]);
		g_free (deprecated_book_keys[i]);
	}

	g_free (registry_keys);
	g_free (book_keys);
	g_free (calendar_keys);
	g_free (deprecated_book_keys);

	return ret;
}
