/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
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
 *
 */

#ifndef _GDATA_GOOGLE_SERVICE_H_
#define _GDATA_GOOGLE_SERVICE_H_

#include <glib.h>
#include <glib-object.h>

#include "gdata-feed.h"
#include "gdata-entry.h"

G_BEGIN_DECLS

#define GDATA_TYPE_GOOGLE_SERVICE           (gdata_google_service_get_type())
#define GDATA_GOOGLE_SERVICE(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), GDATA_TYPE_GOOGLE_SERVICE, GDataGoogleService))
#define GDATA_GOOGLE_SERVICE_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GDATA_TYPE_GOOGLE_SERVICE, GDataGoogleServiceClass))
#define GDATA_IS_GOOGLE_SERVICE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GDATA_TYPE_GOOGLE_SERVICE))
#define GDATA_IS_GOOGLE_SERVICE_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), GDATA_TYPE_GOOGLE_SERVICE))
#define GDATA_GOOGLE_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_INTERFACE((obj), GDATA_TYPE_GOOGLE_SERVICE, GDataGoogleServiceClass))

#define GDATA_GOOGLE_ERROR gdata_google_error_quark ()

typedef struct _GDataGoogleService GDataGoogleService;
typedef struct _GDataGoogleServiceClass GDataGoogleServiceClass;
typedef struct _GDataGoogleServicePrivate GDataGoogleServicePrivate;

struct _GDataGoogleService {
  GObject parent;

  /* private */

};

struct _GDataGoogleServiceClass {
  GObjectClass parent_class;

  /* Public Methods - Inherited from GDATA_SERVICE_IFACE */
};

GType  gdata_google_service_get_type(void);
GQuark gdata_google_error_quark(void);

/**API******/

GDataGoogleService * gdata_google_service_new(const gchar *serviceName, const gchar *agent);
gboolean gdata_google_service_authenticate (GDataGoogleService *service, GError **error);

G_END_DECLS

#endif

