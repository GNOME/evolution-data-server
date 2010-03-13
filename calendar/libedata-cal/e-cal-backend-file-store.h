/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-file-store.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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

#ifndef _E_CAL_BACKEND_FILE_STORE
#define _E_CAL_BACKEND_FILE_STORE

#include <glib-object.h>
#include "e-cal-backend-store.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_FILE_STORE e_cal_backend_file_store_get_type()

#define E_CAL_BACKEND_FILE_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_FILE_STORE, ECalBackendFileStore))

#define E_CAL_BACKEND_FILE_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_FILE_STORE, ECalBackendFileStoreClass))

#define E_IS_CAL_BACKEND_FILE_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_FILE_STORE))

#define E_IS_CAL_BACKEND_FILE_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_FILE_STORE))

#define E_CAL_BACKEND_FILE_STORE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_BACKEND_FILE_STORE, ECalBackendFileStoreClass))

typedef struct _ECalBackendFileStorePrivate ECalBackendFileStorePrivate;

/**
 * ECalBackendFileStore:
 *
 * Since: 2.28
 **/
typedef struct {
	ECalBackendStore parent;
	ECalBackendFileStorePrivate *priv;
} ECalBackendFileStore;

typedef struct {
	ECalBackendStoreClass parent_class;
} ECalBackendFileStoreClass;

GType e_cal_backend_file_store_get_type (void);

ECalBackendFileStore* e_cal_backend_file_store_new (const gchar *uri, ECalSourceType source_type);

G_END_DECLS

#endif /* _E_CAL_BACKEND_FILE_STORE */
