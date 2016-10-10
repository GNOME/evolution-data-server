/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-test-server-utils.c - Test scaffolding to run tests with in-tree data server.
 *
 * Copyright (C) 2012 Intel Corporation
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

/**
 * SECTION: e-test-server-utils
 * @short_description: A utility for unit testing EDS
 *
 * This test fixture provides an encapsulated testing environment for test
 * cases to test #EBookClient and #ECalClient.
 *
 * The #ETestServerFixture should be used as a fixture and must be coupled
 * with the #ETestServerClosure to configure how the test fixture will operate.
 *
 * Both the #ETestServerFixture and #ETestServerClosure can be extended with
 * more complex test fixtures, which must remember to call e_test_server_utils_setup()
 * and e_test_server_utils_teardown() in thier fixture's setup and teardown routines.
 **/

#include "evolution-data-server-config.h"

#include "e-test-server-utils.h"

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#define ADDRESS_BOOK_SOURCE_UID "test-address-book"
#define CALENDAR_SOURCE_UID     "test-calendar"

#define FINALIZE_SECONDS         30

/* FIXME, currently we are unable to achieve server activation
 * twice in a single test case, so we're using one D-Bus server
 * throughout an entire test suite.
 *
 * When this is fixed we can migrate the D-Bus initialization
 * and teardown from e_test_server_utils_run() to
 * e_test_server_utils_setup() and e_test_server_utils_teardown()
 * and this will transparantly change the way tests run using
 * this test framework.
 */
#define GLOBAL_DBUS_DAEMON 1

#if GLOBAL_DBUS_DAEMON
static GTestDBus *global_test_dbus = NULL;
#endif

/* The ESource identifier numerical component, this should
 * not be needed (and should probably be removed) once we
 * can get rid of the GLOBAL_DBUS_DAEMON hack.
 */
static gint global_test_source_id = 0;

/*****************************************************************
 *                    Reference management                       *
 *****************************************************************
 *
 * We need to test that an EClient actually finalizes properly
 * at the end of every test, however these EClient's have a 
 * habit of finalizing asynchronously, not even in the same thread
 * in which they were created.
 *
 * This is why we do this crazy stuff below to ensure asyncrhonous
 * finalization of the client actually happens.
 */
static void
add_weak_ref (ETestServerFixture *fixture,
              ETestServiceType service_type)
{
	GObject *object;

	switch (service_type) {
	case E_TEST_SERVER_NONE:
		g_weak_ref_set (&fixture->registry_ref, fixture->registry);
		break;

	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
	case E_TEST_SERVER_CALENDAR:
	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:
	case E_TEST_SERVER_DEPRECATED_CALENDAR:

		/* They're all the same object pointer */
		object = E_TEST_SERVER_UTILS_SERVICE (fixture, GObject);
		g_weak_ref_set (&fixture->client_ref, object);
		break;
	}
}

static gboolean
object_finalize_timeout (gpointer user_data)
{
	const gchar *message = (const gchar *) user_data;

	g_error ("%s", message);

	return FALSE;
}

static gboolean
object_unref_idle (gpointer user_data)
{
	g_object_unref (user_data);

	return FALSE;
}

static void
weak_notify_loop_quit (gpointer data,
                       GObject *where_the_object_was)
{
	ETestServerFixture *fixture = (ETestServerFixture *) data;

	g_main_loop_quit (fixture->loop);
}

