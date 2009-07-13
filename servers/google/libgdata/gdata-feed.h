/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
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

#ifndef _GDATA_FEED_H_
#define _GDATA_FEED_H_

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GDATA_TYPE_FEED           (gdata_feed_get_type())
#define GDATA_FEED(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), GDATA_TYPE_FEED, GDataFeed))
#define GDATA_FEED_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GDATA_TYPE_FEED, GDataFeedClass))
#define GDATA_IS_FEED(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GDATA_TYPE_FEED))
#define GDATA_IS_FEED_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), GDATA_TYPE_FEED))
#define GDATA_FEED_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GDATA_TYPE_FEED, GDataFeedClass))

typedef struct _GDataFeed GDataFeed;
typedef struct _GDataFeedClass GDataFeedClass;
typedef struct _GDataFeedPrivate GDataFeedPrivate;

struct _GDataFeed {

  GObject parent;

  /* private */
  GDataFeedPrivate *priv;
};

struct _GDataFeedClass {

  GObjectClass parent_class;
  /* class members */

};

GType gdata_feed_get_type(void);

/*** API ***/

GDataFeed * gdata_feed_new(void);

GDataFeed * gdata_feed_new_from_xml(const gchar *feedXML, const gint length);

gchar * gdata_feed_generate_xml(GDataFeed *feed);

gchar * gdata_feed_get_updated (GDataFeed *feed);

GSList * gdata_feed_get_entries (GDataFeed *feed);

const gchar *gdata_feed_get_timezone (GDataFeed *feed);

#endif
