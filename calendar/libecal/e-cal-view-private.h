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

#ifndef E_CAL_DISABLE_DEPRECATED

#ifndef E_CAL_VIEW_PRIVATE_H
#define E_CAL_VIEW_PRIVATE_H

#include <libecal/e-cal.h>
#include <libecal/e-cal-client-view.h>

G_BEGIN_DECLS

ECalView *	_e_cal_view_new			(ECal *cal,
						 ECalClientView *client_view);

G_END_DECLS

#endif /* E_CAL_VIEW_PRIVATE_H */

#endif /* E_CAL_DISABLE_DEPRECATED */
