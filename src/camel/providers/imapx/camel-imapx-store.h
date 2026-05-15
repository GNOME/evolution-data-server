/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_IMAPX_STORE_H
#define CAMEL_IMAPX_STORE_H

#include <camel/camel.h>

#include "camel-imapx-conn-manager.h"
#include "camel-imapx-server.h"

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

/* Avoid a circular reference. */
struct _CamelIMAPXJob;

typedef struct _CamelIMAPXStore CamelIMAPXStore;
typedef struct _CamelIMAPXStoreClass CamelIMAPXStoreClass;
typedef struct _CamelIMAPXStorePrivate CamelIMAPXStorePrivate;

struct _CamelIMAPXStore {
	CamelOfflineStore parent;
	CamelIMAPXStorePrivate *priv;

	CamelStoreSummary *summary; /* in-memory list of folders */
};

struct _CamelIMAPXStoreClass {
	CamelOfflineStoreClass parent_class;

	/* Signals */
	void		(*mailbox_created)	(CamelIMAPXStore *imapx_store,
						 CamelIMAPXMailbox *mailbox);
	void		(*mailbox_renamed)	(CamelIMAPXStore *imapx_store,
						 CamelIMAPXMailbox *mailbox,
						 const gchar *oldname);
	void		(*mailbox_updated)	(CamelIMAPXStore *imapx_store,
						 CamelIMAPXMailbox *mailbox);

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_imapx_store_get_type	(void);
gboolean	camel_imapx_store_is_gmail_server
						(CamelIMAPXStore *store);
gboolean	camel_imapx_store_get_bodystructure_enabled
						(CamelIMAPXStore *store);
void		camel_imapx_store_set_bodystructure_enabled
						(CamelIMAPXStore *store,
						 gboolean enabled);
gboolean	camel_imapx_store_get_preview_enabled
						(CamelIMAPXStore *store);
void		camel_imapx_store_set_preview_enabled
						(CamelIMAPXStore *store,
						 gboolean enabled);
CamelIMAPXConnManager *
		camel_imapx_store_get_conn_manager
						(CamelIMAPXStore *store);
void		camel_imapx_store_set_connecting_server
						(CamelIMAPXStore *store,
						 CamelIMAPXServer *server,
						 gboolean is_concurrent_connection);
gboolean	camel_imapx_store_is_connecting_concurrent_connection
						(CamelIMAPXStore *imapx_store);
CamelIMAPXNamespaceResponse *
		camel_imapx_store_ref_namespaces
						(CamelIMAPXStore *imapx_store);
void		camel_imapx_store_set_namespaces
						(CamelIMAPXStore *imapx_store,
						 CamelIMAPXNamespaceResponse *namespaces);
CamelIMAPXMailbox *
		camel_imapx_store_ref_mailbox	(CamelIMAPXStore *imapx_store,
						 const gchar *mailbox_name);
GList *		camel_imapx_store_list_mailboxes
						(CamelIMAPXStore *imapx_store,
						 CamelIMAPXNamespace *namespace_,
						 const gchar *pattern);
void		camel_imapx_store_emit_mailbox_updated
						(CamelIMAPXStore *imapx_store,
						 CamelIMAPXMailbox *mailbox);
void		camel_imapx_store_handle_mailbox_rename
						(CamelIMAPXStore *imapx_store,
						 CamelIMAPXMailbox *old_mailbox,
						 const gchar *new_mailbox_name);
void		camel_imapx_store_handle_list_response
						(CamelIMAPXStore *imapx_store,
						 CamelIMAPXServer *imapx_server,
						 CamelIMAPXListResponse *response);
void		camel_imapx_store_handle_lsub_response
						(CamelIMAPXStore *imapx_store,
						 CamelIMAPXServer *imapx_server,
						 CamelIMAPXListResponse *response);
CamelFolderQuotaInfo *
		camel_imapx_store_dup_quota_info
						(CamelIMAPXStore *store,
						 const gchar *quota_root_name);
void		camel_imapx_store_set_quota_info
						(CamelIMAPXStore *store,
						 const gchar *quota_root_name,
						 const CamelFolderQuotaInfo *info);
/* for debugging purposes only */
void		camel_imapx_store_dump_queue_status
						(CamelIMAPXStore *imapx_store);
G_END_DECLS

#endif /* CAMEL_IMAPX_STORE_H */

