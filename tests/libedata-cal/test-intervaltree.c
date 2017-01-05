/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright 2008
 *
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
 * Authors: Stanislav Slusny <slusnys@gmail.com>
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libical/ical.h>
#include <libedata-cal/libedata-cal.h>

#define NUM_INTERVALS_CLOSED  100
#define NUM_INTERVALS_OPEN  100
#define NUM_SEARCHES  500
#define _TIME_MIN	((time_t) 0)		/* Min valid time_t	*/
#define _TIME_MAX	((time_t) INT_MAX)	/* Max valid time_t	*/

GRand *myrand = NULL;
const double pbality_delete = 0.3;

struct _EInterval
{
	gint start;
	gint end;
	ECalComponent * comp;
};

typedef struct _EInterval EInterval;

GList *list = NULL;

static inline gint
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

static GList *
search_in_list (GList *l,
                time_t start,
                time_t end)
{
	GList * result = NULL;
	EInterval *i, *i_new;

	while (l)
	{
		i = (EInterval *) l->data;

		if (compare_intervals (start, end, i->start, i->end) == 0)
		{
			i_new = g_new (EInterval, 1);

			i_new->start = i->start;
			i_new->end = i->end;
			i_new->comp = i->comp;
			g_object_ref (i->comp);

			result = g_list_insert (result, i_new, -1);
		}

		l = l->next;
	}

	return result;
}

/*
 * list1 .... from list
 * list2 ... from interval tree
 */
static gboolean
compare_interval_lists (GList *list1,
                        GList *list2)
{
	gboolean found;
	GList *l2;
	EInterval *i1;
	ECalComponent *c2, *c1;

	found = TRUE;

	while (list1 != NULL && found)
	{
		found = FALSE;

		l2 = list2;

		i1 = (EInterval *) list1->data;
		c1 = i1->comp;

		while (!found && l2 != NULL)
		{
			c2 = (ECalComponent *) l2->data;

			found = (c1 == c2);

			l2 = l2->next;
		}

		if (found)
			list1 = list1->next;
	}

	if (!found)
	{
		i1 = (EInterval *) list1->data;

		g_message (G_STRLOC ": Not found %d - %d\n", i1->start, i1->end);
	}

	return found;
}

static ECalComponent *
create_test_component (time_t start,
                       time_t end)
{
	ECalComponent *comp = e_cal_component_new ();
	ECalComponentText summary;
	struct icaltimetype current;
	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

	/*
	ECalComponentDateTime dtstart, dtend;
	struct icaltimetype time_start, time_end;
 *
	time_start = icaltime_from_timet_with_zone (start, 0, NULL);
	dtstart.value = icaltime_from_timet_with_zone (start, 0, NULL);
	dtstart.zone = icaltimezone_get_utc_timezone ();
 *
	dtend.value = icaltime_from_timet_with_zone (end, 0, NULL);
	dtend.value = icaltimezone_get_utc_timezone ();
	e_cal_component_set_dtstart (comp, &dtstart);
	e_cal_component_set_dtend (comp, &dtend);
	*/

	summary.value = g_strdup_printf ("%" G_GINT64_FORMAT "- %" G_GINT64_FORMAT, (gint64) start, (gint64) end);
	summary.altrep = NULL;

	e_cal_component_set_summary (comp, &summary);

	g_free ((gchar *) summary.value);

	current = icaltime_from_timet_with_zone (time (NULL), 0, NULL);
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	return comp;
}

static void
unref_comp (gpointer data,
            gpointer user_data)
{
	EInterval *interval = (EInterval *) data;
	g_object_unref (interval->comp);
	g_free (data);
}

/* Not used at the moment. Use it later 
static void
print_nodes_list (GList *l1)
{
	while (l1)
	{
		ECalComponent *comp = l1->data;
		ECalComponentText summary;
 *
		e_cal_component_get_summary (comp, &summary);
		g_print ("%s\n", summary.value);
		l1 = l1->next;
	}
}
 *
static void
print_list (GList *l2)
{
	while (l2)
	{
		EInterval * i = l2->data;
 *
		g_print ("%d - %d\n", i->start, i->end);
		l2 = l2->next;
	}
}
*/

