/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  A class to cache address book conents on local file system
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Sivaiah Nallagatla <snallagatla@ximian.com>
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

#ifndef E_BOOK_BACKEND_CACHE_H
#define E_BOOK_BACKEND_CACHE_H

#include "libebackend/e-file-cache.h"
#include <libebook/e-contact.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND_CACHE            (e_book_backend_cache_get_type ())
#define E_BOOK_BACKEND_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_BOOK_BACKEND_CACHE, EBookBackendCache))
#define E_BOOK_BACKEND_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_BOOK_BACKEND_CACHE, EBookBackendCacheClass))
#define E_IS_BOOK_BACKEND_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_BOOK_BACKEND_CACHE))
#define E_IS_BOOK_BACKEND_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_BOOK_BACKEND_CACHE))

typedef struct _EBookBackendCachePrivate EBookBackendCachePrivate;

typedef struct {
	EFileCache parent;
	EBookBackendCachePrivate *priv;
} EBookBackendCache;

typedef struct {
	EFileCacheClass parent_class;
} EBookBackendCacheClass;

GType e_book_backend_cache_get_type (void);
EBookBackendCache* e_book_backend_cache_new (const gchar *uri);
EContact* e_book_backend_cache_get_contact (EBookBackendCache *cache, const gchar *uid);
gboolean e_book_backend_cache_add_contact (EBookBackendCache *cache,
					   EContact *contact);
gboolean e_book_backend_cache_remove_contact (EBookBackendCache *cache,
					      const gchar *uid);
gboolean e_book_backend_cache_check_contact (EBookBackendCache *cache, const gchar *uid);
GList*   e_book_backend_cache_get_contacts (EBookBackendCache *cache, const gchar *query);
gboolean e_book_backend_cache_exists (const gchar *uri);
void     e_book_backend_cache_set_populated (EBookBackendCache *cache);
gboolean e_book_backend_cache_is_populated (EBookBackendCache *cache);
void e_book_backend_cache_set_time (EBookBackendCache *cache, const gchar *t);
gchar *e_book_backend_cache_get_time (EBookBackendCache *cache);
GPtrArray* e_book_backend_cache_search (EBookBackendCache *cache, const gchar *query);

G_END_DECLS

#endif
