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



#ifndef E_CAL_BACKEND_MAPI_H
#define E_CAL_BACKEND_MAPI_H

#include <libedata-cal/e-cal-backend.h>
#include <libedata-cal/e-cal-backend-sync.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-factory.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include "exchange-mapi-connection.h"
#include "exchange-mapi-folder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserver/e-url.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_MAPI            (e_cal_backend_mapi_get_type ())
#define E_CAL_BACKEND_MAPI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_MAPI,	ECalBackendMAPI))
#define E_CAL_BACKEND_MAPI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_MAPI,	ECalBackendMAPIClass))
#define E_IS_CAL_BACKEND_MAPI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_MAPI))
#define E_IS_CAL_BACKEND_MAPI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_MAPI))

typedef struct _ECalBackendMAPI        ECalBackendMAPI;
typedef struct _ECalBackendMAPIClass   ECalBackendMAPIClass;

typedef struct _ECalBackendMAPIPrivate ECalBackendMAPIPrivate;

struct _ECalBackendMAPI {
	ECalBackendSync backend;

	/* Private data */
	ECalBackendMAPIPrivate *priv;
};

struct _ECalBackendMAPIClass {
	ECalBackendSyncClass parent_class;
};

GType	e_cal_backend_mapi_get_type(void);

G_END_DECLS

#endif
