/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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

#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "client-test-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };
static ETestServerClosure book_closure_direct_sync = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_async = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

#define N_VALID_SORT_FIELDS 2
static EContactField valid_sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
static EBookCursorSortType valid_sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };

#define N_INVALID_SORT_FIELDS 1
static EContactField invalid_sort_fields[] = { E_CONTACT_TEL };
static EBookCursorSortType invalid_sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING };

static void
test_cursor_create_empty_query_sync (ETestServerFixture *fixture,
                                     gconstpointer user_data)
{
	EBookClient *book_client;
	EBookClientCursor *cursor = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!e_book_client_get_cursor_sync (book_client,
					    NULL,
					    valid_sort_fields,
					    valid_sort_types,
					    N_VALID_SORT_FIELDS,
					    &cursor,
					    NULL, &error))
	  g_error ("Failed to create a cursor with an empty query: %s", error->message);

	g_object_unref (cursor);
}

static void
test_cursor_create_with_query_sync (ETestServerFixture *fixture,
                                     gconstpointer user_data)
{
	EBookClient *book_client;
	EBookClientCursor *cursor = NULL;
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown");
	sexp = e_book_query_to_string (query);

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!e_book_client_get_cursor_sync (book_client,
					    sexp,
					    valid_sort_fields,
					    valid_sort_types,
					    N_VALID_SORT_FIELDS,
					    &cursor,
					    NULL, &error))
	  g_error ("Failed to create a cursor with an empty query: %s", error->message);

	g_object_unref (cursor);
	g_free (sexp);
	e_book_query_unref (query);
}

static void
test_cursor_create_invalid_sort_sync (ETestServerFixture *fixture,
                                      gconstpointer user_data)
{
	EBookClient *book_client;
	EBookClientCursor *cursor = NULL;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (e_book_client_get_cursor_sync (book_client,
					   NULL,
					   invalid_sort_fields,
					   invalid_sort_types,
					   N_INVALID_SORT_FIELDS,
					   &cursor,
					   NULL, &error))
		g_error ("Expected invalid query but successfully created cursor");
	else if (!g_error_matches (error,
				   E_CLIENT_ERROR,
				   E_CLIENT_ERROR_INVALID_QUERY)) {
		g_error (
			"Unexpected error: Domain '%s' Code '%d' Message: %s\n",
			g_quark_to_string (error->domain), error->code,
			error->message);
	}

	g_error_free (error);
}

static void
cursor_create_success_ready_cb (GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	EBookClientCursor *cursor = NULL;
	GError *error = NULL;

	if (!e_book_client_get_cursor_finish (E_BOOK_CLIENT (source_object),
					      result, &cursor, &error))
		g_error ("Failed to create a cursor: %s", error->message);

	g_object_unref (cursor);
	g_main_loop_quit (loop);
}

static void
cursor_create_invalid_query_ready_cb (GObject *source_object,
                                      GAsyncResult *result,
                                      gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	EBookClientCursor *cursor = NULL;
	GError *error = NULL;

	if (e_book_client_get_cursor_finish (E_BOOK_CLIENT (source_object),
					     result, &cursor, &error))
		g_error ("Expected invalid query but successfully created cursor");
	else if (!g_error_matches (error,
				   E_CLIENT_ERROR,
				   E_CLIENT_ERROR_INVALID_QUERY)) {
		g_error (
			"Unexpected error: Domain '%s' Code '%d' Message: %s\n",
			g_quark_to_string (error->domain), error->code,
			error->message);
	}

	g_error_free (error);
	g_main_loop_quit (loop);
}

static void
test_cursor_create_empty_query_async (ETestServerFixture *fixture,
                                      gconstpointer user_data)
{
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	e_book_client_get_cursor (
		book_client,
		NULL,
		valid_sort_fields,
		valid_sort_types,
		N_VALID_SORT_FIELDS,
		NULL,
		cursor_create_success_ready_cb,
		fixture->loop);
	g_main_loop_run (fixture->loop);
}

static void
test_cursor_create_with_query_async (ETestServerFixture *fixture,
                                      gconstpointer user_data)
{
	EBookClient *book_client;
	EBookQuery *query;
	gchar *sexp;

	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown");
	sexp = e_book_query_to_string (query);

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	e_book_client_get_cursor (
		book_client,
		sexp,
		valid_sort_fields,
		valid_sort_types,
		N_VALID_SORT_FIELDS,
		NULL,
		cursor_create_success_ready_cb,
		fixture->loop);

	g_free (sexp);
	e_book_query_unref (query);

	g_main_loop_run (fixture->loop);
}

static void
test_cursor_create_invalid_sort_async (ETestServerFixture *fixture,
                                       gconstpointer user_data)
{
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	e_book_client_get_cursor (
		book_client,
		NULL,
		invalid_sort_fields,
		invalid_sort_types,
		N_INVALID_SORT_FIELDS,
		NULL,
		cursor_create_invalid_query_ready_cb,
		fixture->loop);

	g_main_loop_run (fixture->loop);
}

typedef void (*TestFunc) (ETestServerFixture *fixture, gconstpointer user_data);

typedef struct {
	const gchar        *test_path;
	gboolean            sync_test;
	TestFunc            func;
} TestClosure;

static const TestClosure test_closures[] = {
	{ "/EBookClientCursor/Create/EmptyQuery/Sync", TRUE,
	  test_cursor_create_empty_query_sync
	},
	{ "/EBookClientCursor/Create/EmptyQuery/Async", FALSE,
	  test_cursor_create_empty_query_async
	},
	{ "/EBookClientCursor/Create/WithQuery/Sync", TRUE,
	  test_cursor_create_with_query_sync
	},
	{ "/EBookClientCursor/Create/WithQuery/Async", FALSE,
	  test_cursor_create_with_query_async
	},
	{ "/EBookClientCursor/Create/InvalidSort/Sync", TRUE,
	  test_cursor_create_invalid_sort_sync
	},
	{ "/EBookClientCursor/Create/InvalidSort/Async", FALSE,
	  test_cursor_create_invalid_sort_async
	}
};

gint
main (gint argc,
      gchar **argv)
{
	gint i, j;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	for (i = 0; i < 2; i++) {

		for (j = 0; j < G_N_ELEMENTS (test_closures); j++) {
			ETestServerClosure *closure;
			gchar *test_path;

			/* Regular tests */
			if (i == 0) {
				closure = test_closures[j].sync_test ?
					&book_closure_sync : &book_closure_async;

				test_path = g_strdup (test_closures[j].test_path);

			} else /* DRA tests */ {
				closure = test_closures[j].sync_test ?
					&book_closure_direct_sync : &book_closure_direct_async;

				test_path = g_strdup_printf ("/DRA/%s", test_closures[j].test_path);
			}

			g_test_add (
				test_path,
				ETestServerFixture,
				closure,
				e_test_server_utils_setup,
				test_closures[j].func,
				e_test_server_utils_teardown);

			g_free (test_path);
		}
	}

	return e_test_server_utils_run ();
}
