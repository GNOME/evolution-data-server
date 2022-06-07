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
#include <libecal/libecal.h>

#include "e-test-server-utils.h"

static ETestServerClosure test_closure = { E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };

static ICalComponent *
create_component (const gchar *tz_location)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"SUMMARY:recurs\r\n"
		"UID:recurs-id\r\n"
		"DTSTART%s:20190107T080000%s\r\n"
		"DTEND%s:20190107T090000%s\r\n"
		"DTSTAMP:20190101T050000Z\r\n"
		"CREATED:20190101T050000Z\r\n"
		"LAST-MODIFIED:20190101T050000Z\r\n"
		"RRULE:FREQ=DAILY;COUNT=5\r\n"
		"END:VEVENT\r\n";
	gchar *tzref = NULL, tzsuffix[2] = { 0, 0};
	gchar *str;
	ICalComponent *icomp;
	ICalTimezone *zone = NULL;
	ICalTime *itt;

	if (tz_location) {
		if (g_ascii_strcasecmp (tz_location, "UTC") == 0) {
			tzsuffix[0] = 'Z';
			zone = i_cal_timezone_get_utc_timezone ();
		} else {
			const gchar *tzid;

			zone = i_cal_timezone_get_builtin_timezone (tz_location);
			g_assert_nonnull (zone);

			tzid = i_cal_timezone_get_tzid (zone);
			g_assert_nonnull (tzid);

			tzref = g_strconcat (";TZID=", tzid, NULL);
		}
	}

	str = g_strdup_printf (comp_str, tzref ? tzref : "", tzsuffix, tzref ? tzref : "", tzsuffix);
	icomp = i_cal_component_new_from_string (str);
	g_assert_nonnull (icomp);

	g_free (tzref);
	g_free (str);

	itt = i_cal_component_get_dtstart (icomp);
	g_assert_nonnull (itt);
	g_assert_true (i_cal_time_get_timezone (itt) == zone);
	g_object_unref (itt);

	itt = i_cal_component_get_dtend (icomp);
	g_assert_nonnull (itt);
	g_assert_true (i_cal_time_get_timezone (itt) == zone);
	g_object_unref (itt);

	return icomp;
}

