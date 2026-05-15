/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_VEE_STORE_H
#define CAMEL_VEE_STORE_H

#include <camel/camel-store.h>

/* Standard GObject macros */
#define CAMEL_TYPE_VEE_STORE \
	(camel_vee_store_get_type ())
#define CAMEL_VEE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_VEE_STORE, CamelVeeStore))
#define CAMEL_VEE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_VEE_STORE, CamelVeeStoreClass))
#define CAMEL_IS_VEE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_VEE_STORE))
#define CAMEL_IS_VEE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_VEE_STORE))
#define CAMEL_VEE_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_VEE_STORE, CamelVeeStoreClass))

G_BEGIN_DECLS

typedef struct _CamelVeeStore CamelVeeStore;
typedef struct _CamelVeeStorePrivate CamelVeeStorePrivate;
typedef struct _CamelVeeStoreClass CamelVeeStoreClass;

struct _CamelVeeStore {
	CamelStore parent;

	CamelVeeStorePrivate *priv;
};

struct _CamelVeeStoreClass {
	CamelStoreClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType			camel_vee_store_get_type			(void);

G_END_DECLS

#endif /* CAMEL_VEE_STORE_H */
