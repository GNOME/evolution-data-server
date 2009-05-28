/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Evolution calendar recurrence rule functions
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Author: Damon Chaplin <damon@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef E_CAL_RECUR_H
#define E_CAL_RECUR_H

#include <glib.h>
#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

typedef gboolean (* ECalRecurInstanceFn) (ECalComponent *comp,
					 time_t        instance_start,
					 time_t        instance_end,
					 gpointer      data);

typedef icaltimezone* (* ECalRecurResolveTimezoneFn)	(const gchar   *tzid,
							 gpointer      data);

void	e_cal_recur_generate_instances	(ECalComponent		*comp,
					 time_t			 start,
					 time_t			 end,
					 ECalRecurInstanceFn	 cb,
					 gpointer                cb_data,
					 ECalRecurResolveTimezoneFn tz_cb,
					 gpointer		   tz_cb_data,
					 icaltimezone		*default_timezone);

/* Localized nth-day-of-month strings. (Use with _() ) */
#ifdef G_OS_WIN32
extern const gchar **e_cal_get_recur_nth (void);
#define e_cal_recur_nth (e_cal_get_recur_nth ())
#else
extern const gchar *e_cal_recur_nth[31];
#endif

G_END_DECLS

#endif
