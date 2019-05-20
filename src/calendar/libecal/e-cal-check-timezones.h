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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_CHECK_TIMEZONES_H
#define E_CAL_CHECK_TIMEZONES_H

#include <libical-glib/libical-glib.h>
#include <glib.h>
#include <gio/gio.h>

#include <libecal/e-cal-recur.h>

G_BEGIN_DECLS

gboolean	e_cal_client_check_timezones_sync
						(ICalComponent *vcalendar,
						 GSList *icalcomps, /* ICalComponent * */
						 ECalRecurResolveTimezoneCb tzlookup,
						 gpointer tzlookup_data,
						 GCancellable *cancellable,
						 GError **error);

ICalTimezone *	e_cal_client_tzlookup_cb	(const gchar *tzid,
						 gpointer ecalclient, /* ECalClient * */
						 GCancellable *cancellable,
						 GError **error);

/**
 * ECalClientTzlookupICalCompData:
 *
 * Contains data used as lookup_data of e_cal_client_tzlookup_icalcomp_cb().
 *
 * Since: 3.34
 **/
typedef struct _ECalClientTzlookupICalCompData ECalClientTzlookupICalCompData;

GType		e_cal_client_tzlookup_icalcomp_data_get_type
						(void) G_GNUC_CONST;
ECalClientTzlookupICalCompData *
		e_cal_client_tzlookup_icalcomp_data_new
						(ICalComponent *icomp);
ECalClientTzlookupICalCompData *
		e_cal_client_tzlookup_icalcomp_data_copy
						(const ECalClientTzlookupICalCompData *lookup_data);
void		e_cal_client_tzlookup_icalcomp_data_free
						(ECalClientTzlookupICalCompData *lookup_data);
ICalComponent *	e_cal_client_tzlookup_icalcomp_data_get_icalcomponent
						(const ECalClientTzlookupICalCompData *lookup_data);

ICalTimezone *	e_cal_client_tzlookup_icalcomp_cb
						(const gchar *tzid,
						 gpointer lookup_data, /* ECalClientTzlookupICalCompData * */
						 GCancellable *cancellable,
						 GError **error);

const gchar *	e_cal_match_tzid		(const gchar *tzid);

G_END_DECLS

#endif /* E_CAL_CHECK_TIMEZONES_H */
