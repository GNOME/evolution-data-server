/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-store.h : class for an pop3 store */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_POP3_STORE_H
#define CAMEL_POP3_STORE_H

#include <camel/camel.h>

#include "camel-pop3-engine.h"

/* Standard GObject macros */
#define CAMEL_TYPE_POP3_STORE \
	(camel_pop3_store_get_type ())
#define CAMEL_POP3_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_POP3_STORE, CamelPOP3Store))
#define CAMEL_POP3_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_POP3_STORE, CamelPOP3StoreClass))
#define CAMEL_IS_POP3_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_POP3_STORE))
#define CAMEL_IS_POP3_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_POP3_STORE))
#define CAMEL_POP3_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_POP3_STORE, CamelPOP3StoreClass))

G_BEGIN_DECLS

typedef struct _CamelPOP3Store CamelPOP3Store;
typedef struct _CamelPOP3StoreClass CamelPOP3StoreClass;
typedef struct _CamelPOP3StorePrivate CamelPOP3StorePrivate;

struct _CamelPOP3Store {
	CamelStore parent;
	CamelPOP3StorePrivate *priv;

	CamelPOP3Engine *engine; /* pop processing engine */
	CamelDataCache *cache;
};

struct _CamelPOP3StoreClass {
	CamelStoreClass parent_class;
};

GType		camel_pop3_store_get_type	(void);
gboolean	camel_pop3_store_expunge	(CamelPOP3Store *store,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_POP3_STORE_H */

