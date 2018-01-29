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

#include <libical/ical.h>
#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

typedef icaltimezone * (* ECalRecurResolveTimezoneCb)	(const gchar *tzid,
							 gpointer user_data,
							 GCancellable *cancellable,
							 GError **error);

typedef gboolean (* ECalRecurInstanceCb)		(icalcomponent *comp,
							 struct icaltimetype instance_start,
							 struct icaltimetype instance_end,
							 gpointer user_data,
							 GCancellable *cancellable,
							 GError **error);

gboolean	e_cal_recur_generate_instances_sync	(icalcomponent *comp,
							 struct icaltimetype interval_start,
							 struct icaltimetype interval_end,
							 ECalRecurInstanceCb callback,
							 gpointer callback_user_data,
							 ECalRecurResolveTimezoneCb get_tz_callback,
							 gpointer get_tz_callback_user_data,
							 icaltimezone *default_timezone,
							 GCancellable *cancellable,
							 GError **error);

typedef gboolean (* ECalRecurInstanceFn) (ECalComponent *comp,
					 time_t        instance_start,
					 time_t        instance_end,
					 gpointer      user_data);

typedef icaltimezone * (* ECalRecurResolveTimezoneFn)	(const gchar   *tzid,
							 gpointer      data);

void	e_cal_recur_generate_instances	(ECalComponent		*comp,
					 time_t			 start,
					 time_t			 end,
					 ECalRecurInstanceFn	 cb,
					 gpointer                cb_data,
					 ECalRecurResolveTimezoneFn tz_cb,
					 gpointer		   tz_cb_data,
					 icaltimezone		*default_timezone);

time_t
e_cal_recur_obtain_enddate (struct icalrecurrencetype *ir,
                            icalproperty *prop,
                            icaltimezone *zone,
                            gboolean convert_end_date);

gboolean
e_cal_recur_ensure_end_dates (ECalComponent	*comp,
			    gboolean		 refresh,
			    ECalRecurResolveTimezoneFn  tz_cb,
			    gpointer		 tz_cb_data);

#ifndef EDS_DISABLE_DEPRECATED

/* Localized nth-day-of-month strings. (Use with _() ) */
#ifdef G_OS_WIN32
extern const gchar **e_cal_get_recur_nth (void);
#define e_cal_recur_nth (e_cal_get_recur_nth ())
#else
extern const gchar *e_cal_recur_nth[31];
#endif

#endif /* EDS_DISABLE_DEPRECATED */

const gchar *		e_cal_recur_get_localized_nth		(gint nth);

G_END_DECLS

#endif