static void
random_test (void)
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
	gint i, start, end;
	EInterval *interval = NULL;
	EIntervalTree *tree;
	GList *l1, *l2, *next;
	gint num_deleted = 0;

	tree = e_intervaltree_new ();

	for (i = 0; i < NUM_INTERVALS_CLOSED; i++)
	{
		ECalComponent *comp;
		start = g_rand_int_range (myrand, 0, 1000);
		end = g_rand_int_range (myrand, start, 2000);
		comp = create_test_component (start, end);
		if (!comp)
		{
			g_message (G_STRLOC ": error");
			exit (-1);
		}

		interval = g_new (EInterval, 1);
		interval->start = start;
		interval->end = end;
		interval->comp = comp;

		list = g_list_insert (list, interval, -1);

		e_intervaltree_insert (tree, start, end, comp);
	}

	/* insert open ended intervals */
	for (i = 0; i < NUM_INTERVALS_OPEN; i++)
	{
		ECalComponent *comp;
		start = g_rand_int_range (myrand, 0, 1000);
		comp = create_test_component (start, end);
		if (!comp)
		{
			g_message (G_STRLOC ": error");
			exit (-1);
		}

		interval = g_new (EInterval, 1);
		interval->start = start;
		interval->end = _TIME_MAX;
		interval->comp = comp;
		list = g_list_insert (list, interval, -1);

		e_intervaltree_insert (tree, start, interval->end, comp);
		/* g_print ("%d - %d\n", start, interval->end); */
	}

	g_print ("Number of intervals inserted: %d\n", NUM_INTERVALS_CLOSED + NUM_INTERVALS_OPEN);

	for (i = 0; i < NUM_SEARCHES; i++)
	{
		start = g_rand_int_range (myrand, 0, 1000);
		end = g_rand_int_range (myrand, 2000, _TIME_MAX);

		/*
		 * g_print ("Search for : %d - %d\n", start, end);
		 * */

		l1 = e_intervaltree_search (tree, start, end);

		l2 = search_in_list (list, start, end);

		if (!compare_interval_lists (l2, l1))
		{
			e_intervaltree_dump (tree);
			g_message (G_STRLOC "Error");
			exit (-1);
		}

		/* g_print ("OK\n"); */
		g_list_foreach (l1, (GFunc) g_object_unref, NULL);
		g_list_foreach (l2, (GFunc) unref_comp, NULL);
		g_list_free (l1);
		g_list_free (l2);
	}

	/* open-ended intervals */
	for (i = 0; i < 20; i++)
	{
		start = g_rand_int_range (myrand, 0, 1000);
		end = _TIME_MAX;

		/*
		 * g_print ("Search for : %d - %d\n", start, end);
		 * */

		l1 = e_intervaltree_search (tree, start, end);

		l2 = search_in_list (list, start, end);

		if (!compare_interval_lists (l2, l1))
		{
			e_intervaltree_dump (tree);
			g_message (G_STRLOC "Error");
			exit (-1);
		}

		/* g_print ("OK\n"); */
		g_list_foreach (l1, (GFunc) g_object_unref, NULL);
		g_list_foreach (l2, (GFunc) unref_comp, NULL);
		g_list_free (l1);
		g_list_free (l2);
	}

	l1 = list;

	while (l1)
	{
		/* perhaps we will delete l1 */
		next = l1->next;

		if (g_rand_double (myrand) < pbality_delete)
		{
			ECalComponent *comp;
			const gchar *uid = NULL;
			gchar *rid;
			interval = (EInterval *) l1->data;
			comp = interval->comp;

			/* delete l1 */
			/*
			 * g_print ("Deleting interval %d - %d\n", interval->start,
			 * interval->end);
			 * */

			rid = e_cal_component_get_recurid_as_string (comp);
			e_cal_component_get_uid (comp, &uid);
			if (!e_intervaltree_remove (tree, uid, rid))
			{
				g_free (rid);
				e_intervaltree_dump (tree);
				g_print (
					"Deleting interval %d - %d ERROR\n", interval->start,
					interval->end);
				exit (-1);
			}

			g_free (rid);
			g_object_unref (interval->comp);
			g_free (l1->data);
			list = g_list_delete_link (list, l1);
			num_deleted++;
		}

		l1 = next;
	}

	g_print ("Number of intervals deleted: %d\n", num_deleted);

	for (i = 0; i < NUM_SEARCHES; i++)
	{
		start = g_rand_int_range (myrand, 0, 1000);
		end = g_rand_int_range (myrand, start, 2000);

		/*
		 * g_print ("Search for : %d - %d\n", start, end);
		 * */

		l1 = e_intervaltree_search (tree, start, end);

		/*
		 * g_print ("Results from tree:\n");
		 * print_nodes_list (l1);
		 */

		l2 = search_in_list (list, start, end);

		if (!compare_interval_lists (l2, l1))
		{
			g_print ("ERROR!\n\n");
			return;
		}

		g_list_foreach (l1, (GFunc) g_object_unref, NULL);
		g_list_foreach (l2, (GFunc) unref_comp, NULL);
		g_list_free (l1);
		g_list_free (l2);

		/* g_print ("OK\n"); */
	}

	e_intervaltree_destroy (tree);
	g_list_foreach (list, (GFunc) unref_comp, NULL);
	g_list_free (list);
}

static void
mem_test (void)
{
	EIntervalTree *tree;
	time_t start = 10, end = 50;
	ECalComponent *comp = create_test_component (start, end), *clone_comp;
	const gchar *uid;
	gchar *rid;

	tree = e_intervaltree_new ();

	g_assert (((GObject *) comp)->ref_count == 1);
	e_intervaltree_insert (tree, start, end, comp);
	g_assert (((GObject *) comp)->ref_count == 2);

	e_cal_component_get_uid (comp, &uid);
	rid = e_cal_component_get_recurid_as_string (comp);
	e_intervaltree_remove (tree, uid, rid);
	g_free (rid);
	g_assert (((GObject *) comp)->ref_count == 1);

	e_intervaltree_insert (tree, start, end, comp);
	g_assert (((GObject *) comp)->ref_count == 2);

	clone_comp = e_cal_component_clone (comp);
	e_intervaltree_insert (tree, start, end, clone_comp);

	g_assert (((GObject *) comp)->ref_count == 1);
	g_assert (((GObject *) clone_comp)->ref_count == 2);

	e_intervaltree_destroy (tree);

	g_assert (((GObject *) comp)->ref_count == 1);
	g_assert (((GObject *) clone_comp)->ref_count == 1);

	g_object_unref (comp);
	g_object_unref (clone_comp);
}

gint
main (gint argc,
      gchar **argv)
{
	myrand = g_rand_new ();
	mem_test ();
	random_test ();
	g_print ("Everything OK\n");

	return 0;
}
