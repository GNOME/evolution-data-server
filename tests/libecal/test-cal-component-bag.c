/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <libecal/libecal.h>

#include "e-test-server-utils.h"

#define SKIP_TIME_VAL ((guint) ~0)

/* #define WITH_DEBUG 1 */

#ifdef WITH_DEBUG
static void
dump_bag_content (ECalComponentBag *bag)
{
	guint jj, n_spans;

	if (!bag) {
		printf ("bag:NULL\n");
		return;
	}

	n_spans = e_cal_component_bag_get_n_spans (bag);
	printf ("bag:%p n-spans:%u\n", bag, n_spans);

	for (jj = 0; jj < n_spans; jj++) {
		const GPtrArray *items;

		printf ("   span[%u]:\n", jj);

		items = e_cal_component_bag_get_span (bag, jj);
		if (items) {
			guint ii;

			for (ii = 0; ii < items->len; ii++) {
				ECalComponentBagItem *item = g_ptr_array_index (items, ii);

				printf ("      item[%u]:%p", ii, item);
				if (item) {
					printf (" client:%p(%s) comp:%p(%s) rid:'%s' start:%" G_GINT64_FORMAT " duration:%" G_GUINT64_FORMAT
						" span-index:%u user-data:%p copy-func:%p free-func:%p",
						item->client, item->client ? e_source_get_display_name (e_client_get_source (E_CLIENT (item->client))) : "",
						item->comp, item->comp ? e_cal_component_get_uid (item->comp) : "",
						item->rid,
						item->start,
						item->duration_minutes,
						item->span_index,
						item->user_data,
						item->copy_user_data,
						item->free_user_data);
				}

				printf ("\n");
			}
		} else {
			printf ("      NULL\n");
		}
	}
}
#endif /* WITH_DEBUG */

static ECalClient *
create_client (const gchar *uid)
{
	ECalClient *client;
	ESource *source;
	GError *local_error = NULL;

	source = e_source_new_with_uid (uid, NULL, &local_error);
	g_assert_no_error (local_error);
	g_assert_nonnull (source);

	e_source_set_display_name (source, uid);

	client = g_object_new (E_TYPE_CAL_CLIENT, "source", source, NULL);
	g_assert_nonnull (client);

	g_clear_object (&source);

	return client;
}

static ECalComponent *
create_component (ECalComponentVType vtype,
		  const gchar *uid,
		  const gchar *rid,
		  const gchar *summary,
		  const gchar *tz_location,
		  guint start_date,
		  guint start_time,
		  guint end_date,
		  guint end_time,
		  guint due_date,
		  guint due_time,
		  guint duration_minutes)
{
	GString *ical;
	ECalComponent *comp;
	const gchar *vtype_str = NULL;
	gboolean is_utc = g_strcmp0 (tz_location, "UTC") == 0;

	switch (vtype) {
	case E_CAL_COMPONENT_EVENT:
		vtype_str = "VEVENT";
		break;
	case E_CAL_COMPONENT_TODO:
		vtype_str = "VTODO";
		break;
	case E_CAL_COMPONENT_JOURNAL:
		vtype_str = "VJOURNAL";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_assert_nonnull (vtype_str);

	ical = g_string_sized_new (256);
	g_string_append_printf (ical, "BEGIN:%s\r\n", vtype_str);
	g_string_append_printf (ical, "UID:%s\r\n", uid);
	if (rid && *rid)
		g_string_append_printf (ical, "RECURRENCE-ID:%s\r\n", rid);
	g_string_append_printf (ical, "SUMMARY:%s\r\n", summary);

	#define add_data_time_val(what, dt, tm) { \
		if (dt != SKIP_TIME_VAL) { \
			if (tz_location && *tz_location && !is_utc) \
				g_string_append_printf (ical, "%s;TZID=%s:%u", what, tz_location, dt); \
			else if (tm == SKIP_TIME_VAL) \
				g_string_append_printf (ical, "%s;VALUE=DATE:%u", what, dt); \
			else \
				g_string_append_printf (ical, "%s:%u", what, dt); \
			if (tm != SKIP_TIME_VAL) { \
				g_string_append_printf (ical, "T%06u", tm); \
				if (is_utc) \
					g_string_append_c (ical, 'Z'); \
			} \
			g_string_append (ical, "\r\n"); \
		} \
	}

	add_data_time_val ("DTSTART", start_date, start_time);
	add_data_time_val ("DTEND", end_date, end_time);
	add_data_time_val ("DUE", due_date, due_time);

	if (duration_minutes != SKIP_TIME_VAL)
		g_string_append_printf (ical, "DURATION:PT%uM\r\n", duration_minutes);

	g_string_append_printf (ical, "END:%s\r\n", vtype_str);

	comp = e_cal_component_new_from_string (ical->str);

	g_string_free (ical, TRUE);

	g_assert_nonnull (comp);

	return comp;
}

static void
test_component_bag_item_basic (void)
{
	ECalClient *client;
	ECalComponentBagItem *item1, *item2;
	ECalComponent *comp;
	ICalTimezone *zone;
	gboolean changed;

	/* free accepts NULL */
	e_cal_component_bag_item_free (NULL);

	client = create_client ("cl1");
	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", "Europe/Berlin",
		20250326, 120000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, 15);

	zone = i_cal_timezone_get_utc_timezone ();
	g_assert_nonnull (zone);

	item1 = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item1);
	g_assert_true (item1->client == client);
	g_assert_true (item1->comp == comp);
	g_assert_cmpstr (e_cal_component_get_uid (item1->comp), ==, "e1");
	g_assert_cmpstr (item1->rid, ==, NULL);
	g_assert_cmpint (item1->start, ==, 1742986800);
	g_assert_cmpint (item1->duration_minutes, ==, 15);
	g_assert_cmpint (item1->span_index, ==, 0);
	g_assert_true (item1->user_data == NULL);
	g_assert_true (item1->copy_user_data == NULL);
	g_assert_true (item1->free_user_data == NULL);
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item1, item1));

	item2 = e_cal_component_bag_item_copy (item1);
	g_assert_nonnull (item2);
	g_assert_true (item2->client == client);
	g_assert_true (item2->comp == comp);
	g_assert_cmpstr (e_cal_component_get_uid (item2->comp), ==, "e1");
	g_assert_cmpstr (item2->rid, ==, NULL);
	g_assert_cmpint (item2->start, ==, 1742986800);
	g_assert_cmpint (item2->duration_minutes, ==, 15);
	g_assert_cmpint (item2->span_index, ==, 0);
	g_assert_true (item2->user_data == NULL);
	g_assert_true (item2->copy_user_data == NULL);
	g_assert_true (item2->free_user_data == NULL);
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item2, item2));
	g_assert_true (item1 != item2);
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item1, item2));
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item2, item1));

	g_assert_cmpint (e_cal_component_bag_item_hash_by_comp (item1), ==, e_cal_component_bag_item_hash_by_comp (item2));

	g_clear_pointer (&item2, e_cal_component_bag_item_free);
	g_clear_object (&client);
	g_clear_object (&comp);

	/* different instanes of the same data */
	client = create_client ("cl1");
	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", "America/New_York",
		20250326, 120000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, 15);

	item2 = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item2);
	g_assert_true (item2->client == client);
	g_assert_true (item2->comp == comp);
	g_assert_true (item2->client != item1->client);
	g_assert_true (item2->comp != item1->comp);
	g_assert_cmpstr (e_cal_component_get_uid (item2->comp), ==, "e1");
	g_assert_cmpstr (item2->rid, ==, NULL);
	g_assert_cmpint (item2->start, ==, 1743004800);
	g_assert_cmpint (item2->duration_minutes, ==, 15);
	g_assert_cmpint (item2->span_index, ==, 0);
	g_assert_true (item2->user_data == NULL);
	g_assert_true (item2->copy_user_data == NULL);
	g_assert_true (item2->free_user_data == NULL);
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item2, item2));
	g_assert_true (item1 != item2);
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item1, item2));
	g_assert_true (e_cal_component_bag_item_equal_by_comp (item2, item1));

	e_cal_component_set_uid (item2->comp, "diff");
	g_free (item2->uid);
	item2->uid = g_strdup ("diff");

	g_assert_false (e_cal_component_bag_item_equal_by_comp (item1, item2));
	g_assert_false (e_cal_component_bag_item_equal_by_comp (item2, item1));
	g_assert_cmpint (e_cal_component_bag_item_hash_by_comp (item1), !=, e_cal_component_bag_item_hash_by_comp (item2));

	g_assert_cmpint (item2->duration_minutes, ==, 15);

	changed = e_cal_component_bag_item_read_times (item2, 0, zone);
	g_assert_false (changed);
	g_assert_cmpint (item2->duration_minutes, ==, 15);

	changed = e_cal_component_bag_item_read_times (item2, 10, zone);
	g_assert_false (changed);
	g_assert_cmpint (item2->duration_minutes, ==, 15);

	changed = e_cal_component_bag_item_read_times (item2, 23, zone);
	g_assert_true (changed);
	g_assert_cmpint (item2->duration_minutes, ==, 23);

	changed = e_cal_component_bag_item_read_times (item2, 21, zone);
	g_assert_true (changed);
	g_assert_cmpint (item2->duration_minutes, ==, 21);

	changed = e_cal_component_bag_item_read_times (item2, 14, zone);
	g_assert_true (changed);
	g_assert_cmpint (item2->duration_minutes, ==, 15);

	changed = e_cal_component_bag_item_read_times (item2, 15, zone);
	g_assert_false (changed);
	g_assert_cmpint (item2->duration_minutes, ==, 15);

	g_clear_pointer (&item1, e_cal_component_bag_item_free);
	g_clear_pointer (&item2, e_cal_component_bag_item_free);
	g_clear_object (&client);
	g_clear_object (&comp);
}

