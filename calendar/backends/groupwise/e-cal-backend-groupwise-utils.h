/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef E_CAL_BACKEND_GROUPWISE_UTILS_H
#define E_CAL_BACKEND_GROUPWISE_UTILS_H

#include <e-gw-connection.h>
#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

/*
 * Items management
 */
EGwItem       *e_gw_item_new_from_cal_component (const char *container, const icaltimezone *default_zone, ECalComponent *comp);
ECalComponent *e_gw_item_to_cal_component (EGwItem *item, icaltimezone *default_zone);
void          e_gw_item_set_changes (EGwItem *item, EGwItem *cached_item);

/*
 * Connection-related utility functions
 */
EGwConnectionStatus e_gw_connection_create_appointment (EGwConnection *cnc, const char *container, icaltimezone *default_zone, ECalComponent *comp, GSList **id_list);
EGwConnectionStatus e_gw_connection_send_appointment (EGwConnection *cnc, const char *container, ECalComponent *comp, icalproperty_method method);
EGwConnectionStatus e_gw_connection_get_freebusy_info (EGwConnection *cnc, GList *users, time_t start, time_t end, GList **freebusy, icaltimezone *default_zone);

/*
 * Component related utility functions
 */
  
const char *e_cal_component_get_gw_id (ECalComponent *comp);
G_END_DECLS

#endif
