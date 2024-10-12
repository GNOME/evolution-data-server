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
#include <locale.h>
#include <libecal/libecal.h>

#include "e-test-server-utils.h"
#include "test-cal-cache-utils.h"

#define NUM_INTERVALS_CLOSED	100
#define NUM_INTERVALS_OPEN	100
#define NUM_SEARCHES		500
#define DELETE_PROBABILITY	0.3
#define _TIME_MIN		((time_t) 0)		/* Min valid time_t	*/
#define _TIME_MAX		((time_t) INT_MAX)	/* Max valid time_t	*/

typedef struct _IntervalData {
	gint start;
	gint end;
	ECalComponent * comp;
} IntervalData;

static void
interval_data_free (gpointer ptr)
{
	IntervalData *id = ptr;

	if (id) {
		g_object_unref (id->comp);
		g_free (id);
	}
}

static gint
compare_intervals (time_t x_start,
		   time_t x_end,
		   time_t y_start,
		   time_t y_end)
{
	/* assumption: x_start <= x_end */
	/* assumption: y_start <= y_end */

	/* x is left of y */
	if (x_end < y_start)
		return -1;

	/* x is right of y */
	if (y_end < x_start)
		return 1;

	/* x and y overlap */
	return 0;
}

static GHashTable *
search_in_intervals (ETimezoneCache *zone_cache,
		     GSList *intervals,
		     time_t start,
		     time_t end)
{
	ECalBackendSExp *sexp;
	ICalTime *itt_start, *itt_end;
	gchar *expr;
	GSList *link;
	GHashTable *res;

	itt_start = i_cal_time_new_from_timet_with_zone (start, FALSE, NULL);
	itt_end = i_cal_time_new_from_timet_with_zone (end, FALSE, NULL);

	expr = g_strdup_printf ("(occur-in-time-range? (make-time \"%04d%02d%02dT%02d%02d%02dZ\") (make-time \"%04d%02d%02dT%02d%02d%02dZ\"))",
		i_cal_time_get_year (itt_start), i_cal_time_get_month (itt_start), i_cal_time_get_day (itt_start),
		i_cal_time_get_hour (itt_start), i_cal_time_get_minute (itt_start), i_cal_time_get_second (itt_start),
		i_cal_time_get_year (itt_end), i_cal_time_get_month (itt_end), i_cal_time_get_day (itt_end),
		i_cal_time_get_hour (itt_end), i_cal_time_get_minute (itt_end), i_cal_time_get_second (itt_end));

	sexp = e_cal_backend_sexp_new (expr);

	g_clear_object (&itt_start);
	g_clear_object (&itt_end);
	g_free (expr);

	g_assert_nonnull (sexp);

	res = g_hash_table_new_full ((GHashFunc) e_cal_component_id_hash, (GEqualFunc) e_cal_component_id_equal,
		(GDestroyNotify) e_cal_component_id_free, g_object_unref);

	for (link = intervals; link; link = g_slist_next (link)) {
		IntervalData *data = link->data;

		if (compare_intervals (start, end, data->start, data->end) == 0 &&
		    e_cal_backend_sexp_match_comp (sexp, data->comp, zone_cache)) {
			ECalComponentId *id = NULL;

			id = e_cal_component_get_id (data->comp);
			g_assert_nonnull (id);

			g_hash_table_insert (res, id, g_object_ref (data->comp));
		}
	}

	g_object_unref (sexp);

	return res;
}

static void
check_search_results (GSList *ecalcomps,
		      GHashTable *from_intervals)
{
	GSList *link;

	g_assert_cmpint (g_slist_length (ecalcomps), ==, g_hash_table_size (from_intervals));

	for (link = ecalcomps; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id = NULL;

		id = e_cal_component_get_id (comp);
		g_assert_nonnull (id);

		g_assert_true (g_hash_table_contains (from_intervals, id));

		e_cal_component_id_free (id);
	}
}

static ECalComponent *
create_test_component (time_t start,
		       time_t end)
{
	ECalComponent *comp;
	ECalComponentText *summary;
	ICalTime *current, *ittstart, *ittend;
	gchar *startstr, *endstr, *tmp;

	comp = e_cal_component_new ();

	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

	ittstart = i_cal_time_new_from_timet_with_zone (start, 0, NULL);
	ittend = i_cal_time_new_from_timet_with_zone (end, 0, NULL);

	i_cal_component_set_dtstart (e_cal_component_get_icalcomponent (comp), ittstart);
	i_cal_component_set_dtend (e_cal_component_get_icalcomponent (comp), ittend);

	startstr = i_cal_time_as_ical_string (ittstart);
	endstr = i_cal_time_as_ical_string (ittend);

	tmp = g_strdup_printf ("%s - %s", startstr, endstr);
	summary = e_cal_component_text_new (tmp, NULL);
	g_free (tmp);

	g_object_unref (ittstart);
	g_object_unref (ittend);
	g_free (startstr);
	g_free (endstr);

	e_cal_component_set_summary (comp, summary);

	e_cal_component_text_free (summary);

	current = i_cal_time_new_from_timet_with_zone (time (NULL), 0, NULL);
	e_cal_component_set_created (comp, current);
	e_cal_component_set_last_modified (comp, current);
	g_object_unref (current);

	return comp;
}