static void
test_component_bag_item_read (void)
{
	ECalClient *client;
	ECalComponentBagItem *item;
	ECalComponent *comp;
	ICalTimezone *zone;

	zone = i_cal_timezone_get_utc_timezone ();
	g_assert_nonnull (zone);

	client = create_client ("cl1");

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		20250326, 120000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, 1742990400);
	g_assert_cmpint (item->duration_minutes, ==, 0);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		20250326, 120000, 20250326, 130000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, 1742990400);
	g_assert_cmpint (item->duration_minutes, ==, 60);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		20250326, 120000, SKIP_TIME_VAL, SKIP_TIME_VAL, 20250326, 140000, SKIP_TIME_VAL);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, 1742990400);
	g_assert_cmpint (item->duration_minutes, ==, 120);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		20250326, 120000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, 130);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, 1742990400);
	g_assert_cmpint (item->duration_minutes, ==, 130);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, -1);
	g_assert_cmpint (item->duration_minutes, ==, 0);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		SKIP_TIME_VAL, SKIP_TIME_VAL, 20250326, 130000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, -1);
	g_assert_cmpint (item->duration_minutes, ==, 0);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, 20250326, 140000, SKIP_TIME_VAL);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, -1);
	g_assert_cmpint (item->duration_minutes, ==, 0);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, 130);
	item = e_cal_component_bag_item_new (client, comp, 0, zone);
	g_assert_nonnull (item);
	g_assert_cmpint (item->start, ==, -1);
	g_assert_cmpint (item->duration_minutes, ==, 130);
	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_clear_object (&comp);

	g_clear_object (&client);
}

static gpointer
test_bag_copy_user_data (gpointer ptr)
{
	return GINT_TO_POINTER (GPOINTER_TO_INT (ptr) + 1);
}

static guint test_bag_free_user_data_counter = 0;

static void
test_bag_free_user_data (gpointer ptr)
{
	test_bag_free_user_data_counter++;
}

static void
test_component_bag_item_user_data (void)
{
	ECalClient *client;
	ECalComponentBagItem *item1, *item2;
	ECalComponent *comp;
	ICalTimezone *zone;
	gint val;

	zone = i_cal_timezone_get_utc_timezone ();
	g_assert_nonnull (zone);

	client = create_client ("cl1");

	comp = create_component (E_CAL_COMPONENT_EVENT, "e1", NULL, "event 1", NULL,
		20250326, 120000, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL);
	item1 = e_cal_component_bag_item_new (client, comp, 0, zone);
	item1->user_data = GINT_TO_POINTER (3);

	item2 = e_cal_component_bag_item_copy (item1);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data), ==, GPOINTER_TO_INT (item2->user_data));
	g_assert_null (item1->copy_user_data);
	g_assert_null (item2->copy_user_data);
	g_assert_null (item1->free_user_data);
	g_assert_null (item2->free_user_data);
	g_clear_pointer (&item2, e_cal_component_bag_item_free);

	item1->copy_user_data = test_bag_copy_user_data;

	item2 = e_cal_component_bag_item_copy (item1);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data) + 1, ==, GPOINTER_TO_INT (item2->user_data));
	g_assert_true (item1->copy_user_data == test_bag_copy_user_data);
	g_assert_true (item1->copy_user_data == item2->copy_user_data);
	g_assert_null (item1->free_user_data);
	g_assert_null (item2->free_user_data);
	g_clear_pointer (&item2, e_cal_component_bag_item_free);

	test_bag_free_user_data_counter = 0;
	item1->free_user_data = test_bag_free_user_data;

	item2 = e_cal_component_bag_item_copy (item1);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data) + 1, ==, GPOINTER_TO_INT (item2->user_data));
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 0);
	g_assert_true (item1->copy_user_data == test_bag_copy_user_data);
	g_assert_true (item1->copy_user_data == item2->copy_user_data);
	g_assert_true (item1->free_user_data == test_bag_free_user_data);
	g_assert_true (item1->free_user_data == item2->free_user_data);
	g_clear_pointer (&item2, e_cal_component_bag_item_free);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 1);

	item2 = e_cal_component_bag_item_copy (item1);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data) + 1, ==, GPOINTER_TO_INT (item2->user_data));
	g_assert_true (item1->copy_user_data == test_bag_copy_user_data);
	g_assert_true (item1->copy_user_data == item2->copy_user_data);
	g_assert_true (item1->free_user_data == test_bag_free_user_data);
	g_assert_true (item1->free_user_data == item2->free_user_data);

	val = GPOINTER_TO_INT (item1->user_data);
	item1->copy_user_data = NULL;
	item1->free_user_data = NULL;

	test_bag_free_user_data_counter = 0;
	e_cal_component_bag_item_set_user_data (item2, GINT_TO_POINTER (val + 1), test_bag_copy_user_data, test_bag_free_user_data);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data), ==, val);
	g_assert_cmpint (GPOINTER_TO_INT (item2->user_data), ==, val + 1);
	g_assert_null (item1->copy_user_data);
	g_assert_true (item2->copy_user_data == test_bag_copy_user_data);
	g_assert_null (item1->free_user_data);
	g_assert_true (item2->free_user_data == test_bag_free_user_data);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 1);

	e_cal_component_bag_item_set_user_data (item2, GINT_TO_POINTER (val + 3), NULL, NULL);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data), ==, val);
	g_assert_cmpint (GPOINTER_TO_INT (item2->user_data), ==, val + 3);
	g_assert_null (item1->copy_user_data);
	g_assert_null (item2->copy_user_data);
	g_assert_null (item1->free_user_data);
	g_assert_null (item2->free_user_data);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 2);

	e_cal_component_bag_item_set_user_data (item2, GINT_TO_POINTER (val + 5), test_bag_copy_user_data, test_bag_free_user_data);
	g_assert_cmpint (GPOINTER_TO_INT (item1->user_data), ==, val);
	g_assert_cmpint (GPOINTER_TO_INT (item2->user_data), ==, val + 5);
	g_assert_null (item1->copy_user_data);
	g_assert_true (item2->copy_user_data == test_bag_copy_user_data);
	g_assert_null (item1->free_user_data);
	g_assert_true (item2->free_user_data == test_bag_free_user_data);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 2);

	g_clear_pointer (&item2, e_cal_component_bag_item_free);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 3);

	g_clear_pointer (&item1, e_cal_component_bag_item_free);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 3);

	g_clear_object (&comp);
	g_clear_object (&client);
}

