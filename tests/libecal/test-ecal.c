/* Evolution calendar client - test program
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

/* start_testing_scaffold */
#define mu_assert(message, test) do { if (!(test)) return message; else { tests_passed++; return NULL;}} while (0)
#define mu_run_test(test) do { const gchar *message = test; tests_run++; \
                                if (message) { cl_printf (client, "***Error***\n%s\n", message); break;} } while (0)

static gint tests_run = 0;
static gint tests_passed = 0;
/* end_testing_scaffold */

static ECal *client1;
static ECal *client2;

static GMainLoop *loop;

/* Prints a message with a client identifier */
static void
cl_printf (ECal *client,
           const gchar *format,
           ...)
{
	va_list args;

	if (client != client1)
		return;

	va_start (args, format);
	printf ("Client %s: ", "Test");
	vprintf (format, args);
	va_end (args);
}

static void
objects_added_cb (GObject *object,
                  GList *objects,
                  gpointer data)
{
	GList *l;

	for (l = objects; l; l = l->next)
		cl_printf (data, "Object added %s\n", icalcomponent_get_uid (l->data));
}

static void
objects_modified_cb (GObject *object,
                     GList *objects,
                     gpointer data)
{
	GList *l;

	for (l = objects; l; l = l->next)
		cl_printf (data, "Object modified %s\n", icalcomponent_get_uid (l->data));
}

static void
objects_removed_cb (GObject *object,
                    GList *objects,
                    gpointer data)
{
	GList *l;

	for (l = objects; l; l = l->next)
		cl_printf (data, "Object removed %s\n", icalcomponent_get_uid (l->data));
}

static void
view_complete_cb (GObject *object,
                  ECalendarStatus status,
                  const gchar *error_msg,
                  gpointer data)
{
	cl_printf (data, "View complete (status:%d, error_msg:%s)\n", status, error_msg ? error_msg : "NULL");
}

static gboolean
list_uids (ECal *client)
{
	GList *objects = NULL;
	GList *l;

	if (!e_cal_get_object_list (client, "(contains? \"any\" \"test\")", &objects, NULL))
		return FALSE;

	cl_printf (client, "UIDS: ");

	cl_printf (client, "\nGot %d objects\n", g_list_length (objects));
	if (!objects)
		printf ("none\n");
	else {
		for (l = objects; l; l = l->next) {
			const gchar *uid;

			uid = icalcomponent_get_uid (l->data);
			printf ("`%s' ", uid);
		}

		printf ("\n");

		for (l = objects; l; l = l->next) {
			gchar *obj = icalcomponent_as_ical_string_r (l->data);
			printf ("------------------------------\n");
			printf ("%s", obj);
			printf ("------------------------------\n");
			free (obj);
		}
	}

	e_cal_free_object_list (objects);

	return FALSE;
}

/* Callback used when a client is destroyed */
static void
client_destroy_cb (gpointer data,
                   GObject *object)
{
	if (E_CAL (object) == client1)
		client1 = NULL;
	else if (E_CAL (object) == client2)
		client2 = NULL;

	if (!client1 && !client2)
		g_main_loop_quit (loop);
}

static const gchar *
test_object_creation (ECal *client,
                      gchar **uid)
{
	ECalComponent *comp, *comp_retrieved;
	icalcomponent *icalcomp, *icalcomp_retrieved;
	struct icaltimetype tt;
	ECalComponentText text;
	ECalComponentDateTime dt;
	ECalComponentTransparency transp;
	gboolean compare;
	GError *error = NULL;

	comp = e_cal_component_new ();
	/* set fields */
	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	text.value = "Creation of new test event";
	text.altrep = NULL;
	e_cal_component_set_summary (comp, &text);
	tt = icaltime_from_string ("20040109T090000Z");
	dt.value = &tt;
	dt.tzid ="UTC";
	e_cal_component_set_dtstart (comp, &dt);
	tt = icaltime_from_string ("20040109T103000");
	dt.value = &tt;
	dt.tzid ="UTC";
	e_cal_component_set_dtend (comp, &dt);
	e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);

	e_cal_component_commit_sequence (comp);
	icalcomp = e_cal_component_get_icalcomponent (comp);
	if (!e_cal_create_object (client, icalcomp, uid, &error)) {
		cl_printf (client, "Object creation:  %s\n", error->message);
		g_free (comp);
		g_free (icalcomp);
		return "Test Object Creation failed";
	}
	e_cal_component_commit_sequence (comp);
	if (!e_cal_get_object (client, *uid, NULL, &icalcomp_retrieved, &error)) {
		cl_printf (client, "Object retrieval:  %s\n", error->message);
		g_free (uid);
		g_free (comp);
		g_free (icalcomp);
		return "Test Object Creation failed";

	}

	comp_retrieved = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp_retrieved, icalcomp_retrieved)) {
		cl_printf (client, "Could not set icalcomponent\n");
		g_free (uid);
		g_free (comp);
		g_free (icalcomp);
		g_free (icalcomp_retrieved);
		return "Test Object Creation failed";

	}
	/* Dumping icalcomp into a string is not useful as the retrieved object
	 * has some generated information like timestamps. We compare
	 * member values we set during creation*/
	compare = e_cal_component_event_dates_match (comp, comp_retrieved);

	if (compare) {
		e_cal_component_get_transparency (comp_retrieved, &transp);
		compare = (transp == E_CAL_COMPONENT_TRANSP_OPAQUE);
	}

	g_free (comp_retrieved);
	g_free (comp);
	g_free (icalcomp);
	g_free (icalcomp_retrieved);

	mu_assert ("Test Object creation : Created object does not match retrieved data\n", compare);
	return NULL;
}

