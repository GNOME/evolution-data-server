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
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };

#define NB_COMPONENTS 5

static void
test_icomps (ICalComponent *icomp1,
	     ICalComponent *icomp2)
{
	ICalTime *t1, *t2;

	if (!icomp2)
		g_error ("Failure: get object returned NULL");

	g_assert_cmpstr (i_cal_component_get_uid (icomp1), ==, i_cal_component_get_uid (icomp2));
	g_assert_cmpstr (i_cal_component_get_summary (icomp1), ==, i_cal_component_get_summary (icomp2));

	t1 = i_cal_component_get_dtstart (icomp1);
	t2 = i_cal_component_get_dtstart (icomp2);

	if (i_cal_time_compare (t1, t2) != 0) {
		gchar *str1, *str2;

		str1 = i_cal_time_as_ical_string (t1);
		str2 = i_cal_time_as_ical_string (t2);

		g_error ("Failure: dtend doesn't match, expected '%s', got '%s'\n", str1, str2);

		g_free (str1);
		g_free (str2);
	}

	g_clear_object (&t1);
	g_clear_object (&t2);

	t1 = i_cal_component_get_dtend (icomp1);
	t2 = i_cal_component_get_dtend (icomp2);

	if (i_cal_time_compare (t1, t2) != 0) {
		gchar *str1, *str2;

		str1 = i_cal_time_as_ical_string (t1);
		str2 = i_cal_time_as_ical_string (t2);

		g_error ("Failure: dtend doesn't match, expected '%s', got '%s'\n", str1, str2);

		g_free (str1);
		g_free (str2);
	}

	g_clear_object (&t1);
	g_clear_object (&t2);
}

static void
check_removed (ECalClient *cal_client,
               const GSList *uids)
{
	g_assert (cal_client != NULL);
	g_assert (uids != NULL);

	while (uids) {
		GError *error = NULL;
		ICalComponent *icomp = NULL;

		if (!e_cal_client_get_object_sync (cal_client, uids->data, NULL, &icomp, NULL, &error) &&
		    g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
			g_clear_error (&error);
		} else if (error) {
			g_error ("check objects removed sync: %s", error->message);
			g_clear_error (&error);
		} else {
			g_clear_object (&icomp);

			g_error ("check objects removed sync: object found in the backend");
		}

		uids = uids->next;
	}
}

static GSList *
uid_slist_to_ecalcomponentid_slist (GSList *uids)
{
	GSList *ids = NULL;
	const GSList *l;

	for (l = uids; l; l = l->next) {
		ids = g_slist_append (ids, e_cal_component_id_new (l->data, NULL));
	}

	return ids;
}

static void
check_icomps_exist (ECalClient *cal_client,
		    GSList *icomps)
{
	GSList *link;

	for (link = icomps; link; link = g_slist_next (link)) {
		GError *error = NULL;
		ICalComponent *icomp = link->data;
		ICalComponent *icomp2 = NULL;
		const gchar *uid = i_cal_component_get_uid (icomp);

		if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icomp2, NULL, &error))
			g_error ("get object sync: %s", error->message);

		g_assert_nonnull (icomp2);

		test_icomps (icomp, icomp2);

		g_object_unref (icomp2);
	}
}