static void
test_component_bag_create (void)
{
	ECalComponentBag *bag;
	ICalTimezone *zone;

	bag = e_cal_component_bag_new ();
	g_assert_nonnull (bag);
	g_assert_true (E_IS_CAL_COMPONENT_BAG (bag));

	zone = i_cal_timezone_get_utc_timezone ();
	e_cal_component_bag_set_timezone (bag, zone);
	g_assert_cmpstr (i_cal_timezone_get_tzid (zone), ==, i_cal_timezone_get_tzid (e_cal_component_bag_get_timezone (bag)));

	zone = i_cal_timezone_get_builtin_timezone ("Europe/Berlin");
	e_cal_component_bag_set_timezone (bag, zone);
	g_assert_cmpstr (i_cal_timezone_get_tzid (zone), ==, i_cal_timezone_get_tzid (e_cal_component_bag_get_timezone (bag)));

	zone = NULL;
	g_object_get (bag, "timezone", &zone, NULL);
	g_assert_nonnull (zone);
	g_assert_cmpstr (i_cal_timezone_get_tzid (zone), ==, i_cal_timezone_get_tzid (e_cal_component_bag_get_timezone (bag)));
	g_assert_cmpstr (i_cal_timezone_get_location (zone), ==, "Europe/Berlin");
	g_assert_true (zone == e_cal_component_bag_get_timezone (bag));

	g_object_unref (zone);
	g_object_unref (bag);
}

typedef struct _CheckData {
	const gchar *expected_added_uid;
	const gchar *expected_removed_uid; /* one in the array, not necessarily only that one */
	const gchar *expected_item_changed_uid;
	const gchar *expected_span_changed_uid; /* one in the array, not necessarily only that one */
	guint expected_n_items;
	guint expected_n_spans;
	guint expected_n_removed;
	guint expected_n_span_changed;
	guint called_n_items;
	guint called_n_spans;
	guint called_n_added;
	guint called_n_removed;
	guint called_n_item_changed;
	guint called_n_span_changed;
	gboolean do_checks;
	gboolean expected_counts_set;
	gboolean expected_added_set;
	gboolean expected_removed_set;
	gboolean expected_item_changed_set;
	gboolean expected_span_changed_set;
} CheckData;

static void
check_data_clear (CheckData *ck_data)
{
	memset (ck_data, 0, sizeof (CheckData));

	ck_data->do_checks = TRUE;
}

static void
check_data_set_expected_counts (CheckData *ck_data,
				guint n_items,
				guint n_spans)
{
	ck_data->expected_n_items = n_items;
	ck_data->expected_n_spans = n_spans;
	ck_data->expected_counts_set = TRUE;
}

static void
check_data_set_expected_added (CheckData *ck_data,
			       const gchar *uid)
{
	ck_data->expected_added_uid = uid;
	ck_data->expected_added_set = TRUE;
}

static void
check_data_set_expected_removed (CheckData *ck_data,
				 guint n_items,
				 const gchar *uid)
{
	ck_data->expected_removed_uid = uid;
	ck_data->expected_n_removed = n_items;
	ck_data->expected_removed_set = TRUE;
}

static void
check_data_set_expected_item_changed (CheckData *ck_data,
				      const gchar *uid)
{
	ck_data->expected_item_changed_uid = uid;
	ck_data->expected_item_changed_set = TRUE;
}

static void
check_data_set_expected_span_changed (CheckData *ck_data,
				      guint n_items,
				      const gchar *uid)
{
	ck_data->expected_span_changed_uid = uid;
	ck_data->expected_n_span_changed = n_items;
	ck_data->expected_span_changed_set = TRUE;
}

static void
test_notify_n_items_cb (GObject *object,
			GParamSpec *param,
			gpointer user_data)
{
	ECalComponentBag *bag = E_CAL_COMPONENT_BAG (object);
	CheckData *ck_data = user_data;

	if (ck_data->do_checks) {
		g_assert_true (ck_data->expected_counts_set);
		g_assert_cmpint (ck_data->expected_n_items, ==, e_cal_component_bag_get_n_items (bag));
	}

	ck_data->called_n_items++;
}

static void
test_notify_n_spans_cb (GObject *object,
			GParamSpec *param,
			gpointer user_data)
{
	ECalComponentBag *bag = E_CAL_COMPONENT_BAG (object);
	CheckData *ck_data = user_data;

	if (ck_data->do_checks) {
		g_assert_true (ck_data->expected_counts_set);
		g_assert_cmpint (ck_data->expected_n_spans, ==, e_cal_component_bag_get_n_spans (bag));
	}

	ck_data->called_n_spans++;
}

static void
test_added_cb (ECalComponentBag *bag,
	       ECalComponentBagItem *item,
	       gpointer user_data)
{
	CheckData *ck_data = user_data;

	if (ck_data->do_checks) {
		g_assert_true (ck_data->expected_added_set);
		g_assert_cmpstr (ck_data->expected_added_uid, ==, e_cal_component_get_uid (item->comp));
	}

	ck_data->called_n_added++;
}

static void
test_removed_cb (ECalComponentBag *bag,
		 const GPtrArray *items, /* ECalComponentBagItem * */
		 gpointer user_data)
{
	CheckData *ck_data = user_data;

	if (ck_data->do_checks) {
		gboolean found = FALSE;
		guint ii;

		g_assert_true (ck_data->expected_removed_set);

		for (ii = 0; ii < items->len; ii++) {
			ECalComponentBagItem *item = g_ptr_array_index (items, ii);
			if (g_strcmp0 (ck_data->expected_removed_uid, e_cal_component_get_uid (item->comp)) == 0) {
				found = TRUE;
				break;
			}
		}

		g_assert_cmpint (ck_data->expected_n_removed, ==, items->len);
		g_assert_true (found);
	}

	ck_data->called_n_removed++;
}

static void
test_item_changed_cb (ECalComponentBag *bag,
		      ECalComponentBagItem *item,
		      gpointer user_data)
{
	CheckData *ck_data = user_data;

	if (ck_data->do_checks) {
		g_assert_true (ck_data->expected_item_changed_set);
		g_assert_cmpstr (ck_data->expected_item_changed_uid, ==, e_cal_component_get_uid (item->comp));
	}

	ck_data->called_n_item_changed++;
}

static void
test_span_changed_cb (ECalComponentBag *bag,
		      const GPtrArray *items, /* ECalComponentBagItem * */
		      gpointer user_data)
{
	CheckData *ck_data = user_data;

	if (ck_data->do_checks) {
		gboolean found = FALSE;
		guint ii;

		g_assert_true (ck_data->expected_span_changed_set);

		for (ii = 0; ii < items->len; ii++) {
			ECalComponentBagItem *item = g_ptr_array_index (items, ii);
			if (g_strcmp0 (ck_data->expected_span_changed_uid, e_cal_component_get_uid (item->comp)) == 0) {
				found = TRUE;
				break;
			}
		}

		g_assert_cmpint (ck_data->expected_n_span_changed, ==, items->len);
		g_assert_true (found);
	}

	ck_data->called_n_span_changed++;
}

