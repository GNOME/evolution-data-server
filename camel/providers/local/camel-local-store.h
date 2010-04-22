/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mbox-store.h : class for an mbox store */

/*
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

#ifndef CAMEL_LOCAL_STORE_H
#define CAMEL_LOCAL_STORE_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_LOCAL_STORE \
	(camel_local_store_get_type ())
#define CAMEL_LOCAL_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_LOCAL_STORE, CamelLocalStore))
#define CAMEL_LOCAL_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_LOCAL_STORE, CamelLocalStoreClass))
#define CAMEL_IS_LOCAL_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_LOCAL_STORE))
#define CAMEL_IS_LOCAL_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_LOCAL_STORE))
#define CAMEL_LOCAL_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_LOCAL_STORE, CamelLocalStoreClass))

G_BEGIN_DECLS

typedef struct _CamelLocalStore CamelLocalStore;
typedef struct _CamelLocalStoreClass CamelLocalStoreClass;

struct _CamelLocalStore {
	CamelStore parent;

	gchar *toplevel_dir;
};

struct _CamelLocalStoreClass {
	CamelStoreClass parent_class;

	gchar *(*get_full_path)(CamelLocalStore *ls, const gchar *full_name);
	gchar *(*get_meta_path)(CamelLocalStore *ls, const gchar *full_name, const gchar *ext);
};

GType camel_local_store_get_type (void);

const gchar *camel_local_store_get_toplevel_dir (CamelLocalStore *store);

#define camel_local_store_get_full_path(ls, name) \
	(CAMEL_LOCAL_STORE_GET_CLASS (ls)->get_full_path \
	(CAMEL_LOCAL_STORE (ls), (name)))
#define camel_local_store_get_meta_path(ls, name, ext) \
	(CAMEL_LOCAL_STORE_GET_CLASS (ls)->get_meta_path \
	(CAMEL_LOCAL_STORE (ls), (name), (ext)))

G_END_DECLS

#endif /* CAMEL_LOCAL_STORE_H */

