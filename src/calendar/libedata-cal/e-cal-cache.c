/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2016 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION: e-cal-cache
 * @include: libedata-cal/libedata-cal.h
 * @short_description: An #ECache descendant for calendars
 *
 * The #ECalCache is an API for storing and looking up calendar
 * components in an #ECache.
 *
 * The API is thread safe, in the similar way as the #ECache is.
 *
 * Any operations which can take a lot of time to complete (depending
 * on the size of your calendar) can be cancelled using a #GCancellable.
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include <libebackend/libebackend.h>
#include <libecal/libecal.h>

#include "e-cal-cache.h"

#define E_CAL_CACHE_VERSION		1

#define ECC_TABLE_TIMEZONES		"timezones"

#define ECC_COLUMN_OCCUR_START		"occur_start"
#define ECC_COLUMN_OCCUR_END		"occur_end"
#define ECC_COLUMN_DUE			"due"
#define ECC_COLUMN_COMPLETED		"completed"
#define ECC_COLUMN_SUMMARY		"summary"
#define ECC_COLUMN_COMMENT		"comment"
#define ECC_COLUMN_DESCRIPTION		"description"
#define ECC_COLUMN_LOCATION		"location"
#define ECC_COLUMN_ATTENDEES		"attendees"
#define ECC_COLUMN_ORGANIZER		"organizer"
#define ECC_COLUMN_CLASSIFICATION	"classification"
#define ECC_COLUMN_STATUS		"status"
#define ECC_COLUMN_PRIORITY		"priority"
#define ECC_COLUMN_CATEGORIES		"categories"
#define ECC_COLUMN_HAS_ALARM		"has_alarm"
#define ECC_COLUMN_HAS_START		"has_start"
#define ECC_COLUMN_HAS_RECURRENCES	"has_recurrences"
#define ECC_COLUMN_EXTRA		"bdata"

struct _ECalCachePrivate {
	GHashTable *loaded_timezones; /* gchar *tzid ~> icaltimezone * */
	GMutex loaded_timezones_lock;
};

enum {
	DUP_COMPONENT_REVISION,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (ECalCache, e_cal_cache, E_TYPE_CACHE,
			 G_IMPLEMENT_INTERFACE (E_TYPE_EXTENSIBLE, NULL))

G_DEFINE_BOXED_TYPE (ECalCacheSearchData, e_cal_cache_search_data, e_cal_cache_search_data_copy, e_cal_cache_search_data_free)

/**
 * e_cal_cache_search_data_new:
 * @uid: a component UID; cannot be %NULL
 * @rid: (nullable): a component Recurrence-ID; can be %NULL
 * @object: the component as an iCal string; cannot be %NULL
 * @extra: (nullable): any extra data stored with the component, or %NULL
 *
 * Creates a new ECalCacheSearchData prefilled with the given values.
 *
 * Returns: (transfer full): A new #ECalCacheSearchData. Free it with
 *    e_cal_cache_search_data_free() when no longer needed.
 *
 * Since: 3.26
 **/
ECalCacheSearchData *
e_cal_cache_search_data_new (const gchar *uid,
			     const gchar *rid,
			     const gchar *object,
			     const gchar *extra)
{
	ECalCacheSearchData *data;

	g_return_val_if_fail (uid != NULL, NULL);
	g_return_val_if_fail (object != NULL, NULL);

	data = g_new0 (ECalCacheSearchData, 1);
	data->uid = g_strdup (uid);
	data->rid = (rid && *rid) ? g_strdup (rid) : NULL;
	data->object = g_strdup (object);
	data->extra = g_strdup (extra);

	return data;
}

/**
 * e_cal_cache_search_data_copy:
 * @data: (nullable): a source #ECalCacheSearchData to copy, or %NULL
 *
 * Returns: (transfer full): Copy of the given @data. Free it with
 *    e_cal_cache_search_data_free() when no longer needed.
 *    If the @data is %NULL, then returns %NULL as well.
 *
 * Since: 3.26
 **/
ECalCacheSearchData *
e_cal_cache_search_data_copy (const ECalCacheSearchData *data)
{
	if (!data)
		return NULL;

	return e_cal_cache_search_data_new (data->uid, data->rid, data->object, data->extra);
}

/**
 * e_cal_cache_search_data_free:
 * @data: (nullable): an #ECalCacheSearchData
 *
 * Frees the @data structure, previously allocated with e_cal_cache_search_data_new()
 * or e_cal_cache_search_data_copy().
 *
 * Since: 3.26
 **/
void
e_cal_cache_search_data_free (gpointer ptr)
{
	ECalCacheSearchData *data = ptr;

	if (data) {
		g_free (data->uid);
		g_free (data->rid);
		g_free (data->object);
		g_free (data->extra);
		g_free (data);
	}
}

static gboolean
e_cal_cache_get_string (ECache *cache,
			gint ncols,
			const gchar **column_names,
			const gchar **column_values,
			gpointer user_data)
{
	gchar **pvalue = user_data;

	g_return_val_if_fail (ncols == 1, FALSE);
	g_return_val_if_fail (column_names != NULL, FALSE);
	g_return_val_if_fail (column_values != NULL, FALSE);
	g_return_val_if_fail (pvalue != NULL, FALSE);

	if (!*pvalue)
		*pvalue = g_strdup (column_values[0]);

	return TRUE;
}

static gboolean
e_cal_cache_get_strings (ECache *cache,
			 gint ncols,
			 const gchar **column_names,
			 const gchar **column_values,
			 gpointer user_data)
{
	GSList **pstrings = user_data;

	g_return_val_if_fail (ncols == 1, FALSE);
	g_return_val_if_fail (column_names != NULL, FALSE);
	g_return_val_if_fail (column_values != NULL, FALSE);
	g_return_val_if_fail (pstrings != NULL, FALSE);

	*pstrings = g_slist_prepend (*pstrings, g_strdup (column_values[0]));

	return TRUE;
}

static void
e_cal_cache_populate_other_columns (ECalCache *cal_cache,
				    GSList **out_other_columns)
{
	g_return_if_fail (out_other_columns != NULL);

	*out_other_columns = NULL;

	#define add_column(name, type, idx_name) \
		*out_other_columns = g_slist_prepend (*out_other_columns, \
			e_cache_column_info_new (name, type, idx_name))

	add_column (ECC_COLUMN_OCCUR_START, "TEXT", "IDX_OCCURSTART");
	add_column (ECC_COLUMN_OCCUR_END, "TEXT", "IDX_OCCUREND");
	add_column (ECC_COLUMN_DUE, "TEXT", "IDX_DUE");
	add_column (ECC_COLUMN_COMPLETED, "TEXT", "IDX_COMPLETED");
	add_column (ECC_COLUMN_SUMMARY, "TEXT", "IDX_SUMMARY");
	add_column (ECC_COLUMN_COMMENT, "TEXT", NULL);
	add_column (ECC_COLUMN_DESCRIPTION, "TEXT", NULL);
	add_column (ECC_COLUMN_LOCATION, "TEXT", NULL);
	add_column (ECC_COLUMN_ATTENDEES, "TEXT", NULL);
	add_column (ECC_COLUMN_ORGANIZER, "TEXT", NULL);
	add_column (ECC_COLUMN_CLASSIFICATION, "TEXT", NULL);
	add_column (ECC_COLUMN_STATUS, "TEXT", NULL);
	add_column (ECC_COLUMN_PRIORITY, "INTEGER", NULL);
	add_column (ECC_COLUMN_CATEGORIES, "TEXT", NULL);
	add_column (ECC_COLUMN_HAS_ALARM, "INTEGER", NULL);
	add_column (ECC_COLUMN_HAS_START, "INTEGER", NULL);
	add_column (ECC_COLUMN_HAS_RECURRENCES, "INTEGER", NULL);
	add_column (ECC_COLUMN_EXTRA, "TEXT", NULL);

	#undef add_column

	*out_other_columns = g_slist_reverse (*out_other_columns);
}

static gchar *
ecc_encode_id_sql (const gchar *uid,
		   const gchar *rid)
{
	g_return_val_if_fail (uid != NULL, NULL);

	if (rid && *rid)
		return g_strdup_printf ("%s\n%s", uid, rid);

	return g_strdup (uid);
}

/*static gboolean
ecc_decode_id_sql (const gchar *id,
		   gchar **out_uid,
		   gchar **out_rid)
{
	gchar **split;

	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (out_uid != NULL, FALSE);
	g_return_val_if_fail (out_rid != NULL, FALSE);

	*out_uid = NULL;
	*out_rid = NULL;

	if (!*id)
		return FALSE;

	split = g_strsplit (id, "\n", 1);

	if (!split || !split[0] || !*split[0]) {
		g_strfreev (split);
		return FALSE;
	}

	*out_uid = split[0];

	if (split[1])
		*out_rid = split[1];

	/ * array elements are taken by the out arguments * /
	g_free (split);

	return TRUE;
}*/

static gchar *
ecc_encode_itt_to_sql (struct icaltimetype itt)
{
	return g_strdup_printf ("%04d%02d%02d%02d%02d%02d",
		itt.year, itt.month, itt.day,
		itt.hour, itt.minute, itt.second);
}

static gchar *
ecc_encode_time_to_sql (ECalCache *cal_cache,
			const ECalComponentDateTime *dt)
{
	struct icaltimetype itt;
	icaltimezone *zone = NULL;

	if (!dt || !dt->value)
		return NULL;

	itt = *dt->value;

	if (!e_cal_cache_get_timezone (cal_cache, dt->tzid, &zone, NULL, NULL))
		zone = NULL;

	icaltimezone_convert_time (&itt, zone, icaltimezone_get_utc_timezone ());

	return ecc_encode_itt_to_sql (itt);
}

static gchar *
ecc_encode_timet_to_sql (ECalCache *cal_cache,
			 time_t tt)
{
	struct icaltimetype itt;

	if (tt <= 0)
		return NULL;

	itt = icaltime_from_timet_with_zone (tt, FALSE, NULL);

	return ecc_encode_itt_to_sql (itt);
}

static icaltimezone *
ecc_resolve_tzid_cb (const gchar *tzid,
		     gpointer user_data)
{
	ECalCache *cal_cache = user_data;
	icaltimezone *zone = NULL;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), NULL);

	if (!e_cal_cache_get_timezone (cal_cache, tzid, &zone, NULL, NULL))
		return NULL;

	return zone;
}