static void
assert_object_finalized (ETestServerFixture *fixture,
                         ETestServiceType service_type)
{
	const gchar *message = NULL;
	GObject *object = NULL;
	GWeakRef *ref = NULL;

	switch (service_type) {
	case E_TEST_SERVER_NONE:
		message = "Timed out waiting for source registery to finalize";
		ref = &fixture->registry_ref;
		break;
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:
		message = "Timed out waiting for addressbook client to finalize";
		ref = &fixture->client_ref;
		break;
	case E_TEST_SERVER_CALENDAR:
	case E_TEST_SERVER_DEPRECATED_CALENDAR:
		message = "Timed out waiting for calendar client to finalize";
		ref = &fixture->client_ref;
		break;
	}

	/* Give her a second chance */
	object = g_weak_ref_get (ref);
	if (object) {
		guint timeout_id;

		/* Add a finalize callback while we have a strong reference */
		g_object_weak_ref (object, weak_notify_loop_quit, fixture);

		/* Fail the test if we reach the timeout */
		timeout_id = g_timeout_add_seconds (
			FINALIZE_SECONDS,
			object_finalize_timeout,
			(gpointer) message);

		/* We can't release the strong reference yet, it might try
		 * to quit the main loop before we've started it.
		 *
		 * Instead release it in the loop (no need to track the source id,
		 * it's guaranteed to be removed in the scope of this test).
		 */
		g_idle_add (object_unref_idle, object);

		/* Wait for asynchronous finalization, if this loop
		 * returns then we finalized properly, if the timeout
		 * is reached then we've aborted with an error message
		 */
		g_main_loop_run (fixture->loop);

		/* If we reached here, better remove the timeout source to
		 * avoid it timing out in following tests
		 */
		g_source_remove (timeout_id);
	}
}

/*****************************************************************
 * Bootstrapping, manage work directory and create test ESource  *
 *****************************************************************/
typedef struct {
	ETestServerFixture *fixture;
	ETestServerClosure *closure;
} FixturePair;

static gboolean
test_installed_services (void)
{
	static gint use_installed_services = -1;

	if (use_installed_services < 0) {
		if (g_getenv ("TEST_INSTALLED_SERVICES") != NULL)
			use_installed_services = 1;
		else
			use_installed_services = 0;
	}
	return use_installed_services;
}

static gchar *
generate_source_name (void)
{
	gchar *source_name = NULL;

	if (test_installed_services ()) {
		gchar buffer[128] = "eds-source-XXXXXX";
		gint  fd;

		fd = g_mkstemp (buffer);
		if (fd < 0)
			g_error ("Failed to generate source ID with temporary file");
		close (fd);

		source_name = g_strdup (buffer);

	} else {
		source_name = g_strdup_printf (
			"%s-%d",
			ADDRESS_BOOK_SOURCE_UID,
			global_test_source_id++);
	}

	return source_name;
}

static void
setup_environment (void)
{
	GString *libs_dir;
	const gchar *libs_dir_env;

	libs_dir_env = g_getenv ("LD_LIBRARY_PATH");

	libs_dir = g_string_new ("");

	#define add_lib_path(x) G_STMT_START { \
		if (libs_dir->len) \
			g_string_append_c (libs_dir, ':'); \
		g_string_append_printf (libs_dir, EDS_TEST_TOP_BUILD_DIR x); \
		} G_STMT_END

	add_lib_path ("addressbook/libebook/.libs");
	add_lib_path ("addressbook/libebook-contacts/.libs");
	add_lib_path ("addressbook/libedata-book/.libs");
	add_lib_path ("calendar/libecal/.libs");
	add_lib_path ("calendar/libedata-cal/.libs");
	add_lib_path ("camel/.libs");
	add_lib_path ("libebackend/.libs");
	add_lib_path ("libedataserver/.libs");
	add_lib_path ("libedataserverui/.libs");
	add_lib_path ("private/.libs");

	#undef add_lib_path

	if (libs_dir_env && *libs_dir_env) {
		if (libs_dir->len)
			g_string_append_c (libs_dir, ':');
		g_string_append (libs_dir, libs_dir_env);
	}

	g_assert (g_setenv ("LD_LIBRARY_PATH", libs_dir->str, TRUE));
	g_assert (g_setenv ("XDG_DATA_HOME", EDS_TEST_WORK_DIR, TRUE));
	g_assert (g_setenv ("XDG_CACHE_HOME", EDS_TEST_WORK_DIR, TRUE));
	g_assert (g_setenv ("XDG_CONFIG_HOME", EDS_TEST_WORK_DIR, TRUE));
	g_assert (g_setenv ("GSETTINGS_SCHEMA_DIR", EDS_TEST_SCHEMA_DIR, TRUE));
	g_assert (g_setenv ("EDS_CALENDAR_MODULES", EDS_TEST_CALENDAR_DIR, TRUE));
	g_assert (g_setenv ("EDS_ADDRESS_BOOK_MODULES", EDS_TEST_ADDRESS_BOOK_DIR, TRUE));
	g_assert (g_setenv ("EDS_REGISTRY_MODULES", EDS_TEST_REGISTRY_DIR, TRUE));
	g_assert (g_setenv ("EDS_CAMEL_PROVIDER_DIR", EDS_TEST_CAMEL_DIR, TRUE));
	g_assert (g_setenv ("EDS_SUBPROCESS_CAL_PATH", EDS_TEST_SUBPROCESS_CAL_PATH, TRUE));
	g_assert (g_setenv ("EDS_SUBPROCESS_BOOK_PATH", EDS_TEST_SUBPROCESS_BOOK_PATH, TRUE));
	g_assert (g_setenv ("GIO_USE_VFS", "local", TRUE));
	g_assert (g_setenv ("EDS_TESTING", "1", TRUE));
	g_assert (g_setenv ("GSETTINGS_BACKEND", "memory", TRUE));

	g_unsetenv ("DISPLAY");

	g_string_free (libs_dir, TRUE);
}

