/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
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
#include <servers/google/libgdata/gdata-entry.h>
#include <servers/google/libgdata/gdata-feed.h>
#include <servers/google/libgdata-google/gdata-google-service.h>
#include <servers/google/libgdata/gdata-service-iface.h>

ECalComponent *
e_go_item_to_cal_component (EGoItem *item, ECalBackendGoogle *cbgo);

void
e_go_item_set_entry (EGoItem *item, GDataEntry *entry);

GDataEntry *
e_go_item_get_entry (EGoItem *item);

EGoItem *
e_go_item_from_cal_component (ECalBackendGoogle *cbgo, ECalComponent *comp);

gpointer
e_cal_backend_google_utils_update (gpointer handle);

GDataEntry *
gdata_entry_get_entry_by_id (GSList *entries, const gchar *id);

ECalBackendSyncStatus
e_cal_backend_google_utils_connect (ECalBackendGoogle *cbgo);

