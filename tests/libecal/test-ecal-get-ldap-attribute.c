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
#include <libecal/libecal.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_get_ldap_attribute (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECal *cal;
	GError *error = NULL;
	gchar *attr = NULL;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	if (e_cal_get_ldap_attribute (cal, &attr, &error))
		g_error ("e_cal_get_ldap_attribute() is dropped but returned TRUE");
	else if (!g_error_matches (error, E_CALENDAR_ERROR, E_CALENDAR_STATUS_NOT_SUPPORTED))
		g_error (
			"e_cal_get_ldap_attribute() returned unexpected "
			"error code '%d' from domain %s: %s",
			error->code,
			g_quark_to_string (error->domain),
			error->message);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/GetLdapAttribute/NotSupported",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_get_ldap_attribute,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
