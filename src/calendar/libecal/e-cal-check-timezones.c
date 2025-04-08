/*
 * Copyright (C) 2008 Novell, Inc.
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
 *
 * Authors: Patrick Ohly <patrick.ohly@gmx.de>
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <ctype.h>
#include <libical-glib/libical-glib.h>

#include "e-cal-client.h"

#include "e-cal-check-timezones.h"

/*
 * Matches a location to a system timezone definition via a fuzzy
 * search and returns the matching TZID, or NULL if none found.
 *
 * Currently simply strips a suffix introduced by a hyphen,
 * as in "America/Denver-(Standard)".
 */
static const gchar *
e_cal_match_location (const gchar *location)
{
	icaltimezone *zone;
	const gchar *tail;
	gsize len;
	gchar *buffer;

	zone = icaltimezone_get_builtin_timezone (location);
	if (zone) {
		return icaltimezone_get_tzid (zone);
	}

	/* try a bit harder by stripping trailing suffix */
	tail = strrchr (location, '-');
	len = tail ? (tail - location) : strlen (location);
	buffer = g_malloc (len + 1);

	if (buffer) {
		memcpy (buffer, location, len);
		buffer[len] = 0;
		zone = icaltimezone_get_builtin_timezone (buffer);
		g_free (buffer);
		if (zone) {
			return icaltimezone_get_tzid (zone);
		}
	}

	return NULL;
}

/**
 * e_cal_match_tzid:
 * @tzid: a timezone ID
 *
 * Matches @tzid against the system timezone definitions
 * and returns the matching TZID, or %NULL if none found
 *
 * Returns: (nullable): The matching TZID, or %NULL if none found or for UTC
 *
 * Since: 2.24
 */
const gchar *
e_cal_match_tzid (const gchar *tzid)
{
	const gchar *location;
	const gchar *systzid = NULL;
	gsize len = strlen (tzid);
	gssize eostr;

	/*
	 * Try without any trailing spaces/digits: they might have been added
	 * by e_cal_check_timezones() in order to distinguish between
	 * different incompatible definitions. At that time mapping
	 * to system time zones must have failed, but perhaps now
	 * we have better code and it succeeds...
	 */
	eostr = len - 1;
	while (eostr >= 0 && isdigit (tzid[eostr])) {
		eostr--;
	}
	while (eostr >= 0 && isspace (tzid[eostr])) {
		eostr--;
	}
	if (eostr + 1 < len) {
		gchar *strippedtzid = g_strndup (tzid, eostr + 1);
		if (strippedtzid) {
			systzid = e_cal_match_tzid (strippedtzid);
			g_free (strippedtzid);
			if (systzid) {
				goto done;
			}
		}
	}

	/*
	 * old-style Evolution: /softwarestudio.org/Olson_20011030_5/America/Denver
	 *
	 * jump from one slash to the next and check whether the remainder
	 * is a known location; start with the whole string (just in case)
	 */
	for (location = tzid;
		 location && location[0];
		 location = strchr (location + 1, '/')) {
		systzid = e_cal_match_location (
			location[0] == '/' ?
			location + 1 : location);
		if (systzid) {
			goto done;
		}
	}

	/* TODO: lookup table for Exchange TZIDs */

 done:
	if (systzid && !strcmp (systzid, "UTC")) {
		/*
		 * UTC is special: it doesn't have a real VTIMEZONE in
		 * EDS. Matching some pseudo VTTIMEZONE with UTC in the TZID
		 * to our internal UTC "timezone" breaks
		 * e_cal_check_timezones() (it patches the event to use
		 * TZID=UTC, which cannot be exported correctly later on) and
		 * e_cal_get_timezone() (triggers an assert).
		 *
		 * So better avoid matching against it...
		 */
		return NULL;
	} else {
		return systzid;
	}
}

