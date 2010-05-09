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
#define CAMEL_GROUPWISE_STORE_H

#include <camel/camel.h>

#include "camel-groupwise-store-summary.h"

#include <e-gw-connection.h>
#include <e-gw-container.h>

/* Standard GObject macros */
#define CAMEL_TYPE_GROUPWISE_STORE \
	(camel_groupwise_store_get_type ())
#define CAMEL_GROUPWISE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_GROUPWISE_STORE, CamelGroupwiseStore))
#define CAMEL_GROUPWISE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_GROUPWISE_STORE, CamelGroupwiseStoreClass))
#define CAMEL_IS_GROUPWISE_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_GROUPWISE_STORE))
#define CAMEL_IS_GROUPWISE_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_GROUPWISE_STORE))
#define CAMEL_GROUPWISE_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_GROUPWISE_STORE, CamelGroupwiseStoreClass))

#define GW_PARAM_FILTER_INBOX		(1 << 0)

G_BEGIN_DECLS

typedef struct _CamelGroupwiseStore CamelGroupwiseStore;
typedef struct _CamelGroupwiseStoreClass CamelGroupwiseStoreClass;
typedef struct _CamelGroupwiseStorePrivate CamelGroupwiseStorePrivate;

struct _CamelGroupwiseStore {
	CamelOfflineStore parent;

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

GType camel_groupwise_store_get_type (void);
gchar * groupwise_get_name(CamelService *service, gboolean brief);

/*IMplemented*/
const gchar *camel_groupwise_store_container_id_lookup (CamelGroupwiseStore *gw_store, const gchar *folder_name);
const gchar *camel_groupwise_store_folder_lookup (CamelGroupwiseStore *gw_store, const gchar *container_id);
EGwConnection *cnc_lookup (CamelGroupwiseStorePrivate *priv);
gchar *storage_path_lookup (CamelGroupwiseStorePrivate *priv);
const gchar *groupwise_base_url_lookup (CamelGroupwiseStorePrivate *priv);
CamelFolderInfo * create_junk_folder (CamelStore *store);
gboolean camel_groupwise_store_connected (CamelGroupwiseStore *store, GError **error);
gboolean gw_store_reload_folder (CamelGroupwiseStore *store, CamelFolder *folder, guint32 flags, GError **error);
void groupwise_store_set_current_folder (CamelGroupwiseStore *groupwise_store, CamelFolder *folder);

G_END_DECLS

#endif /* CAMEL_GROUPWISE_STORE_H */
