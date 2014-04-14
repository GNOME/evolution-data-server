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

#define EVENT_SUMMARY "Creation of new test event"

static void
test_create_object_2 (ETestServerFixture *fixture,
                      gconstpointer user_data)
{
	ECal *cal;
	ECalComponent *e_component, *e_component_final;
	icalcomponent *icalcomponent_final;
	gchar *uid;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	ecal_test_utils_create_component (
		cal,
		"20040109T090000Z", "UTC",
		"20040109T103000", "UTC",
		EVENT_SUMMARY, &e_component, &uid);

	icalcomponent_final = ecal_test_utils_cal_get_object (cal, uid);
	e_component_final = e_cal_component_new ();
	ecal_test_utils_cal_component_set_icalcomponent (
		e_component_final, icalcomponent_final);
	ecal_test_utils_cal_assert_e_cal_components_equal (
		e_component, e_component_final);

	g_object_unref (e_component_final);
	g_object_unref (e_component);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/CreateObject2",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		test_create_object_2,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
