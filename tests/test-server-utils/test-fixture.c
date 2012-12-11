/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* test-fixture.c - Test to ensure the server test fixture works.
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "e-test-server-utils.h"

static ETestServerClosure registry_closure = { E_TEST_SERVER_NONE, NULL, 0 };
static ETestServerClosure book_closure     = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };
static ETestServerClosure calendar_closure = { E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };
static ETestServerClosure deprecated_book_closure     = { E_TEST_SERVER_DEPRECATED_ADDRESS_BOOK, NULL, 0 };
static ETestServerClosure deprecated_calendar_closure = { E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
empty_test (ETestServerFixture *fixture,
	    gconstpointer       user_data)
{
	/* Basic Empty case just to run the fixture */
}

int
main (int   argc,
      char *argv[])
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
  g_type_init ();
#endif
  g_test_init (&argc, &argv, NULL);

  /* Test that internal implementations can return all kinds of type through its api */
  g_test_add ("/Fixture/Registry1", ETestServerFixture, &registry_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Registry2", ETestServerFixture, &registry_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Registry3", ETestServerFixture, &registry_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Registry4", ETestServerFixture, &registry_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);

  g_test_add ("/Fixture/Book1", ETestServerFixture, &book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Book2", ETestServerFixture, &book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Book3", ETestServerFixture, &book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Book4", ETestServerFixture, &book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);

  g_test_add ("/Fixture/Calendar1", ETestServerFixture, &calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Calendar2", ETestServerFixture, &calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Calendar3", ETestServerFixture, &calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Calendar4", ETestServerFixture, &calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);

  g_test_add ("/Fixture/Deprecated/Book1", ETestServerFixture, &deprecated_book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Deprecated/Book2", ETestServerFixture, &deprecated_book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Deprecated/Book3", ETestServerFixture, &deprecated_book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Deprecated/Book4", ETestServerFixture, &deprecated_book_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);

  g_test_add ("/Fixture/Deprecated/Calendar1", ETestServerFixture, &deprecated_calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Deprecated/Calendar2", ETestServerFixture, &deprecated_calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Deprecated/Calendar3", ETestServerFixture, &deprecated_calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  g_test_add ("/Fixture/Deprecated/Calendar4", ETestServerFixture, &deprecated_calendar_closure,
  	      e_test_server_utils_setup, empty_test, e_test_server_utils_teardown);
  
  return e_test_server_utils_run ();
}
