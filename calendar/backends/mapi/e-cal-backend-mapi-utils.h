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


#ifndef E_CAL_BACKEND_MAPI_UTILS_H
#define E_CAL_BACKEND_MAPI_UTILS_H

#include <e-cal-backend-mapi.h>
#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

void
e_cal_backend_mapi_util_fetch_attachments (ECalBackendMAPI *cbmapi, ECalComponent *comp, GSList **attach_list);
//void
//set_attachments_to_cal_component (ECalBackendMAPI *cbmapi, ECalComponent *comp, GSList *attach_list);

ECalComponent *
e_cal_backend_mapi_props_to_comp (ECalBackendMAPI *cbmapi, const gchar *mid, struct mapi_SPropValue_array *properties, 
				  GSList *streams, GSList *recipients, GSList *attachments, const icaltimezone *default_zone);

gboolean
mapi_cal_build_name_id (struct mapi_nameid *nameid, gpointer data);

int
mapi_cal_build_props (struct SPropValue **value, struct SPropTagArray *proptag_array, gpointer data);

G_END_DECLS

#endif