static void
test_intervals (TCUFixture *fixture,
		gconstpointer user_data)
{
	/*
	 * outline:
	 * 1. create new tree and empty list of intervals
	 * 2. insert some intervals into tree and list
	 * 3. do various searches, compare results of both structures
	 * 4. delete some intervals
	 * 5. do various searches, compare results of both structures
	 * 6. free memory
	 */
	GRand *myrand;
	IntervalData *interval;
	ECalComponent *comp;
	ETimezoneCache *zone_cache;
	GSList *l1, *intervals = NULL;
	GHashTable *from_intervals;
	gint num_deleted = 0;
	gint ii, start, end;
	gboolean success;
	GError *error = NULL;

	zone_cache = E_TIMEZONE_CACHE (fixture->cal_cache);

	myrand = g_rand_new ();

	for (ii = 0; ii < NUM_INTERVALS_CLOSED; ii++) {
		start = g_rand_int_range (myrand, 0, 1000);
		end = g_rand_int_range (myrand, start, 2000);
		comp = create_test_component (start, end);
		g_assert_true (comp != NULL);

		interval = g_new (IntervalData, 1);
		interval->start = start;
		interval->end = end;
		interval->comp = comp;

		intervals = g_slist_prepend (intervals, interval);

		success = e_cal_cache_put_component (fixture->cal_cache, comp, NULL, 0, E_CACHE_IS_ONLINE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
	}

	end = _TIME_MAX;

	/* insert open ended intervals */
	for (ii = 0; ii < NUM_INTERVALS_OPEN; ii++) {
		start = g_rand_int_range (myrand, 0, 1000);
		comp = create_test_component (start, end);
		g_assert_true (comp != NULL);

		interval = g_new (IntervalData, 1);
		interval->start = start;
		interval->end = end;
		interval->comp = comp;

		intervals = g_slist_prepend (intervals, interval);

		success = e_cal_cache_put_component (fixture->cal_cache, comp, NULL, 0, E_CACHE_IS_ONLINE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
	}

	for (ii = 0; ii < NUM_SEARCHES; ii++) {
		start = g_rand_int_range (myrand, 0, 1000);
		end = g_rand_int_range (myrand, 2000, _TIME_MAX);

		l1 = NULL;

		success = e_cal_cache_get_components_in_range (fixture->cal_cache, start, end, &l1, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		from_intervals = search_in_intervals (zone_cache, intervals, start, end);

		check_search_results (l1, from_intervals);

		g_slist_free_full (l1, g_object_unref);
		g_hash_table_destroy (from_intervals);
	}

	/* open-ended intervals */
	for (ii = 0; ii < 20; ii++) {
		start = g_rand_int_range (myrand, 0, 1000);
		end = _TIME_MAX;

		l1 = NULL;

		success = e_cal_cache_get_components_in_range (fixture->cal_cache, start, end, &l1, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		from_intervals = search_in_intervals (zone_cache, intervals, start, end);

		check_search_results (l1, from_intervals);

		g_slist_free_full (l1, g_object_unref);
		g_hash_table_destroy (from_intervals);
	}

	l1 = intervals;

	while (l1) {
		/* perhaps we will delete l1 */
		GSList *next = l1->next;

		if (g_rand_double (myrand) < DELETE_PROBABILITY) {
			ECalComponentId *id;

			interval = l1->data;
			comp = interval->comp;

			id = e_cal_component_get_id (comp);
			g_assert_true (id != NULL);

			success = e_cal_cache_remove_component (fixture->cal_cache, e_cal_component_id_get_uid (id),
				e_cal_component_id_get_rid (id), 0, E_CACHE_IS_ONLINE, NULL, &error);
			g_assert_no_error (error);
			g_assert_true (success);

			e_cal_component_id_free (id);

			interval_data_free (interval);
			intervals = g_slist_delete_link (intervals, l1);

			num_deleted++;
		}

		l1 = next;
	}

	for (ii = 0; ii < NUM_SEARCHES; ii++) {
		start = g_rand_int_range (myrand, 0, 1000);
		end = g_rand_int_range (myrand, start + 1, 2000);

		l1 = NULL;

		success = e_cal_cache_get_components_in_range (fixture->cal_cache, start, end, &l1, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		from_intervals = search_in_intervals (zone_cache, intervals, start, end);

		check_search_results (l1, from_intervals);

		g_slist_free_full (l1, g_object_unref);
		g_hash_table_destroy (from_intervals);
	}

	g_slist_free_full (intervals, interval_data_free);
	g_rand_free (myrand);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	tcu_read_args (argc, argv);

	/* Ensure that the client and server get the same locale */
	g_assert_true (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	g_test_add ("/ECalCache/Intervals", TCUFixture, NULL,
		tcu_fixture_setup, test_intervals, tcu_fixture_teardown);

	return e_test_server_utils_run_full (argc, argv, 0);
}