static void
patch_tzids (ICalComponent *subcomp,
             GHashTable *mapping)
{
	gchar *tzid = NULL;

	if (i_cal_component_isa (subcomp) != I_CAL_VTIMEZONE_COMPONENT) {
		ICalProperty *prop;

		for (prop = i_cal_component_get_first_property (subcomp, I_CAL_ANY_PROPERTY);
		     prop;
		     g_object_unref (prop), prop = i_cal_component_get_next_property (subcomp, I_CAL_ANY_PROPERTY)) {
			ICalParameter *param;

			for (param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
			     param;
			     g_object_unref (param), param = i_cal_property_get_next_parameter (prop, I_CAL_TZID_PARAMETER)) {
				const gchar *oldtzid;
				const gchar *newtzid;

				g_free (tzid);
				tzid = g_strdup (i_cal_parameter_get_tzid (param));

				if (!g_hash_table_lookup_extended (
					mapping, tzid,
					(gpointer *) &oldtzid,
					(gpointer *) &newtzid)) {
					/* Corresponding VTIMEZONE not seen before! */
					newtzid = e_cal_match_tzid (tzid);
				}
				if (newtzid) {
					i_cal_parameter_set_tzid (param, newtzid);
				}
			}
		}
	}

	g_free (tzid);
}

static void
addsystemtz (gpointer key,
             gpointer value,
             gpointer user_data)
{
	const gchar *tzid = key;
	ICalComponent *vcalendar = user_data;
	ICalTimezone *zone;

	zone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
	if (zone) {
		ICalComponent *zone_comp;

		zone_comp = i_cal_timezone_get_component (zone);
		if (zone_comp) {
			i_cal_component_take_component (vcalendar, i_cal_component_clone (zone_comp));
			g_object_unref (zone_comp);
		}
	}
}