static ECalComponentBag *
create_bag (CheckData *ck_data)
{
	ECalComponentBag *bag;

	check_data_clear (ck_data);

	bag = e_cal_component_bag_new ();
	g_assert_nonnull (bag);
	g_assert_true (E_IS_CAL_COMPONENT_BAG (bag));

	g_signal_connect (bag, "notify::n-items", G_CALLBACK (test_notify_n_items_cb), ck_data);
	g_signal_connect (bag, "notify::n-spans", G_CALLBACK (test_notify_n_spans_cb), ck_data);
	g_signal_connect (bag, "added", G_CALLBACK (test_added_cb), ck_data);
	g_signal_connect (bag, "removed", G_CALLBACK (test_removed_cb), ck_data);
	g_signal_connect (bag, "item-changed", G_CALLBACK (test_item_changed_cb), ck_data);
	g_signal_connect (bag, "span-changed", G_CALLBACK (test_span_changed_cb), ck_data);

	return bag;
}

static void
test_component_bag_modify_on_added_cb (ECalComponentBag *bag,
				       ECalComponentBagItem *item,
				       gpointer user_data)
{
	g_assert_null (item->user_data);
	item->user_data = user_data;
}

static gboolean
test_component_bag_foreach_count_called_cb (ECalComponentBag *bag,
					    ECalComponentBagItem *item,
					    gpointer user_data)
{
	guint *p_n_called = user_data;

	*p_n_called = (*p_n_called) + 1;

	return TRUE;
}

static gboolean
test_component_bag_foreach_check_expected_cb (ECalComponentBag *bag,
					      ECalComponentBagItem *item,
					      gpointer user_data)
{
	gchar *buff = user_data;
	guint ii;

	for (ii = 0; buff[ii]; ii++) {
		if (buff[ii] == item->uid[0]) {
			buff[ii] = 'x';
			return TRUE;
		}
	}

	g_assert_not_reached ();

	return TRUE;
}

static gboolean
test_component_bag_foreach_count_and_stop_early_cb (ECalComponentBag *bag,
						    ECalComponentBagItem *item,
						    gpointer user_data)
{
	guint *pval = user_data;
	guint to_do, val;

	val = (*pval) & ((1 << 6) - 1);
	to_do = (*pval) >> 6;

	g_assert_cmpint (to_do, >, 0);
	to_do--;
	val++;

	*pval = (to_do << 6) | val;

	return to_do > 0;
}

static gboolean
test_component_bag_foreach_set_user_data_cb (ECalComponentBag *bag,
					     ECalComponentBagItem *item,
					     gpointer user_data)
{
	guint *p_n_called = user_data;

	*p_n_called = (*p_n_called) + 1;
	item->user_data = GUINT_TO_POINTER (*p_n_called);

	return TRUE;
}