static gchar *
ecc_extract_text_list (const GSList *list)
{
	const GSList *link;
	GString *value;

	if (!list)
		return NULL;

	value = g_string_new ("");

	for (link = list; link; link = g_slist_next (link)) {
		ECalComponentText *text = link->data;

		if (text && text->value) {
			gchar *str;

			str = e_util_utf8_decompose (text->value);
			if (str)
				g_string_append (value, str);
			g_free (str);
		}
	}

	return g_string_free (value, !value->len);
}

static gchar *
ecc_extract_comment (ECalComponent *comp)
{
	GSList *list = NULL;
	gchar *value;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	e_cal_component_get_comment_list (comp, &list);
	value = ecc_extract_text_list (list);
	e_cal_component_free_text_list (list);

	return value;
}

static gchar *
ecc_extract_description (ECalComponent *comp)
{
	GSList *list = NULL;
	gchar *value;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	e_cal_component_get_description_list (comp, &list);
	value = ecc_extract_text_list (list);
	e_cal_component_free_text_list (list);

	return value;
}

static void
ecc_encode_mail (GString *out_value,
		 const gchar *in_cn,
		 const gchar *in_val)
{
	gchar *cn = NULL, *val = NULL;

	g_return_if_fail (in_val != NULL);

	if (in_cn && *in_cn)
		cn = e_util_utf8_decompose (in_cn);

	if (in_val) {
		const gchar *str = in_val;

		if (g_ascii_strncasecmp (str, "mailto:", 7) == 0) {
			str += 7;
		}

		if (*str)
			val = e_util_utf8_decompose (str);
	}

	if ((cn && *cn) || (val && *val)) {
		if (out_value->len)
			g_string_append_c (out_value, '\n');
		if (cn && *cn)
			g_string_append (out_value, cn);
		if (val && *val) {
			if (cn && *cn)
				g_string_append_c (out_value, '\t');
			g_string_append (out_value, val);
		}
	}

	g_free (cn);
	g_free (val);
}

static gchar *
ecc_extract_attendees (ECalComponent *comp)
{
	GSList *attendees = NULL, *link;
	GString *value;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	e_cal_component_get_attendee_list (comp, &attendees);
	if (!attendees)
		return NULL;

	value = g_string_new ("");

	for (link = attendees; link; link = g_slist_next (link)) {
		ECalComponentAttendee *att = link->data;

		if (!att)
			continue;

		ecc_encode_mail (value, att->cn, att->value);
	}

	e_cal_component_free_attendee_list (attendees);

	if (value->len) {
		/* This way it is encoded as:
		   <\n> <common-name> <\t> <mail> <\n> <common-name> <\t> <mail> <\n> ... </n> */
		g_string_prepend (value, "\n");
		g_string_append (value, "\n");
	}

	return g_string_free (value, !value->len);
}

static gchar *
ecc_extract_organizer (ECalComponent *comp)
{
	ECalComponentOrganizer org;
	GString *value;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	e_cal_component_get_organizer (comp, &org);

	value = g_string_new ("");

	ecc_encode_mail (value, org.cn, org.value);

	return g_string_free (value, !value->len);
}

static gchar *
ecc_extract_categories (ECalComponent *comp)
{
	GSList *categories, *link;
	GString *value;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	e_cal_component_get_categories_list (comp, &categories);

	if (!categories)
		return NULL;

	value = g_string_new ("");

	for (link = categories; link; link = g_slist_next (link)) {
		const gchar *category = link->data;

		if (category && *category) {
			if (value->len)
				g_string_append_c (value, '\n');
			g_string_append (value, category);
		}
	}

	e_cal_component_free_categories_list (categories);

	if (value->len) {
		/* This way it is encoded as:
		   <\n> <category> <\n> <category> <\n> ... </n>
		   which allows to search for exact category with: LIKE "%\ncategory\n%"
		*/
		g_string_prepend (value, "\n");
		g_string_append (value, "\n");
	}

	return g_string_free (value, !value->len);
}