static void
setup_cal (ECalClient *cal_client,
	   const gchar *tz_location)
{
	ICalComponent *icomp;
	gboolean success;
	gchar *uid = NULL;
	GError *error = NULL;

	icomp = create_component (tz_location);

	/* Ignore errors, it'll fail the first time, but will succeed all other times */
	e_cal_client_remove_object_sync (cal_client, i_cal_component_get_uid (icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

	success = e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uid);

	g_object_unref (icomp);
	g_free (uid);
}

typedef struct _Instance {
	ICalTime *start;
	ICalTime *end;
	ICalComponent *icomp;
} Instance;

static Instance *
instance_new (ICalTime *start,
	      ICalTime *end,
	      ICalComponent *icomp)
{
	Instance *ins;

	ins = g_new0 (Instance, 1);
	ins->start = i_cal_time_clone (start);
	ins->end = i_cal_time_clone (end);
	ins->icomp = i_cal_component_clone (icomp);

	return ins;
}

static void
instance_free (gpointer ptr)
{
	Instance *ins = ptr;

	if (ins) {
		g_clear_object (&ins->start);
		g_clear_object (&ins->end);
		g_clear_object (&ins->icomp);
		g_free (ins);
	}
}

static guint
instance_hash (gconstpointer ptr)
{
	/* Place everything into a single basket */
	return 0;
}

static gboolean
instance_equal (gconstpointer ptr1,
		gconstpointer ptr2)
{
	Instance *ins1 = (Instance *) ptr1;
	Instance *ins2 = (Instance *) ptr2;

	if (!ins1 || !ins2)
		return ins1 == ins2;

	return i_cal_time_compare (ins1->start, ins2->start) == 0;
}

static void
verify_received_instances (GHashTable *instances,
			   ICalTimezone *comp_zone)
{
	const gchar *expected_times[] = {
		"20190107T080000",
		"20190108T080000",
		"20190109T080000",
		"20190110T080000",
		"20190111T080000"
	};
	gint ii;

	g_assert_nonnull (instances);

	for (ii = 0; ii < G_N_ELEMENTS (expected_times); ii++) {
		ICalTime *expected_start;
		Instance ins = { 0, };

		expected_start = i_cal_time_new_from_string (expected_times[ii]);
		g_assert_nonnull (expected_start);

		ins.start = expected_start;
		g_assert_true (g_hash_table_remove (instances, &ins));

		g_object_unref (expected_start);
	}

	g_assert_cmpint (g_hash_table_size (instances), ==, 0);
}

typedef struct _RecurData {
	GHashTable *instances;
} RecurData;

static gboolean
recur_instance_cb (ICalComponent *icomp,
		   ICalTime *instance_start,
		   ICalTime *instance_end,
		   gpointer user_data,
		   GCancellable *cancellable,
		   GError **error)
{
	RecurData *rd = user_data;
	Instance *ins;
	gsize pre;

	g_assert_nonnull (rd);

	ins = instance_new (instance_start, instance_end, icomp);

	pre = g_hash_table_size (rd->instances);
	g_hash_table_insert (rd->instances, ins, NULL);

	g_assert_cmpint (pre + 1, ==, g_hash_table_size (rd->instances));

	return TRUE;
}

static void
test_recur_plain_run (ECalClient *client,
		      const gchar *default_tz,
		      const gchar *comp_tz)
{
	ICalTimezone *default_zone, *comp_zone;
	ICalComponent *icomp;
	ICalTime *start, *end;
	RecurData rd;
	gboolean success;
	GError *error = NULL;

	default_zone = default_tz ? i_cal_timezone_get_builtin_timezone (default_tz) : NULL;
	if (default_tz)
		g_assert_nonnull (default_zone);

	comp_zone = comp_tz ? i_cal_timezone_get_builtin_timezone (comp_tz) : NULL;
	if (comp_tz)
		g_assert_nonnull (comp_zone);

	icomp = create_component (comp_tz);
	start = i_cal_time_new_from_string ("20190103T080000Z");
	end = i_cal_time_new_from_string ("20190115T080000Z");

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);

	success = e_cal_recur_generate_instances_sync (icomp, start, end,
		recur_instance_cb, &rd,
		e_cal_client_tzlookup_cb, client,
		default_zone, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 5);

	verify_received_instances (rd.instances, comp_zone);

	g_hash_table_destroy (rd.instances);
	g_clear_object (&icomp);
	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_client_run (ECalClient *client,
		       const gchar *default_tz,
		       const gchar *comp_tz)
{
	ICalTimezone *default_zone, *comp_zone;
	ICalTime *start, *end;
	ICalComponent *icomp;
	RecurData rd;

	default_zone = default_tz ? i_cal_timezone_get_builtin_timezone (default_tz) : NULL;
	if (default_tz)
		g_assert_nonnull (default_zone);

	comp_zone = comp_tz ? i_cal_timezone_get_builtin_timezone (comp_tz) : NULL;
	if (comp_tz)
		g_assert_nonnull (comp_zone);

	e_cal_client_set_default_timezone (client, default_zone);
	setup_cal (client, comp_tz);

	start = i_cal_time_new_from_string ("20190103T080000Z");
	end = i_cal_time_new_from_string ("20190115T080000Z");

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);

	e_cal_client_generate_instances_sync (client,
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		NULL, /* GCancellable * */
		recur_instance_cb, &rd);

	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 5);

	verify_received_instances (rd.instances, comp_zone);

	g_hash_table_destroy (rd.instances);

	icomp = create_component (comp_tz);
	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);

	e_cal_client_generate_instances_for_object_sync (client,
		icomp,
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		NULL, /* GCancellable * */
		recur_instance_cb, &rd);

	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 5);

	verify_received_instances (rd.instances, comp_zone);

	g_hash_table_destroy (rd.instances);
	g_clear_object (&icomp);
	g_clear_object (&start);
	g_clear_object (&end);
}