static const gchar *
test_object_modification (ECal *client,
                          gchar *uid)
{
	const gchar *summary = "This summary was modified";
	icalcomponent *icalcomp, *icalcomp_modified;
	gboolean compare;
	GError *error = NULL;

	if (!e_cal_get_object (client, uid, NULL, &icalcomp, &error)) {
		cl_printf (client, "Test Modify object : Could not get the object: %s\n", error->message);
		g_free (uid);
		return error->message;
	}

	/* Modify one property of the icalcomp and save it.
	 * Now retrieve it and check the field. */
	icalcomponent_set_summary (icalcomp, summary);
	if (!e_cal_modify_object  (client, icalcomp, CALOBJ_MOD_THIS, &error)) {
		cl_printf (client, "Test Modify object : Could not modify the object: %s\n", error->message);
		g_free (uid);
		g_free (icalcomp);
		return error->message;
	}

	if (!e_cal_get_object (client, uid, NULL, &icalcomp_modified, &error)) {
		cl_printf (client, "Test Modify object : Could not get the modified object: %s\n", error->message);
		g_free (uid);
		g_free (icalcomp);
		return "Test Object Creation failed";
	}

	compare = !strcmp ( icalcomponent_get_summary (icalcomp_modified), summary);

	g_free (uid);
	g_free (icalcomp);
	g_free (icalcomp_modified);

	mu_assert ("Test Modify object : Modification failed\n", compare);
	return NULL;
}

#if 0
static gchar *
test_object_removal (ECal *client)
{

	gchar *uid;
	ECalComponent *comp;
	icalcomponent *icalcomp;
	gboolean compare = 1;
	GError *error = NULL;

	comp = e_cal_component_new ();
	e_cal_component_commit_sequence (comp);
	icalcomp = e_cal_component_get_icalcomponent (comp);
	if (!e_cal_create_object (client, icalcomp, &uid, &error)) {
		cl_printf (client, "Test object removal - Object creation:  %s\n", error->message);
		g_object_unref (comp);
		g_object_unref (icalcomp);
		return "Test Object Removal failed\n";
	}

	if (!e_cal_remove_object (client, uid, &error)) {
		cl_printf (client, "Test object removal - Could not remove the object\n");
		g_free (uid);
		g_object_unref (comp);
		g_object_unref (icalcomp);
		return "Test Object Removal failed\n";

	}

	compare =  e_cal_get_object (client, uid, NULL, &icalcomp, &error);

	g_free (uid);
	g_object_unref (comp);
	g_object_unref (icalcomp);

	mu_assert ("Test object removal - Failed\n", compare);
	return NULL;
}
#endif

static const gchar *
test_get_alarms_in_range (ECal *client)
{
	GSList *alarms;
	icaltimezone *utc;
	time_t start, end;
	gboolean compare;

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);

	alarms = e_cal_get_alarms_in_range (client, start, end);
	compare = (g_slist_length (alarms) == 3);

	e_cal_free_alarms (alarms);
	mu_assert ("Test getting alarms in range\n", compare);

	return NULL;
}

static const gchar *
test_cal_loaded (ECal *client)
{
	/* Test one loaded calendar and another that is not loaded. */
	mu_assert (
		"Test get_cal_load_state : Failed \n",
		(E_CAL_LOAD_LOADED == e_cal_get_load_state (client)) &&
		(E_CAL_LOAD_NOT_LOADED == e_cal_get_load_state (NULL)));

	return NULL;
}