/**
 * e_cal_client_check_timezones_sync:
 * @vcalendar: a VCALENDAR containing a list of
 *    VTIMEZONE and arbitrary other components, in
 *    arbitrary order: these other components are
 *    modified by this call
 * @icalcomps: (element-type ICalComponent) (nullable): a list of #ICalComponent
 *    instances which also have to be patched; may be %NULL
 * @tzlookup: (scope call) (closure tzlookup_data): a callback function which is called to retrieve
 *    a calendar's VTIMEZONE definition; the returned
 *    definition is *not* freed by e_cal_client_check_timezones()
 *    NULL indicates that no such timezone exists
 *    or an error occurred
 * @tzlookup_data: an arbitrary pointer which is passed
 *    through to the @tzlookup function
 * @cancellable: a #GCancellable to use in @tzlookup function
 * @error: an error description in case of a failure
 *
 * This function cleans up VEVENT, VJOURNAL, VTODO and VTIMEZONE
 * items which are to be imported into Evolution.
 *
 * Using VTIMEZONE definitions is problematic because they cannot be
 * updated properly when timezone definitions change. They are also
 * incomplete (for compatibility reason only one set of rules for
 * summer saving changes can be included, even if different rules
 * apply in different years). This function looks for matches of the
 * used TZIDs against system timezones and replaces such TZIDs with
 * the corresponding system timezone. This works for TZIDs containing
 * a location (found via a fuzzy string search) and for Outlook TZIDs
 * (via a hard-coded lookup table).
 *
 * Some programs generate broken meeting invitations with TZID, but
 * without including the corresponding VTIMEZONE. Importing such
 * invitations unchanged causes problems later on (meeting displayed
 * incorrectly, e_cal_component_get_as_string() fails). The situation
 * where this occurred in the past (found by a SyncEvolution user) is
 * now handled via the location based mapping.
 *
 * If this mapping fails, this function also deals with VTIMEZONE
 * conflicts: such conflicts occur when the calendar already contains
 * an old VTIMEZONE definition with the same TZID, but different
 * summer saving rules. Replacing the VTIMEZONE potentially breaks
 * displaying of old events, whereas not replacing it breaks the new
 * events (the behavior in Evolution <= 2.22.1).
 *
 * The way this problem is resolved by renaming the new VTIMEZONE
 * definition until the TZID is unique. A running count is appended to
 * the TZID. All items referencing the renamed TZID are adapted
 * accordingly.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_client_check_timezones_sync (ICalComponent *vcalendar,
				   GSList *icalcomps,
				   ECalRecurResolveTimezoneCb tzlookup,
				   gpointer tzlookup_data,
				   GCancellable *cancellable,
				   GError **error)
{
	gboolean success = TRUE;
	ICalComponent *subcomp;
	ICalTimezone *zone = i_cal_timezone_new ();
	gchar *key = NULL, *value = NULL;
	gchar *buffer = NULL;
	gchar *zonestr = NULL;
	gchar *tzid = NULL;
	GSList *link;

	/* a hash from old to new tzid; strings dynamically allocated */
	GHashTable *mapping = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* a hash of all system time zone IDs which have to be added; strings are shared with mapping hash */
	GHashTable *systemtzids = g_hash_table_new (g_str_hash, g_str_equal);

	if (!mapping || !zone) {
		goto nomem;
	}

	/* iterate over all VTIMEZONE definitions */
	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_VTIMEZONE_COMPONENT)) {
		if (i_cal_timezone_set_component (zone, subcomp)) {
			g_free (tzid);
			tzid = g_strdup (i_cal_timezone_get_tzid (zone));
			if (tzid) {
				const gchar *newtzid = e_cal_match_tzid (tzid);
				if (newtzid) {
					/* matched against system time zone */
					g_free (key);
					key = g_strdup (tzid);
					if (!key) {
						g_object_unref (subcomp);
						goto nomem;
					}

					g_free (value);
					value = g_strdup (newtzid);
					if (!value) {
						g_object_unref (subcomp);
						goto nomem;
					}

					g_hash_table_insert (mapping, key, value);
					g_hash_table_insert (systemtzids, value, NULL);
					key = value = NULL;
				} else {
					gint counter;

					zonestr = i_cal_component_as_ical_string (subcomp);

					/* check for collisions with existing timezones */
					for (counter = 0;
					     counter < 100 /* sanity limit */;
					     counter++) {
						ICalTimezone *existing_zone;
						ICalComponent *zone_comp;
						GError *local_error = NULL;

						if (counter) {
							g_free (value);
							value = g_strdup_printf ("%s %d", tzid, counter);
						}
						existing_zone = tzlookup (counter ? value : tzid, tzlookup_data, cancellable, &local_error);
						if (!existing_zone) {
							if (local_error) {
								g_propagate_error (error, local_error);
								g_object_unref (subcomp);
								goto failed;
							} else {
								break;
							}
						}
						g_free (buffer);
						zone_comp = i_cal_timezone_get_component (existing_zone);
						buffer = zone_comp ? i_cal_component_as_ical_string (zone_comp) : NULL;
						g_clear_object (&zone_comp);

						if (!buffer)
							continue;

						if (counter) {
							gchar *fulltzid = g_strdup_printf ("TZID:%s", value);
							gsize baselen = strlen ("TZID:") + strlen (tzid);
							gsize fulllen = strlen (fulltzid);
							gchar *tzidprop;
							/*
							 * Map TZID with counter suffix back to basename.
							 */
							tzidprop = strstr (buffer, fulltzid);
							if (tzidprop) {
								memmove (
									tzidprop + baselen,
									tzidprop + fulllen,
									strlen (tzidprop + fulllen) + 1);
							}
							g_free (fulltzid);
						}

						/*
						 * If the strings are identical, then the
						 * VTIMEZONE definitions are identical.  If
						 * they are not identical, then VTIMEZONE
						 * definitions might still be semantically
						 * correct and we waste some space by
						 * needlesly duplicating the VTIMEZONE. This
						 * is expected to occur rarely (if at all) in
						 * practice.
						 */
						if (!g_strcmp0 (zonestr, buffer)) {
							break;
						}
					}

					if (!counter) {
						/* does not exist, nothing to do */
					} else {
						/* timezone renamed */
						ICalProperty *prop;

						for (prop = i_cal_component_get_first_property (subcomp, I_CAL_TZID_PROPERTY);
						     prop;
						     g_object_unref (prop), prop = i_cal_component_get_next_property (subcomp, I_CAL_TZID_PROPERTY)) {
							i_cal_property_set_value_from_string (prop, value, "NO");
						}
						g_free (key);
						key = g_strdup (tzid);
						g_hash_table_insert (mapping, key, value);
						key = value = NULL;
					}
				}
			}
		}
	}

	/*
	 * now replace all TZID parameters in place
	 */
	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {
		/*
		 * Leave VTIMEZONE unchanged, iterate over properties of
		 * everything else.
		 *
		 * Note that no attempt is made to remove unused VTIMEZONE
		 * definitions. That would just make the code more complex for
		 * little additional gain. However, newly used time zones are
		 * added below.
		 */
		patch_tzids (subcomp, mapping);
	}

	for (link = icalcomps; link; link = g_slist_next (link)) {
		ICalComponent *icalcomp = link->data;
		patch_tzids (icalcomp, mapping);
	}

	/*
	 * add system time zones that we mapped to: adding them ensures
	 * that the VCALENDAR remains consistent
	 */
	g_hash_table_foreach (systemtzids, addsystemtz, vcalendar);

	goto done;
 nomem:
	/* set gerror for "out of memory" if possible, otherwise abort via g_error() */
	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_OTHER_ERROR, "out of memory");
	if (error && !*error) {
		g_error ("%s: out of memory, cannot proceed - sorry!", G_STRFUNC);
	}
 failed:
	/* gerror should have been set already */
	success = FALSE;
 done:
	if (mapping)
		g_hash_table_destroy (mapping);

	if (systemtzids)
		g_hash_table_destroy (systemtzids);

	g_clear_object (&zone);
	g_free (tzid);
	g_free (zonestr);
	g_free (buffer);
	g_free (key);
	g_free (value);

	return success;
}