static void
delete_work_directory (void)
{
	/* XXX Instead of complex error checking here, we should ideally use
	 * a recursive GDir / g_unlink() function.
	 *
	 * We cannot use GFile and the recursive delete function without
	 * corrupting our contained D-Bus environment with service files
	 * from the OS.
	 */
	const gchar *argv[] = { "/bin/rm", "-rf", EDS_TEST_WORK_DIR, NULL };
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

static gboolean
e_test_server_utils_bootstrap_timeout (FixturePair *pair)
{
	g_error ("Timed out while waiting for ESource creation from the registry");

	pair->fixture->timeout_source_id = 0;
	return FALSE;
}

static void
e_test_server_utils_client_ready (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
	FixturePair *pair = (FixturePair *) user_data;
	GError *error = NULL;

	switch (pair->closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
		pair->fixture->service.book_client = (EBookClient *)
			e_book_client_connect_finish (res, &error);

		if (!pair->fixture->service.book_client)
			g_error ("Unable to create the test book: %s", error->message);

		break;
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
		pair->fixture->service.book_client = (EBookClient *)
			e_book_client_connect_direct_finish (res, &error);

		if (!pair->fixture->service.book_client)
			g_error ("Unable to create the test book: %s", error->message);

		break;
	case E_TEST_SERVER_CALENDAR:
		pair->fixture->service.calendar_client = (ECalClient *)
			e_cal_client_connect_finish (res, &error);

		if (!pair->fixture->service.calendar_client)
			g_error ("Unable to create the test calendar: %s", error->message);

		break;
	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:
	case E_TEST_SERVER_DEPRECATED_CALENDAR:
	case E_TEST_SERVER_NONE:
		g_assert_not_reached ();
	}

	/* Track ref counts now that we have a client */
	add_weak_ref (pair->fixture, pair->closure->type);

	g_main_loop_quit (pair->fixture->loop);
}

static void
e_test_server_utils_source_added (ESourceRegistry *registry,
                                  ESource *source,
                                  FixturePair *pair)
{
	GError  *error = NULL;

	if (g_strcmp0 (e_source_get_uid (source), pair->fixture->source_name) != 0)
		return;

	switch (pair->closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:

		if (pair->closure->type == E_TEST_SERVER_DIRECT_ADDRESS_BOOK) {
			if (pair->closure->use_async_connect)
				e_book_client_connect_direct (source, (guint32) -1, NULL, e_test_server_utils_client_ready, pair);
			else
				pair->fixture->service.book_client = (EBookClient *)
					e_book_client_connect_direct_sync (
						pair->fixture->registry,
						source, (guint32) -1, NULL, &error);
		} else {

			if (pair->closure->use_async_connect)
				e_book_client_connect (source, (guint32) -1, NULL, e_test_server_utils_client_ready, pair);
			else
				pair->fixture->service.book_client = (EBookClient *)
					e_book_client_connect_sync (source, (guint32) -1, NULL, &error);
		}

		if (!pair->closure->use_async_connect &&
		    !pair->fixture->service.book_client)
			g_error ("Unable to create the test book: %s", error ? error->message : "Unknown error");

		break;

	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:

		/* Dont bother testing the Async apis for deprecated APIs */
		pair->fixture->service.book = e_book_new (source, &error);
		if (!pair->fixture->service.book)
			g_error ("Unable to create the test book: %s", error->message);

		if (!e_book_open (pair->fixture->service.book, FALSE, &error))
			g_error ("Unable to open book: %s", error->message);

		break;

	case E_TEST_SERVER_CALENDAR:

		if (pair->closure->use_async_connect) {
			e_cal_client_connect (
				source, pair->closure->calendar_source_type, (guint32) -1,
				NULL, e_test_server_utils_client_ready, pair);

		} else {

			pair->fixture->service.calendar_client = (ECalClient *)
				e_cal_client_connect_sync (
					source,
					pair->closure->calendar_source_type, (guint32) -1, NULL, &error);
			if (!pair->fixture->service.calendar_client)
				g_error ("Unable to create the test calendar: %s", error->message);
		}

		break;

	case E_TEST_SERVER_DEPRECATED_CALENDAR:

		/* Dont bother testing the Async apis for deprecated APIs */
		pair->fixture->service.calendar = e_cal_new (source, pair->closure->calendar_source_type);
		if (!pair->fixture->service.calendar)
			g_error ("Unable to create the test calendar");

		if (!e_cal_open (pair->fixture->service.calendar, FALSE, &error))
			g_error ("Unable to open calendar: %s", error->message);

		break;

	case E_TEST_SERVER_NONE:
		return;
	}

	/* Add the weak ref now if we just created it */
	if (pair->closure->type != E_TEST_SERVER_NONE &&
	    pair->closure->use_async_connect == FALSE)
		add_weak_ref (pair->fixture, pair->closure->type);

	if (!pair->closure->use_async_connect)
		g_main_loop_quit (pair->fixture->loop);
}

static gboolean
e_test_server_utils_bootstrap_idle (FixturePair *pair)
{
	ESourceBackend *backend = NULL;
	ESource *scratch = NULL;
	GError  *error = NULL;

	pair->fixture->registry = e_source_registry_new_sync (NULL, &error);

	if (!pair->fixture->registry)
		g_error ("Unable to create the test registry: %s", error->message);

	/* Add weak ref for the registry */
	add_weak_ref (pair->fixture, E_TEST_SERVER_NONE);

	g_signal_connect (
		pair->fixture->registry, "source-added",
		G_CALLBACK (e_test_server_utils_source_added), pair);

	/* Create an address book */
	switch (pair->closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:

		if (!pair->fixture->source_name)
			pair->fixture->source_name = generate_source_name ();

		scratch = e_source_new_with_uid (pair->fixture->source_name, NULL, &error);
		if (!scratch)
			g_error ("Failed to create scratch source for an addressbook: %s", error->message);

		/* Ensure Book type */
		backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
		e_source_backend_set_backend_name (backend, "local");

		break;
	case E_TEST_SERVER_CALENDAR:
	case E_TEST_SERVER_DEPRECATED_CALENDAR:

		if (!pair->fixture->source_name)
			pair->fixture->source_name = generate_source_name ();

		scratch = e_source_new_with_uid (pair->fixture->source_name, NULL, &error);
		if (!scratch)
			g_error ("Failed to create scratch source for a calendar: %s", error->message);

		/* Ensure Calendar type source (how to specify the backend here ?? */
		backend = e_source_get_extension (scratch, E_SOURCE_EXTENSION_CALENDAR);
		e_source_backend_set_backend_name (backend, "local");

		break;

	case E_TEST_SERVER_NONE:
		break;
	}

	if (scratch) {
		if (pair->closure->customize)
			pair->closure->customize (scratch, pair->closure);

		if (!e_source_registry_commit_source_sync (pair->fixture->registry, scratch, NULL, &error)) {
			/* Allow sources to carry from one test to the next, if the keep_work_directory
			 * semantics are used then that's what we want (to reuse a source from the
			 * previous test case).
			 */
			if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				ESource *source = e_source_registry_ref_source (
					pair->fixture->registry,
					pair->fixture->source_name);

				g_clear_error (&error);

				g_assert (E_IS_SOURCE (source));

				e_test_server_utils_source_added (pair->fixture->registry, source, pair);
				g_object_unref (source);
			} else
				g_error ("Unable to add new addressbook source to the registry: %s", error->message);
		}

		g_object_unref (scratch);
	}

	if (pair->closure->type != E_TEST_SERVER_NONE)
		pair->fixture->timeout_source_id =
			g_timeout_add_seconds (20, (GSourceFunc) e_test_server_utils_bootstrap_timeout, pair);
	else
		g_main_loop_quit (pair->fixture->loop);

	return FALSE;
}

