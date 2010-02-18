/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-store.h : class for an groupwise store */

/*
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
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

#ifndef CAMEL_GROUPWISE_STORE_H
#define CAMEL_GROUPWISE_STORE_H 1

#include <camel/camel-store.h>
#include <camel/camel-offline-store.h>
#include "camel-groupwise-store-summary.h"

#include <e-gw-connection.h>
#include <e-gw-container.h>

#define CAMEL_GROUPWISE_STORE_TYPE     (camel_groupwise_store_get_type ())
#define CAMEL_GROUPWISE_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_GROUPWISE_STORE_TYPE, CamelGroupwiseStore))
#define CAMEL_GROUPWISE_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_GROUPWISE_STORE_TYPE, CamelGroupwiseStoreClass))
#define CAMEL_IS_GROUPWISE_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_GROUPWISE_STORE_TYPE))

#define GW_PARAM_FILTER_INBOX		(1 << 0)

G_BEGIN_DECLS

typedef struct _CamelGroupwiseStore CamelGroupwiseStore;
typedef struct _CamelGroupwiseStoreClass CamelGroupwiseStoreClass;
typedef struct _CamelGroupwiseStorePrivate CamelGroupwiseStorePrivate;

struct _CamelGroupwiseStore {
	CamelOfflineStore parent_object;

	struct _CamelGroupwiseStoreSummary *summary;

	gchar *root_container;
	CamelGroupwiseStorePrivate *priv;
	CamelFolder *current_folder;

	/* the parameters field is not to be included not. probably for 2.6*/
	/*guint32 parameters;*/
	time_t refresh_stamp;
};

struct _CamelGroupwiseStoreClass {
	CamelOfflineStoreClass parent_class;
};

/* Standard Camel function */
CamelType camel_groupwise_store_get_type (void);
gchar * groupwise_get_name(CamelService *service, gboolean brief);

/*IMplemented*/
const gchar *camel_groupwise_store_container_id_lookup (CamelGroupwiseStore *gw_store, const gchar *folder_name);
const gchar *camel_groupwise_store_folder_lookup (CamelGroupwiseStore *gw_store, const gchar *container_id);
EGwConnection *cnc_lookup (CamelGroupwiseStorePrivate *priv);
gchar *storage_path_lookup (CamelGroupwiseStorePrivate *priv);
const gchar *groupwise_base_url_lookup (CamelGroupwiseStorePrivate *priv);
CamelFolderInfo * create_junk_folder (CamelStore *store);
gboolean camel_groupwise_store_connected (CamelGroupwiseStore *store, CamelException *ex);
void gw_store_reload_folder (CamelGroupwiseStore *store, CamelFolder *folder, guint32 flags, CamelException *ex);
void groupwise_store_set_current_folder (CamelGroupwiseStore *groupwise_store, CamelFolder *folder);

G_END_DECLS

#endif /* CAMEL_GROUPWISE_STORE_H */