static const gchar *
ecc_get_classification_as_string (ECalComponentClassification classification)
{
	const gchar *str;

	switch (classification) {
	case E_CAL_COMPONENT_CLASS_PUBLIC:
		str = "public";
		break;
	case E_CAL_COMPONENT_CLASS_PRIVATE:
		str = "private";
		break;
	case E_CAL_COMPONENT_CLASS_CONFIDENTIAL:
		str = "confidential";
		break;
	default:
		str = NULL;
		break;
	}

	return str;
}

static const gchar *
ecc_get_status_as_string (icalproperty_status status)
{
	switch (status) {
	case ICAL_STATUS_NONE:
		return "NOT STARTED";
	case ICAL_STATUS_COMPLETED:
		return "COMPLETED";
	case ICAL_STATUS_CANCELLED:
		return "CANCELLED";
	case ICAL_STATUS_INPROCESS:
		return "IN PROGRESS";
	case ICAL_STATUS_NEEDSACTION:
		return "NEEDS ACTION";
	case ICAL_STATUS_TENTATIVE:
		return "TENTATIVE";
	case ICAL_STATUS_CONFIRMED:
		return "CONFIRMED";
	case ICAL_STATUS_DRAFT:
		return "DRAFT";
	case ICAL_STATUS_FINAL:
		return "FINAL";
	case ICAL_STATUS_SUBMITTED:
		return "SUBMITTED";
	case ICAL_STATUS_PENDING:
		return "PENDING";
	case ICAL_STATUS_FAILED:
		return "FAILED";
	case ICAL_STATUS_X:
		break;
	}

	return NULL;
}

static void
ecc_fill_other_columns (ECalCache *cal_cache,
			ECacheColumnValues *other_columns,
			ECalComponent *comp)
{
	time_t occur_start = -1, occur_end = -1;
	ECalComponentDateTime dt;
	ECalComponentText text;
	ECalComponentClassification classification;
	icalcomponent *icalcomp;
	icalproperty_status status;
	struct icaltimetype *itt;
	const gchar *str = NULL;
	gint *priority = NULL;
	gboolean has;

	g_return_if_fail (E_IS_CAL_CACHE (cal_cache));
	g_return_if_fail (other_columns != NULL);
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	#define add_value(_col, _val) e_cache_column_values_take_value (other_columns, _col, _val)

	icalcomp = e_cal_component_get_icalcomponent (comp);

	e_cal_util_get_component_occur_times (
		comp, &occur_start, &occur_end,
		ecc_resolve_tzid_cb, cal_cache, icaltimezone_get_utc_timezone (),
		icalcomponent_isa (icalcomp));

	add_value (ECC_COLUMN_OCCUR_START, ecc_encode_timet_to_sql (cal_cache, occur_start));
	add_value (ECC_COLUMN_OCCUR_END, ecc_encode_timet_to_sql (cal_cache, occur_end));

	e_cal_component_get_due (comp, &dt);
	add_value (ECC_COLUMN_DUE, ecc_encode_time_to_sql (cal_cache, &dt));
	e_cal_component_free_datetime (&dt);

	itt = NULL;
	e_cal_component_get_completed (comp, &itt);
	add_value (ECC_COLUMN_COMPLETED, itt ? ecc_encode_itt_to_sql (*itt) : NULL);

	text.value = NULL;
	e_cal_component_get_summary (comp, &text);
	add_value (ECC_COLUMN_SUMMARY, text.value ? e_util_utf8_decompose (text.value) : NULL);

	e_cal_component_get_location (comp, &str);
	add_value (ECC_COLUMN_LOCATION, str ? e_util_utf8_decompose (str) : NULL);

	e_cal_component_get_classification (comp, &classification);
	add_value (ECC_COLUMN_CLASSIFICATION, g_strdup (ecc_get_classification_as_string (classification)));

	e_cal_component_get_status (comp, &status);
	add_value (ECC_COLUMN_STATUS, g_strdup (ecc_get_status_as_string (status)));

	e_cal_component_get_priority (comp, &priority);
	add_value (ECC_COLUMN_PRIORITY, priority && *priority ? g_strdup_printf ("%d", *priority) : NULL);

	has = e_cal_component_has_alarms (comp);
	add_value (ECC_COLUMN_HAS_ALARM, g_strdup (has ? "1" : "0"));

	e_cal_component_get_dtstart (comp, &dt);
	has = dt.value != NULL;
	add_value (ECC_COLUMN_HAS_START, g_strdup (has ? "1" : "0"));
	e_cal_component_free_datetime (&dt);

	has = e_cal_component_has_recurrences (comp) ||
	      e_cal_component_is_instance (comp);
	add_value (ECC_COLUMN_HAS_RECURRENCES, g_strdup (has ? "1" : "0"));

	add_value (ECC_COLUMN_COMMENT, ecc_extract_comment (comp));
	add_value (ECC_COLUMN_DESCRIPTION, ecc_extract_description (comp));
	add_value (ECC_COLUMN_ATTENDEES, ecc_extract_attendees (comp));
	add_value (ECC_COLUMN_ORGANIZER, ecc_extract_organizer (comp));
	add_value (ECC_COLUMN_CATEGORIES, ecc_extract_categories (comp));
}

