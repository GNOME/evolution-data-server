/* Evolution calendar - caldav backend
 *
 * Copyright (C) 2005 Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Christian Kellner <gicmo@gnome.org> 
 */

#ifndef E_CAL_BACKEND_CALDAV_H
#define E_CAL_BACKEND_CALDAV_H

#include <libedata-cal/e-cal-backend-sync.h>

G_BEGIN_DECLS


#define E_TYPE_CAL_BACKEND_CALDAV             (e_cal_backend_caldav_get_type ())
#define E_CAL_BACKEND_CALDAV(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAV))
#define E_CAL_BACKEND_CALDAV_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAVClass))
#define E_IS_CAL_BACKEND_CALDAV(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_CALDAV))
#define E_IS_CAL_BACKEND_CALDAV_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_CALDAV))
#define E_CAL_BACKEND_CALDAV_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAVPrivate))

typedef struct _ECalBackendCalDAV ECalBackendCalDAV;
typedef struct _ECalBackendCalDAVClass ECalBackendCalDAVClass;

typedef struct _ECalBackendCalDAVPrivate ECalBackendCalDAVPrivate;

struct _ECalBackendCalDAV {
	   ECalBackendSync backend;
};

struct _ECalBackendCalDAVClass {
	   ECalBackendSyncClass parent_class;
};

GType       e_cal_backend_caldav_get_type      (void);


G_END_DECLS

#endif /* E_CAL_BACKEND_CALDAV_H */
