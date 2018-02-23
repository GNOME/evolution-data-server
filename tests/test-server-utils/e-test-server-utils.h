/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-test-server-utils.h - Test scaffolding to run tests with in-tree data server.
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

#ifndef E_TEST_UTILS_H
#define E_TEST_UTILS_H

#include <libedataserver/libedataserver.h>
#include <libebook/libebook.h>
#include <libecal/libecal.h>

typedef struct _ETestServerFixture ETestServerFixture;
typedef struct _ETestServerClosure ETestServerClosure;

/**
 * E_TEST_SERVER_UTILS_SERVICE:
 * @fixture: An #ETestServerFixture
 * @service_type: The type to cast for the service in use
 *
 * A convenience macro to fetch the service for a given test case:
 *
 * |[
 *    EBookClient *book = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
 * ]|
 *
 */
#define E_TEST_SERVER_UTILS_SERVICE(fixture, service_type) \
	((service_type *)((ETestServerFixture *) fixture)->service.generic)

/**
 * ETestSourceCustomizeFunc:
 * @scratch: The scratch #ESource template being used to create an addressbook or calendar
 * @closure: The #ETestServerClosure for this test case
 *
 * This can be used to parameterize the addressbook or calendar @scratch #ESource
 * before creating/committing it.
 */
typedef void (* ETestSourceCustomizeFunc) (ESource            *scratch,
					   ETestServerClosure *closure);

/**
 * ETestServiceType:
 * @E_TEST_SERVER_NONE: Only the #ESourceRegistry will be created
 * @E_TEST_SERVER_ADDRESS_BOOK: An #EBookClient will be created and opened for the test
 * @E_TEST_SERVER_DIRECT_ADDRESS_BOOK: An #EBookClient in direct read access mode will be created and opened for the test
 * @E_TEST_SERVER_CALENDAR: An #ECalClient will be created and opened for the test
 * @E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK: An #EBook will be created and opened for the test
 * @E_TEST_SERVER_DEPRECATED_CALENDAR: An #ECal will be created and opened for the test
 *
 * The type of service to test
 */
typedef enum {
	E_TEST_SERVER_NONE = 0,
	E_TEST_SERVER_ADDRESS_BOOK,
	E_TEST_SERVER_DIRECT_ADDRESS_BOOK,
	E_TEST_SERVER_CALENDAR,
	E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK,
	E_TEST_SERVER_DEPRECATED_CALENDAR
} ETestServiceType;

/**
 * ETestServerClosure:
 * @type:                 An #ETestServiceType, type of the service
 * @customize:            An #ETestSourceCustomizeFunc to use to parameterize the scratch #ESource, or %NULL
 * @calendar_source_type: An #ECalClientSourceType or #ECalSourceType; for %E_TEST_SERVER_CALENDAR
 *                        and %E_TEST_SERVER_DEPRECATED_CALENDAR tests
 * @keep_work_directory:  If specified, the work directory will not be deleted between tests
 * @destroy_closure_func: A function to destroy an allocated #ETestServerClosure, this it
 *                        typically used by sub-fixtures which embed this structure in their closures.
 * @use_async_connect:    Specifies whether a synchronous or asyncrhonous API should be used to
 *                        create the #EClient you plan to test.
 *
 * This structure provides the parameters for the #ETestServerFixture tests,
 * it can be included as the first member of a derived structure
 * for any tests deriving from the #ETestServerFixture test type
 */
struct _ETestServerClosure {
	ETestServiceType         type;
	ETestSourceCustomizeFunc customize;
	gint                     calendar_source_type;
	gboolean                 keep_work_directory;
	GDestroyNotify           destroy_closure_func;
	gboolean                 use_async_connect;
};

/**
 * ETestService:
 * @generic: A generic pointer for the test service.
 * @book_client: An #EBookClient, created for %E_TEST_SERVER_ADDRESS_BOOK tests
 * @calendar_client: An #ECalClient, created for %E_TEST_SERVER_CALENDAR tests
 * @book: An #EBook, created for %E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK tests
 * @calendar: An #ECal, created for %E_TEST_SERVER_DEPRECATED_CALENDAR tests
 *
 * A union of service types, holds the object to test in a #ETestServerFixture.
 *
 */
typedef union {
	gpointer     generic;
	EBookClient *book_client;
	ECalClient  *calendar_client;
	EBook       *book;
	ECal        *calendar;
} ETestService;

/**
 * ETestServerFixture:
 * @loop: A Main loop to run traffic in
 * @dbus: The D-Bus test scaffold
 * @registry: An #ESourceRegistry
 * @service: The #ETestService
 * @source_name: This can be given an allocated string before calling e_test_server_utils_setup() and will be the #ESource UID
 * for the test addressbook or calendar, leaving this %NULL will result in a suitable unique id being generated for the test. 
 *
 * A fixture for running tests on the Evolution Data Server
 * components in an encapsulated D-Bus environment.
 */
struct _ETestServerFixture {
	GMainLoop       *loop;
	GTestDBus       *dbus;
	ESourceRegistry *registry;
	ETestService     service;
	gchar           *source_name;

	/*< private >*/
	guint            timeout_source_id;
	GWeakRef         registry_ref;
	GWeakRef         client_ref;
};

/**
 * ETestServiceFlags:
 * @E_TEST_SERVER_KEEP_WORK_DIRECTORY: Don't delete working directory upon startup.
 *
 * Instructions upon e_test_server_utils_run_full() operation.
 */
typedef enum {
	E_TEST_SERVER_KEEP_WORK_DIRECTORY = (1 << 0)
} ETestServerFlags;

void e_test_server_utils_setup    (ETestServerFixture *fixture,
				   gconstpointer       user_data);

void e_test_server_utils_teardown (ETestServerFixture *fixture,
				   gconstpointer       user_data);

gint e_test_server_utils_run      (void);
gint e_test_server_utils_run_full (ETestServerFlags flags);
void e_test_server_utils_prepare_run (ETestServerFlags flags);
void e_test_server_utils_finish_run (void);

#endif /* E_TEST_UTILS_H */
