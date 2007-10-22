/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) Rémi L'Ecolier 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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



#ifndef E_CAL_BACKEND_OPENCHANGE_H
#define E_CAL_BACKEND_OPENCHANGE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>
/* #include <glib/gi18n-lib.h> */
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserver/e-url.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sync.h>
#include <libedata-cal/e-cal-backend-factory.h>
#include <libedata-cal/e-cal-backend.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include <glib-object.h>


#define OC_DEBUG(t) \
{\
	int	fd;\
	fd = open("/home/amne/debug3", O_RDWR | O_CREAT |O_APPEND, S_IRWXU); \
	write(fd, t, strlen(t));\
	close(fd);\
}\


  




typedef struct
{
	ECalBackendFactory	parent_object;
} ECalBackendOpenchangeFactory;

typedef struct
{
	ECalBackendFactoryClass	parent_class;
} ECalBackendOpenchangeFactoryClass;




#define E_TYPE_CAL_BACKEND_OPENCHANGE            (e_cal_backend_openchange_get_type ())
#define E_CAL_BACKEND_OPENCHANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_OPENCHANGE,	ECalBackendOpenchange))
#define E_CAL_BACKEND_OPENCHANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_OPENCHANGE,	ECalBackendOpenchangeClass))
#define E_IS_CAL_BACKEND_OPENCHANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_OPENCHANGE))
#define E_IS_CAL_BACKEND_OPENCHANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_OPENCHANGE))




typedef struct _ECalBackendOpenchange        ECalBackendOpenchange;
typedef struct _ECalBackendOpenchangeClass   ECalBackendOpenchangeClass;

typedef struct _ECalBackendOpenchangePrivate ECalBackendOpenchangePrivate;

struct _ECalBackendOpenchange {
	ECalBackendSync backend;

	/* Private data */
	ECalBackendOpenchangePrivate *priv;
};

struct _ECalBackendOpenchangeClass {
	ECalBackendSyncClass parent_class;
};




GType			e_cal_backend_openchange_get_type(void);



#endif