static const gchar *
test_get_source (ECal *client,
                 const gchar *expected)
{
	const gchar *uri;
	gchar *cal_uri;
	gboolean compare = 0;

	/* FIXME ESources no longer have built-in URIs. */
	/* uri = e_source_get_uri (source); */
	uri = "";
	cal_uri = g_strconcat ("file://", expected, NULL);
	compare = !strcmp (expected, uri);

	g_free (cal_uri);
	mu_assert ("Test get_source : Failed\n", compare);

	return NULL;
}

static const gchar *
test_query (ECal *client,
            const gchar *query,
            gint expected)
{
	/* This uses pre-loaded data. Hence its results are valid only
	 * when called before any write operation is performed.
	 */
	gint i = 0;
	GList *objects = NULL;

	if (!e_cal_get_object_list (client, query, &objects, NULL))
		return "Could not get the list of objects";
	i = g_list_length (objects);
	e_cal_free_object_list (objects);

	mu_assert ("Test get_object_list : Expected number of objects not found", i == expected);

	return NULL;
}

#if 0
static gchar *
test_e_cal_new (ECal **cal,
                const gchar *uri)
{
	GError *error = NULL;
	gchar *cal_uri, *cal_file;
	gboolean created = 0;

	cal_uri = g_strconcat ("file://", uri, NULL);
	*cal = e_cal_new_from_uri (cal_uri, E_CAL_SOURCE_TYPE_EVENT);
	if (!*cal) {
		g_message (G_STRLOC ": could not create the client");
		g_free (cal_uri);
		return "Test Creation of new calendar : Failed";
	}
	g_object_weak_ref (G_OBJECT (*cal), client_destroy_cb, NULL);

	cl_printf (*cal, "Calendar loading `%s'...\n", uri);

	if (!e_cal_open (*cal, FALSE, &error)) {
		cl_printf (*cal, "Load/create %s\n", error->message);
		g_free (cal_uri);
		return "Test creation of new calendar : Failed";
	}

	cal_file = g_strconcat (uri, "/calendar.ics", NULL);

	created = g_file_test (cal_file, G_FILE_TEST_EXISTS);
	g_free (cal_uri);
	g_free (cal_file);

	mu_assert ("Test creation of new calendar : Failed", created);

	return NULL;
}

static gchar *
test_e_cal_remove (ECal *ecal,
                   const gchar *uri)
{
	gchar *cal_uri;
	GError *error = NULL;
	gboolean removed = 0;

	cal_uri = g_strconcat (uri, "/calendar.ics", NULL);
	if (!e_cal_remove (ecal, &error)) {
		cl_printf (ecal, "Test Calendar removal : Could not remove the Calendar : %s\n", error->message);
	}

	removed = !g_file_test (uri, G_FILE_TEST_EXISTS);
	g_free (cal_uri);

	mu_assert ("Test Remove calendar : Failed ", removed);

	return NULL;
}
#endif

static const gchar *
test_new_system_calendar (void)
{
#if 0  /* ACCOUNT_MGMT */
	const gchar *user_data_dir;
	gchar *filename;
	gboolean created;

	e_cal_new_system_calendar ();

	user_data_dir = e_get_user_data_dir ();
	filename = g_build_filename (
		user_data_dir, "calendar", "system", "calendar.ics", NULL);
	created = g_file_test (filename, G_FILE_TEST_EXISTS);
	g_free (filename);

	mu_assert ("Test creation of default system calendar : Failed", created);
#endif /* ACCOUNT_MGMT */

	return NULL;
}

static const gchar *
test_new_system_tasks (void)
{
#if 0  /* ACCOUNT_MGMT */
	const gchar *user_data_dir;
	gchar *filename;
	gboolean created;

	e_cal_new_system_tasks ();

	user_data_dir = e_get_user_data_dir ();
	filename = g_build_filename (
		user_data_dir, "tasks", "system", "tasks.ics", NULL);
	created = g_file_test (filename, G_FILE_TEST_EXISTS);
	g_free (filename);

	mu_assert ("Test creation of default system tasks : Failed", created);
#endif /* ACCOUNT_MGMT */

	return NULL;
}

static const gchar *
test_new_system_memos (void)
{
#if 0  /* ACCOUNT_MGMT */
	const gchar *user_data_dir;
	gchar *filename;
	gboolean created;

	e_cal_new_system_memos ();

	user_data_dir = e_get_user_data_dir ();
	filename = g_build_filename (
		user_data_dir, "memos", "system", "journal.ics", NULL);
	created = g_file_test (filename, G_FILE_TEST_EXISTS);
	g_free (filename);

	mu_assert ("Test creation of default system memos : Failed", created);
#endif /* ACCOUNT_MGMT */

	return NULL;
}

