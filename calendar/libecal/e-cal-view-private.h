/* Evolution calendar - Live view client object
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_VIEW_PRIVATE_H
#define E_CAL_VIEW_PRIVATE_H

#include <libecal/e-cal-types.h>
#include <libecal/e-cal-view.h>
#include <libecal/e-cal-view-listener.h>

#include "Evolution-DataServer-Calendar.h"

G_BEGIN_DECLS

ECalView *e_cal_view_new (GNOME_Evolution_Calendar_CalView corba_view, ECalViewListener *listener, struct _ECal *client);

G_END_DECLS

#endif