static gboolean
ecc_init_aux_tables (ECalCache *cal_cache,
		     GCancellable *cancellable,
		     GError **error)
{
	gchar *stmt;
	gboolean success;

	stmt = e_cache_sqlite_stmt_printf ("CREATE TABLE IF NOT EXISTS %Q ("
		"tzid TEXT PRIMARY INDEX, "
		"zone TEXT)",
		ECC_TABLE_TIMEZONES);
	success = e_cache_sqlite_exec (E_CACHE (cal_cache), stmt, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

static gboolean
e_cal_cache_migrate (ECache *cache,
		     gint from_version,
		     GCancellable *cancellable,
		     GError **error)
{
	/* ECalCache *cal_cache = E_CAL_CACHE (cache); */
	gboolean success = TRUE;

	/* Add any version-related changes here */
	/*if (from_version < E_CAL_CACHE_VERSION) {
	}*/

	return success;
}

static gboolean
e_cal_cache_initialize (ECalCache *cal_cache,
			const gchar *filename,
			GCancellable *cancellable,
			GError **error)
{
	ECache *cache;
	GSList *other_columns = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	cache = E_CACHE (cal_cache);

	e_cal_cache_populate_other_columns (cal_cache, &other_columns);

	success = e_cache_initialize_sync (cache, filename, other_columns, cancellable, error);
	if (!success)
		goto exit;

	e_cache_lock (cache, E_CACHE_LOCK_WRITE);

	success = success && ecc_init_aux_tables (cal_cache, cancellable, error);

	/* Check for data migration */
	success = success && e_cal_cache_migrate (cache, e_cache_get_version (cache), cancellable, error);

	e_cache_unlock (cache, success ? E_CACHE_UNLOCK_COMMIT : E_CACHE_UNLOCK_ROLLBACK);

	if (!success)
		goto exit;

	if (e_cache_get_version (cache) != E_CAL_CACHE_VERSION)
		e_cache_set_version (cache, E_CAL_CACHE_VERSION);

 exit:
	g_slist_free_full (other_columns, e_cache_column_info_free);

	return success;
}

/**
 * e_cal_cache_new:
 * @filename: file name to load or create the new cache
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #ECalCache.
 *
 * Returns: (transfer full) (nullable): A new #ECalCache or %NULL on error
 *
 * Since: 3.26
 **/
ECalCache *
e_cal_cache_new (const gchar *filename,
		 GCancellable *cancellable,
		 GError **error)
{
	ECalCache *cal_cache;

	g_return_val_if_fail (filename != NULL, NULL);

	cal_cache = g_object_new (E_TYPE_CAL_CACHE, NULL);

	if (!e_cal_cache_initialize (cal_cache, filename, cancellable, error)) {
		g_object_unref (cal_cache);
		cal_cache = NULL;
	}

	return cal_cache;
}

/**
 * e_cal_cache_dup_component_revision:
 * @cal_cache: an #ECalCache
 * @component: an #ECalComponent
 *
 * Returns the @component revision, used to detect changes.
 * The returned string should be freed with g_free(), when
 * no longer needed.
 *
 * Returns: (transfer full): A newly allocated string containing
 *    revision of the @component.
 *
 * Since: 3.26
 **/
gchar *
e_cal_cache_dup_component_revision (ECalCache *cal_cache,
				    ECalComponent *component)
{
	gchar *revision = NULL;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), NULL);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (component), NULL);

	g_signal_emit (cal_cache, signals[DUP_COMPONENT_REVISION], 0, component, &revision);

	return revision;
}