static const gchar *timezones[] = {
	NULL,			/* floating time */
	"Pacific/Midway",	/* UTC-11 */
	"Pacific/Honolulu",	/* UTC-10 */
	"America/Adak",		/* UTC-9 */
	"America/Yakutat",	/* UTC-8 */
	"America/Vancouver",	/* UTC-7 */
	"America/Boise",	/* UTC-6 */
	"America/Cancun",	/* UTC-5 */
	"America/New_York",	/* UTC-4 */
	"Atlantic/Bermuda",	/* UTC-3 */
	"America/Noronha",	/* UTC-2 */
	"Atlantic/Cape_Verde",	/* UTC-1 */
	"Atlantic/Azores",	/* UTC */
	"UTC",			/* UTC */
	"Africa/Casablanca",	/* UTC+1 */
	"Europe/Malta",		/* UTC+2 */
	"Europe/Athens",	/* UTC+3 */
	"Asia/Baku",		/* UTC+4 */
	"Asia/Qyzylorda",	/* UTC+5 */
	"Asia/Urumqi",		/* UTC+6 */
	"Asia/Hovd",		/* UTC+7 */
	"Asia/Irkutsk",		/* UTC+8 */
	"Asia/Seoul",		/* UTC+9 */
	"Asia/Vladivostok",	/* UTC+10 */
	"Asia/Sakhalin",	/* UTC+11 */
	"Asia/Kamchatka",	/* UTC+12 */
	"Pacific/Enderbury",	/* UTC+13 */
	"Pacific/Kiritimati"	/* UTC+14 */
};

static void
test_recur_plain (ETestServerFixture *fixture,
		  gconstpointer user_data)
{
	ECalClient *cal;
	gint deftz_ii, comptz_ii;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	for (deftz_ii = 0; deftz_ii < G_N_ELEMENTS (timezones); deftz_ii++) {
		for (comptz_ii = 0; comptz_ii < G_N_ELEMENTS (timezones); comptz_ii++) {
			test_recur_plain_run (cal, timezones[deftz_ii], timezones[comptz_ii]);
		}
	}
}

static ICalComponent *
create_component_midnight (const gchar *tz_location)
{
	const gchar *comp_str =
		"BEGIN:VEVENT\r\n"
		"SUMMARY:recurs\r\n"
		"UID:recurs-id\r\n"
		"DTSTART%s:20190107T000000%s\r\n"
		"DTEND%s:20190107T003000%s\r\n"
		"DTSTAMP:20190101T050000Z\r\n"
		"CREATED:20190101T050000Z\r\n"
		"LAST-MODIFIED:20190101T050000Z\r\n"
		"RRULE:FREQ=DAILY;UNTIL=20190109\r\n"
		"END:VEVENT\r\n";
	gchar *tzref = NULL, tzsuffix[2] = { 0, 0 };
	gchar *str;
	ICalComponent *icomp;
	ICalTimezone *zone = NULL;
	ICalTime *itt;

	if (tz_location) {
		if (g_ascii_strcasecmp (tz_location, "UTC") == 0) {
			tzsuffix[0] = 'Z';
			zone = i_cal_timezone_get_utc_timezone ();
		} else {
			const gchar *tzid;

			zone = i_cal_timezone_get_builtin_timezone (tz_location);
			g_assert_nonnull (zone);

			tzid = i_cal_timezone_get_tzid (zone);
			g_assert_nonnull (tzid);

			tzref = g_strconcat (";TZID=", tzid, NULL);
		}
	}

	str = g_strdup_printf (comp_str, tzref ? tzref : "", tzsuffix, tzref ? tzref : "", tzsuffix);
	icomp = i_cal_component_new_from_string (str);
	g_assert_nonnull (icomp);

	g_free (tzref);
	g_free (str);

	itt = i_cal_component_get_dtstart (icomp);
	g_assert_nonnull (itt);
	g_assert_true (i_cal_time_get_timezone (itt) == zone);
	g_object_unref (itt);

	itt = i_cal_component_get_dtend (icomp);
	g_assert_nonnull (itt);
	g_assert_true (i_cal_time_get_timezone (itt) == zone);
	g_object_unref (itt);

	return icomp;
}

