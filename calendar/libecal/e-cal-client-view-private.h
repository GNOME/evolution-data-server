/* Evolution calendar - Live view client object
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef E_CAL_CLIENT_VIEW_PRIVATE_H
#define E_CAL_CLIENT_VIEW_PRIVATE_H

#include "libecal/e-cal-client-view.h"

G_BEGIN_DECLS

struct _EGdbusCalView;
struct _ECalClient;

ECalClientView *_e_cal_client_view_new (struct _ECalClient *client,  struct _EGdbusCalView *gdbus_calview);

G_END_DECLS

#endif