/**
 * e_cal_cache_put_component:
 * @cal_cache: an #ECalCache
 * @component: an #ECalComponent to put into the @cal_cache
 * @extra: (nullable): an extra data to store in association with the @component
 * @offline_flag: one of #ECacheOfflineFlag, whether putting this component in offline
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Adds a @component into the @cal_cache. Any existing with the same UID
 * and RID is replaced.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_put_component (ECalCache *cal_cache,
			   ECalComponent *component,
			   const gchar *extra,
			   ECacheOfflineFlag offline_flag,
			   GCancellable *cancellable,
			   GError **error)
{
	GSList *components = NULL;
	GSList *extras = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	components = g_slist_prepend (components, component);
	if (extra)
		extras = g_slist_prepend (extras, (gpointer) extra);

	success = e_cal_cache_put_components (cal_cache, components, extras, offline_flag, cancellable, error);

	g_slist_free (components);
	g_slist_free (extras);

	return success;
}

/**
 * e_cal_cache_put_components:
 * @cal_cache: an #ECalCache
 * @components: (element-type ECalComponent): a #GSList of #ECalComponent to put into the @cal_cache
 * @extras: (nullable) (element-type utf8): an extra data to store in association with the @components
 * @offline_flag: one of #ECacheOfflineFlag, whether putting these components in offline
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Adds a list of @components into the @cal_cache. Any existing with the same UID
 * and RID are replaced.
 *
 * If @extras is not %NULL, it's length should be the same as the length
 * of the @components.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_put_components (ECalCache *cal_cache,
			    const GSList *components,
			    const GSList *extras,
			    ECacheOfflineFlag offline_flag,
			    GCancellable *cancellable,
			    GError **error)
{
	const GSList *clink, *elink;
	ECache *cache;
	ECacheColumnValues *other_columns;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (extras == NULL || g_slist_length ((GSList *) components) == g_slist_length ((GSList *) extras), FALSE);

	cache = E_CACHE (cal_cache);
	other_columns = e_cache_column_values_new ();

	e_cache_lock (cache, E_CACHE_LOCK_WRITE);

	for (clink = components, elink = extras; clink; clink = g_slist_next (clink), elink = g_slist_next (elink)) {
		ECalComponent *component = clink->data;
		const gchar *extra = elink ? elink->data : NULL;
		ECalComponentId *id;
		gchar *uid, *rev, *icalstring;

		g_return_val_if_fail (E_IS_CAL_COMPONENT (component), FALSE);

		icalstring = e_cal_component_get_as_string (component);
		g_return_val_if_fail (icalstring != NULL, FALSE);

		e_cache_column_values_remove_all (other_columns);

		if (extra)
			e_cache_column_values_take_value (other_columns, ECC_COLUMN_EXTRA, g_strdup (extra));

		id = e_cal_component_get_id (component);
		if (id) {
			uid = ecc_encode_id_sql (id->uid, id->rid);
		} else {
			g_warn_if_reached ();
			uid = g_strdup ("");
		}
		e_cal_component_free_id (id);

		rev = e_cal_cache_dup_component_revision (cal_cache, component);

		success = e_cache_put (cache, uid, rev, icalstring, other_columns, offline_flag, cancellable, error);

		g_free (icalstring);
		g_free (rev);
		g_free (uid);

		if (!success)
			break;
	}

	e_cache_unlock (cache, success ? E_CACHE_UNLOCK_COMMIT : E_CACHE_UNLOCK_ROLLBACK);

	e_cache_column_values_free (other_columns);

	return success;
}

/**
 * e_cal_cache_remove_component:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component to remove
 * @rid: (nullable): an optional Recurrence-ID to remove
 * @offline_flag: one of #ECacheOfflineFlag, whether removing this component in offline
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removes a component idenitified by @uid and @rid from the @cal_cache.
 * When the @rid is %NULL, or an empty string, then removes the master
 * object and all detached instances identified by @uid.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_remove_component (ECalCache *cal_cache,
			      const gchar *uid,
			      const gchar *rid,
			      ECacheOfflineFlag offline_flag,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalComponentId id;
	GSList *ids = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	id.uid = (gchar *) uid;
	id.rid = (gchar *) rid;

	ids = g_slist_prepend (ids, &id);

	success = e_cal_cache_remove_components (cal_cache, ids, offline_flag, cancellable, error);

	g_slist_free (ids);

	return success;
}

/**
 * e_cal_cache_remove_components:
 * @cal_cache: an #ECalCache
 * @ids: (element-type ECalComponentId): a #GSList of components to remove
 * @offline_flag: one of #ECacheOfflineFlag, whether removing these comonents in offline
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removes components idenitified by @uid and @rid from the @cal_cache
 * in the @ids list. When the @rid is %NULL, or an empty string, then
 * removes the master object and all detached instances identified by @uid.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_remove_components (ECalCache *cal_cache,
			       const GSList *ids,
			       ECacheOfflineFlag offline_flag,
			       GCancellable *cancellable,
			       GError **error)
{
	ECache *cache;
	const GSList *link;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	cache = E_CACHE (cal_cache);

	e_cache_lock (cache, E_CACHE_LOCK_WRITE);

	for (link = ids; success && link; link = g_slist_next (link)) {
		const ECalComponentId *id = link->data;
		gchar *uid;

		g_warn_if_fail (id != NULL);

		if (!id)
			continue;

		uid = ecc_encode_id_sql (id->uid, id->rid);

		success = e_cache_remove (cache, uid, offline_flag, cancellable, error);

		g_free (uid);
	}

	e_cache_unlock (cache, success ? E_CACHE_UNLOCK_COMMIT : E_CACHE_UNLOCK_ROLLBACK);

	return success;
}

/**
 * e_cal_cache_get_component:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component
 * @rid: (nullable): an optional Recurrence-ID
 * @out_component: (out) (transfer full): return location for an #ECalComponent
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a component identified by @uid, and optionally by the @rid,
 * from the @cal_cache. The returned @out_component should be freed with
 * g_object_unref(), when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_component (ECalCache *cal_cache,
			   const gchar *uid,
			   const gchar *rid,
			   ECalComponent **out_component,
			   GCancellable *cancellable,
			   GError **error)
{
	gchar *icalstring = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);

	success = e_cal_cache_get_component_as_string (cal_cache, uid, rid, &icalstring, cancellable, error);
	if (success) {
		*out_component = e_cal_component_new_from_string (icalstring);
		g_free (icalstring);
	}

	return success;
}

/**
 * e_cal_cache_get_component_as_string:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component
 * @rid: (nullable): an optional Recurrence-ID
 * @out_icalstring: (out) (transfer full): return location for an iCalendar string
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a component identified by @uid, and optionally by the @rid,
 * from the @cal_cache. The returned @out_icalstring should be freed with
 * g_free(), when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_component_as_string (ECalCache *cal_cache,
				     const gchar *uid,
				     const gchar *rid,
				     gchar **out_icalstring,
				     GCancellable *cancellable,
				     GError **error)
{
	gchar *id;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_icalstring != NULL, FALSE);

	id = ecc_encode_id_sql (uid, rid);

	*out_icalstring = e_cache_get (E_CACHE (cal_cache), id, NULL, NULL, cancellable, error);

	g_free (id);

	return *out_icalstring != NULL;
}

/**
 * e_cal_cache_set_component_extra:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component
 * @rid: (nullable): an optional Recurrence-ID
 * @extra: (nullable): extra data to set for the component
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Sets or replaces the extra data associated with a component
 * identified by @uid and optionally @rid.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_set_component_extra (ECalCache *cal_cache,
				 const gchar *uid,
				 const gchar *rid,
				 const gchar *extra,
				 GCancellable *cancellable,
				 GError **error)
{
	gchar *id, *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	id = ecc_encode_id_sql (uid, rid);

	if (!e_cache_contains (E_CACHE (cal_cache), id, E_CACHE_INCLUDE_DELETED)) {
		g_free (id);

		if (rid && *rid)
			g_set_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Object “%s”, “%s” not found"), uid, rid);
		else
			g_set_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Object “%s” not found"), uid);

		return FALSE;
	}

	if (extra) {
		stmt = e_cache_sqlite_stmt_printf (
			"UPDATE " E_CACHE_TABLE_OBJECTS " SET " ECC_COLUMN_EXTRA "=%Q"
			" WHERE " E_CACHE_COLUMN_UID "=%Q",
			extra, id);
	} else {
		stmt = e_cache_sqlite_stmt_printf (
			"UPDATE " E_CACHE_TABLE_OBJECTS " SET " ECC_COLUMN_EXTRA "=NULL"
			" WHERE " E_CACHE_COLUMN_UID "=%Q",
			id);
	}

	success = e_cache_sqlite_exec (E_CACHE (cal_cache), stmt, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);
	g_free (id);

	return success;
}

/**
 * e_cal_cache_get_component_extra:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component
 * @rid: (nullable): an optional Recurrence-ID
 * @out_extra: (out) (transfer full): return location to store the extra data
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the extra data previously set for @uid and @rid, either with
 * e_cal_cache_set_component_extra() or when adding components.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_component_extra (ECalCache *cal_cache,
				 const gchar *uid,
				 const gchar *rid,
				 gchar **out_extra,
				 GCancellable *cancellable,
				 GError **error)
{
	gchar *id, *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	id = ecc_encode_id_sql (uid, rid);

	if (!e_cache_contains (E_CACHE (cal_cache), id, E_CACHE_INCLUDE_DELETED)) {
		g_free (id);

		if (rid && *rid)
			g_set_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Object “%s”, “%s” not found"), uid, rid);
		else
			g_set_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Object “%s” not found"), uid);

		return FALSE;
	}

	stmt = e_cache_sqlite_stmt_printf (
		"SELECT " ECC_COLUMN_EXTRA " FROM " E_CACHE_TABLE_OBJECTS
		" WHERE " E_CACHE_COLUMN_UID "=%Q",
		id);

	success = e_cache_sqlite_select (E_CACHE (cal_cache), stmt, e_cal_cache_get_string, out_extra, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);
	g_free (id);

	return success;
}

static GSList *
ecc_icalstrings_to_components (GSList *icalstrings)
{
	GSList *link;

	for (link = icalstrings; link; link = g_slist_next (link)) {
		gchar *icalstring = link->data;

		link->data = e_cal_component_new_from_string (icalstring);

		g_free (icalstring);
	}

	return icalstrings;
}

/**
 * e_cal_cache_get_components_by_uid:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component
 * @out_components: (out) (transfer full) (element-type ECalComponent): return location for the components
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the master object and all detached instances for a component
 * identified by the @uid. Free the returned #GSList with
 * g_slist_free_full (components, g_object_unref); when
 * no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_components_by_uid (ECalCache *cal_cache,
				   const gchar *uid,
				   GSList **out_components,
				   GCancellable *cancellable,
				   GError **error)
{
	GSList *icalstrings = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_components != NULL, FALSE);

	success = e_cal_cache_get_components_by_uid_as_strings (cal_cache, uid, &icalstrings, cancellable, error);
	if (success) {
		*out_components = ecc_icalstrings_to_components (icalstrings);
	}

	return success;
}

/**
 * e_cal_cache_get_components_by_uid_as_strings:
 * @cal_cache: an #ECalCache
 * @uid: a UID of the component
 * @out_icalstrings: (out) (transfer full) (element-type utf8): return location for the iCal strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the master object and all detached instances as string
 * for a component identified by the @uid. Free the returned #GSList
 * with g_slist_free_full (icalstrings, g_free); when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_components_by_uid_as_strings (ECalCache *cal_cache,
					      const gchar *uid,
					      GSList **out_icalstrings,
					      GCancellable *cancellable,
					      GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_icalstrings != NULL, FALSE);

	*out_icalstrings = NULL;

	/* Using 'ORDER BY' to get the master object first */
	stmt = e_cache_sqlite_stmt_printf (
		"SELECT " E_CACHE_COLUMN_OBJECT " FROM " E_CACHE_TABLE_OBJECTS
		" WHERE " E_CACHE_COLUMN_UID "=%Q OR " E_CACHE_COLUMN_UID " LIKE '%q\n%%'"
		" ORDER BY " E_CACHE_COLUMN_UID,
		uid, uid);

	success = e_cache_sqlite_select (E_CACHE (cal_cache), stmt, e_cal_cache_get_strings, out_icalstrings, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	if (success && !*out_icalstrings) {
		success = FALSE;
		g_set_error (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Object “%s” not found"), uid);
	} else if (success) {
		*out_icalstrings = g_slist_reverse (*out_icalstrings);
	}

	return success;
}

/**
 * e_cal_cache_get_components_in_range:
 * @cal_cache: an #ECalCache
 * @range_start: start of the range, as time_t, inclusive
 * @range_end: end of the range, as time_t, exclusive
 * @out_components: (out) (transfer full) (element-type ECalComponent): return location for the components
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a list of components which occur in the given time range.
 * It's not an error if none is found.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_components_in_range (ECalCache *cal_cache,
				     time_t range_start,
				     time_t range_end,
				     GSList **out_components,
				     GCancellable *cancellable,
				     GError **error)
{
	GSList *icalstrings = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (out_components != NULL, FALSE);

	success = e_cal_cache_get_components_in_range_as_strings (cal_cache, range_start, range_end, &icalstrings, cancellable, error);
	if (success)
		*out_components = ecc_icalstrings_to_components (icalstrings);

	return success;
}

/**
 * e_cal_cache_get_components_in_range_as_strings:
 * @cal_cache: an #ECalCache
 * @range_start: start of the range, as time_t, inclusive
 * @range_end: end of the range, as time_t, exclusive
 * @out_icalstrings: (out) (transfer full) (element-type utf8): return location for the iCal strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a list of components, as iCal strings, which occur in the given time range.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_components_in_range_as_strings (ECalCache *cal_cache,
						time_t range_start,
						time_t range_end,
						GSList **out_icalstrings,
						GCancellable *cancellable,
						GError **error)
{
	gchar *stmt, *range_start_str, *range_end_str;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (out_icalstrings != NULL, FALSE);

	*out_icalstrings = NULL;

	range_start_str = ecc_encode_timet_to_sql (cal_cache, range_start);
	range_end_str = ecc_encode_timet_to_sql (cal_cache, range_end);

	/* Using 'ORDER BY' to get the master object first */
	stmt = e_cache_sqlite_stmt_printf (
		"SELECT " E_CACHE_COLUMN_OBJECT " FROM " E_CACHE_TABLE_OBJECTS
		" WHERE (" ECC_COLUMN_OCCUR_START " IS NULL OR " ECC_COLUMN_OCCUR_START "<%Q)"
		" AND (" ECC_COLUMN_OCCUR_END " IS NULL OR " ECC_COLUMN_OCCUR_END ">%Q)"
		" ORDER BY " E_CACHE_COLUMN_UID,
		range_end, range_start);

	success = e_cache_sqlite_select (E_CACHE (cal_cache), stmt, e_cal_cache_get_strings, out_icalstrings, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);
	g_free (range_start_str);
	g_free (range_end_str);

	return success;
}