static void
test_component_bag_basic (void)
{
	CheckData ck_data;
	ECalClient *client;
	ECalComponentBag *bag;
	const ECalComponentBagItem *item;
	ECalComponent *c07, *c08, *c09, *c11, *c13, *c15a, *c15b;
	ECalComponent *comps_array[7];
	ICalComponent *icomp;
	gchar expected_ids[8] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', '\0' };
	GPtrArray *items; /* ECalComponentBagItem * */
	const GPtrArray *const_items; /* ECalComponentBagItem * */
	guint ii, n_items, n_spans, n_called;

	client = create_client ("cl1");

	#define create_comp(_id, _start_time, _end_time) \
		create_component (E_CAL_COMPONENT_EVENT, _id, NULL, _id, NULL, \
			20250327, _start_time, 20250327, _end_time, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL)

	/*
	 06 07 08 09 10 11 12 13 14 15 16 17 18
	     AAAAAAAAAAAAAAAAAAAAAAAAAAA
	        BBBBBB   DDDDDDDDD   FFFFFF
	           CCCCCCCCC   EEEEEEgggggg
	 */

	c07  = comps_array[0] = create_comp ("A",  70000, 160000);
	c08  = comps_array[1] = create_comp ("B",  80000, 100000);
	c09  = comps_array[2] = create_comp ("C",  90000, 120000);
	c11  = comps_array[3] = create_comp ("D", 110000, 140000);
	c13  = comps_array[4] = create_comp ("E", 130000, 150000);
	c15a = comps_array[5] = create_comp ("F", 150000, 170000);
	c15b = comps_array[6] = create_comp ("G", 150000, 170000);

	#undef create_comp

	bag = create_bag (&ck_data);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[ii];
		const gchar *uid;

		uid = e_cal_component_get_uid (comp);
		check_data_set_expected_counts (&ck_data, 1, 1);
		check_data_set_expected_added (&ck_data, uid);

		/* out of bounds when it's empty */
		const_items = e_cal_component_bag_get_span (bag, 1234);
		g_assert_null (const_items);
		items = e_cal_component_bag_dup_span (bag, 1234);
		g_assert_null (items);

		e_cal_component_bag_add (bag, client, comp);
		items = e_cal_component_bag_list (bag);

		g_assert_nonnull (items);
		g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 1);
		g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
		g_assert_cmpint (items->len, ==, 1);
		g_assert_true (((ECalComponentBagItem *) g_ptr_array_index (items, 0))->comp == comp);

		g_clear_pointer (&items, g_ptr_array_unref);

		e_cal_component_bag_remove (bag, client, "non-existent", NULL);
		items = e_cal_component_bag_list (bag);

		g_assert_nonnull (items);
		g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 1);
		g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
		g_assert_cmpint (items->len, ==, 1);
		g_assert_true (((ECalComponentBagItem *) g_ptr_array_index (items, 0))->comp == comp);

		g_clear_pointer (&items, g_ptr_array_unref);

		/* adding the same component twice does nothing */
		e_cal_component_bag_add (bag, client, comp);

		items = e_cal_component_bag_list (bag);

		g_assert_nonnull (items);
		g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 1);
		g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
		g_assert_cmpint (items->len, ==, 1);
		g_assert_true (((ECalComponentBagItem *) g_ptr_array_index (items, 0))->comp == comp);

		g_clear_pointer (&items, g_ptr_array_unref);

		/* out of bounds when something is in there */
		const_items = e_cal_component_bag_get_span (bag, 1234);
		g_assert_null (const_items);
		items = e_cal_component_bag_dup_span (bag, 1234);
		g_assert_null (items);

		g_clear_pointer (&items, g_ptr_array_unref);

		const_items = e_cal_component_bag_get_span (bag, 0);
		g_assert_nonnull (const_items);
		items = e_cal_component_bag_dup_span (bag, 0);
		g_assert_nonnull (items);
		g_assert_true (const_items != items);
		g_assert_cmpint (const_items->len, ==, 1);
		g_assert_cmpint (const_items->len, ==, items->len);
		g_assert_true (g_ptr_array_index (const_items, 0) != g_ptr_array_index (items, 0));
		g_assert_true (g_ptr_array_index (const_items, 0) != g_ptr_array_index (items, 0));
		g_assert_true (e_cal_component_bag_item_equal_by_comp (g_ptr_array_index (const_items, 0), g_ptr_array_index (items, 0)));

		g_clear_pointer (&items, g_ptr_array_unref);

		check_data_set_expected_counts (&ck_data, 0, 0);
		check_data_set_expected_removed (&ck_data, 1, uid);

		e_cal_component_bag_remove (bag, client, uid, NULL);

		items = e_cal_component_bag_list (bag);

		g_assert_nonnull (items);
		g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
		g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
		g_assert_cmpint (items->len, ==, 0);

		g_clear_pointer (&items, g_ptr_array_unref);

		g_assert_cmpint (ck_data.called_n_items, ==, 2);
		g_assert_cmpint (ck_data.called_n_spans, ==, 2);
		g_assert_cmpint (ck_data.called_n_added, ==, 1);
		g_assert_cmpint (ck_data.called_n_removed, ==, 1);
		g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
		g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

		check_data_clear (&ck_data);
	}

	/* not counting additions */
	ck_data.do_checks = FALSE;

	n_called = 0;
	e_cal_component_bag_foreach (bag, test_component_bag_foreach_count_called_cb, &n_called);
	g_assert_cmpint (n_called, ==, 0);

	e_cal_component_bag_lock (bag);

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[ii];
		e_cal_component_bag_add (bag, client, comp);
	}

	n_called = 0;
	e_cal_component_bag_foreach (bag, test_component_bag_foreach_count_called_cb, &n_called);
	g_assert_cmpint (n_called, ==, 7);

	e_cal_component_bag_foreach (bag, test_component_bag_foreach_check_expected_cb, expected_ids);
	g_assert_cmpint (expected_ids[0], ==, 'x');
	g_assert_cmpint (expected_ids[1], ==, 'x');
	g_assert_cmpint (expected_ids[2], ==, 'x');
	g_assert_cmpint (expected_ids[3], ==, 'x');
	g_assert_cmpint (expected_ids[4], ==, 'x');
	g_assert_cmpint (expected_ids[5], ==, 'x');
	g_assert_cmpint (expected_ids[6], ==, 'x');

	n_called = 3 << 6;
	e_cal_component_bag_foreach (bag, test_component_bag_foreach_count_and_stop_early_cb, &n_called);
	g_assert_cmpint (n_called, ==, 3);

	n_called = 0;
	e_cal_component_bag_foreach (bag, test_component_bag_foreach_set_user_data_cb, &n_called);

	items = e_cal_component_bag_list (bag);
	g_assert_cmpint (items->len, ==, 7);
	for (ii = 0; ii < items->len; ii++) {
		item = g_ptr_array_index (items, ii);
		g_assert_cmpint (GPOINTER_TO_UINT (item->user_data), ==, ii + 1);
	}
	g_ptr_array_unref (items);

	e_cal_component_bag_unlock (bag);

	e_cal_component_bag_lock (bag);
	e_cal_component_bag_lock (bag);
	e_cal_component_bag_unlock (bag);
	e_cal_component_bag_unlock (bag);

	n_items = ~0;
	n_spans = ~0;

	g_object_get (bag,
		"n-items", &n_items,
		"n-spans", &n_spans,
		NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, n_items);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, n_spans);
	g_assert_cmpint (ck_data.called_n_items, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_spans, ==, 3);
	g_assert_cmpint (ck_data.called_n_added, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	for (ii = 0; ii < 3; ii++) {
		const_items = e_cal_component_bag_get_span (bag, ii);
		items = e_cal_component_bag_dup_span (bag, ii);

		g_assert_true (const_items != items);
		g_assert_cmpint (const_items->len, ==, items->len);
		g_assert_true (const_items->len == 1 || const_items->len == 3);

		g_clear_pointer (&items, g_ptr_array_unref);
	}

	e_cal_component_bag_clear (bag);

	n_items = ~0;
	n_spans = ~0;

	g_object_get (bag,
		"n-items", &n_items,
		"n-spans", &n_spans,
		NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, n_items);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, n_spans);
	g_assert_cmpint (ck_data.called_n_items, ==, G_N_ELEMENTS (comps_array) + 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 4);
	g_assert_cmpint (ck_data.called_n_added, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	ck_data.do_checks = FALSE;

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[ii];
		e_cal_component_bag_add (bag, client, comp);
	}

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[ii];
		e_cal_component_bag_remove (bag, client, e_cal_component_get_uid (comp), NULL);
	}

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (ck_data.called_n_items, ==, 2 * G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_spans, ==, 6);
	g_assert_cmpint (ck_data.called_n_added, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_removed, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 2);

	check_data_clear (&ck_data);
	ck_data.do_checks = FALSE;

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[G_N_ELEMENTS (comps_array) - ii - 1];
		e_cal_component_bag_add (bag, client, comp);
	}

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[ii];
		e_cal_component_bag_remove (bag, client, e_cal_component_get_uid (comp), NULL);
	}

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (ck_data.called_n_items, ==, 2 * G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_spans, ==, 6);
	g_assert_cmpint (ck_data.called_n_added, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_removed, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	ck_data.do_checks = FALSE;

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[G_N_ELEMENTS (comps_array) - ii - 1];
		e_cal_component_bag_add (bag, client, comp);
	}

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[G_N_ELEMENTS (comps_array) - ii - 1];
		e_cal_component_bag_remove (bag, client, e_cal_component_get_uid (comp), NULL);
	}

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (ck_data.called_n_items, ==, 2 * G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_spans, ==, 6);
	g_assert_cmpint (ck_data.called_n_added, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_removed, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 2);

	check_data_clear (&ck_data);
	ck_data.do_checks = FALSE;

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[ii];
		e_cal_component_bag_add (bag, client, comp);
	}

	for (ii = 0; ii < G_N_ELEMENTS (comps_array); ii++) {
		ECalComponent *comp = comps_array[G_N_ELEMENTS (comps_array) - ii - 1];
		e_cal_component_bag_remove (bag, client, e_cal_component_get_uid (comp), NULL);
	}

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (ck_data.called_n_items, ==, 2 * G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_spans, ==, 6);
	g_assert_cmpint (ck_data.called_n_added, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_removed, ==, G_N_ELEMENTS (comps_array));
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	/* changing item data in the signal handler should preserve changes */
	check_data_clear (&ck_data);
	ck_data.do_checks = FALSE;

	g_signal_connect (bag, "added",
		G_CALLBACK (test_component_bag_modify_on_added_cb), GINT_TO_POINTER (321));

	e_cal_component_bag_add (bag, client, c07);

	item = e_cal_component_bag_get_item (bag, client, "A", NULL);
	g_assert_nonnull (item);
	g_assert_cmpint (GPOINTER_TO_INT (item->user_data), ==, 321);

	g_signal_handlers_disconnect_by_func (bag, G_CALLBACK (test_component_bag_modify_on_added_cb), GINT_TO_POINTER (321));

	icomp = e_cal_component_get_icalcomponent (c07);
	g_assert_cmpstr (i_cal_component_get_summary (icomp), ==, "A");

	/* changing the component should preserve the set user data... */
	i_cal_component_set_summary (icomp, "changed");
	e_cal_component_bag_add (bag, client, c07);

	item = e_cal_component_bag_get_item (bag, client, "A", NULL);
	g_assert_nonnull (item);
	g_assert_cmpint (GPOINTER_TO_INT (item->user_data), ==, 321);
	icomp = e_cal_component_get_icalcomponent (item->comp);
	g_assert_cmpstr (i_cal_component_get_summary (icomp), ==, "changed");

	/* ...unless it's explicitly changed */
	icomp = e_cal_component_get_icalcomponent (c07);
	i_cal_component_set_summary (icomp, "a bit");
	e_cal_component_bag_add_with_user_data (bag, client, c07, GINT_TO_POINTER (789), NULL, NULL);

	item = e_cal_component_bag_get_item (bag, client, "A", NULL);
	g_assert_nonnull (item);
	g_assert_cmpint (GPOINTER_TO_INT (item->user_data), ==, 789);
	icomp = e_cal_component_get_icalcomponent (item->comp);
	g_assert_cmpstr (i_cal_component_get_summary (icomp), ==, "a bit");

	g_clear_object (&bag);

	g_clear_object (&c07);
	g_clear_object (&c08);
	g_clear_object (&c09);
	g_clear_object (&c11);
	g_clear_object (&c13);
	g_clear_object (&c15a);
	g_clear_object (&c15b);
	g_clear_object (&client);
}