static gchar *
test_get_free_busy (ECal *client)
{
	/* TODO uses NULL for users and currently specific to file backend. */
	GList *l, *freebusy = NULL;
	GError *error = NULL;
	icaltimezone *utc;
	time_t start, end;

	utc = icaltimezone_get_utc_timezone ();
	start = time_from_isodate ("20040212T000000Z");
	end = time_add_day_with_zone (start, 2, utc);

	if (!e_cal_get_free_busy (client, NULL, start, end, &freebusy, &error)) {
		cl_printf (client, "Test free/busy : Could not retrieve free busy information :  %s\n", error->message);
		return error->message;
	}
	if (freebusy) {
		cl_printf (client, "Printing free busy information\n");
		for (l = freebusy; l; l = l->next) {
			gchar *comp_string;
			ECalComponent *comp = E_CAL_COMPONENT (l->data);

			comp_string = e_cal_component_get_as_string (comp);
			cl_printf (client, "%s\n\n", comp_string);
			g_object_unref (comp);
			g_free (comp_string);
		}
	}
	else {
		cl_printf (client, "free_busy was returned but NULL");
	}
	return NULL;
}

static gchar *
test_get_default_object (ECal *client)
{
	icalcomponent *icalcomp;
	GError *error = NULL;
	gchar *ical_string;
	if (e_cal_get_default_object (client, &icalcomp, &error)) {
		ical_string = icalcomponent_as_ical_string_r (icalcomp);
		cl_printf (client, "Obtained default object: %s\n", ical_string);
		g_free (ical_string);
		tests_passed++;
		return NULL;

	} else
		cl_printf (client, "Test Get default object : Could not get the default object: %s\n", error->message);
	return error->message;
}

/* XXX The string pasted below is *really* ugly. Alternatively, it could be
 * read from a file at run-time. Not sure if it is an elegant solution when
 * multiple clients try to load the same file during stress testing.
 * how can this be done better ?
 */
#define EXPECTED \
"BEGIN : VEVENT\
UID : 20040213T055519Z - 15802 - 500 - 1 - 3@testcal\
DTSTAMP : 20040213T055519Z\
DTSTART; TZID=/softwarestudio.org / Olson_20011030_5 / Asia / Calcutta:\
 20040213T130000\
DTEND; TZID=/softwarestudio.org / Olson_20011030_5 / Asia / Calcutta:\
 20040213T133000\
TRANSP : OPAQUE\
SEQUENCE : 3\
SUMMARY : Test - Travel plans to Kansas\
LOCATION : Yellow Brick road\
CLASS : PUBLIC\
ORGANIZER; CN = dorothy : MAILTO : dorothy@oz\
DESCRIPTION: Discuss way to home\
ATTENDEE; CUTYPE = INDIVIDUAL; ROLE = REQ - PARTICIPANT; PARTSTAT = ACCEPTED;\
 RSVP = TRUE; CN = dorothy; LANGUAGE = en : MAILTO : dorothy@oz\
ATTENDEE; CUTYPE = INDIVIDUAL; ROLE = REQ - PARTICIPANT; PARTSTAT = NEEDS - ACTION;\
 RSVP = TRUE; CN = tinman; LANGUAGE = en : MAILTO : tinman@oz\
ATTENDEE; CUTYPE = INDIVIDUAL; ROLE = REQ - PARTICIPANT; PARTSTAT = NEEDS - ACTION;\
 RSVP = TRUE; CN = toto; LANGUAGE = en : MAILTO : toto@oz\
ATTENDEE; CUTYPE = INDIVIDUAL; ROLE = OPT - PARTICIPANT; PARTSTAT = NEEDS - ACTION;\
 RSVP = TRUE; CN = scarecrow; LANGUAGE = en : MAILTO : scarecrow@oz\
LAST - MODIFIED : 20040213T055647Z\
END : VEVENT"

static const gchar *
test_get_object (ECal *client)
{
	const gchar *uid = "20040213T055519Z-15802-500-1-3@testcal";
	gchar *actual;
	icalcomponent *icalcomp;
	gboolean compare;
	GError *error = NULL;

	if (!e_cal_get_object (client, uid, NULL, &icalcomp, &error)) {
		cl_printf (client, "Test Get object : Could not get the object: %s\n", error->message);
		return error->message;
	}

	actual = icalcomponent_as_ical_string_r (icalcomp);
	compare = !strcmp (actual, EXPECTED);

	g_free (actual);

	mu_assert ("Test : get_object does not match the expected output", compare);
	return NULL;
}

