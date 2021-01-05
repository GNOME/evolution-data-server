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
#include <string.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

/* asynchronous callback with a main-loop running */
static void
async_refresh_result_ready (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	book_client = E_BOOK_CLIENT (source_object);

	if (!e_client_refresh_finish (E_CLIENT (book_client), result, &error)) {
		g_error ("refresh finish: %s", error->message);
		return;
	}

	g_main_loop_quit (loop);
}

static void
test_refresh_sync (ETestServerFixture *fixture,
                   gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	g_print ("Refresh supported: %s\n", e_client_check_refresh_supported (E_CLIENT (book_client)) ? "yes" : "no");
	if (!e_client_check_refresh_supported (E_CLIENT (book_client)))
		return;

	if (!e_client_refresh_sync (E_CLIENT (book_client), NULL, &error)) {
		g_error ("Error in refresh: %s", error->message);
	}
}

static gboolean
main_loop_fail_timeout (gpointer unused)
{
	g_error ("Failed to refresh, async call timed out");
	return FALSE;
}

static void
test_refresh_async (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	EBookClient *book_client;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	g_print ("Refresh supported: %s\n", e_client_check_refresh_supported (E_CLIENT (book_client)) ? "yes" : "no");
	if (!e_client_check_refresh_supported (E_CLIENT (book_client)))
		return;

	e_client_refresh (E_CLIENT (book_client), NULL, async_refresh_result_ready, fixture->loop);
	g_timeout_add_seconds (5, (GSourceFunc) main_loop_fail_timeout, NULL);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/Refresh/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_refresh_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/Refresh/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_refresh_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