static void
test_component_bag_recurrence_id (void)
{
	CheckData ck_data;
	ECalClient *client, *client2;
	ECalComponentBag *bag;
	ECalComponent *c1, *c2, *c3;
	const ECalComponentBagItem *item;
	const GPtrArray *span;

	client = create_client ("cl1");
	bag = create_bag (&ck_data);

	#define create_comp_recr(_id, _start_time, _end_time) \
		create_component (E_CAL_COMPONENT_EVENT, _id, "20250327T" # _start_time "Z", _id, NULL, \
			20250327, _start_time, 20250327, _end_time, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL)

	/* split into multiple spans */
	c1 = create_comp_recr ("A", 120000, 130000);
	c2 = create_comp_recr ("A", 122500, 132500);
	c3 = create_comp_recr ("B", 125000, 135000);

	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c1);

	check_data_set_expected_counts (&ck_data, 2, 2);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	check_data_set_expected_counts (&ck_data, 3, 3);
	check_data_set_expected_added (&ck_data, "B");
	e_cal_component_bag_add (bag, client, c3);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 3);
	g_assert_cmpint (ck_data.called_n_spans, ==, 3);
	g_assert_cmpint (ck_data.called_n_added, ==, 3);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	e_cal_component_bag_remove (bag, client, "unknown-id", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 3);
	g_assert_cmpint (ck_data.called_n_spans, ==, 3);
	g_assert_cmpint (ck_data.called_n_added, ==, 3);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 2);
	check_data_set_expected_removed (&ck_data, 1, "A");
	check_data_set_expected_span_changed (&ck_data, 1, "B");

	e_cal_component_bag_remove (bag, client, "A", "20250327T122500Z");

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 2);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 2);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 3, 3);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);

	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_removed (&ck_data, 2, "A");
	check_data_set_expected_span_changed (&ck_data, 1, "B");

	e_cal_component_bag_remove (bag, client, "A", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 1);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 2);
	g_assert_cmpint (ck_data.called_n_spans, ==, 2);
	g_assert_cmpint (ck_data.called_n_added, ==, 1);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 2);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c1);

	check_data_set_expected_counts (&ck_data, 3, 3);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 2);
	check_data_set_expected_removed (&ck_data, 1, "B");
	check_data_set_expected_span_changed (&ck_data, 2, "A");

	e_cal_component_bag_remove (bag, client, "B", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 2);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 2);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 0, 0);

	e_cal_component_bag_clear (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	g_clear_object (&c1);
	g_clear_object (&c2);
	g_clear_object (&c3);

	/* in a single span */
	c1 = create_comp_recr ("A", 120000, 121500);
	c2 = create_comp_recr ("A", 122000, 123500);
	c3 = create_comp_recr ("B", 124000, 125500);

	#undef create_comp

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c1);

	check_data_set_expected_counts (&ck_data, 2, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	check_data_set_expected_counts (&ck_data, 3, 1);
	check_data_set_expected_added (&ck_data, "B");
	e_cal_component_bag_add (bag, client, c3);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 3);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 3);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	e_cal_component_bag_remove (bag, client, "unknown-id", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 3);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 3);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 1);
	check_data_set_expected_removed (&ck_data, 1, "A");

	e_cal_component_bag_remove (bag, client, "A", "20250327T122000Z");

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 2);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 3, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);

	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_removed (&ck_data, 2, "A");

	e_cal_component_bag_remove (bag, client, "A", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 1);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 2);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 1);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c1);

	check_data_set_expected_counts (&ck_data, 3, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 1);
	check_data_set_expected_removed (&ck_data, 1, "B");

	e_cal_component_bag_remove (bag, client, "B", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 2);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 0, 0);

	e_cal_component_bag_clear (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 0);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 0);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	/* use a different client for one of the 'A' instances */
	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c1);

	check_data_set_expected_counts (&ck_data, 2, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c2);

	check_data_set_expected_counts (&ck_data, 3, 1);
	check_data_set_expected_added (&ck_data, "B");
	e_cal_component_bag_add (bag, client, c3);

	client2 = create_client ("cl2");

	check_data_set_expected_counts (&ck_data, 4, 2);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client2, c2);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 4);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 2);
	g_assert_cmpint (ck_data.called_n_items, ==, 4);
	g_assert_cmpint (ck_data.called_n_spans, ==, 2);
	g_assert_cmpint (ck_data.called_n_added, ==, 4);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 3, 2);
	check_data_set_expected_removed (&ck_data, 1, "A");

	e_cal_component_bag_remove (bag, client, "A", "20250327T122000Z");

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 2);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 2);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (e_source_get_uid (e_client_get_source (E_CLIENT (item->client))), ==, "cl1");
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpstr (item->rid, ==, "20250327T120000Z");
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (e_source_get_uid (e_client_get_source (E_CLIENT (item->client))), ==, "cl1");
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpstr (item->rid, ==, "20250327T124000Z");
	span = e_cal_component_bag_get_span (bag, 1);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (e_source_get_uid (e_client_get_source (E_CLIENT (item->client))), ==, "cl2");
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpstr (item->rid, ==, "20250327T122000Z");

	/* remove the leftover 'A' from the 'cl1' */
	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 2, 2);
	check_data_set_expected_removed (&ck_data, 1, "A");

	e_cal_component_bag_remove (bag, client, "A", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 2);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 2);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (e_source_get_uid (e_client_get_source (E_CLIENT (item->client))), ==, "cl1");
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpstr (item->rid, ==, "20250327T124000Z");
	span = e_cal_component_bag_get_span (bag, 1);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (e_source_get_uid (e_client_get_source (E_CLIENT (item->client))), ==, "cl2");
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpstr (item->rid, ==, "20250327T122000Z");

	g_clear_object (&bag);
	g_clear_object (&client);
	g_clear_object (&client2);
	g_clear_object (&c1);
	g_clear_object (&c2);
	g_clear_object (&c3);
}

static void
test_component_bag_change_time (ECalComponent *comp,
				gboolean start,
				gint hour,
				gint minute,
				gint second)
{
	ICalComponent *icomp;
	ICalProperty *prop;
	ICalTime *itt = NULL;

	icomp = e_cal_component_get_icalcomponent (comp);
	prop = i_cal_component_get_first_property (icomp, start ? I_CAL_DTSTART_PROPERTY : I_CAL_DTEND_PROPERTY);
	g_assert_nonnull (prop);

	itt = start ? i_cal_component_get_dtstart (icomp) : i_cal_component_get_dtend (icomp);
	g_assert_nonnull (itt);

	i_cal_time_set_time (itt, hour, minute, second);

	if (start)
		i_cal_property_set_dtstart (prop, itt);
	else
		i_cal_property_set_dtend (prop, itt);

	g_clear_object (&itt);
	g_clear_object (&prop);
}