static void
setup_cal_midnight (ECalClient *cal_client,
		    const gchar *tz_location)
{
	ICalComponent *icomp;
	gboolean success;
	gchar *uid = NULL;
	GError *error = NULL;

	icomp = create_component_midnight (tz_location);

	if (!e_cal_client_remove_object_sync (cal_client, i_cal_component_get_uid (icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error)) {
		g_assert_error (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
	}

	success = e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uid);

	g_object_unref (icomp);
	g_free (uid);
}

static gboolean
recur_instance_midnight_cb (ICalComponent *icomp,
			    ICalTime *instance_start,
			    ICalTime *instance_end,
			    gpointer user_data,
			    GCancellable *cancellable,
			    GError **error)
{
	GSList **listp = user_data;

	*listp = g_slist_append (*listp, i_cal_time_as_ical_string (instance_start));

	return TRUE;
}

static void
test_recur_midnight_for_zone (ECalClient *client,
			      const gchar *tz_location)
{
	ICalTime *start, *end;
	GSList *list = NULL, *last;

	setup_cal_midnight (client, tz_location);

	start = i_cal_time_new_from_string ("20190101T000000Z");
	end = i_cal_time_new_from_string ("20190131T000000Z");

	e_cal_client_generate_instances_sync (client,
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		NULL, /* GCancellable * */
		recur_instance_midnight_cb, &list);

	last = g_slist_last (list);
	g_assert_nonnull (last);
	if (g_ascii_strcasecmp (tz_location, "UTC") == 0)
		g_assert_cmpstr (last->data, ==, "20190109T000000Z");
	else
		g_assert_cmpstr (last->data, ==, "20190109T000000");
	g_assert_cmpint (g_slist_length (list), ==, 3);

	g_slist_free_full (list, g_free);
	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_midnight (ETestServerFixture *fixture,
		     gconstpointer user_data)
{
	ECalClient *client;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	e_cal_client_set_default_timezone (client, i_cal_timezone_get_builtin_timezone ("UTC"));

	test_recur_midnight_for_zone (client, "UTC");
	test_recur_midnight_for_zone (client, "America/New_York");
	test_recur_midnight_for_zone (client, "Europe/Berlin");
}

static void
test_recur_client (ETestServerFixture *fixture,
		   gconstpointer user_data)
{
	ECalClient *cal;
	gint deftz_ii, comptz_ii;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	for (deftz_ii = 0; deftz_ii < G_N_ELEMENTS (timezones); deftz_ii++) {
		for (comptz_ii = 0; comptz_ii < G_N_ELEMENTS (timezones); comptz_ii++) {
			/* Skip the floating time for the client's default timezone */
			if (timezones[deftz_ii])
				test_recur_client_run (cal, timezones[deftz_ii], timezones[comptz_ii]);
		}
	}
}

static gboolean
got_instance_cb (ICalComponent *icomp,
		 ICalTime *instance_start,
		 ICalTime *instance_end,
		 gpointer user_data,
		 GCancellable *cancellable,
		 GError **error)
{
	gint *pfound = user_data;

	(*pfound)++;

	return TRUE;
}

static ICalTimezone *
lookup_tzid_cb (const gchar *tzid,
		gpointer lookup_data,
		GCancellable *cancellable,
		GError **error)
{
	return i_cal_timezone_get_builtin_timezone (tzid);
}

static void
test_recur_exdate_component (const gchar *comp_str)
{
	ICalComponent *comp;
	ICalTime *start, *end;
	gint found = 0;
	gboolean success;
	GError *error = NULL;

	comp = i_cal_component_new_from_string (comp_str);

	g_assert_nonnull (comp);

	start = i_cal_time_new_from_string ("20191001T000000Z");
	end = i_cal_time_new_from_string ("20191031T235959Z");

	g_assert_nonnull (start);
	g_assert_nonnull (end);

	success = e_cal_recur_generate_instances_sync (comp, start, end,
		got_instance_cb, &found,
		lookup_tzid_cb, NULL,
		i_cal_timezone_get_builtin_timezone ("Europe/Berlin"),
		NULL, &error);

	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (found, ==, 2);

	found = 0;

	success = e_cal_recur_generate_instances_sync (comp, start, end,
		got_instance_cb, &found,
		lookup_tzid_cb, NULL,
		i_cal_timezone_get_builtin_timezone ("America/New_York"),
		NULL, &error);

	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (found, ==, 2);

	g_object_unref (start);
	g_object_unref (end);
	g_object_unref (comp);
}

static void
test_recur_exdate (ETestServerFixture *fixture,
		   gconstpointer user_data)
{
	test_recur_exdate_component (
		"BEGIN:VEVENT\r\n"
		"UID:007\r\n"
		"DTSTART;TZID=Europe/Amsterdam:20191010T120000\r\n"
		"DTEND;TZID=Europe/Amsterdam:20191010T170000\r\n"
		"SUMMARY:Test\r\n"
		"RRULE:FREQ=DAILY;COUNT=4\r\n"
		"EXDATE;VALUE=DATE:20191011\r\n"
		"EXDATE:20191012T100000Z\r\n"
		"END:VEVENT\r\n"
	);

	test_recur_exdate_component (
		"BEGIN:VEVENT\r\n"
		"UID:007\r\n"
		"DTSTART:20191010T120000\r\n"
		"DTEND:20191010T170000\r\n"
		"SUMMARY:Test\r\n"
		"RRULE:FREQ=DAILY;COUNT=4\r\n"
		"EXDATE;VALUE=DATE:20191011\r\n"
		"EXDATE:20191012T120000\r\n"
		"END:VEVENT\r\n"
	);
}

typedef struct _DurationData {
	gint n_found;
	gint expected_duration;
} DurationData;

static gboolean
duration_got_instance_cb (ICalComponent *icomp,
			  ICalTime *instance_start,
			  ICalTime *instance_end,
			  gpointer user_data,
			  GCancellable *cancellable,
			  GError **error)
{
	DurationData *dd = user_data;
	ICalDuration *dur;

	dd->n_found++;

	dur = i_cal_component_get_duration (icomp);
	g_assert_nonnull (dur);
	g_assert_cmpint (i_cal_duration_as_int (dur), ==, dd->expected_duration);
	g_assert_cmpint (i_cal_time_as_timet (instance_end) - i_cal_time_as_timet (instance_start), ==, dd->expected_duration);

	g_object_unref (dur);

	return TRUE;
}

static void
test_recur_duration (ETestServerFixture *fixture,
		     gconstpointer user_data)
{
	ICalComponent *comp;
	ICalDuration *dur;
	ICalTime *start, *end;
	DurationData dd;
	gboolean success;
	GError *error = NULL;

	comp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\n"
		"UID:1\n"
		"DTSTART;TZID=Australia/Melbourne:20200212T100000\n"
		"DURATION:PT30M\n"
		"CREATED:20200212T111400Z\n"
		"DTSTAMP:20200212T111400Z\n"
		"SUMMARY:With duration\n"
		"RRULE:FREQ=WEEKLY\n"
		"END:VEVENT\n"
	);

	g_assert_nonnull (comp);

	start = i_cal_time_new_from_string ("20200201T000000Z");
	end = i_cal_time_new_from_string ("20200331T235959Z");

	g_assert_nonnull (start);
	g_assert_nonnull (end);

	dur = i_cal_component_get_duration (comp);
	g_assert_nonnull (dur);

	dd.expected_duration = i_cal_duration_as_int (dur);
	g_object_unref (dur);

	g_assert_cmpint (dd.expected_duration, ==, 30 * 60);

	dd.n_found = 0;

	success = e_cal_recur_generate_instances_sync (comp, start, end,
		duration_got_instance_cb, &dd,
		lookup_tzid_cb, NULL,
		i_cal_timezone_get_builtin_timezone ("Australia/Melbourne"),
		NULL, &error);

	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (dd.n_found, ==, 8);

	dd.n_found = 0;

	success = e_cal_recur_generate_instances_sync (comp, start, end,
		duration_got_instance_cb, &dd,
		lookup_tzid_cb, NULL,
		i_cal_timezone_get_builtin_timezone ("Europe/Berlin"),
		NULL, &error);

	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (dd.n_found, ==, 8);

	dd.n_found = 0;

	success = e_cal_recur_generate_instances_sync (comp, start, end,
		duration_got_instance_cb, &dd,
		lookup_tzid_cb, NULL,
		i_cal_timezone_get_builtin_timezone ("America/New_York"),
		NULL, &error);

	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (dd.n_found, ==, 8);

	g_object_unref (start);
	g_object_unref (end);
	g_object_unref (comp);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalRecur/Plain",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_plain,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/Client",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_client,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/Exdate",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_exdate,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/Duration",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_duration,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/Midnight",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_midnight,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
