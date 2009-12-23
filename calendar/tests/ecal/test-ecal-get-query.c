/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

#define COMPLETE_TIMEOUT 30

#define EVENT_SUMMARY "Creation of the initial test event"
#define INITIAL_BEGIN_TIME     "20040109T090000Z"
#define INITIAL_BEGIN_TIMEZONE "UTC"
#define INITIAL_END_TIME       "20040109T103000"
#define INITIAL_END_TIMEZONE   "UTC"
#define FINAL_BEGIN_TIME       "20091221T090000Z"
#define FINAL_BEGIN_TIMEZONE   "UTC"

static void complete_timeout_cb (gpointer user_data) __attribute__ ((noreturn));

static GMainLoop *loop;
static guint complete_timeout_id;
static guint alter_cal_id;

typedef enum {
	SUBTEST_OBJECTS_ADDED,
	SUBTEST_OBJECTS_MODIFIED,
	SUBTEST_OBJECTS_REMOVED,
	SUBTEST_VIEW_DONE,
	NUM_SUBTESTS,
} SubTestId;

static void
subtest_passed (SubTestId id)
{
	static guint subtests_complete = 0;

	subtests_complete |= (1 << id);

	if (subtests_complete == ((1 << NUM_SUBTESTS) - 1))
		g_main_loop_quit (loop);
}

static void
objects_added_cb (GObject *object, GList *objects, gpointer data)
{
        GList *l;

        for (l = objects; l; l = l->next)
                g_print ("Object added %s\n", icalcomponent_get_uid (l->data));

	subtest_passed (SUBTEST_OBJECTS_ADDED);
}

static void
objects_modified_cb (GObject *object, GList *objects, gpointer data)
{
        GList *l;

        for (l = objects; l; l = l->next)
                g_print ("Object modified %s\n", icalcomponent_get_uid (l->data));

	subtest_passed (SUBTEST_OBJECTS_MODIFIED);
}

static void
objects_removed_cb (GObject *object, GList *objects, gpointer data)
{
        GList *l;

        for (l = objects; l; l = l->next) {
		ECalComponentId *id = l->data;

                g_print ("Object removed: uid: %s, rid: %s\n", id->uid,
				id->rid);
	}

	subtest_passed (SUBTEST_OBJECTS_REMOVED);
}

static void
view_done_cb (GObject *object, ECalendarStatus status, gpointer data)
{
        g_print ("View done\n");

	g_source_remove (complete_timeout_id);

	subtest_passed (SUBTEST_VIEW_DONE);
}

static void
complete_timeout_cb (gpointer user_data)
{
        g_error ("failed to complete all the pieces of the test in time");
}

static gboolean
alter_cal_cb (ECal *cal)
{
	ECalComponent *e_component;
	ECalComponent *e_component_final;
	icalcomponent *component;
	icalcomponent *component_final;
	struct icaltimetype icaltime;
	char *uid;

	/* create a calendar object */
	ecal_test_utils_create_component (cal, INITIAL_BEGIN_TIME,
			INITIAL_BEGIN_TIMEZONE, INITIAL_END_TIME,
			INITIAL_END_TIMEZONE, EVENT_SUMMARY, &e_component,
			&uid);
        component = e_cal_component_get_icalcomponent (e_component);

	component_final = ecal_test_utils_cal_get_object (cal, uid);
	ecal_test_utils_cal_assert_objects_equal_shallow (component,
			component_final);
	icalcomponent_free (component_final);

	/* make and commit changes to the object */
	icaltime = icaltime_from_string (FINAL_BEGIN_TIME);
	icalcomponent_set_dtstart (component, icaltime);
	ecal_test_utils_cal_component_set_icalcomponent (e_component,
			component);
	ecal_test_utils_cal_modify_object (cal, component, CALOBJ_MOD_ALL);

	/* verify the modification */
        component_final = ecal_test_utils_cal_get_object (cal, uid);
        e_component_final = e_cal_component_new ();
        ecal_test_utils_cal_component_set_icalcomponent (e_component_final,
                                component_final);

        ecal_test_utils_cal_assert_e_cal_components_equal (e_component,
                        e_component_final);

	/* remove the object */
	ecal_test_utils_cal_remove_object (cal, uid);

	/* Clean-up */
	ecal_test_utils_cal_remove (cal);

	g_object_unref (e_component_final);
	g_free (uid);
	icalcomponent_free (component);

	return FALSE;
}

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
	ECalView *view = NULL;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	view = ecal_test_utils_get_query (cal, "(contains? \"any\" \"event\")");

	/* monitor changes to the calendar */
	g_signal_connect (G_OBJECT (view), "objects_added",
			G_CALLBACK (objects_added_cb), cal);
	g_signal_connect (G_OBJECT (view), "objects_modified",
			G_CALLBACK (objects_modified_cb), cal);
	g_signal_connect (G_OBJECT (view), "objects_removed",
			G_CALLBACK (objects_removed_cb), cal);
	g_signal_connect (G_OBJECT (view), "view_done",
			G_CALLBACK (view_done_cb), cal);

	e_cal_view_start (view);

        loop = g_main_loop_new (NULL, TRUE);
	alter_cal_id = g_idle_add ((GSourceFunc) alter_cal_cb, cal);
        complete_timeout_id = g_timeout_add_seconds (COMPLETE_TIMEOUT,
                        (GSourceFunc) complete_timeout_cb, cal);

        g_main_loop_run (loop);

	g_object_unref (view);

	return 0;
}
