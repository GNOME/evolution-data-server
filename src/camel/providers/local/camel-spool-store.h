/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_SPOOL_STORE_H
#define CAMEL_SPOOL_STORE_H

#include "camel-mbox-store.h"

/* Standard GObject macros */
#define CAMEL_TYPE_SPOOL_STORE \
	(camel_spool_store_get_type ())
#define CAMEL_SPOOL_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SPOOL_STORE, CamelSpoolStore))
#define CAMEL_SPOOL_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SPOOL_STORE, CamelSpoolStoreClass))
#define CAMEL_IS_SPOOL_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SPOOL_STORE))
#define CAMEL_IS_SPOOL_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SPOOL_STORE))
#define CAMEL_SPOOL_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SPOOL_STORE, CamelSpoolStoreClass))

G_BEGIN_DECLS

typedef struct _CamelSpoolStore CamelSpoolStore;
typedef struct _CamelSpoolStoreClass CamelSpoolStoreClass;
typedef struct _CamelSpoolStorePrivate CamelSpoolStorePrivate;

struct _CamelSpoolStore {
	CamelMboxStore parent;
	CamelSpoolStorePrivate *priv;
};

struct _CamelSpoolStoreClass {
	CamelMboxStoreClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_spool_store_get_type	(void);

G_END_DECLS

#endif /* CAMEL_SPOOL_STORE_H */

