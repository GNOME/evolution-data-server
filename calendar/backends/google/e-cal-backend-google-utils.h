/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Philip Withnall <philip@tecnocode.co.uk>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef E_CAL_BACKEND_GOOGLE_UTILS_H
#define E_CAL_BACKEND_GOOGLE_UTILS_H
#endif
#include <e-cal-backend-google.h>
#include <libecal/e-cal-component.h>
#include <gdata/services/calendar/gdata-calendar-event.h>

ECalComponent *
e_gdata_event_to_cal_component (GDataCalendarEvent *event, ECalBackendGoogle *cbgo);

GDataCalendarEvent *
e_gdata_event_from_cal_component (ECalBackendGoogle *cbgo, ECalComponent *comp);

void
e_gdata_event_update_from_cal_component (ECalBackendGoogle *cbgo, GDataCalendarEvent *event, ECalComponent *comp);

gpointer
e_cal_backend_google_utils_update (gpointer handle);

ECalBackendSyncStatus
e_cal_backend_google_utils_connect (ECalBackendGoogle *cbgo);