/*****************************************************************
 *                  Fixture setup and teardown                   *
 *****************************************************************/
/**
 * e_test_server_utils_setup:
 * @fixture: A #ETestServerFixture
 * @user_data: A #ETestServerClosure or derived structure provided by the test.
 *
 * A setup function for the #ETestServerFixture fixture
 */
void
e_test_server_utils_setup (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	ETestServerClosure *closure = (ETestServerClosure *) user_data;
	FixturePair         pair = { fixture, closure };

	/* Create work directory */
	if (!test_installed_services ())
		g_assert (g_mkdir_with_parents (EDS_TEST_WORK_DIR, 0755) == 0);

	/* Init refs */
	g_weak_ref_init (&fixture->registry_ref, NULL);
	g_weak_ref_init (&fixture->client_ref, NULL);

	fixture->loop = g_main_loop_new (NULL, FALSE);

	if (!test_installed_services ()) {
#if !GLOBAL_DBUS_DAEMON
		/* Create the global dbus-daemon for this test suite */
		fixture->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

		/* Add the private directory with our in-tree service files */
		g_test_dbus_add_service_dir (fixture->dbus, EDS_TEST_DBUS_SERVICE_DIR);

		/* Start the private D-Bus daemon */
		g_test_dbus_up (fixture->dbus);
#else
		fixture->dbus = global_test_dbus;
#endif
	}

	g_idle_add ((GSourceFunc) e_test_server_utils_bootstrap_idle, &pair);
	g_main_loop_run (fixture->loop);

	/* This needs to be explicitly removed, otherwise the timeout source
	 * stays in the default GMainContext and after running tests for 20 seconds
	 * in the same test suite... the tests bail out.
	 */
	if (fixture->timeout_source_id) {
		g_source_remove (fixture->timeout_source_id);
		fixture->timeout_source_id = 0;
	}

	g_signal_handlers_disconnect_by_func (fixture->registry, e_test_server_utils_source_added, &pair);
}

