/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-store.h: abstruct class for a disconnectable store */

/*
 * Authors: Dan Winship <danw@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_DISCO_STORE_H
#define CAMEL_DISCO_STORE_H

#include <camel/camel-store.h>

/* Standard GObject macros */
#define CAMEL_TYPE_DISCO_STORE \
	(camel_disco_store_get_type ())
#define CAMEL_DISCO_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_DISCO_STORE, CamelDiscoStore))
#define CAMEL_DISCO_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_DISCO_STORE, CamelDiscoStoreClass))
#define CAMEL_IS_DISCO_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_DISCO_STORE))
#define CAMEL_IS_DISCO_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_DISCO_STORE))
#define CAMEL_DISCO_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_DISCO_STORE, CamelDiscoStoreClass))

G_BEGIN_DECLS

struct _CamelDiscoDiary;

typedef struct _CamelDiscoStore CamelDiscoStore;
typedef struct _CamelDiscoStoreClass CamelDiscoStoreClass;

enum {
	CAMEL_DISCO_STORE_ARG_FIRST  = CAMEL_STORE_ARG_FIRST + 100
};

typedef enum {
	CAMEL_DISCO_STORE_ONLINE,
	CAMEL_DISCO_STORE_OFFLINE,
	CAMEL_DISCO_STORE_RESYNCING
} CamelDiscoStoreStatus;

struct _CamelDiscoStore {
	CamelStore parent;

	CamelDiscoStoreStatus status;
	struct _CamelDiscoDiary *diary;
};

struct _CamelDiscoStoreClass {
	CamelStoreClass parent_class;

	void              (*set_status)              (CamelDiscoStore *,
						      CamelDiscoStoreStatus,
						      CamelException *);
	gboolean          (*can_work_offline)        (CamelDiscoStore *);

	gboolean          (*connect_online)          (CamelService *,
						      CamelException *);
	gboolean          (*connect_offline)         (CamelService *,
						      CamelException *);

	gboolean          (*disconnect_online)       (CamelService *, gboolean,
						      CamelException *);
	gboolean          (*disconnect_offline)      (CamelService *, gboolean,
						      CamelException *);

	CamelFolder *     (*get_folder_online)       (CamelStore *store,
						      const gchar *name,
						      guint32 flags,
						      CamelException *ex);
	CamelFolder *     (*get_folder_offline)      (CamelStore *store,
						      const gchar *name,
						      guint32 flags,
						      CamelException *ex);
	CamelFolder *     (*get_folder_resyncing)    (CamelStore *store,
						      const gchar *name,
						      guint32 flags,
						      CamelException *ex);

	CamelFolderInfo * (*get_folder_info_online)    (CamelStore *store,
							const gchar *top,
							guint32 flags,
							CamelException *ex);
	CamelFolderInfo * (*get_folder_info_offline)   (CamelStore *store,
							const gchar *top,
							guint32 flags,
							CamelException *ex);
	CamelFolderInfo * (*get_folder_info_resyncing) (CamelStore *store,
							const gchar *top,
							guint32 flags,
							CamelException *ex);
};

GType camel_disco_store_get_type (void);

/* Public methods */
CamelDiscoStoreStatus camel_disco_store_status           (CamelDiscoStore *store);
void                  camel_disco_store_set_status       (CamelDiscoStore *store,
							  CamelDiscoStoreStatus status,
							  CamelException *ex);
gboolean              camel_disco_store_can_work_offline (CamelDiscoStore *store);

/* Convenience functions */
gboolean camel_disco_store_check_online (CamelDiscoStore *store, CamelException *ex);
void camel_disco_store_prepare_for_offline(CamelDiscoStore *store, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_DISCO_STORE_H */

#endif /* CAMEL_DISABLE_DEPRECATED */