static gchar *
test_timezones (ECal *client)
{
	icaltimezone *zone;
	GError *error = NULL;
	if (!e_cal_get_timezone (client, "UTC", &zone, &error))
	{
		cl_printf (client, "Could not get the timezone\n");
	}

	printf ("\n\nTime Zones : \n%s *** %s", icaltimezone_get_display_name (zone), icaltimezone_get_tzid (zone));
	printf ("\n\nTime Zones : \n%s", icaltimezone_get_location (zone));

	return NULL;
}

static const gchar *
all_tests (ECal *client,
           const gchar *uri)
{
	gchar *uid;

	mu_run_test (test_new_system_calendar ());
	mu_run_test (test_new_system_tasks ());
	mu_run_test (test_new_system_memos ());
	mu_run_test (test_get_source (client, uri));
	mu_run_test (test_cal_loaded (client));

	/* test_query acts on pre-loaded data. Hence it must executed before
	 * any writes are made */
	mu_run_test (test_query (client, "(contains? \"any\" \"test\")", 2));
	mu_run_test (test_query (client, "(contains? \"summary\" \"Kansas\")", 1));
	mu_run_test (test_query (client, "(contains? \"any\" \"gibberish\")", 0));

	mu_run_test (test_get_default_object (client));
	mu_run_test (test_get_object (client));
	mu_run_test (test_get_free_busy (client));
	mu_run_test (test_object_creation (client, &uid));
	mu_run_test (test_object_modification (client, uid));
	/* mu_run_test (test_object_removal (client)); */
	mu_run_test (test_get_alarms_in_range (client));

#if 0
	tmp = g_strconcat (uri, "_tmp", NULL);
	mu_run_test (test_e_cal_new (&ecal, tmp));
	mu_run_test (test_e_cal_remove (ecal, tmp));
	g_free (tmp);
#endif

	test_timezones (client);

	return NULL;
}

/* Creates a calendar client and tries to load the specified URI into it */
static void
create_client (ECal **client,
               const gchar *uri,
               ECalSourceType type,
               gboolean only_if_exists)
{
	const gchar *results;
	ECalView *query;
	gchar *cal_uri;
	GError *error = NULL;

	cal_uri = g_strconcat ("file://", uri, NULL);
	/* FIXME We don't build ECals from URIs anymore. */
	/* *client = e_cal_new_from_uri (cal_uri, type); */
	*client = NULL;
	if (!*client) {
		g_message (G_STRLOC ": could not create the client");
		exit (1);
	}

	g_object_weak_ref (G_OBJECT (*client), client_destroy_cb, NULL);

	cl_printf (*client, "Calendar loading `%s'...\n", uri);

	if (!e_cal_open (*client, only_if_exists, &error)) {
		cl_printf (*client, "Load/create %s\n", error->message);
		exit (1);
	}
	g_clear_error (&error);

	if (!e_cal_get_query (*client, "(contains? \"any\" \"Event\")", &query, NULL)) {
		cl_printf (*client, G_STRLOC ": Unable to obtain query");
		exit (1);
	}

	g_signal_connect (
		G_OBJECT (query), "objects_added",
		G_CALLBACK (objects_added_cb), client);
	g_signal_connect (
		G_OBJECT (query), "objects_modified",
		G_CALLBACK (objects_modified_cb), client);
	g_signal_connect (
		G_OBJECT (query), "objects_removed",
		G_CALLBACK (objects_removed_cb), client);
	g_signal_connect (
		G_OBJECT (query), "view_complete",
		G_CALLBACK (view_complete_cb), client);

	e_cal_view_start (query);

	results = all_tests (*client, uri);
	cl_printf (*client, "\n\n\n*************Tests run: %d****************\n\n", tests_run);
	cl_printf (*client, "*************Tests passed: %d*************\n\n\n", tests_passed);
	if (results != NULL)
		cl_printf (*client, "***Failures********%s\n", results);

	cl_printf (*client, "dump of the test calendar data");
	list_uids (*client);
	g_free (cal_uri);

}

gint
main (gint argc,
      gchar **argv)
{
	gchar *uri;
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();
	loop = g_main_loop_new (NULL, TRUE);

	/* arg1- file name; arg2- client suffix */
	uri = g_strconcat (argv[1], argv[2], NULL);
	create_client (&client1, uri, E_CAL_SOURCE_TYPE_EVENT, FALSE);

	g_free (uri);
	g_main_loop_run (loop);
	return 0;
}