static void
test_bulk_methods_sync (ECalClient *cal_client,
			GSList *icomps)
{
	GError *error = NULL;
	GSList *uids = NULL, *ids = NULL;
	const GSList *lcomp, *luid;
	gint i = 0;

	g_assert_nonnull (icomps);

	/* Create all the objects in bulk */
	if (!e_cal_client_create_objects_sync (cal_client, icomps, E_CAL_OPERATION_FLAG_NONE, &uids, NULL, &error))
		g_error ("create objects sync: %s", error->message);

	g_assert (uids != NULL);
	g_assert_cmpint (g_slist_length (uids), ==, NB_COMPONENTS);

	/* Update ICalComponents uids */
	luid = uids;
	lcomp = icomps;
	while (luid && lcomp) {
		i_cal_component_set_uid (lcomp->data, luid->data);
		luid = luid->next;
		lcomp = lcomp->next;
	}

	/* Retrieve all the objects and check that they are the same */
	check_icomps_exist (cal_client, icomps);

	/* Modify the objects */
	for (lcomp = icomps; lcomp; lcomp = lcomp->next) {
		gchar *summary;
		ICalComponent *icomp = lcomp->data;

		summary = g_strdup_printf ("Edited test summary %d", i);
		i_cal_component_set_summary (icomp, summary);

		g_free (summary);
		++i;
	}

	/* Save the modified objects in bulk */
	if (!e_cal_client_modify_objects_sync (cal_client, icomps, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("modify objects sync: %s", error->message);

	/* Retrieve all the objects and check that they have been modified */
	check_icomps_exist (cal_client, icomps);

	/* Remove all the objects in bulk */
	ids = uid_slist_to_ecalcomponentid_slist (uids);

	if (!e_cal_client_remove_objects_sync (cal_client, ids, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("remove objects sync: %s", error->message);

	g_slist_free_full (ids, e_cal_component_id_free);

	/* Check that the objects don't exist anymore */
	check_removed (cal_client, uids);

	g_slist_free_full (uids, g_free);
}

typedef struct _AsyncContext {
	ECalClient *cal_client;
	GSList *icomps; /* ICalComponent * */
	GSList *ids; /* ECalComponentId * */
	GSList *uids; /* gchar * */
	GMainLoop *main_loop;
} AsyncContext;

static void
bulk_async_remove_objects_cb (GObject *source_object,
			      GAsyncResult *result,
			      gpointer user_data)
{
	AsyncContext *async_context = user_data;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (async_context);
	g_assert (E_IS_CAL_CLIENT (source_object));
	g_assert (async_context->cal_client == E_CAL_CLIENT (source_object));

	success = e_cal_client_remove_objects_finish (async_context->cal_client, result, &error);
	g_assert_no_error (error);
	g_assert (success);

	/* Check that the objects don't exist anymore */
	check_removed (async_context->cal_client, async_context->uids);

	g_main_loop_quit (async_context->main_loop);
}

static void
bulk_async_modify_objects_cb (GObject *source_object,
			      GAsyncResult *result,
			      gpointer user_data)
{
	AsyncContext *async_context = user_data;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (async_context);
	g_assert (E_IS_CAL_CLIENT (source_object));
	g_assert (async_context->cal_client == E_CAL_CLIENT (source_object));

	success = e_cal_client_modify_objects_finish (async_context->cal_client, result, &error);
	g_assert_no_error (error);
	g_assert (success);

	/* Retrieve all the objects and check that they have been modified */
	check_icomps_exist (async_context->cal_client, async_context->icomps);

	/* Remove all the objects in bulk */
	async_context->ids = uid_slist_to_ecalcomponentid_slist (async_context->uids);

	e_cal_client_remove_objects (async_context->cal_client, async_context->ids, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE,
		NULL, bulk_async_remove_objects_cb, async_context);
}

static void
bulk_async_create_objects_cb (GObject *source_object,
			      GAsyncResult *result,
			      gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GSList *luid, *lcomp;
	gboolean success;
	gint ii;
	GError *error = NULL;

	g_assert_nonnull (async_context);
	g_assert (E_IS_CAL_CLIENT (source_object));
	g_assert (async_context->cal_client == E_CAL_CLIENT (source_object));

	success = e_cal_client_create_objects_finish (async_context->cal_client, result, &async_context->uids, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (async_context->uids);
	g_assert_cmpint (g_slist_length (async_context->uids), ==, NB_COMPONENTS);

	/* Update ICalComponents uids */
	for (luid = async_context->uids, lcomp = async_context->icomps;
	     luid && lcomp;
	     luid = g_slist_next (luid), lcomp = g_slist_next (lcomp)) {
		i_cal_component_set_uid (lcomp->data, luid->data);
	}

	/* Retrieve all the objects and check that they are the same */
	check_icomps_exist (async_context->cal_client, async_context->icomps);

	/* Modify the objects */
	for (ii = 0, lcomp = async_context->icomps; lcomp; ii++, lcomp = g_slist_next (lcomp)) {
		gchar *summary;
		ICalComponent *icomp = lcomp->data;

		summary = g_strdup_printf ("Edited test summary %d", ii);
		i_cal_component_set_summary (icomp, summary);

		g_free (summary);
	}

	e_cal_client_modify_objects (async_context->cal_client, async_context->icomps, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE,
		NULL, bulk_async_modify_objects_cb, async_context);
}

static void
test_bulk_methods_async (ECalClient *cal_client,
			 GSList *icomps)
{
	AsyncContext async_context;

	g_assert_nonnull (icomps);

	async_context.cal_client = cal_client;
	async_context.icomps = icomps;
	async_context.ids = NULL;
	async_context.uids = NULL;
	async_context.main_loop = g_main_loop_new (NULL, FALSE);

	e_cal_client_create_objects (async_context.cal_client, async_context.icomps, E_CAL_OPERATION_FLAG_NONE, NULL,
		bulk_async_create_objects_cb, &async_context);

	g_main_loop_run (async_context.main_loop);

	g_slist_free_full (async_context.ids, e_cal_component_id_free);
	g_slist_free_full (async_context.uids, g_free);
	g_main_loop_unref (async_context.main_loop);
}

static void
run_test_bulk_methods_wrapper (ETestServerFixture *fixture,
			       void (* func)(ECalClient *cal_client,
					     GSList *icomps))
{
	ECalClient *cal_client;
	GSList *icomps = NULL;
	ICalTime *dtstart, *dtend;
	gint ii;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	dtstart = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	dtend = i_cal_time_clone (dtstart);
	i_cal_time_adjust (dtend, 0, 1, 0, 0);

	/* Build up new components */
	for (ii = 0; ii < NB_COMPONENTS; ++ii) {
		ICalComponent *icomp;
		gchar *summary;

		icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
		summary = g_strdup_printf ("Test summary %d", ii);
		i_cal_component_set_summary (icomp, summary);
		i_cal_component_set_dtstart (icomp, dtstart);
		i_cal_component_set_dtend (icomp, dtend);

		icomps = g_slist_append (icomps, icomp);
		g_free (summary);
	}

	g_clear_object (&dtstart);
	g_clear_object (&dtend);

	/* Test synchronous bulk methods */
	func (cal_client, icomps);

	g_slist_free_full (icomps, g_object_unref);
}

static void
run_test_bulk_methods_sync (ETestServerFixture *fixture,
			    gconstpointer user_data)
{
	run_test_bulk_methods_wrapper (fixture, test_bulk_methods_sync);
}

static void
run_test_bulk_methods_async (ETestServerFixture *fixture,
			     gconstpointer user_data)
{
	run_test_bulk_methods_wrapper (fixture, test_bulk_methods_async);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/BulkMethods/Sync",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		run_test_bulk_methods_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/BulkMethods/Async",
		ETestServerFixture,
		&cal_closure,
		e_test_server_utils_setup,
		run_test_bulk_methods_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
