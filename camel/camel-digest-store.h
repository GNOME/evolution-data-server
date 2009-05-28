/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef __CAMEL_DIGEST_STORE_H__
#define __CAMEL_DIGEST_STORE_H__

#include <glib.h>
#include <camel/camel-store.h>

#define CAMEL_DIGEST_STORE(obj)         CAMEL_CHECK_CAST (obj, camel_digest_store_get_type (), CamelDigestStore)
#define CAMEL_DIGEST_STORE_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_digest_store_get_type (), CamelDigestStoreClass)
#define CAMEL_IS_DIGEST_STORE(obj)      CAMEL_CHECK_TYPE (obj, camel_digest_store_get_type ())

G_BEGIN_DECLS

typedef struct _CamelDigestStoreClass CamelDigestStoreClass;

struct _CamelDigestStore {
	CamelStore parent;

};

struct _CamelDigestStoreClass {
	CamelStoreClass parent_class;

};

CamelType camel_digest_store_get_type (void);

CamelStore *camel_digest_store_new (const gchar *url);

G_END_DECLS

#endif /* __CAMEL_DIGEST_STORE_H__ */

#endif /* CAMEL_DISABLE_DEPRECATED */
