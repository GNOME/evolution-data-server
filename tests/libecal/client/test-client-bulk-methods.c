/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "client-test-utils.h"

#define NB_COMPONENTS 5

static gboolean
test_icalcomps (icalcomponent *icalcomp1,
                                icalcomponent *icalcomp2)
{
	struct icaltimetype t1, t2;

	if (!icalcomp2) {
		g_printerr ("Failure: get object returned NULL\n");
		return FALSE;
	}

	if (g_strcmp0 (icalcomponent_get_uid (icalcomp1), icalcomponent_get_uid (icalcomp2)) != 0) {
		g_printerr ("Failure: uid doesn't match, expected '%s', got '%s'\n", icalcomponent_get_uid (icalcomp1), icalcomponent_get_uid (icalcomp2));
		return FALSE;
	}

	if (g_strcmp0 (icalcomponent_get_summary (icalcomp1), icalcomponent_get_summary (icalcomp2)) != 0) {
		g_printerr ("Failure: summary doesn't match, expected '%s', got '%s'\n", icalcomponent_get_summary (icalcomp1), icalcomponent_get_summary (icalcomp2));
		return FALSE;
	}

	t1 = icalcomponent_get_dtstart (icalcomp1);
	t2 = icalcomponent_get_dtstart (icalcomp2);

	if (icaltime_compare (t1, t2) != 0) {
		g_printerr ("Failure: dtend doesn't match, expected '%s', got '%s'\n", icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));
		return FALSE;
	}

	t1 = icalcomponent_get_dtend (icalcomp1);
	t2 = icalcomponent_get_dtend (icalcomp2);

	if (icaltime_compare (t1, t2) != 0) {
		g_printerr ("Failure: dtend doesn't match, expected '%s', got '%s'\n", icaltime_as_ical_string (t1), icaltime_as_ical_string (t2));
		return FALSE;
	}

	return TRUE;
}

static gboolean
check_removed (ECalClient *cal_client,
                           const GSList *uids)
{
	g_return_val_if_fail (cal_client != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	while (uids) {
		GError *error = NULL;
		icalcomponent *icalcomp = NULL;

		if (!e_cal_client_get_object_sync (cal_client, uids->data, NULL, &icalcomp, NULL, &error) &&
				g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
			g_clear_error (&error);
		} else {
			report_error ("check objects removed sync", &error);
			icalcomponent_free (icalcomp);
			return FALSE;
		}

		uids = uids->next;
	}

	return TRUE;
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

static gboolean
check_icalcomps_exist (ECalClient *cal_client,
                                           GSList *icalcomps)
{
	const GSList *l;

	for (l = icalcomps; l; l = l->next) {
		GError *error = NULL;
		icalcomponent *icalcomp = l->data;
		icalcomponent *icalcomp2 = NULL;
		const gchar *uid = icalcomponent_get_uid (icalcomp);

		if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp2, NULL, &error)) {
			report_error ("get object sync", &error);
			return FALSE;
		}

		g_return_val_if_fail (icalcomp2 != NULL, FALSE);

		if (!test_icalcomps (icalcomp, icalcomp2)) {
			icalcomponent_free (icalcomp2);
			return FALSE;
		}

		icalcomponent_free (icalcomp2);
	}

	return TRUE;
}

static gboolean
test_bulk_methods (GSList *icalcomps)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *uids = NULL, *ids = NULL;
	const GSList *lcomp, *luid;
	gint i = 0;

	g_return_val_if_fail (icalcomps != NULL, FALSE);

	cal_client = new_temp_client (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, NULL);
	g_return_val_if_fail (cal_client != NULL, FALSE);

	if (!e_client_open_sync (E_CLIENT (cal_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	/* Create all the objects in bulk */
	if (!e_cal_client_create_objects_sync (cal_client, icalcomps, &uids, NULL, &error)) {
		report_error ("create objects sync", &error);
		g_object_unref (cal_client);
		return FALSE;
	}

	g_return_val_if_fail (uids != NULL, FALSE);
	g_return_val_if_fail (g_slist_length (uids) == NB_COMPONENTS, FALSE);

	/* Update icalcomponents uids */
	luid = uids;
	lcomp = icalcomps;
	while (luid && lcomp) {
		icalcomponent_set_uid (lcomp->data, luid->data);
		luid = luid->next;
		lcomp = lcomp->next;
	}

	/* Retrieve all the objects and check that they are the same */
	if (!check_icalcomps_exist (cal_client, icalcomps)) {
		g_object_unref (cal_client);
		g_slist_free_full (uids, g_free);
		return FALSE;
	}

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
	if (!e_cal_client_modify_objects_sync (cal_client, icalcomps, CALOBJ_MOD_ALL, NULL, &error)) {
		report_error ("modify objects sync", &error);
		g_object_unref (cal_client);
		g_slist_free_full (uids, g_free);
		return FALSE;
	}

	/* Retrieve all the objects and check that they have been modified */
	if (!check_icalcomps_exist (cal_client, icalcomps)) {
		g_object_unref (cal_client);
		g_slist_free_full (uids, g_free);
		return FALSE;
	}

	/* Remove all the objects in bulk */
	ids = uid_slist_to_ecalcomponentid_slist (uids);

	if (!e_cal_client_remove_objects_sync (cal_client, ids, CALOBJ_MOD_ALL, NULL, &error)) {
		report_error ("remove objects sync", &error);
		g_object_unref (cal_client);
		g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
		g_slist_free_full (uids, g_free);
		return FALSE;
	}
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);

	/* Check that the objects don't exist anymore */
	if (!check_removed (cal_client, uids)) {
		g_object_unref (cal_client);
		g_slist_free_full (uids, g_free);
		return FALSE;
	}

	g_slist_free_full (uids, g_free);
	g_object_unref (cal_client);
	return TRUE;
}

gint
main (gint argc,
          gchar **argv)
{
	GSList *icalcomps = NULL;
	struct icaltimetype now;
	gint i;
	gboolean res;

	main_initialize ();

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
	res = test_bulk_methods (icalcomps);

	g_slist_free_full (icalcomps, (GDestroyNotify) icalcomponent_free);

	return (res != TRUE);
}
