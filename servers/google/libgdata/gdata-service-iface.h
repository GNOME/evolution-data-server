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

#ifndef _GDATA_SERVICE_H_
#define _GDATA_SERVICE_H_

#include <glib.h>
#include <glib-object.h>
#include <libsoup/soup.h>

#include "gdata-feed.h"
#include "gdata-entry.h"

G_BEGIN_DECLS

#define GDATA_TYPE_SERVICE           (gdata_service_get_type())
#define GDATA_SERVICE(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), GDATA_TYPE_SERVICE, GDataService))
#define GDATA_SERVICE_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GDATA_TYPE_SERVICE, GDataServiceClass))
#define GDATA_IS_SERVICE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GDATA_TYPE_SERVICE))
#define GDATA_IS_SERVICE_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), GDATA_TYPE_SERVICE))
#define GDATA_SERVICE_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE((obj), GDATA_TYPE_SERVICE, GDataServiceIface))

typedef struct _GDataService GDataService;
typedef struct _GDataServiceIface GDataServiceIface;

struct _GDataServiceIface {

  GTypeInterface parent;

  /* Public Methods */
  void         (*set_proxy) (GDataService *self, SoupURI *proxy);
  void         (*set_credentials)(GDataService *self, const gchar *username, const gchar *password);
  GDataFeed *  (*get_feed)    (GDataService *self, const gchar *feed_getURL, GError **error);
  GDataEntry*  (*insert_entry)(GDataService *self, const gchar *feed_postURL, GDataEntry *entry, GError **error);
  GDataEntry*  (*get_entry)   (GDataService *self, const gchar *entry_getURL, GError **error);
  GDataEntry*  (*update_entry)(GDataService *self, GDataEntry *entry, GError **error);
  GDataEntry*  (*update_entry_with_link)(GDataService *self, GDataEntry *entry, const gchar *link, GError **error);
  gboolean     (*delete_entry)(GDataService *self, GDataEntry *entry, GError **error);
};

GType gdata_service_get_type(void);

/* Function Prototypes */
void        gdata_service_set_proxy (GDataService *self, SoupURI *proxy);
void        gdata_service_set_credentials(GDataService *self, const gchar *username, const gchar *password);

GDataFeed*  gdata_service_get_feed(GDataService *self, const gchar *feed_getURL, GError **error);

GDataEntry* gdata_service_insert_entry(GDataService *self, const gchar *feed_postURL, GDataEntry *entry, GError **error);

GDataEntry* gdata_service_get_entry(GDataService *self, const gchar *entry_getURL, GError **error);

GDataEntry* gdata_service_update_entry(GDataService *self, GDataEntry *entry, GError **error);

GDataEntry* gdata_service_update_entry_with_link(GDataService *self, GDataEntry *entry, gchar *link, GError **error);

gboolean    gdata_service_delete_entry(GDataService *self, GDataEntry *entry, GError **error);

G_END_DECLS

#endif
