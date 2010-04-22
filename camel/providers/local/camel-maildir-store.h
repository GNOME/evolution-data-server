/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

struct _CamelMaildirStore {
	CamelLocalStore parent;
};

struct _CamelMaildirStoreClass {
	CamelLocalStoreClass parent_class;
};

GType camel_maildir_store_get_type(void);

G_END_DECLS

#endif /* CAMEL_MAILDIR_STORE_H */
