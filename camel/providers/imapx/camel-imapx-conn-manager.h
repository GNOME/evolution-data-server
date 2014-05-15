/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-conn-manager.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
 */

#ifndef _CAMEL_IMAPX_CONN_MANAGER_H
#define _CAMEL_IMAPX_CONN_MANAGER_H

#include "camel-imapx-server.h"

G_BEGIN_DECLS

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_CONN_MANAGER \
	(camel_imapx_conn_manager_get_type ())
#define CAMEL_IMAPX_CONN_MANAGER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_CONN_MANAGER, CamelIMAPXConnManager))
#define CAMEL_IMAPX_CONN_MANAGER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_CONN_MANAGER, CamelIMAPXConnManagerClass))
#define CAMEL_IS_IMAPX_CONN_MANAGER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_CONN_MANAGER))
#define CAMEL_IS_IMAPX_CONN_MANAGER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_CONN_MANAGER))
#define CAMEL_IMAPX_CONN_MANAGER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_CONN_MANAGER, CamelIMAPXConnManagerClass))

typedef struct _CamelIMAPXConnManager CamelIMAPXConnManager;
typedef struct _CamelIMAPXConnManagerClass CamelIMAPXConnManagerClass;
typedef struct _CamelIMAPXConnManagerPrivate CamelIMAPXConnManagerPrivate;

struct _CamelIMAPXConnManager {
	GObject parent;

	CamelIMAPXConnManagerPrivate *priv;
};

struct _CamelIMAPXConnManagerClass {
	GObjectClass parent_class;
};

GType		camel_imapx_conn_manager_get_type (void);
CamelIMAPXConnManager *
		camel_imapx_conn_manager_new	(CamelStore *store);
CamelStore *	camel_imapx_conn_manager_ref_store
						(CamelIMAPXConnManager *con_man);
CamelIMAPXServer *
		camel_imapx_conn_manager_get_connection
						(CamelIMAPXConnManager *con_man,
						 const gchar *folder_name,
						 gboolean for_expensive_job,
						 GCancellable *cancellable,
						 GError **error);
void		camel_imapx_conn_manager_close_connections
						(CamelIMAPXConnManager *con_man,
						 const GError *error);
GList *		camel_imapx_conn_manager_get_connections
						(CamelIMAPXConnManager *con_man);
void		camel_imapx_conn_manager_update_con_info
						(CamelIMAPXConnManager *con_man,
						 CamelIMAPXServer *server,
						 const gchar *folder_name);

/* for debugging purposes only */
void		camel_imapx_conn_manager_dump_queue_status
						(CamelIMAPXConnManager *con_man);
G_END_DECLS

#endif /* _CAMEL_IMAPX_SERVER_H */
