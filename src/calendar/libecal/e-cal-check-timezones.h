/*
 * SPDX-FileCopyrightText: (C) 2008 Novell, Inc.
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Patrick Ohly <patrick.ohly@gmx.de>
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
