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
			   ICalTimezone *comp_zone,
			   const gchar **expected_times) /* nullable */
{
	const gchar *default_expected_times[] = {
		"20190107T080000",
		"20190108T080000",
		"20190109T080000",
		"20190110T080000",
		"20190111T080000",
		NULL
	};
	gint ii;

	g_assert_nonnull (instances);

	if (!expected_times)
		expected_times = default_expected_times;

	for (ii = 0; expected_times[ii]; ii++) {
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
	RecurData rd = { NULL };
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

	verify_received_instances (rd.instances, comp_zone, NULL);

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
	RecurData rd = { NULL };

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

	verify_received_instances (rd.instances, comp_zone, NULL);

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

	verify_received_instances (rd.instances, comp_zone, NULL);

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
		"RRULE%s:FREQ=DAILY;UNTIL=20190109T000000%s\r\n"
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

	str = g_strdup_printf (comp_str,
		tzref ? tzref : "", tzsuffix, /* DTSTART */
		tzref ? tzref : "", tzsuffix, /* DTEND */
		tzref ? tzref : "", tzsuffix); /* RRULE */
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

static void
setup_cal_reminders (ECalClient *cal_client,
		     gboolean keep_alarm)
{
	ICalComponent *icomp;
	gboolean success;
	gchar *uid = NULL;
	GError *error = NULL;

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTAMP:20231113T090259Z\r\n"
		"DTSTART;TZID=Pacific/Auckland:20231114T045100\r\n"
		"DTEND;TZID=Pacific/Auckland:20231114T045500\r\n"
		"SUMMARY:test\r\n"
		"CREATED:20231113T090627Z\r\n"
		"LAST-MODIFIED:20231113T132152Z\r\n"
		"BEGIN:VALARM\r\n"
		"X-EVOLUTION-ALARM-UID:a1\r\n"
		"ACTION:DISPLAY\r\n"
		"DESCRIPTION:test\r\n"
		"TRIGGER;RELATED=START:-PT50M\r\n"
		"END:VALARM\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	if (!keep_alarm) {
		ICalComponent *alarm_comp;

		while (alarm_comp = i_cal_component_get_first_component (icomp, I_CAL_VALARM_COMPONENT), alarm_comp) {
			i_cal_component_remove_component (icomp, alarm_comp);
			g_clear_object (&alarm_comp);
		}
	}

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

static void
test_recur_reminders (ETestServerFixture *fixture,
		      gconstpointer user_data)
{
	ECalClient *client;
	ECalComponentAlarmAction omit[] = { -1 };
	ECalComponentAlarms *alarms;
	ECalComponentAlarmInstance *instance;
	ICalTime *start, *end;
	ICalTimezone *default_timezone;
	GError *error = NULL;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	default_timezone = i_cal_timezone_get_utc_timezone ();
	start = i_cal_time_new_from_string ("20231113T132001Z");
	end = i_cal_time_new_from_string ("20231113T235000Z");

	e_cal_client_set_default_timezone (client, default_timezone);
	setup_cal_reminders (client, TRUE);

	/* without default alarm */
	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "a1");

	e_cal_component_alarms_free (alarms);

	/* with default alarm 50 minutes before start, which matches existing alarm, thus it does not apply */
	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 50 * 60,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "a1");

	e_cal_component_alarms_free (alarms);
	g_object_unref (start);
	g_object_unref (end);

	/* with default alarm one day before start */
	start = i_cal_time_new_from_string ("20231112T150001Z");
	end = i_cal_time_new_from_string ("20231112T160000Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 24 * 60 * 60,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "x-evolution-default-alarm");

	e_cal_component_alarms_free (alarms);

	/* with default alarm at the event start, but out-of-range */
	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 0,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_null (alarms);

	g_object_unref (start);
	g_object_unref (end);

	/* with default alarm at the event start, with expected range */
	start = i_cal_time_new_from_string ("20231113T153001Z");
	end = i_cal_time_new_from_string ("20231113T160000Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 0,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "x-evolution-default-alarm");

	e_cal_component_alarms_free (alarms);
	g_object_unref (start);
	g_object_unref (end);

	/* remove all alarms from the component to try with default alarm only */
	setup_cal_reminders (client, FALSE);

	start = i_cal_time_new_from_string ("20231113T132001Z");
	end = i_cal_time_new_from_string ("20231113T235000Z");

	/* without default alarm */
	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_null (alarms);

	/* with default alarm 50 minutes before start */
	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 50 * 60,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "x-evolution-default-alarm");

	e_cal_component_alarms_free (alarms);
	g_object_unref (start);
	g_object_unref (end);

	/* with default alarm one day before start */
	start = i_cal_time_new_from_string ("20231112T150001Z");
	end = i_cal_time_new_from_string ("20231112T160000Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 24 * 60 * 60,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "x-evolution-default-alarm");

	e_cal_component_alarms_free (alarms);

	/* with default alarm at the event start, but out-of-range */
	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 0,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_null (alarms);

	g_object_unref (start);
	g_object_unref (end);

	/* with default alarm at the event start, with expected range */
	start = i_cal_time_new_from_string ("20231113T153001Z");
	end = i_cal_time_new_from_string ("20231113T160000Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, 0,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	instance = e_cal_component_alarms_get_instances (alarms)->data;
	g_assert_cmpstr (e_cal_component_alarm_instance_get_uid (instance), ==, "x-evolution-default-alarm");

	e_cal_component_alarms_free (alarms);
	g_object_unref (start);
	g_object_unref (end);
}

static void
test_recur_detached (ETestServerFixture *fixture,
		     gconstpointer user_data)
{
	const gchar *expected_first[] = {
		"20250726T150000Z",
		"20250727T150000Z",
		"20250728T150000Z",
		NULL
	};
	const gchar *expected_second[] = {
		"20250726T150000Z",
		"20250727T170000Z",
		"20250728T150000Z",
		NULL
	};
	const gchar *expected_third[] = {
		"20250726T150000Z",
		"20250727T170000Z",
		NULL
	};
	const gchar *expected_fourth[] = {
		"20250726T150000Z",
		"20250727T170000Z",
		"20250729T130000Z",
		"20250729T150000Z",
		NULL
	};
	const gchar *expected_fifth[] = {
		"20250726T150000Z",
		NULL
	};
	ECalClient *client;
	ICalComponent *icomp;
	ICalProperty *prop;
	ICalTime *start, *end;
	GError *error = NULL;
	RecurData rd = { NULL };
	gchar *uid;
	gboolean success;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART:20250724T150000Z\r\n"
		"DTEND:20250724T151500Z\r\n"
		"RRULE:FREQ=DAILY;COUNT=7\r\n"
		"SUMMARY:test\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	if (!e_cal_client_remove_object_sync (client, i_cal_component_get_uid (icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error)) {
		g_assert_error (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
	}

	success = e_cal_client_create_object_sync (client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uid);

	g_object_unref (icomp);
	g_free (uid);

	success = e_cal_client_get_object_sync (client, "1", NULL, &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&icomp);

	start = i_cal_time_new_from_string ("20250726T000000Z");
	end = i_cal_time_new_from_string ("20250728T235959Z");

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, "1", i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 3);
	verify_received_instances (rd.instances, NULL, expected_first);
	g_hash_table_destroy (rd.instances);

	/* move the instance on the 27th by two hours */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"RECURRENCE-ID:20250727T150000Z\r\n"
		"DTSTART:20250727T170000Z\r\n"
		"DTEND:20250727T171500Z\r\n"
		"SUMMARY:moved by two hours\r\n"
		"END:VEVENT\r\n");

	success = e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", NULL, &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", "20250727T150000Z", &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_nonnull ((prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY)));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&prop);
	g_clear_object (&icomp);

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, "1", i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 3);
	verify_received_instances (rd.instances, NULL, expected_second);
	g_hash_table_destroy (rd.instances);

	/* move the instance on the 28th to the next day, which is out of the search interval */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"RECURRENCE-ID:20250728T150000Z\r\n"
		"DTSTART:20250729T130000Z\r\n"
		"DTEND:20250729T131500Z\r\n"
		"SUMMARY:moved by day\r\n"
		"END:VEVENT\r\n");

	success = e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", NULL, &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", "20250727T150000Z", &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_nonnull ((prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY)));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&prop);
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", "20250728T150000Z", &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_nonnull ((prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY)));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&prop);
	g_clear_object (&icomp);

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, "1", i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 2);
	verify_received_instances (rd.instances, NULL, expected_third);
	g_hash_table_destroy (rd.instances);

	g_object_unref (start);
	g_object_unref (end);

	/* extend the interval to include both detached instances */
	start = i_cal_time_new_from_string ("20250726T000000Z");
	end = i_cal_time_new_from_string ("20250729T235959Z");

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, "1", i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 4);
	verify_received_instances (rd.instances, NULL, expected_fourth);
	g_hash_table_destroy (rd.instances);

	g_object_unref (start);
	g_object_unref (end);

	/* shorten the interval to not include any detached instances */
	start = i_cal_time_new_from_string ("20250726T000000Z");
	end = i_cal_time_new_from_string ("20250726T235959Z");

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, "1", i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpint (g_hash_table_size (rd.instances), ==, 1);
	verify_received_instances (rd.instances, NULL, expected_fifth);
	g_hash_table_destroy (rd.instances);

	g_object_unref (start);
	g_object_unref (end);
}

