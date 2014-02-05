/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OFFLINE_STORE_H
#define CAMEL_OFFLINE_STORE_H

#include <camel/camel-store.h>

/* Standard GObject macros */
#define CAMEL_TYPE_OFFLINE_STORE \
	(camel_offline_store_get_type ())
#define CAMEL_OFFLINE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStore))
#define CAMEL_OFFLINE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStoreClass))
#define CAMEL_IS_OFFLINE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_OFFLINE_STORE))
#define CAMEL_IS_OFFLINE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_OFFLINE_STORE))
#define CAMEL_OFFLINE_STORE_GET_CLASS(obj) \
	(CAMEL_CHECK_GET_CLASS \
	((obj), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStoreClass))

G_BEGIN_DECLS

typedef struct _CamelOfflineStore CamelOfflineStore;
typedef struct _CamelOfflineStoreClass CamelOfflineStoreClass;
typedef struct _CamelOfflineStorePrivate CamelOfflineStorePrivate;

struct _CamelOfflineStore {
	CamelStore parent;
	CamelOfflineStorePrivate *priv;
};

struct _CamelOfflineStoreClass {
	CamelStoreClass parent_class;
};

GType		camel_offline_store_get_type (void);
gboolean	camel_offline_store_get_online	(CamelOfflineStore *store);
gboolean	camel_offline_store_set_online_sync
						(CamelOfflineStore *store,
						 gboolean online,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_offline_store_prepare_for_offline_sync
						(CamelOfflineStore *store,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_offline_store_requires_downsync
						(CamelOfflineStore *store);

G_END_DECLS

#endif /* CAMEL_OFFLINE_STORE_H */
