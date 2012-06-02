/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.h : class for an imap store */

/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_IMAPX_STORE_H
#define CAMEL_IMAPX_STORE_H

#include <camel/camel.h>

#include "camel-imapx-server.h"
#include "camel-imapx-store-summary.h"
#include "camel-imapx-conn-manager.h"

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_STORE \
	(camel_imapx_store_get_type ())
#define CAMEL_IMAPX_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_STORE, CamelIMAPXStore))
#define CAMEL_IMAPX_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_STORE, CamelIMAPXStoreClass))
#define CAMEL_IS_IMAPX_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_STORE))
#define CAMEL_IS_IMAPX_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_STORE))
#define CAMEL_IMAPX_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_STORE, CamelIMAPXStoreClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXStore CamelIMAPXStore;
typedef struct _CamelIMAPXStoreClass CamelIMAPXStoreClass;
typedef struct _CamelIMAPXStorePrivate CamelIMAPXStorePrivate;

struct _CamelIMAPXStore {
	CamelOfflineStore parent;
	CamelIMAPXStorePrivate *priv;

	CamelIMAPXConnManager *con_man;
	CamelIMAPXServer *authenticating_server;

	CamelIMAPXStoreSummary *summary; /* in-memory list of folders */
	gchar dir_sep;

	guint32 rec_options;

	/* Used for syncronizing get_folder_info. Check for re-use of any other lock. At the
	 * moment, could not find anything suitable for this */
	GMutex *get_finfo_lock;
	time_t last_refresh_time;

	/* hash table of UIDs to ignore as recent when updating folder */
	GHashTable *ignore_recent;

	/* if we had a login error, what to show to user */
	gchar *login_error;

	GPtrArray *pending_list;
};

struct _CamelIMAPXStoreClass {
	CamelOfflineStoreClass parent_class;
};

GType		camel_imapx_store_get_type	(void);
CamelIMAPXServer *
		camel_imapx_store_get_server	(CamelIMAPXStore *store,
						const gchar *folder_name,
						GCancellable *cancellable,
						GError **error);
void		camel_imapx_store_op_done	(CamelIMAPXStore *istore,
						CamelIMAPXServer *server,
						const gchar *folder_name);

G_END_DECLS

#endif /* CAMEL_IMAPX_STORE_H */