static void
verified_received_alarms (ECalComponentAlarms *alarms,
			  const gchar **expected)
{
	GSList *link;
	GHashTable *received;
	guint ii;

	received = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (link = e_cal_component_alarms_get_instances (alarms); link; link = g_slist_next (link)) {
		ECalComponentAlarmInstance *instance = link->data;
		ICalTime *itt;
		gchar *tt_str;

		itt = i_cal_time_new_from_timet_with_zone (e_cal_component_alarm_instance_get_occur_start (instance), FALSE, NULL);
		tt_str = i_cal_time_as_ical_string (itt);
		g_hash_table_add (received, tt_str);
		g_clear_object (&itt);
	}

	for (ii = 0; expected[ii]; ii++) {
		g_assert_true (g_hash_table_remove (received, expected[ii]));
	}

	g_assert_cmpuint (g_hash_table_size (received), ==, 0);
	g_hash_table_destroy (received);
}

static void
test_recur_reminders_detached (ETestServerFixture *fixture,
			       gconstpointer user_data)
{
	const gchar *expected_first[] = {
		"20250726T150000",
		"20250727T150000",
		"20250728T150000",
		NULL
	};
	const gchar *expected_second[] = {
		"20250726T150000",
		"20250727T170000",
		"20250728T150000",
		NULL
	};
	const gchar *expected_third[] = {
		"20250726T150000",
		"20250727T170000",
		NULL
	};
	const gchar *expected_fourth[] = {
		"20250726T150000",
		"20250727T170000",
		"20250729T130000",
		"20250729T150000",
		NULL
	};
	const gchar *expected_fifth[] = {
		"20250726T150000",
		NULL
	};
	ECalComponentAlarmAction omit[] = { -1 };
	ECalComponentAlarms *alarms;
	ECalClient *client;
	ICalComponent *icomp;
	ICalProperty *prop;
	ICalTime *start, *end;
	ICalTimezone *default_timezone;
	GError *error = NULL;
	gchar *uid;
	gboolean success;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	default_timezone = e_cal_client_get_default_timezone (client);

	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"DTSTART:20250724T150000Z\r\n"
		"DTEND:20250724T151500Z\r\n"
		"RRULE:FREQ=DAILY;COUNT=7\r\n"
		"SUMMARY:test\r\n"
		"BEGIN:VALARM\r\n"
		"X-EVOLUTION-ALARM-UID:a1\r\n"
		"ACTION:DISPLAY\r\n"
		"DESCRIPTION:test\r\n"
		"TRIGGER;RELATED=START:-PT10M\r\n"
		"END:VALARM\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (icomp);

	if (!e_cal_client_remove_object_sync (client, i_cal_component_get_uid (icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error)) {
		g_assert_error (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
		g_clear_error (&error);
	} else {
		g_assert_no_error (error);
	}

	success = e_cal_client_create_object_sync (client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (uid);

	g_object_unref (icomp);
	g_free (uid);

	success = e_cal_client_get_object_sync (client, "1", NULL, &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&icomp);

	start = i_cal_time_new_from_string ("20250726T000000Z");
	end = i_cal_time_new_from_string ("20250728T235959Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 3);
	verified_received_alarms (alarms, expected_first);
	e_cal_component_alarms_free (alarms);

	/* move the instance on the 27th by two hours */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"RECURRENCE-ID:20250727T150000Z\r\n"
		"DTSTART:20250727T170000Z\r\n"
		"DTEND:20250727T171500Z\r\n"
		"SUMMARY:moved by two hours\r\n"
		"BEGIN:VALARM\r\n"
		"X-EVOLUTION-ALARM-UID:a1\r\n"
		"ACTION:DISPLAY\r\n"
		"DESCRIPTION:test\r\n"
		"TRIGGER;RELATED=START:-PT10M\r\n"
		"END:VALARM\r\n"
		"END:VEVENT\r\n");

	success = e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", NULL, &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", "20250727T150000Z", &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_nonnull ((prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY)));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&prop);
	g_clear_object (&icomp);

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 3);
	verified_received_alarms (alarms, expected_second);
	e_cal_component_alarms_free (alarms);

	/* move the instance on the 28th to the next day, which is out of the search interval */
	icomp = i_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:1\r\n"
		"RECURRENCE-ID:20250728T150000Z\r\n"
		"DTSTART:20250729T130000Z\r\n"
		"DTEND:20250729T131500Z\r\n"
		"SUMMARY:moved by day\r\n"
		"BEGIN:VALARM\r\n"
		"X-EVOLUTION-ALARM-UID:a1\r\n"
		"ACTION:DISPLAY\r\n"
		"DESCRIPTION:test\r\n"
		"TRIGGER;RELATED=START:-PT10M\r\n"
		"END:VALARM\r\n"
		"END:VEVENT\r\n");

	success = e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", NULL, &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", "20250727T150000Z", &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_nonnull ((prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY)));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&prop);
	g_clear_object (&icomp);

	success = e_cal_client_get_object_sync (client, "1", "20250728T150000Z", &icomp, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (icomp);
	g_assert_nonnull ((prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY)));
	g_assert_null (i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY));
	g_clear_object (&prop);
	g_clear_object (&icomp);

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 2);
	verified_received_alarms (alarms, expected_third);
	e_cal_component_alarms_free (alarms);

	g_object_unref (start);
	g_object_unref (end);

	/* extend the interval to include both detached instances */
	start = i_cal_time_new_from_string ("20250726T000000Z");
	end = i_cal_time_new_from_string ("20250729T235959Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 4);
	verified_received_alarms (alarms, expected_fourth);
	e_cal_component_alarms_free (alarms);

	g_object_unref (start);
	g_object_unref (end);

	/* shorten the interval to not include any detached instances */
	start = i_cal_time_new_from_string ("20250726T000000Z");
	end = i_cal_time_new_from_string ("20250726T235959Z");

	alarms = e_cal_util_generate_alarms_for_uid_sync (client, "1",
		i_cal_time_as_timet (start),
		i_cal_time_as_timet (end),
		omit, e_cal_client_tzlookup_cb, client, default_timezone, -1,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (alarms);
	g_assert_cmpint (g_slist_length (e_cal_component_alarms_get_instances (alarms)), ==, 1);
	verified_received_alarms (alarms, expected_fifth);
	e_cal_component_alarms_free (alarms);

	g_object_unref (start);
	g_object_unref (end);
}

static void
test_recur_remove_instance_case_check (ECalClient *client,
				       ICalComponent *icomp,
				       ICalTime *start,
				       ICalTime *end,
				       ICalTime *rid,
				       ECalObjModType mod,
				       const gchar **expected_all,
				       const gchar **expected_shortened,
				       ICalPropertyKind expected_property)
{
	ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
	GError *local_error = NULL;
	RecurData rd = { NULL };
	guint n_expected_all, n_expected_shortened;
	gboolean success;

	for (n_expected_all = 0; expected_all[n_expected_all]; n_expected_all++) {
		/* just count them */
	}

	for (n_expected_shortened = 0; expected_shortened[n_expected_shortened]; n_expected_shortened++) {
		/* just count them */
	}

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	success = e_cal_recur_generate_instances_sync (icomp, start, end, recur_instance_cb, &rd,
		e_cal_client_tzlookup_cb, client, utc_zone, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (g_hash_table_size (rd.instances), ==, n_expected_all);
	verify_received_instances (rd.instances, i_cal_time_get_timezone (rid), expected_all);
	g_clear_pointer (&rd.instances, g_hash_table_unref);

	if (expected_property != I_CAL_NO_PROPERTY)
		g_assert_false (e_cal_util_component_has_property (icomp, expected_property));

	e_cal_util_remove_instances_ex (icomp, rid, mod, e_cal_client_tzlookup_cb, client);

	if (expected_property != I_CAL_NO_PROPERTY)
		g_assert_true (e_cal_util_component_has_property (icomp, expected_property));

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	success = e_cal_recur_generate_instances_sync (icomp, start, end, recur_instance_cb, &rd,
		e_cal_client_tzlookup_cb, client, utc_zone, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (g_hash_table_size (rd.instances), ==, n_expected_shortened);
	verify_received_instances (rd.instances, i_cal_time_get_timezone (rid), expected_shortened);
	g_clear_pointer (&rd.instances, g_hash_table_unref);
}

static void
test_recur_remove_instance_case (ECalClient *client,
				 ICalComponent *icomp,
				 ICalTime *start,
				 ICalTime *end,
				 ICalTime *rid,
				 ECalObjModType mod,
				 const gchar **expected_all,
				 const gchar **expected_shortened,
				 ICalPropertyKind expected_property)
{
	ICalComponent *clone_icomp;
	ICalTime *clone_rid;

	clone_icomp = i_cal_component_clone (icomp);
	g_assert_nonnull (clone_icomp);

	clone_rid = i_cal_time_clone (rid);
	g_assert_nonnull (clone_rid);
	g_assert_true (i_cal_time_get_timezone (rid) == i_cal_time_get_timezone (clone_rid));

	/* first use the rid as passed in */
	test_recur_remove_instance_case_check (client, icomp, start, end, rid, mod, expected_all, expected_shortened, expected_property);

	/* then convert the rid into the UTC and try again */
	i_cal_time_convert_to_zone_inplace (clone_rid, i_cal_timezone_get_utc_timezone ());
	test_recur_remove_instance_case_check (client, clone_icomp, start, end, clone_rid, mod, expected_all, expected_shortened, expected_property);

	g_clear_object (&clone_icomp);
	g_clear_object (&clone_rid);
}

static void
test_recur_remove_instance_first (ETestServerFixture *fixture,
				  gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	/* cannot remove this-and-future for the first instance, because it means removing
	   the whole component, which the caller is responsible to check for on its own */

	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_remove_instance_mid (ETestServerFixture *fixture,
				gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_remove_instance_last (ETestServerFixture *fixture,
				 gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	/* cannot remove this-and-prior for the last instance, because it means removing
	   the whole component, which the caller is responsible to check for on its own */

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_remove_instance_cal_case (ECalClient *client,
				     ICalComponent *new_icomp,
				     ICalTime *start,
				     ICalTime *end,
				     ICalTime *rid,
				     ECalObjModType mod,
				     const gchar **expected_all,
				     const gchar **expected_shortened,
				     ICalPropertyKind expected_property)
{
	GSList *ecomps = NULL;
	GError *local_error = NULL;
	gchar *uid = NULL, *rid_str;
	RecurData rd = { NULL };
	guint n_expected_all, n_expected_shortened;
	gboolean success;

	for (n_expected_all = 0; expected_all[n_expected_all]; n_expected_all++) {
		/* just count them */
	}

	for (n_expected_shortened = 0; expected_shortened[n_expected_shortened]; n_expected_shortened++) {
		/* just count them */
	}

	/* this can fail, it's only to prepare the calendar */
	e_cal_client_remove_object_sync	(client, i_cal_component_get_uid (new_icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

	success = e_cal_client_create_object_sync (client, new_icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uid);

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, uid, i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpuint (g_hash_table_size (rd.instances), ==, n_expected_all);
	verify_received_instances (rd.instances, i_cal_time_get_timezone (rid), expected_all);
	g_clear_pointer (&rd.instances, g_hash_table_unref);

	success = e_cal_client_get_objects_for_uid_sync (client, uid, &ecomps, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (g_slist_length (ecomps), ==, 1);

	if (expected_property != I_CAL_NO_PROPERTY)
		g_assert_false (e_cal_util_component_has_property (e_cal_component_get_icalcomponent (ecomps->data), expected_property));

	/* mod only-this requires that to be a detached instance, thus create it first */
	if (mod == E_CAL_OBJ_MOD_ONLY_THIS) {
		ICalComponent *icomp = e_cal_component_get_icalcomponent (ecomps->data);

		i_cal_component_set_recurrenceid (icomp, rid);
		i_cal_component_set_summary (icomp, "modified");

		success = e_cal_client_modify_object_sync (client, icomp, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);
	}

	g_slist_free_full (ecomps, g_object_unref);
	ecomps = NULL;

	rid_str = i_cal_time_as_ical_string (rid);
	g_assert_nonnull (rid_str);
	success = e_cal_client_remove_object_sync (client, uid, rid_str, mod, E_CAL_OPERATION_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_free (rid_str);

	success = e_cal_client_get_objects_for_uid_sync (client, uid, &ecomps, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (g_slist_length (ecomps), ==, 1);

	if (expected_property != I_CAL_NO_PROPERTY)
		g_assert_true (e_cal_util_component_has_property (e_cal_component_get_icalcomponent (ecomps->data), expected_property));

	g_slist_free_full (ecomps, g_object_unref);
	ecomps = NULL;

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, uid, i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpuint (g_hash_table_size (rd.instances), ==, n_expected_shortened);
	verify_received_instances (rd.instances, i_cal_time_get_timezone (rid), expected_shortened);
	g_clear_pointer (&rd.instances, g_hash_table_unref);

	g_free (uid);
}

static void
test_recur_remove_instance_like_all_cal_case (ECalClient *client,
					      ICalComponent *new_icomp,
					      ICalTime *start,
					      ICalTime *end,
					      ICalTime *rid,
					      ECalObjModType mod,
					      const gchar **expected_all)
{
	GSList *ecomps = NULL;
	GError *local_error = NULL;
	gchar *uid = NULL, *rid_str;
	RecurData rd = { NULL };
	guint n_expected_all;
	gboolean success;

	for (n_expected_all = 0; expected_all[n_expected_all]; n_expected_all++) {
		/* just count them */
	}

	/* this can fail, it's only to prepare the calendar */
	e_cal_client_remove_object_sync	(client, i_cal_component_get_uid (new_icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

	success = e_cal_client_create_object_sync (client, new_icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_nonnull (uid);

	rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
	e_cal_client_generate_instances_for_uid_sync (client, uid, i_cal_time_as_timet (start), i_cal_time_as_timet (end),
		NULL, recur_instance_cb, &rd);
	g_assert_cmpuint (g_hash_table_size (rd.instances), ==, n_expected_all);
	verify_received_instances (rd.instances, i_cal_time_get_timezone (rid), expected_all);
	g_clear_pointer (&rd.instances, g_hash_table_unref);

	success = e_cal_client_get_objects_for_uid_sync (client, uid, &ecomps, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_assert_cmpuint (g_slist_length (ecomps), ==, 1);

	g_slist_free_full (ecomps, g_object_unref);
	ecomps = NULL;

	rid_str = i_cal_time_as_ical_string (rid);
	g_assert_nonnull (rid_str);
	success = e_cal_client_remove_object_sync (client, uid, rid_str, mod, E_CAL_OPERATION_FLAG_NONE, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);
	g_free (rid_str);

	success = e_cal_client_get_objects_for_uid_sync (client, uid, &ecomps, NULL, &local_error);
	g_assert_error (local_error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
	g_assert_false (success);
	g_assert_cmpuint (g_slist_length (ecomps), ==, 0);
	g_clear_error (&local_error);

	g_free (uid);
}

static void
test_recur_remove_instance_first_cal (ETestServerFixture *fixture,
				      gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_like_all_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, expected_all);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250805T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_like_all_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE, expected_all);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_remove_instance_mid_cal (ETestServerFixture *fixture,
				    gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR,
			expected_all, expected_shortened, I_CAL_EXRULE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_remove_instance_last_cal (ETestServerFixture *fixture,
				     gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_ONLY_THIS,
			expected_all, expected_shortened, I_CAL_EXDATE_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_like_all_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, expected_all);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_like_all_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_PRIOR, expected_all);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		const gchar *expected_all[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			"20250809T100000",
			NULL };
		const gchar *expected_shortened[] = {
			"20250805T100000",
			"20250806T100000",
			"20250807T100000",
			"20250808T100000",
			NULL };
		ICalComponent *icomp;
		ICalTime *rid;
		ICalTimezone *zone;
		gchar *str;

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* using COUNT */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);

		/* using UNTIL */
		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;UNTIL=20250809T235959Z\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);

		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);

		rid = i_cal_time_new_from_string ("20250809T100000");
		i_cal_time_set_timezone (rid, zone);

		test_recur_remove_instance_cal_case (client, icomp, start, end, rid, E_CAL_OBJ_MOD_THIS_AND_FUTURE,
			expected_all, expected_shortened, I_CAL_NO_PROPERTY);

		g_clear_object (&icomp);
		g_clear_object (&rid);
		g_free (str);
	}

	g_clear_object (&start);
	g_clear_object (&end);
}

static void
test_recur_multiple_detached (ETestServerFixture *fixture,
			      gconstpointer user_data)
{
	struct _zones {
		const gchar *location;
		const gchar *tzid_param;
		const gchar *time_suffix;
	} zones[] = {
		{ "UTC",		"",			  "Z" },
		{ "Europe/Berlin",	";TZID=Europe/Berlin",	  "" },
		{ "America/New_York",	";TZID=America/New_York", "" }
	};
	const gchar *expected_all[] = {
		"20250805T100000",
		"20250806T100000",
		"20250807T100000",
		"20250808T100000",
		"20250809T100000",
		NULL };
	const gchar *expected_shortened[] = {
		"20250806T100000",
		"20250807T100000",
		"20250808T100000",
		"20250809T100000",
		NULL };
	ECalClient *client;
	ICalTime *start, *end;
	guint ii;

	client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);
	start = i_cal_time_new_from_string ("20250801T000000");
	end = i_cal_time_new_from_string ("20250831T235959");

	for (ii = 0; ii < G_N_ELEMENTS (zones); ii++) {
		GError *local_error = NULL;
		GSList *ecomps = NULL;
		ICalComponent *icomp, *modified;
		ICalProperty *prop;
		ICalTime *rid, *itt;
		ICalTimezone *zone;
		RecurData rd = { NULL };
		gchar *uid = NULL, *str;
		gboolean success;

		str = g_strdup_printf (
			"BEGIN:VEVENT\r\n"
			"UID:1\r\n"
			"DTSTART%s:20250805T100000%s\r\n"
			"DTEND%s:20250805T101500%s\r\n"
			"RRULE:FREQ=DAILY;COUNT=5\r\n"
			"SUMMARY:test\r\n"
			"END:VEVENT\r\n",
			zones[ii].tzid_param, zones[ii].time_suffix,
			zones[ii].tzid_param, zones[ii].time_suffix);
		icomp = i_cal_component_new_from_string (str);
		g_assert_nonnull (icomp);
		g_free (str);

		zone = i_cal_timezone_get_builtin_timezone (zones[ii].location);
		g_assert_nonnull (zone);

		/* this can fail, it's only to prepare the calendar */
		e_cal_client_remove_object_sync	(client, i_cal_component_get_uid (icomp), NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

		success = e_cal_client_create_object_sync (client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);
		g_assert_nonnull (uid);

		rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
		e_cal_client_generate_instances_for_uid_sync (client, uid, i_cal_time_as_timet (start), i_cal_time_as_timet (end),
			NULL, recur_instance_cb, &rd);
		g_assert_cmpuint (g_hash_table_size (rd.instances), ==, G_N_ELEMENTS (expected_all) - 1);
		verify_received_instances (rd.instances, zone, expected_all);
		g_clear_pointer (&rd.instances, g_hash_table_unref);

		/* remove the first instance */
		success = e_cal_client_remove_object_sync (client, uid, "20250805T100000", E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		modified = i_cal_component_clone (icomp);

		prop = i_cal_component_get_first_property (modified, I_CAL_RRULE_PROPERTY);
		g_assert_nonnull (prop);
		i_cal_component_remove_property (icomp, prop);
		g_clear_object (&prop);

		/* modify the second instance */
		rid = i_cal_time_new_from_string ("20250806T100000");
		i_cal_time_set_timezone (rid, zone);
		i_cal_component_set_recurrenceid (modified, rid);
		g_clear_object (&rid);

		prop = i_cal_component_get_first_property (modified, I_CAL_RECURRENCEID_PROPERTY);
		g_assert_nonnull (prop);
		if (g_strcmp0 (zones[ii].location, "UTC") != 0) {
			ICalParameter *param;
			param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
			g_assert_null (param);
			param = i_cal_parameter_new_tzid (zones[ii].location);
			i_cal_property_add_parameter (prop, param);
			g_clear_object (&param);
		}
		g_clear_object (&prop);

		itt = i_cal_time_new_from_string ("20250806T100000");
		i_cal_time_set_timezone (itt, zone);
		i_cal_component_set_dtstart (modified, itt);
		g_clear_object (&itt);

		itt = i_cal_time_new_from_string ("20250806T150000");
		i_cal_time_set_timezone (itt, zone);
		i_cal_component_set_dtend (modified, itt);
		g_clear_object (&itt);

		i_cal_component_set_summary (modified, "2nd modified");

		success = e_cal_client_modify_object_sync (client, modified, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		/* modify the third instance */
		prop = i_cal_component_get_first_property (modified, I_CAL_RECURRENCEID_PROPERTY);
		g_assert_nonnull (prop);
		i_cal_component_remove_property (modified, prop);
		g_clear_object (&prop);

		rid = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (rid, zone);
		i_cal_component_set_recurrenceid (modified, rid);
		g_clear_object (&rid);

		prop = i_cal_component_get_first_property (modified, I_CAL_RECURRENCEID_PROPERTY);
		g_assert_nonnull (prop);
		if (g_strcmp0 (zones[ii].location, "UTC") != 0) {
			ICalParameter *param;
			param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
			g_assert_null (param);
			param = i_cal_parameter_new_tzid (zones[ii].location);
			i_cal_property_add_parameter (prop, param);
			g_clear_object (&param);
		}
		g_clear_object (&prop);

		itt = i_cal_time_new_from_string ("20250807T100000");
		i_cal_time_set_timezone (itt, zone);
		i_cal_component_set_dtstart (modified, itt);
		g_clear_object (&itt);

		itt = i_cal_time_new_from_string ("20250807T150000");
		i_cal_time_set_timezone (itt, zone);
		i_cal_component_set_dtend (modified, itt);
		g_clear_object (&itt);

		i_cal_component_set_summary (modified, "3nd modified");

		success = e_cal_client_modify_object_sync (client, modified, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		g_clear_object (&modified);

		/* verify instances */
		success = e_cal_client_get_objects_for_uid_sync (client, uid, &ecomps, NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);
		g_assert_cmpuint (g_slist_length (ecomps), ==, 3);
		g_slist_free_full (ecomps, g_object_unref);
		ecomps = NULL;

		rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
		e_cal_client_generate_instances_for_uid_sync (client, uid, i_cal_time_as_timet (start), i_cal_time_as_timet (end),
			NULL, recur_instance_cb, &rd);
		g_assert_cmpuint (g_hash_table_size (rd.instances), ==, G_N_ELEMENTS (expected_shortened) - 1);
		verify_received_instances (rd.instances, zone, expected_shortened);
		g_clear_pointer (&rd.instances, g_hash_table_unref);

		rd.instances = g_hash_table_new_full (instance_hash, instance_equal, instance_free, NULL);
		e_cal_client_generate_instances_for_object_sync (client, icomp, i_cal_time_as_timet (start), i_cal_time_as_timet (end),
			NULL, recur_instance_cb, &rd);
		g_assert_cmpuint (g_hash_table_size (rd.instances), ==, G_N_ELEMENTS (expected_shortened) - 1);
		verify_received_instances (rd.instances, zone, expected_shortened);
		g_clear_pointer (&rd.instances, g_hash_table_unref);

		g_clear_object (&icomp);
		g_free (uid);
	}

	g_clear_object (&start);
	g_clear_object (&end);
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
	g_test_add (
		"/ECalRecur/Reminders",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_reminders,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/Detached",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_detached,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemindersDetached",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_reminders_detached,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemoveInstanceFirst",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_remove_instance_first,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemoveInstanceMid",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_remove_instance_mid,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemoveInstanceLast",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_remove_instance_last,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemoveInstanceFirstCal",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_remove_instance_first_cal,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemoveInstanceMidCal",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_remove_instance_mid_cal,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/RemoveInstanceLastCal",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_remove_instance_last_cal,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalRecur/MultipleDetached",
		ETestServerFixture,
		&test_closure,
		e_test_server_utils_setup,
		test_recur_multiple_detached,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