/**
 * e_test_server_utils_teardown:
 * @fixture: A #ETestServerFixture
 * @user_data: A #ETestServerClosure or derived structure provided by the test.
 *
 * A teardown function for the #ETestServerFixture fixture
 */
void
e_test_server_utils_teardown (ETestServerFixture *fixture,
                              gconstpointer user_data)
{
	ETestServerClosure *closure = (ETestServerClosure *) user_data;
	GError             *error = NULL;

	/* Try to finalize the EClient */
	switch (closure->type) {
	case E_TEST_SERVER_ADDRESS_BOOK:
	case E_TEST_SERVER_DIRECT_ADDRESS_BOOK:
		if (!closure->keep_work_directory &&
		    !e_client_remove_sync (E_CLIENT (fixture->service.book_client), NULL, &error)) {
			g_message ("Failed to remove test book: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.book_client);
		fixture->service.book_client = NULL;
		break;

	case E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK:
		if (!closure->keep_work_directory &&
		    !e_book_remove (fixture->service.book, &error)) {
			g_message ("Failed to remove test book: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.book);
		fixture->service.book = NULL;
		break;

	case E_TEST_SERVER_CALENDAR:
		if (!closure->keep_work_directory &&
		    !e_client_remove_sync (E_CLIENT (fixture->service.calendar_client), NULL, &error)) {
			g_message ("Failed to remove test calendar: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.calendar_client);
		fixture->service.calendar_client = NULL;
		break;

	case E_TEST_SERVER_DEPRECATED_CALENDAR:
		if (!closure->keep_work_directory &&
		    !e_cal_remove (fixture->service.calendar, &error)) {
			g_message ("Failed to remove test calendar: %s (ignoring)", error->message);
			g_clear_error (&error);
		}
		g_object_unref (fixture->service.calendar);
		fixture->service.calendar = NULL;

	case E_TEST_SERVER_NONE:
		break;
	}

	/* Assert that our EClient has finalized */
	if (closure->type != E_TEST_SERVER_NONE)
		assert_object_finalized (fixture, closure->type);

	/* Try to finalize the registry now */
	g_object_run_dispose (G_OBJECT (fixture->registry));
	g_object_unref (fixture->registry);

	/* Assert that the registry finalizes */
	assert_object_finalized (fixture, E_TEST_SERVER_NONE);

	g_free (fixture->source_name);
	g_main_loop_unref (fixture->loop);
	fixture->registry = NULL;
	fixture->loop = NULL;
	fixture->service.generic = NULL;

	/* Clear refs */
	g_weak_ref_clear (&fixture->registry_ref);
	g_weak_ref_clear (&fixture->client_ref);

	if (!test_installed_services ()) {
#if !GLOBAL_DBUS_DAEMON
		/* Teardown the D-Bus Daemon
		 *
		 * Note that we intentionally leak the TestDBus daemon
		 * in this case, presumably this is due to some leaked
		 * GDBusConnection reference counting
		 */
		g_test_dbus_down (fixture->dbus);
		g_object_unref (fixture->dbus);
		fixture->dbus = NULL;
#else
		fixture->dbus = NULL;
#endif
	}

	/* Cleanup work directory
	 *
	 * XXX This is avoided for now since we are currently using
	 * a separate ESource UID for each test, removing the work directory
	 * would cause the cache-reaper module to spew error messages when
	 * attempting to move missing removed ESources to the trash.
	 *
	 * This should probably be all completely removed once the
	 * GLOBAL_DBUS_DAEMON clauses can be removed.
	 */
	/* if (!closure->keep_work_directory && !test_installed_services ()) */
	/* 	delete_work_directory (); */

	/* Destroy dynamically allocated closure */
	if (closure->destroy_closure_func)
		closure->destroy_closure_func (closure);
}

gint
e_test_server_utils_run (void)
{
	return e_test_server_utils_run_full (0);
}

gint
e_test_server_utils_run_full (ETestServerFlags flags)
{
	gint tests_ret;

	/* Cleanup work directory */
	if (!test_installed_services ()) {

		if ((flags & E_TEST_SERVER_KEEP_WORK_DIRECTORY) == 0)
			delete_work_directory ();

		setup_environment ();
	}

#if GLOBAL_DBUS_DAEMON
	if (!test_installed_services ()) {
		/* Create the global dbus-daemon for this test suite */
		global_test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

		/* Add the private directory with our in-tree service files */
		g_test_dbus_add_service_dir (global_test_dbus, EDS_TEST_DBUS_SERVICE_DIR);

		/* Start the private D-Bus daemon */
		g_test_dbus_up (global_test_dbus);
	}
#endif

	/* Run the GTest suite */
	tests_ret = g_test_run ();

#if GLOBAL_DBUS_DAEMON
	if (!test_installed_services ()) {
		/* Teardown the D-Bus Daemon
		 *
		 * Note that we intentionally leak the TestDBus daemon
		 * in this case, presumably this is due to some leaked
		 * GDBusConnection reference counting
		 */
		g_test_dbus_stop (global_test_dbus);
		/* g_object_unref (global_test_dbus); */
		global_test_dbus = NULL;
	}
#endif

	return tests_ret;
}
