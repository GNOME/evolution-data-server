/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-data-cache.h: Class for a Camel filesystem cache
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_DATA_CACHE_H
#define CAMEL_DATA_CACHE_H 1

#include <glib.h>

#include <camel/camel-stream.h>
#include <camel/camel-exception.h>

#define CAMEL_DATA_CACHE_TYPE     (camel_data_cache_get_type ())
#define CAMEL_DATA_CACHE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_DATA_CACHE_TYPE, CamelFolder))
#define CAMEL_DATA_CACHE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_DATA_CACHE_TYPE, CamelFolderClass))
#define CAMEL_IS_DATA_CACHE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_DATA_CACHE_TYPE))

G_BEGIN_DECLS

typedef struct _CamelDataCache CamelDataCache;
typedef struct _CamelDataCacheClass CamelDataCacheClass;

struct _CamelDataCache {
	CamelObject parent_object;

	struct _CamelDataCachePrivate *priv;

	gchar *path;
	guint32 flags;

	time_t expire_age;
	time_t expire_access;
};

struct _CamelDataCacheClass {
	CamelObjectClass parent_class;

	/* None are virtual yet */
#if 0
	/* Virtual methods */
	CamelStream *(*add)(CamelDataCache *cmc, const gchar *path, const gchar *key, CamelException *ex);
	CamelStream *(*get)(CamelDataCache *cmc, const gchar *path, const gchar *key, CamelException *ex);
	gint (*close)(CamelDataCache *cmc, CamelStream *stream, CamelException *ex);
	gint (*remove)(CamelDataCache *cmc, const gchar *path, const gchar *key, CamelException *ex);

	gint (*clear)(CamelDataCache *cmc, const gchar *path, CamelException *ex);
#endif
};

/* public methods */
CamelDataCache *camel_data_cache_new(const gchar *path, guint32 flags, CamelException *ex);

void camel_data_cache_set_expire_age(CamelDataCache *cdc, time_t when);
void camel_data_cache_set_expire_access(CamelDataCache *cdc, time_t when);

gint             camel_data_cache_rename(CamelDataCache *cache,
					const gchar *old, const gchar *new, CamelException *ex);

CamelStream    *camel_data_cache_add(CamelDataCache *cdc,
				     const gchar *path, const gchar *key, CamelException *ex);
CamelStream    *camel_data_cache_get(CamelDataCache *cdc,
				     const gchar *path, const gchar *key, CamelException *ex);
gint             camel_data_cache_remove(CamelDataCache *cdc,
					const gchar *path, const gchar *key, CamelException *ex);

gint             camel_data_cache_clear(CamelDataCache *cache,
				       const gchar *path, CamelException *ex);

gchar *         camel_data_cache_get_filename(CamelDataCache *cdc,
					      const gchar *path, const gchar *key, CamelException *ex);

/* Standard Camel function */
CamelType camel_data_cache_get_type (void);

G_END_DECLS

#endif /* CAMEL_DATA_CACHE_H */