static gboolean
ecc_search_components_cb (ECache *cache,
			  const gchar *uid,
			  const gchar *rid,
			  const gchar *revision,
			  const gchar *object,
			  const gchar *extra,
			  EOfflineState offline_state,
			  gpointer user_data)
{
	GSList **out_components = user_data;

	g_return_val_if_fail (out_components != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_components = g_slist_prepend (*out_components,
		e_cal_component_new_from_string (object));

	return TRUE;
}

static gboolean
ecc_search_icalstrings_cb (ECache *cache,
			   const gchar *uid,
			   const gchar *rid,
			   const gchar *revision,
			   const gchar *object,
			   const gchar *extra,
			   EOfflineState offline_state,
			   gpointer user_data)
{
	GSList **out_icalstrings = user_data;

	g_return_val_if_fail (out_icalstrings != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_icalstrings = g_slist_prepend (*out_icalstrings, g_strdup (object));

	return TRUE;
}

static gboolean
ecc_search_ids_cb (ECache *cache,
		   const gchar *uid,
		   const gchar *rid,
		   const gchar *revision,
		   const gchar *object,
		   const gchar *extra,
		   EOfflineState offline_state,
		   gpointer user_data)
{
	GSList **out_ids = user_data;

	g_return_val_if_fail (out_ids != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_ids = g_slist_prepend (*out_ids, e_cal_component_id_new (uid, rid));

	return TRUE;
}

/**
 * e_cal_cache_search:
 * @cal_cache: an #ECalCache
 * @sexp: (nullable): search expression; use %NULL or an empty string to list all stored components
 * @out_components: (out) (transfer full) (element-type ECalComponent): stored components satisfied by @sexp
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Searches the @cal_cache with the given @sexp and
 * returns those components which satisfy the search
 * expression. The @out_components should be freed with
 * g_slist_free_full (components, g_object_unref); when
 * no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_search (ECalCache *cal_cache,
		    const gchar *sexp,
		    GSList **out_components,
		    GCancellable *cancellable,
		    GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (out_components != NULL, FALSE);

	*out_components = NULL;

	success = e_cal_cache_search_with_callback (cal_cache, sexp, ecc_search_components_cb,
		out_components, cancellable, error);
	if (success) {
		*out_components = g_slist_reverse (*out_components);
	} else {
		g_slist_free_full (*out_components, g_object_unref);
		*out_components = NULL;
	}

	return success;
}

/**
 * e_cal_cache_search_as_strings:
 * @cal_cache: an #ECalCache
 * @sexp: (nullable): search expression; use %NULL or an empty string to list all stored components
 * @out_icalstrings: (out) (transfer full) (element-type utf8): stored components satisfied by @sexp as iCal strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Searches the @cal_cache with the given @sexp and returns those
 * components which satisfy the search expression as iCal strings.
 * The @out_icalstrings should be freed with
 * g_slist_free_full (components, g_free); when
 * no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_search_as_strings (ECalCache *cal_cache,
			       const gchar *sexp,
			       GSList **out_icalstrings,
			       GCancellable *cancellable,
			       GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (out_icalstrings != NULL, FALSE);

	*out_icalstrings = NULL;

	success = e_cal_cache_search_with_callback (cal_cache, sexp, ecc_search_icalstrings_cb,
		out_icalstrings, cancellable, error);
	if (success) {
		*out_icalstrings = g_slist_reverse (*out_icalstrings);
	} else {
		g_slist_free_full (*out_icalstrings, g_free);
		*out_icalstrings = NULL;
	}

	return success;
}

/**
 * e_cal_cache_search_ids:
 * @cal_cache: an #ECalCache
 * @sexp: (nullable): search expression; use %NULL or an empty string to list all stored components
 * @out_ids: (out) (transfer full) (element-type ECalComponentId): IDs of stored components satisfied by @sexp
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Searches the @cal_cache with the given @sexp and returns ECalComponentId
 * for those components which satisfy the search expression.
 * The @out_ids should be freed with
 * g_slist_free_full (components, (GDestroyNotify) e_cal_component_free_id);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_search_ids (ECalCache *cal_cache,
			const gchar *sexp,
			GSList **out_ids,
			GCancellable *cancellable,
			GError **error)

{
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (out_ids != NULL, FALSE);

	*out_ids = NULL;

	success = e_cal_cache_search_with_callback (cal_cache, sexp, ecc_search_ids_cb,
		out_ids, cancellable, error);
	if (success) {
		*out_ids = g_slist_reverse (*out_ids);
	} else {
		g_slist_free_full (*out_ids, g_object_unref);
		*out_ids = NULL;
	}

	return success;
}

/**
 * e_cal_cache_search_with_callback:
 * @cal_cache: an #ECalCache
 * @sexp: (nullable): search expression; use %NULL or an empty string to list all stored components
 * @func: an #ECalCacheSearchFunc callback to call for each row which satisfies @sexp
 * @user_data: user data for @func
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Searches the @cal_cache with the given @sexp and calls @func for each
 * row which satisfy the search expression.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_search_with_callback (ECalCache *cal_cache,
				  const gchar *sexp,
				  ECalCacheSearchFunc func,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	return success;
}

/**
 * e_cal_cache_put_timezone:
 * @cal_cache: an #ECalCache
 * @zone: an icaltimezone to put
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Puts the @zone into the @cal_cache using its timezone ID as
 * an identificator. The function does nothing if any such already
 * exists in the @cal_cache.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_put_timezone (ECalCache *cal_cache,
			  icaltimezone *zone,
			  GCancellable *cancellable,
			  GError **error)
{
	gboolean success;
	gchar *stmt;
	const gchar *tzid;
	gchar *component_str;
	icalcomponent *component;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	tzid = icaltimezone_get_tzid (zone);
	if (!tzid) {
		g_set_error_literal (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Cannot add timezone without tzid"));
		return FALSE;
	}

	component = icaltimezone_get_component (zone);
	if (!component) {
		g_set_error_literal (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Cannot add timezone without component"));
		return FALSE;
	}

	component_str = icalcomponent_as_ical_string_r (component);
	if (!component_str) {
		g_set_error_literal (error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND, _("Cannot add timezone with invalid component"));
		return FALSE;
	}

	stmt = e_cache_sqlite_stmt_printf (
		"INSERT or REPLACE INTO " ECC_TABLE_TIMEZONES " (tzid, zone) VALUES (%Q, %Q)",
		tzid, component_str);

	success = e_cache_sqlite_exec (E_CACHE (cal_cache), stmt, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	g_free (component_str);

	return success;
}

static icaltimezone *
ecc_timezone_from_string (const gchar *icalstring)
{
	icalcomponent *component;

	g_return_val_if_fail (icalstring != NULL, NULL);

	component = icalcomponent_new_from_string (icalstring);
	if (component) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		if (!icaltimezone_set_component (zone, component)) {
			icalcomponent_free (component);
			icaltimezone_free (zone, 1);
		} else {
			return zone;
		}
	}

	return NULL;
}

/**
 * e_cal_cache_get_timezone:
 * @cal_cache: an #ECalCache
 * @tzid: a timezone ID to get
 * @out_zone: (out) (transfer none): return location for the icaltimezone
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a timezone with given @tzid, which had been previously put
 * into the @cal_cache with e_cal_cache_put_timezone().
 * The returned icaltimezone is owned by the @cal_cache and should
 * not be freed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_get_timezone (ECalCache *cal_cache,
			  const gchar *tzid,
			  icaltimezone **out_zone,
			  GCancellable *cancellable,
			  GError **error)

{
	gchar *zone_str = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);
	g_return_val_if_fail (out_zone != NULL, FALSE);

	g_mutex_lock (&cal_cache->priv->loaded_timezones_lock);

	*out_zone = g_hash_table_lookup (cal_cache->priv->loaded_timezones, tzid);
	if (*out_zone) {
		g_mutex_unlock (&cal_cache->priv->loaded_timezones_lock);
		return TRUE;
	}

	success = e_cal_cache_dup_timezone_as_string (cal_cache, tzid, &zone_str, cancellable, error);

	if (success && zone_str) {
		icaltimezone *zone;

		zone = ecc_timezone_from_string (zone_str);
		if (zone) {
			g_hash_table_insert (cal_cache->priv->loaded_timezones, g_strdup (tzid), zone);
			*out_zone = zone;
		} else {
			success = FALSE;
		}
	}

	g_mutex_unlock (&cal_cache->priv->loaded_timezones_lock);

	g_free (zone_str);

	return success;
}

/**
 * e_cal_cache_dup_timezone_as_string:
 * @cal_cache: an #ECalCache
 * @tzid: a timezone ID to get
 * @out_zone_string: (out) (transfer full): return location for the icaltimezone as iCal string
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a timezone with given @tzid, which had been previously put
 * into the @cal_cache with e_cal_cache_put_timezone().
 * The returned string is an iCal string for that icaltimezone and
 * should be freed with g_free() when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_dup_timezone_as_string (ECalCache *cal_cache,
				    const gchar *tzid,
				    gchar **out_zone_string,
				    GCancellable *cancellable,
				    GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);
	g_return_val_if_fail (out_zone_string, FALSE);

	*out_zone_string = NULL;

	stmt = e_cache_sqlite_stmt_printf (
		"SELECT zone FROM " ECC_TABLE_TIMEZONES " WHERE tzid=%Q",
		tzid);

	success = e_cache_sqlite_select (E_CACHE (cal_cache), stmt, e_cal_cache_get_string, out_zone_string, cancellable, error);

	e_cache_sqlite_stmt_free (stmt);

	return success;
}

static gboolean
e_cal_cache_get_uint64_cb (ECache *cache,
			   gint ncols,
			   const gchar **column_names,
			   const gchar **column_values,
			   gpointer user_data)
{
	guint64 *pui64 = user_data;

	g_return_val_if_fail (pui64 != NULL, FALSE);

	if (ncols == 1) {
		*pui64 = column_values[0] ? g_ascii_strtoull (column_values[0], NULL, 10) : 0;
	} else {
		*pui64 = 0;
	}

	return TRUE;
}

static gboolean
e_cal_cache_load_zones_cb (ECache *cache,
			   gint ncols,
			   const gchar *column_names[],
			   const gchar *column_values[],
			   gpointer user_data)
{
	GHashTable *loaded_zones = user_data;

	g_return_val_if_fail (loaded_zones != NULL, FALSE);
	g_return_val_if_fail (ncols != 2, FALSE);

	/* Do not overwrite already loaded timezones, they can be used anywhere around */
	if (!g_hash_table_lookup (loaded_zones, column_values[0])) {
		icaltimezone *zone;

		zone = ecc_timezone_from_string (column_values[1]);
		if (zone) {
			g_hash_table_insert (loaded_zones, g_strdup (column_values[0]), zone);
		}
	}

	return TRUE;
}

/**
 * e_cal_cache_list_timezones:
 * @cal_cache: an #ECalCache
 * @out_timezones: (out) (transfer container) (element-type icaltimezone): return location for the list of stored timezones
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a list of all stored timezones by the @cal_cache.
 * Only the returned list should be freed with g_list_free()
 * when no longer needed; the icaltimezone-s are owned
 * by the @cal_cache.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_cache_list_timezones (ECalCache *cal_cache,
			    GList **out_timezones,
			    GCancellable *cancellable,
			    GError **error)
{
	guint64 n_stored = 0;
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (out_timezones != NULL, FALSE);

	g_mutex_lock (&cal_cache->priv->loaded_timezones_lock);

	success = e_cache_sqlite_select (E_CACHE (cal_cache),
		"SELECT COUNT(*) FROM " ECC_TABLE_TIMEZONES,
		e_cal_cache_get_uint64_cb, &n_stored, cancellable, error);

	if (success && n_stored != g_hash_table_size (cal_cache->priv->loaded_timezones)) {
		stmt = e_cache_sqlite_stmt_printf ("SELECT tzid, zone FROM " ECC_TABLE_TIMEZONES);
		success = e_cache_sqlite_select (E_CACHE (cal_cache), stmt,
			e_cal_cache_load_zones_cb, cal_cache->priv->loaded_timezones, cancellable, error);
		e_cache_sqlite_stmt_free (stmt);
	}

	if (success)
		*out_timezones = g_hash_table_get_values (cal_cache->priv->loaded_timezones);

	g_mutex_unlock (&cal_cache->priv->loaded_timezones_lock);

	return success;
}

static gboolean
ecc_empty_aux_tables (ECache *cache,
		      GCancellable *cancellable,
		      GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cache), FALSE);

	stmt = e_cache_sqlite_stmt_printf ("DELETE FROM %Q", ECC_TABLE_TIMEZONES);
	success = e_cache_sqlite_exec (cache, stmt, cancellable, error);
	e_cache_sqlite_stmt_free (stmt);

	return success;
}

/* The default revision is a concatenation of
   <DTSTAMP> "-" <LAST-MODIFIED> "-" <SEQUENCE> */
static gchar *
ecc_dup_component_revision (ECalCache *cal_cache,
			    ECalComponent *component)
{
	icalcomponent *icalcomp;
	struct icaltimetype itt;
	icalproperty *prop;
	GString *revision;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (component), NULL);

	icalcomp = e_cal_component_get_icalcomponent (component);

	revision = g_string_sized_new (48);

	itt = icalcomponent_get_dtstamp (icalcomp);
	if (icaltime_is_null_time (itt) || !icaltime_is_valid_time (itt)) {
		g_string_append_c (revision, 'x');
	} else {
		g_string_append_printf (revision, "%04d%02d%02d%02d%02d%02d",
			itt.year, itt.month, itt.day,
			itt.hour, itt.minute, itt.second);
	}

	g_string_append_c (revision, '-');

	prop = icalcomponent_get_first_property (icalcomp, ICAL_LASTMODIFIED_PROPERTY);
	if (prop)
		itt = icalproperty_get_lastmodified (prop);

	if (!prop || icaltime_is_null_time (itt) || !icaltime_is_valid_time (itt)) {
		g_string_append_c (revision, 'x');
	} else {
		g_string_append_printf (revision, "%04d%02d%02d%02d%02d%02d",
			itt.year, itt.month, itt.day,
			itt.hour, itt.minute, itt.second);
	}

	g_string_append_c (revision, '-');

	prop = icalcomponent_get_first_property (icalcomp, ICAL_SEQUENCE_PROPERTY);
	if (!prop) {
		g_string_append_c (revision, 'x');
	} else {
		g_string_append_printf (revision, "%d", icalproperty_get_sequence (prop));
	}

	return g_string_free (revision, FALSE);
}

static gboolean
e_cal_cache_put_locked (ECache *cache,
			const gchar *uid,
			const gchar *revision,
			const gchar *object,
			ECacheColumnValues *other_columns,
			EOfflineState offline_state,
			gboolean is_replace,
			GCancellable *cancellable,
			GError **error)
{
	ECalCache *cal_cache;
	ECalComponent *comp;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cache), FALSE);
	g_return_val_if_fail (E_CACHE_CLASS (e_cal_cache_parent_class)->put_locked != NULL, FALSE);

	cal_cache = E_CAL_CACHE (cache);

	comp = e_cal_component_new_from_string (object);
	if (!comp)
		return FALSE;

	ecc_fill_other_columns (cal_cache, other_columns, comp);

	success = E_CACHE_CLASS (e_cal_cache_parent_class)->put_locked (cache, uid, revision, object, other_columns, offline_state,
		is_replace, cancellable, error);

	g_clear_object (&comp);

	return success;
}