/**
 * e_cal_client_tzlookup_cb:
 * @tzid: ID of the timezone to lookup
 * @ecalclient: (type ECalClient): a valid #ECalClient pointer
 * @cancellable: an optional #GCancellable to use, or %NULL
 * @error: an error description in case of a failure
 *
 * An implementation of the #ECalRecurResolveTimezoneCb callback which clients
 * can use. Calls e_cal_client_get_timezone_sync().
 *
 * The returned timezone object, if not %NULL, is owned by the @ecalclient.
 *
 * Returns: (transfer none) (nullable): A timezone object, or %NULL on failure
 *    or when not found.
 *
 * Since: 3.34
 **/
ICalTimezone *
e_cal_client_tzlookup_cb (const gchar *tzid,
			  gpointer ecalclient,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalClient *cal_client = ecalclient;
	ICalTimezone *zone = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (cal_client), NULL);

	if (e_cal_client_get_timezone_sync (cal_client, tzid, &zone, cancellable, &local_error)) {
		g_warn_if_fail (local_error == NULL);
		return zone;
	}

	if (g_error_matches (local_error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
		/* We had to trigger this error to check for the
		 * timezone existance, clear it and return NULL. */
		g_clear_error (&local_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return NULL;
}

G_DEFINE_BOXED_TYPE (ECalClientTzlookupICalCompData, e_cal_client_tzlookup_icalcomp_data, e_cal_client_tzlookup_icalcomp_data_copy, e_cal_client_tzlookup_icalcomp_data_free)

struct _ECalClientTzlookupICalCompData {
	ICalComponent *icomp;
	GHashTable *tzcache; /* gchar *tzid ~> ICalTimezone * */
};

/**
 * e_cal_client_tzlookup_icalcomp_data_new:
 * @icomp: an #ICalComponent
 *
 * Constructs a new #ECalClientTzlookupICalCompData, which can
 * be used as a lookup_data argument of e_cal_client_tzlookup_icalcomp_cb().
 * Free it with e_cal_client_tzlookup_icalcomp_data_free(), when no longer needed.
 *
 * Returns: (transfer full): a new #ECalClientTzlookupICalCompData
 *
 * Since: 3.34
 **/
ECalClientTzlookupICalCompData *
e_cal_client_tzlookup_icalcomp_data_new (ICalComponent *icomp)
{
	ECalClientTzlookupICalCompData *lookup_data;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (icomp), NULL);

	lookup_data = g_slice_new0 (ECalClientTzlookupICalCompData);
	lookup_data->icomp = g_object_ref (icomp);
	lookup_data->tzcache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	return lookup_data;
}

/**
 * e_cal_client_tzlookup_icalcomp_data_copy:
 * @lookup_data: (nullable): source #ECalClientTzlookupICalCompData, or %NULL
 *
 * Copies given #ECalClientTzlookupICalCompData structure.
 * When the @lookup_data is %NULL, simply returns %NULL as well.
 *
 * Returns: (transfer full) (nullable): copy of the @lookup_data. Free the returned structure
 *    with e_cal_client_tzlookup_icalcomp_data_free(), when no longer needed.
 *
 * Since: 3.34
 **/
ECalClientTzlookupICalCompData *
e_cal_client_tzlookup_icalcomp_data_copy (const ECalClientTzlookupICalCompData *lookup_data)
{
	if (!lookup_data)
		return NULL;

	return e_cal_client_tzlookup_icalcomp_data_new (
		e_cal_client_tzlookup_icalcomp_data_get_icalcomponent (lookup_data));
}

/**
 * e_cal_client_tzlookup_icalcomp_data_free:
 * @lookup_data: (nullable): an #ECalClientTzlookupICalCompData, or %NULL
 *
 * Frees previously allocated #ECalClientTzlookupICalCompData structure
 * with e_cal_client_tzlookup_icalcomp_data_new() or e_cal_client_tzlookup_icalcomp_data_copy().
 * The function does nothing when @lookup_data is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_client_tzlookup_icalcomp_data_free (ECalClientTzlookupICalCompData *lookup_data)
{
	if (lookup_data) {
		g_clear_object (&lookup_data->icomp);
		g_hash_table_destroy (lookup_data->tzcache);
		g_slice_free (ECalClientTzlookupICalCompData, lookup_data);
	}
}

/**
 * e_cal_client_tzlookup_icalcomp_data_get_icalcomponent:
 * @lookup_data: an #ECalClientTzlookupICalCompData
 *
 * Returns: (transfer none): The #ICalComponent associated with the @lookup_data
 *
 * Since: 3.34
 **/
ICalComponent *
e_cal_client_tzlookup_icalcomp_data_get_icalcomponent (const ECalClientTzlookupICalCompData *lookup_data)
{
	g_return_val_if_fail (lookup_data != NULL, NULL);

	return lookup_data->icomp;
}

/**
 * e_cal_client_tzlookup_icalcomp_cb:
 * @tzid: ID of the timezone to lookup
 * @lookup_data: (type ECalClientTzlookupICalCompData): an #ECalClientTzlookupICalCompData
 *    strcture, created with e_cal_client_tzlookup_icalcomp_data_new()
 * @cancellable: an optional #GCancellable to use, or %NULL
 * @error: an error description in case of a failure
 *
 * An implementation of the #ECalRecurResolveTimezoneCb callback which
 * backends can use. Searches for the timezone in an %ICalComponent
 * associated with the @lookup_data %ECalClientTzlookupICalCompData.
 *
 * The returned timezone object is owned by the @lookup_data.
 *
 * Returns: (transfer none) (nullable): A timezone object, or %NULL, if
 *    not found inside @lookup_data 's #ICalComponent.
 *
 * Since: 3.34
 **/
ICalTimezone *
e_cal_client_tzlookup_icalcomp_cb (const gchar *tzid,
				   gpointer lookup_data,
				   GCancellable *cancellable,
				   GError **error)
{
	ECalClientTzlookupICalCompData *ld = lookup_data;
	ICalTimezone *zone;

	g_return_val_if_fail (tzid != NULL, NULL);
	g_return_val_if_fail (lookup_data != NULL, NULL);

	zone = g_hash_table_lookup (ld->tzcache, tzid);
	if (!zone) {
		zone = i_cal_component_get_timezone (e_cal_client_tzlookup_icalcomp_data_get_icalcomponent (ld), tzid);
		if (zone)
			g_hash_table_insert (ld->tzcache, g_strdup (tzid), zone);
	}

	return zone;
}
