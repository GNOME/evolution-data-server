/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
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

#ifndef E_CAL_BACKEND_GOOGLE_H
#define E_CAL_BACKEND_GOOGLE_H

#include <libedata-cal/e-cal-backend-sync.h>
#include <libedata-cal/e-cal-backend-cache.h>

#include <servers/google/libgdata/gdata-entry.h>
#include <servers/google/libgdata/gdata-feed.h>
#include <servers/google/libgdata-google/gdata-google-service.h>
#include <servers/google/libgdata/gdata-service-iface.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_GOOGLE 		(e_cal_backend_google_get_type ())
#define E_CAL_BACKEND_GOOGLE(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_GOOGLE, ECalBackendGoogle))
#define E_CAL_BACKEND_GOOGLE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_GOOGLE, ECalBackendGoogleClass))
#define E_IS_CAL_BACKEND_GOOGLE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_GOOGLE))
#define E_IS_CAL_BACKEND_GOOGLE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_GOOGLE))

typedef struct _ECalBackendGoogle ECalBackendGoogle;
typedef struct _ECalBackendGoogleClass ECalBackendGoogleClass;

typedef struct _ECalBackendGooglePrivate ECalBackendGooglePrivate;

struct _ECalBackendGoogle {
	ECalBackendSync Backend;

	/* private data */
	ECalBackendGooglePrivate *priv;
};

struct _ECalBackendGoogleClass {
	ECalBackendSyncClass parent_class;
};

struct _EGoItem {
	GDataEntry *entry;
	GDataFeed *feed;
};
typedef struct _EGoItem EGoItem;

GType e_cal_backend_google_get_type (void);
EGoItem * e_cal_backend_google_get_item (ECalBackendGoogle *cbgo);
GDataEntry * e_cal_backend_google_get_entry (ECalBackendGoogle *cbgo);
ECalBackendCache * e_cal_backend_google_get_cache (ECalBackendGoogle *cbgo);
GDataGoogleService * e_cal_backend_google_get_service (ECalBackendGoogle *cbgo);
gchar * e_cal_backend_google_get_uri (ECalBackendGoogle *cbgo);
icaltimezone * e_cal_backend_google_get_default_zone (ECalBackendGoogle *cbgo);
gboolean e_cal_backend_google_get_mode_changed (ECalBackendGoogle *cbgo);
gchar * e_cal_backend_google_get_username (ECalBackendGoogle *cbgo);
gchar * e_cal_backend_google_get_password (ECalBackendGoogle *cbgo);
gchar * e_cal_backend_google_get_local_attachments_store (ECalBackendGoogle *cbgo);
gint e_cal_backend_google_get_timeout_id (ECalBackendGoogle *cbgo);

void e_cal_backend_google_set_entry (ECalBackendGoogle *cbgo, GDataEntry *entry);
void e_cal_backend_google_set_cache (ECalBackendGoogle *cbgo, ECalBackendCache *cache);
void e_cal_backend_google_set_item (ECalBackendGoogle *cbgo, EGoItem *item);
void e_cal_backend_google_set_service (ECalBackendGoogle *cbgo, GDataGoogleService *service);
void e_cal_backend_google_set_uri (ECalBackendGoogle *cbgo, gchar *uri);
void e_cal_backend_google_set_item (ECalBackendGoogle *cbgo, EGoItem *item);
void e_cal_backend_google_set_mode_changed (ECalBackendGoogle *cbgo, gboolean mode_changed);
void e_cal_backend_google_set_username (ECalBackendGoogle *cbgo, gchar *username);
void e_cal_backend_google_set_password (ECalBackendGoogle *cbgo, gchar *password);
void e_cal_backend_google_set_timeout_id (ECalBackendGoogle *cbgo,gint timeout_id);

G_END_DECLS
#endif
