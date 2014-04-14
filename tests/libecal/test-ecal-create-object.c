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
#include <libical/ical.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_create_object (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	ECal *cal;
	icalcomponent *component;
	icalcomponent *component_final;
	gchar *uid;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	component = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	uid = ecal_test_utils_cal_create_object (cal, component);

	/* Assert that we can fetch the newly created component
	 * and that it's valid
	 */
	component_final = ecal_test_utils_cal_get_object (cal, uid);
	g_assert (component_final);
	g_assert (icalcomponent_is_valid (component_final));

	g_free (uid);
	icalcomponent_free (component);
	icalcomponent_free (component_final);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/CreateObject",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_create_object,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
