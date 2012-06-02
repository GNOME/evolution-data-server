/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-conn-manager.h
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef _CAMEL_IMAPX_CONN_MANAGER_H
#define _CAMEL_IMAPX_CONN_MANAGER_H

#include "camel-imapx-server.h"

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
	CamelObject cobject;
	CamelIMAPXConnManagerPrivate *priv;
};

struct _CamelIMAPXConnManagerClass {
	CamelObjectClass cclass;
};

GType		camel_imapx_conn_manager_get_type (void);
CamelIMAPXConnManager *
		camel_imapx_conn_manager_new	(CamelStore *store);
CamelStore *	camel_imapx_conn_manager_get_store
						(CamelIMAPXConnManager *con_man);
CamelIMAPXServer *
		camel_imapx_conn_manager_get_connection
						(CamelIMAPXConnManager *con_man,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
void		camel_imapx_conn_manager_close_connections
						(CamelIMAPXConnManager *con_man);
GList *		camel_imapx_conn_manager_get_connections
						(CamelIMAPXConnManager *con_man);
void		camel_imapx_conn_manager_update_con_info
						(CamelIMAPXConnManager *con_man,
						 CamelIMAPXServer *server,
						 const gchar *folder_name);

#endif /* _CAMEL_IMAPX_SERVER_H */
