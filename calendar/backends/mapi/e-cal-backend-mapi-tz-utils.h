/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifndef E_CAL_BACKEND_MAPI_TZ_UTILS_H
#define E_CAL_BACKEND_MAPI_TZ_UTILS_H
#endif

#include <glib.h>

const gchar *
e_cal_backend_mapi_tz_util_get_mapi_equivalent (const gchar *ical_tzid);

const gchar *
e_cal_backend_mapi_tz_util_get_ical_equivalent (const gchar *mapi_tzid);

gboolean
e_cal_backend_mapi_tz_util_populate ();