static void
test_component_bag_changes (void)
{
	CheckData ck_data;
	ECalClient *client;
	ECalComponent *c07, *c08, *c09, *c11, *c13, *c15a, *c15b;
	ECalComponentBag *bag;
	ECalComponentBagItem *item;
	const ECalComponentBagItem *const_item;

	client = create_client ("cl1");
	bag = create_bag (&ck_data);

	#define create_comp(_id, _start_time, _end_time) \
		create_component (E_CAL_COMPONENT_EVENT, _id, NULL, _id, NULL, \
			20250327, _start_time, 20250327, _end_time, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL)

	/*
	 06 07 08 09 10 11 12 13 14 15 16 17 18
	     AAAAAAAAAAAAAAAAAAAAAAAAAAA
	        BBBBBB   DDDDDDDDD   FFFFFF
	           CCCCCCCCC   EEEEEEgggggg
	 */

	c07  = create_comp ("A",  70000, 160000);
	c08  = create_comp ("B",  80000, 100000);
	c09  = create_comp ("C",  90000, 120000);
	c11  = create_comp ("D", 110000, 140000);
	c13  = create_comp ("E", 130000, 150000);
	c15a = create_comp ("F", 150000, 170000);
	c15b = create_comp ("G", 150000, 170000);

	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c07);

	check_data_set_expected_counts (&ck_data, 2, 2);
	check_data_set_expected_added (&ck_data, "B");
	e_cal_component_bag_add (bag, client, c08);

	check_data_set_expected_counts (&ck_data, 3, 3);
	check_data_set_expected_added (&ck_data, "C");
	e_cal_component_bag_add (bag, client, c09);

	check_data_set_expected_counts (&ck_data, 4, 3);
	check_data_set_expected_added (&ck_data, "D");
	e_cal_component_bag_add (bag, client, c11);

	check_data_set_expected_counts (&ck_data, 5, 3);
	check_data_set_expected_added (&ck_data, "E");
	e_cal_component_bag_add (bag, client, c13);

	check_data_set_expected_counts (&ck_data, 6, 3);
	check_data_set_expected_added (&ck_data, "F");
	e_cal_component_bag_add (bag, client, c15a);

	check_data_set_expected_counts (&ck_data, 7, 3);
	check_data_set_expected_added (&ck_data, "G");
	e_cal_component_bag_add (bag, client, c15b);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 7);
	g_assert_cmpint (ck_data.called_n_spans, ==, 3);
	g_assert_cmpint (ck_data.called_n_added, ==, 7);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	/* adding without changing anything does nothing */
	e_cal_component_bag_add (bag, client, c15b);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 7);
	g_assert_cmpint (ck_data.called_n_spans, ==, 3);
	g_assert_cmpint (ck_data.called_n_added, ==, 7);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	check_data_clear (&ck_data);
	check_data_set_expected_item_changed (&ck_data, "G");

	/* new instance, with the same times */
	g_clear_object (&c15b);
	c15b = create_comp ("G", 150000, 170000);
	e_cal_component_bag_add (bag, client, c15b);

	#undef create_comp

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 1);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	/* change client instance */
	g_clear_object (&client);
	client = create_client ("cl1");

	test_bag_free_user_data_counter = 0;
	check_data_set_expected_item_changed (&ck_data, "G");
	e_cal_component_bag_add_with_user_data (bag, client, c15b, GINT_TO_POINTER (77), test_bag_copy_user_data, test_bag_free_user_data);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 2);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 0);

	e_cal_component_bag_lock (bag);

	test_bag_free_user_data_counter = 0;
	const_item = e_cal_component_bag_get_item (bag, client, "G", NULL);
	item = e_cal_component_bag_dup_item (bag, client, "G", NULL);
	g_assert_nonnull (const_item);
	g_assert_nonnull (item);
	g_assert_true (const_item != item);
	g_assert_cmpstr (const_item->uid, ==, "G");
	g_assert_cmpstr (const_item->rid, ==, NULL);
	g_assert_cmpint (const_item->duration_minutes, ==, 120);
	g_assert_cmpint (GPOINTER_TO_INT (const_item->user_data), ==, 77);
	g_assert_true (const_item->copy_user_data == test_bag_copy_user_data);
	g_assert_true (const_item->free_user_data == test_bag_free_user_data);
	g_assert_cmpint (item->duration_minutes, ==, 120);
	g_assert_cmpstr (item->uid, ==, "G");
	g_assert_cmpstr (item->rid, ==, NULL);
	g_assert_cmpint (GPOINTER_TO_INT (item->user_data), ==, 78);
	g_assert_true (item->copy_user_data == test_bag_copy_user_data);
	g_assert_true (item->free_user_data == test_bag_free_user_data);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 0);

	/* same value, will call free once */
	e_cal_component_bag_item_set_user_data (item, GINT_TO_POINTER (78), test_bag_copy_user_data, test_bag_free_user_data);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 1);

	g_clear_pointer (&item, e_cal_component_bag_item_free);
	g_assert_cmpint (test_bag_free_user_data_counter, ==, 2);

	e_cal_component_bag_unlock (bag);

	/* later start, still the same span */
	check_data_clear (&ck_data);
	test_component_bag_change_time (c15b, TRUE, 15, 10, 00);
	check_data_set_expected_item_changed (&ck_data, "G");
	e_cal_component_bag_add (bag, client, c15b);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 1);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	const_item = e_cal_component_bag_get_item (bag, client, "G", NULL);
	item = e_cal_component_bag_dup_item (bag, client, "G", NULL);
	g_assert_nonnull (const_item);
	g_assert_nonnull (item);
	g_assert_true (const_item != item);
	g_assert_cmpstr (const_item->uid, ==, "G");
	g_assert_cmpstr (const_item->rid, ==, NULL);
	g_assert_cmpint (const_item->duration_minutes, ==, 110);
	g_assert_cmpint (item->duration_minutes, ==, 110);
	g_assert_cmpstr (item->uid, ==, "G");
	g_assert_cmpstr (item->rid, ==, NULL);

	g_clear_pointer (&item, e_cal_component_bag_item_free);

	/* sooner end, still the same span */
	test_component_bag_change_time (c15b, FALSE, 16, 30, 00);
	check_data_set_expected_item_changed (&ck_data, "G");
	e_cal_component_bag_add (bag, client, c15b);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 2);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	const_item = e_cal_component_bag_get_item (bag, client, "G", NULL);
	item = e_cal_component_bag_dup_item (bag, client, "G", NULL);
	g_assert_nonnull (const_item);
	g_assert_nonnull (item);
	g_assert_true (const_item != item);
	g_assert_cmpstr (const_item->uid, ==, "G");
	g_assert_cmpstr (const_item->rid, ==, NULL);
	g_assert_cmpint (const_item->duration_minutes, ==, 80);
	g_assert_cmpint (item->duration_minutes, ==, 80);
	g_assert_cmpstr (item->uid, ==, "G");
	g_assert_cmpstr (item->rid, ==, NULL);

	g_clear_pointer (&item, e_cal_component_bag_item_free);

	/* clash with E */
	check_data_clear (&ck_data);
	test_component_bag_change_time (c15b, TRUE, 14, 30, 00);
	test_component_bag_change_time (c15b, FALSE, 15, 25, 00);
	check_data_set_expected_counts (&ck_data, 0, 4);
	check_data_set_expected_item_changed (&ck_data, "G");
	check_data_set_expected_span_changed (&ck_data, 1, "G");
	e_cal_component_bag_add (bag, client, c15b);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 4);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 1);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	/* "un-clash" with E, though it won't merge it back to the span 2 */
	check_data_clear (&ck_data);
	test_component_bag_change_time (c15b, TRUE, 16, 55, 00);
	test_component_bag_change_time (c15b, FALSE, 17, 10, 00);
	check_data_set_expected_item_changed (&ck_data, "G");
	e_cal_component_bag_add (bag, client, c15b);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 7);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 4);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 1);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	/* remove the first component => all have changed the span */
	check_data_clear (&ck_data);
	check_data_set_expected_counts (&ck_data, 6, 3);
	check_data_set_expected_removed (&ck_data, 1, "A");
	check_data_set_expected_span_changed (&ck_data, 6, "B");
	e_cal_component_bag_remove (bag, client, "A", NULL);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 6);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 1);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 1);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	g_clear_object (&bag);

	g_clear_object (&c07);
	g_clear_object (&c08);
	g_clear_object (&c09);
	g_clear_object (&c11);
	g_clear_object (&c13);
	g_clear_object (&c15a);
	g_clear_object (&c15b);
	g_clear_object (&client);
}

