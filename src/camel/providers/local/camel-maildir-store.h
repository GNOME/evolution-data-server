/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_MAILDIR_STORE_H
#define CAMEL_MAILDIR_STORE_H

#include "camel-local-store.h"

/* Standard GObject macros */
#define CAMEL_TYPE_MAILDIR_STORE \
	(camel_maildir_store_get_type ())
#define CAMEL_MAILDIR_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAILDIR_STORE, CamelMaildirStore))
#define CAMEL_MAILDIR_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAILDIR_STORE, CamelMaildirStoreClass))
#define CAMEL_IS_MAILDIR_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAILDIR_STORE))
#define CAMEL_IS_MAILDIR_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAILDIR_STORE))
#define CAMEL_MAILDIR_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAILDIR_STORE, CamelMaildirStoreClass))

G_BEGIN_DECLS

typedef struct _CamelMaildirStore CamelMaildirStore;
typedef struct _CamelMaildirStoreClass CamelMaildirStoreClass;
typedef struct _CamelMaildirStorePrivate CamelMaildirStorePrivate;

struct _CamelMaildirStore {
	CamelLocalStore parent;
	CamelMaildirStorePrivate *priv;
};

struct _CamelMaildirStoreClass {
	CamelLocalStoreClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_maildir_store_get_type (void);
gchar camel_maildir_store_get_filename_flag_sep (CamelMaildirStore *maildir_store);

G_END_DECLS

#endif /* CAMEL_MAILDIR_STORE_H */
