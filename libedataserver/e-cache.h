/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cache.h
 *
 * Copyright (C) 2003 Novell, Inc.
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
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#ifndef E_CACHE_H
#define E_CACHE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define E_TYPE_CACHE            (e_cache_get_type ())
#define E_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CACHE, ECache))
#define E_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CACHE, ECacheClass))
#define E_IS_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CACHE))
#define E_IS_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CACHE))

typedef struct _ECachePrivate ECachePrivate;

typedef struct {
	GObject parent;
	ECachePrivate *priv;
} ECache;

typedef struct {
	GObjectClass parent_class;
} ECacheClass;

GType       e_cache_get_type (void);

ECache     *e_cache_new (const char *filename);

const char *e_cache_get_object (ECache *cache, const char *key);
gboolean    e_cache_add_object (ECache *cache, const char *key, const char *value);
gboolean    e_cache_replace_object (ECache *cache, const char *key, const char *new_value);
gboolean    e_cache_remove_object (ECache *cache, const char *key);

const char *e_cache_get_filename (ECache *cache);

G_END_DECLS

#endif