static gboolean
e_cal_cache_remove_all_locked (ECache *cache,
			       const GSList *uids,
			       GCancellable *cancellable,
			       GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CACHE (cache), FALSE);
	g_return_val_if_fail (E_CACHE_CLASS (e_cal_cache_parent_class)->remove_all_locked != NULL, FALSE);

	/* Cannot free content of priv->loaded_timezones, because those can be used anywhere */
	success = ecc_empty_aux_tables (cache, cancellable, error);

	success = success && E_CACHE_CLASS (e_cal_cache_parent_class)->remove_all_locked (cache, uids, cancellable, error);

	return success;
}

static void
cal_cache_free_zone (gpointer ptr)
{
	icaltimezone *zone = ptr;

	if (zone)
		icaltimezone_free (zone, 1);
}

static void
e_cal_cache_finalize (GObject *object)
{
	ECalCache *cal_cache = E_CAL_CACHE (object);

	g_hash_table_destroy (cal_cache->priv->loaded_timezones);

	g_mutex_clear (&cal_cache->priv->loaded_timezones_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_cache_parent_class)->finalize (object);
}

static void
e_cal_cache_class_init (ECalCacheClass *klass)
{
	GObjectClass *object_class;
	ECacheClass *cache_class;

	g_type_class_add_private (klass, sizeof (ECalCachePrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_cal_cache_finalize;

	cache_class = E_CACHE_CLASS (klass);
	cache_class->put_locked = e_cal_cache_put_locked;
	cache_class->remove_all_locked = e_cal_cache_remove_all_locked;

	klass->dup_component_revision = ecc_dup_component_revision;

	/**
	 * @ECalCache:dup-component-revision:
	 * A signal being called to get revision of an #ECalComponent.
	 * The default implementation uses a concatenation of
	 * DTSTAMP '-' LASTMODIFIED '-' SEQUENCE.
	 **/
	signals[DUP_COMPONENT_REVISION] = g_signal_new (
		"dup-component-revision",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (ECalCacheClass, dup_component_revision),
		g_signal_accumulator_first_wins,
		NULL,
		g_cclosure_marshal_generic,
		G_TYPE_STRING, 1,
		E_TYPE_CAL_COMPONENT);
}

static void
e_cal_cache_init (ECalCache *cal_cache)
{
	cal_cache->priv = G_TYPE_INSTANCE_GET_PRIVATE (cal_cache, E_TYPE_CAL_CACHE, ECalCachePrivate);
	cal_cache->priv->loaded_timezones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, cal_cache_free_zone);

	g_mutex_init (&cal_cache->priv->loaded_timezones_lock);
}