static void
test_component_bag_rebuild (void)
{
	CheckData ck_data;
	ECalClient *client;
	ECalComponent *c1, *c2, *c3;
	ECalComponentBag *bag;
	ECalComponentBagItem *item;
	const GPtrArray *span;

	client = create_client ("cl1");
	bag = create_bag (&ck_data);

	#define create_comp(_id, _start_time, _end_time) \
		create_component (E_CAL_COMPONENT_EVENT, _id, NULL, _id, NULL, \
			20250327, _start_time, 20250327, _end_time, SKIP_TIME_VAL, SKIP_TIME_VAL, SKIP_TIME_VAL)

	c1  = create_comp ("A",  100000, 101500);
	c2  = create_comp ("B",  102000, 104500);
	c3  = create_comp ("C",  114900, 115900);

	check_data_set_expected_counts (&ck_data, 1, 1);
	check_data_set_expected_added (&ck_data, "A");
	e_cal_component_bag_add (bag, client, c1);

	check_data_set_expected_counts (&ck_data, 2, 1);
	check_data_set_expected_added (&ck_data, "C");
	e_cal_component_bag_add (bag, client, c3);

	check_data_set_expected_counts (&ck_data, 3, 1);
	check_data_set_expected_added (&ck_data, "B");
	e_cal_component_bag_add (bag, client, c2);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 3);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 3);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 3);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->duration_minutes, ==, 15);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->duration_minutes, ==, 25);
	item = g_ptr_array_index (span, 2);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->duration_minutes, ==, 10);

	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 3);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 3);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 3);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->duration_minutes, ==, 15);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->duration_minutes, ==, 25);
	item = g_ptr_array_index (span, 2);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->duration_minutes, ==, 10);

	check_data_clear (&ck_data);
	g_assert_cmpint (e_cal_component_bag_get_min_duration_minutes (bag), ==, 0);
	e_cal_component_bag_set_min_duration_minutes (bag, 10);
	g_assert_cmpint (e_cal_component_bag_get_min_duration_minutes (bag), ==, 10);
	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 3);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->duration_minutes, ==, 15);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->duration_minutes, ==, 25);
	item = g_ptr_array_index (span, 2);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->duration_minutes, ==, 10);

	/* the A and the C needs to expand, but only B will move to a new span */
	check_data_clear (&ck_data);
	e_cal_component_bag_set_min_duration_minutes (bag, 21);
	g_assert_cmpint (e_cal_component_bag_get_min_duration_minutes (bag), ==, 21);
	check_data_set_expected_counts (&ck_data, 0, 2);
	check_data_set_expected_span_changed (&ck_data, 1, "B");
	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 2);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 2);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->duration_minutes, ==, 21);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->duration_minutes, ==, 21);

	span = e_cal_component_bag_get_span (bag, 1);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->duration_minutes, ==, 25);

	/* back to a single span */
	check_data_clear (&ck_data);
	e_cal_component_bag_set_min_duration_minutes (bag, 0);
	g_assert_cmpint (e_cal_component_bag_get_min_duration_minutes (bag), ==, 0);
	check_data_set_expected_counts (&ck_data, 0, 1);
	check_data_set_expected_span_changed (&ck_data, 1, "B");
	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 3);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->duration_minutes, ==, 15);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->duration_minutes, ==, 25);
	item = g_ptr_array_index (span, 2);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->duration_minutes, ==, 10);

	/* let each take its own span */
	check_data_clear (&ck_data);
	e_cal_component_bag_set_min_duration_minutes (bag, 999);
	g_assert_cmpint (e_cal_component_bag_get_min_duration_minutes (bag), ==, 999);
	check_data_set_expected_counts (&ck_data, 0, 3);
	check_data_set_expected_span_changed (&ck_data, 2, "B");
	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 3);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->duration_minutes, ==, 999);

	span = e_cal_component_bag_get_span (bag, 1);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->duration_minutes, ==, 999);

	span = e_cal_component_bag_get_span (bag, 2);
	g_assert_cmpint (span->len, ==, 1);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->duration_minutes, ==, 999);

	/* back to a single span again */
	check_data_clear (&ck_data);
	e_cal_component_bag_set_min_duration_minutes (bag, 0);
	g_assert_cmpint (e_cal_component_bag_get_min_duration_minutes (bag), ==, 0);
	check_data_set_expected_counts (&ck_data, 0, 1);
	check_data_set_expected_span_changed (&ck_data, 2, "B");
	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 1);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 1);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 3);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->start, ==, 1743069600);
	g_assert_cmpint (item->duration_minutes, ==, 15);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->start, ==, 1743070800);
	g_assert_cmpint (item->duration_minutes, ==, 25);
	item = g_ptr_array_index (span, 2);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->start, ==, 1743076140);
	g_assert_cmpint (item->duration_minutes, ==, 10);

	e_cal_component_bag_set_timezone (bag, i_cal_timezone_get_builtin_timezone ("America/New_York"));

	check_data_clear (&ck_data);
	e_cal_component_bag_rebuild (bag);

	g_assert_cmpint (e_cal_component_bag_get_n_items (bag), ==, 3);
	g_assert_cmpint (e_cal_component_bag_get_n_spans (bag), ==, 1);
	g_assert_cmpint (ck_data.called_n_items, ==, 0);
	g_assert_cmpint (ck_data.called_n_spans, ==, 0);
	g_assert_cmpint (ck_data.called_n_added, ==, 0);
	g_assert_cmpint (ck_data.called_n_removed, ==, 0);
	g_assert_cmpint (ck_data.called_n_item_changed, ==, 0);
	g_assert_cmpint (ck_data.called_n_span_changed, ==, 0);

	span = e_cal_component_bag_get_span (bag, 0);
	g_assert_cmpint (span->len, ==, 3);
	item = g_ptr_array_index (span, 0);
	g_assert_cmpstr (item->uid, ==, "A");
	g_assert_cmpint (item->start, ==, 1743084000);
	g_assert_cmpint (item->duration_minutes, ==, 15);
	item = g_ptr_array_index (span, 1);
	g_assert_cmpstr (item->uid, ==, "B");
	g_assert_cmpint (item->start, ==, 1743085200);
	g_assert_cmpint (item->duration_minutes, ==, 25);
	item = g_ptr_array_index (span, 2);
	g_assert_cmpstr (item->uid, ==, "C");
	g_assert_cmpint (item->start, ==, 1743090540);
	g_assert_cmpint (item->duration_minutes, ==, 10);

	g_clear_object (&bag);

	g_clear_object (&c1);
	g_clear_object (&c2);
	g_clear_object (&c3);
	g_clear_object (&client);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/ECalComponentBagItem/Basic", test_component_bag_item_basic);
	g_test_add_func ("/ECalComponentBagItem/Read", test_component_bag_item_read);
	g_test_add_func ("/ECalComponentBagItem/UserData", test_component_bag_item_user_data);
	g_test_add_func ("/ECalComponentBag/Create", test_component_bag_create);
	g_test_add_func ("/ECalComponentBag/Basic", test_component_bag_basic);
	g_test_add_func ("/ECalComponentBag/RecurrenceId", test_component_bag_recurrence_id);
	g_test_add_func ("/ECalComponentBag/Changes", test_component_bag_changes);
	g_test_add_func ("/ECalComponentBag/Rebuild", test_component_bag_rebuild);

	return e_test_server_utils_run (argc, argv);
}
