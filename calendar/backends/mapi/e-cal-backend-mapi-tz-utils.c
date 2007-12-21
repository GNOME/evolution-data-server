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



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-mapi-tz-utils.h"

static gchar **mapi_tz_names = NULL;
static guint n_mapi_tz_names = G_N_ELEMENTS (mapi_tz_names);

static gchar **ical_tz_names = NULL;
static guint n_ical_tz_names = G_N_ELEMENTS (ical_tz_names);

const gchar *
e_cal_backend_mapi_tz_util_get_mapi_equivalent (const gchar *ical_tzid)
{
	guint i;

	g_return_val_if_fail ((n_ical_tz_names == n_mapi_tz_names), NULL);

	for (i=0; i < n_ical_tz_names; ++i)
		if (!g_ascii_strcasecmp (ical_tzid, ical_tz_names[i]))
			return mapi_tz_names[i];

	return NULL;
}

const gchar *
e_cal_backend_mapi_tz_util_get_ical_equivalent (const gchar *mapi_tzid)
{
	guint i;

	g_return_val_if_fail ((n_mapi_tz_names == n_ical_tz_names), NULL);

	for (i=0; i < n_mapi_tz_names; ++i)
		if (!g_ascii_strcasecmp (mapi_tzid, mapi_tz_names[i]))
			return ical_tz_names[i];

	return NULL;
}

gboolean
e_cal_backend_mapi_tz_util_populate ()
{
	if (mapi_tz_names && ical_tz_names && (n_mapi_tz_names == n_ical_tz_names))
		return TRUE;

	/* Reset all the values */
	*mapi_tz_names = NULL;
	n_mapi_tz_names = 0;
	*ical_tz_names = NULL;
	n_ical_tz_names = 0;

	/* FIXME: finish this function dude :) */

	return TRUE;
}

