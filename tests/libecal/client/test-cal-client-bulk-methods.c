/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };

#define NB_COMPONENTS 5

static void
test_icalcomps (icalcomponent *icalcomp1,
                icalcomponent *icalcomp2)
{
	struct icaltimetype t1, t2;

	if (!icalcomp2)
		g_error ("Failure: get object returned NULL");

	g_assert_cmpstr (icalcomponent_get_uid (icalcomp1), ==, icalcomponent_get_uid (icalcomp2));
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp1), ==, icalcomponent_get_summary (icalcomp2));

	t1 = icalcomponent_get_dtstart (icalcomp1);
	t2 = icalcomponent_get_dtstart (icalcomp2);

	if (icaltime_compare (t1, t2) != 0)
		g_error (
			"Failure: dtend doesn't match, expected '%s', got '%s'\n",
			icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));

	t1 = icalcomponent_get_dtend (icalcomp1);
	t2 = icalcomponent_get_dtend (icalcomp2);

	if (icaltime_compare (t1, t2) != 0)
		g_error (
			"Failure: dtend doesn't match, expected '%s', got '%s'\n",
			icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));
}

static void
check_removed (ECalClient *cal_client,
               const GSList *uids)
{
	g_assert (cal_client != NULL);
	g_assert (uids != NULL);

	while (uids) {
		GError *error = NULL;
		icalcomponent *icalcomp = NULL;

		if (!e_cal_client_get_object_sync (cal_client, uids->data, NULL, &icalcomp, NULL, &error) &&
				g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
			g_clear_error (&error);
		} else
			g_error ("check objects removed sync: %s", error->message);

		uids = uids->next;
	}
}

static GSList *
uid_slist_to_ecalcomponentid_slist (GSList *uids)
{
	GSList *ids = NULL;
	const GSList *l;

	for (l = uids; l; l = l->next) {
		ECalComponentId *id = g_new0 (ECalComponentId, 1);
		id->uid = g_strdup (l->data);
		ids = g_slist_append (ids, id);
	}

	return ids;
}

static void
check_icalcomps_exist (ECalClient *cal_client,
                       GSList *icalcomps)
{
	const GSList *l;

	for (l = icalcomps; l; l = l->next) {
		GError *error = NULL;
		icalcomponent *icalcomp = l->data;
		icalcomponent *icalcomp2 = NULL;
		const gchar *uid = icalcomponent_get_uid (icalcomp);

		if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp2, NULL, &error))
			g_error ("get object sync: %s", error->message);

		g_assert (icalcomp2 != NULL);

		test_icalcomps (icalcomp, icalcomp2);
		icalcomponent_free (icalcomp2);
	}
}

static void
test_bulk_methods (ECalClient *cal_client,
                   GSList *icalcomps)
{
	GError *error = NULL;
	GSList *uids = NULL, *ids = NULL;
	const GSList *lcomp, *luid;
	gint i = 0;

	g_assert (icalcomps != NULL);

	/* Create all the objects in bulk */
	if (!e_cal_client_create_objects_sync (cal_client, icalcomps, &uids, NULL, &error))
		g_error ("create objects sync: %s", error->message);

	g_assert (uids != NULL);
	g_assert_cmpint (g_slist_length (uids), ==, NB_COMPONENTS);

	/* Update icalcomponents uids */
	luid = uids;
	lcomp = icalcomps;
	while (luid && lcomp) {
		icalcomponent_set_uid (lcomp->data, luid->data);
		luid = luid->next;
		lcomp = lcomp->next;
	}

	/* Retrieve all the objects and check that they are the same */
	check_icalcomps_exist (cal_client, icalcomps);

	/* Modify the objects */
	for (lcomp = icalcomps; lcomp; lcomp = lcomp->next) {
		gchar *summary;
		icalcomponent *icalcomp = lcomp->data;

		summary = g_strdup_printf ("Edited test summary %d", i);
		icalcomponent_set_summary (icalcomp, summary);

		g_free (summary);
		++i;
	}

	/* Save the modified objects in bulk */
	if (!e_cal_client_modify_objects_sync (cal_client, icalcomps, E_CAL_OBJ_MOD_ALL, NULL, &error))
		g_error ("modify objects sync: %s", error->message);

	/* Retrieve all the objects and check that they have been modified */
	check_icalcomps_exist (cal_client, icalcomps);

	/* Remove all the objects in bulk */
	ids = uid_slist_to_ecalcomponentid_slist (uids);

	if (!e_cal_client_remove_objects_sync (cal_client, ids, E_CAL_OBJ_MOD_ALL, NULL, &error))
		g_error ("remove objects sync: %s", error->message);

	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);

	/* Check that the objects don't exist anymore */
	check_removed (cal_client, uids);

	g_slist_free_full (uids, g_free);
}

static void
run_test_bulk_methods (ETestServerFixture *fixture,
                       gconstpointer user_data)
{
	ECalClient *cal_client;
	GSList *icalcomps = NULL;
	struct icaltimetype now;
	gint i;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());

	/* Build up new components */
	for (i = 0; i < NB_COMPONENTS; ++i) {
		icalcomponent *icalcomp;
		gchar *summary;

		icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
		summary = g_strdup_printf ("Test summary %d", i);
		icalcomponent_set_summary (icalcomp, summary);
		icalcomponent_set_dtstart (icalcomp, now);
		icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

		icalcomps = g_slist_append (icalcomps, icalcomp);
		g_free (summary);
	}

	/* Test synchronous bulk methods */
	test_bulk_methods (cal_client, icalcomps);

	g_slist_free_full (icalcomps, (GDestroyNotify) icalcomponent_free);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/BulkMethods",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		run_test_bulk_methods,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
