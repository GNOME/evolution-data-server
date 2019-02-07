/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Evolution calendar recurrence rule functions
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Damon Chaplin <damon@ximian.com>
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_RECUR_H
#define E_CAL_RECUR_H

#include <glib.h>
#include <gio/gio.h>

#include <libical-glib/libical-glib.h>

#include <libecal/e-cal-component.h>
#include <libecal/e-cal-enums.h>

G_BEGIN_DECLS

/**
 * E_CAL_EVOLUTION_ENDDATE_PARAMETER:
 *
 * The X parameter name being used to store the enddate in RRULE and EXRULE properties.
 *
 * Since: 3.36
 **/
#define E_CAL_EVOLUTION_ENDDATE_PARAMETER	"X-EVOLUTION-ENDDATE"

/**
 * ECalRecurResolveTimezoneCb:
 * @tzid: timezone ID to resolve
 * @user_data: user data used for this callback
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Resolve timezone by its ID provided as @tzid. Free the returned object,
 * if not %NULL, with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a new #ICalTimezone object for @tzid,
 *    or %NULL, on error or if not found.
 *
 * Since: 3.36
 **/
typedef ICalTimezone * (* ECalRecurResolveTimezoneCb)	(const gchar *tzid,
							 gpointer user_data,
							 GCancellable *cancellable,
							 GError **error);

/**
 * ECalRecurInstanceCb:
 * @comp: an #ICalComponent
 * @instance_start: start time of an instance
 * @instance_end: end time of an instance
 * @user_data: user data used for this callback in e_cal_recur_generate_instances_sync()
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Callback used by e_cal_recur_generate_instances_sync(), called
 * for each instance of a (recurring) component within given time range.
 *
 * Returns: %TRUE, to continue recurrence generation, %FALSE to stop
 *
 * Since: 3.36
 **/
typedef gboolean (* ECalRecurInstanceCb)		(ICalComponent *comp,
							 ICalTimetype *instance_start,
							 ICalTimetype *instance_end,
							 gpointer user_data,
							 GCancellable *cancellable,
							 GError **error);

gboolean	e_cal_recur_generate_instances_sync	(ICalComponent *icalcomp,
							 ICalTimetype *interval_start,
							 ICalTimetype *interval_end,
							 ECalRecurInstanceCb callback,
							 gpointer callback_user_data,
							 ECalRecurResolveTimezoneCb get_tz_callback,
							 gpointer get_tz_callback_user_data,
							 ICalTimezone *default_timezone,
							 GCancellable *cancellable,
							 GError **error);

time_t		e_cal_recur_obtain_enddate		(ICalRecurrenceType *ir,
							 ICalProperty *prop,
							 ICalTimezone *zone,
							 gboolean convert_end_date);

gboolean	e_cal_recur_ensure_end_dates		(ECalComponent *comp,
							 gboolean refresh,
							 ECalRecurResolveTimezoneCb tz_cb,
							 gpointer tz_cb_data,
							 GCancellable *cancellable,
							 GError **error);

const gchar *	e_cal_recur_get_localized_nth		(gint nth);

gchar *		e_cal_recur_describe_recurrence		(ICalComponent *icalcomp,
							 GDateWeekday week_start_day,
							 guint32 flags); /* bit-or of ECalRecurDescribeRecurrenceFlags */

G_END_DECLS

#endif
