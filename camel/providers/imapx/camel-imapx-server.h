/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#ifndef CAMEL_IMAPX_SERVER_H
#define CAMEL_IMAPX_SERVER_H

#include <camel/camel.h>
#include <libedataserver/e-flag.h>

#include "camel-imapx-stream.h"
#include "camel-imapx-store-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_SERVER \
	(camel_imapx_server_get_type ())
#define CAMEL_IMAPX_SERVER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServer))
#define CAMEL_IMAPX_SERVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServerClass))
#define CAMEL_IS_IMAPX_SERVER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_SERVER))
#define CAMEL_IS_IMAPX_SERVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_SERVER))
#define CAMEL_IMAPX_SERVER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_SERVER, CamelIMAPXServerClass))

#define IMAPX_MODE_READ (1<<0)
#define IMAPX_MODE_WRITE (1<<1)

G_BEGIN_DECLS

typedef struct _CamelIMAPXServer CamelIMAPXServer;
typedef struct _CamelIMAPXServerClass CamelIMAPXServerClass;

typedef struct _CamelIMAPXCommand CamelIMAPXCommand;
typedef struct _CamelIMAPXIdle CamelIMAPXIdle;
struct _IMAPXJobQueueInfo;

struct _CamelIMAPXServer {
	CamelObject parent;

	CamelStore *store;
	CamelSession *session;

	/* Info about the current connection */
	CamelURL *url;
	CamelIMAPXStream *stream;
	struct _capability_info *cinfo;
	gboolean is_process_stream;

	CamelIMAPXNamespaceList *nsl;

	/* incoming jobs */
	CamelMsgPort *port;
	CamelDList jobs;
	/* in micro seconds */
	guint job_timeout;

	gchar tagprefix;
	gint state:4;

	/* Current command/work queue.  All commands are stored in one list,
	   all the time, so they can be cleaned up in exception cases */
	GStaticRecMutex queue_lock;
	CamelIMAPXCommand *literal;
	CamelDList queue;
	CamelDList active;
	CamelDList done;

	/* info on currently selected folder */
	CamelFolder *select_folder;
	CamelFolderChangeInfo *changes;
	CamelFolder *select_pending;
	guint32 permanentflags;
	guint32 unseen;
	guint64 uidvalidity;
	guint64 highestmodseq;
	guint32 uidnext;
	guint32 exists;
	guint32 recent;
	guint32 mode;

	/* any expunges that happened from the last command, they are
	   processed after the command completes. */
	GSList *expunged;

	GThread *parser_thread;
	/* Protects the output stream between parser thread (which can disconnect from server) and other threads that issue
	   commands. Input stream does not require a lock since only parser_thread can operate on it */
	GStaticRecMutex ostream_lock;
	/* Used for canceling operations as well as signaling parser thread to disconnnect/quit */
	CamelOperation *op;
	gboolean parser_quit;

	/* Idle */
	CamelIMAPXIdle *idle;
	gboolean use_idle;

	gboolean use_qresync;

	/* used for storing eflags to syncronize duplicate get_message requests */
	GHashTable *uid_eflags;
};

struct _CamelIMAPXServerClass {
	CamelObjectClass parent_class;

	/* Signals */
	void	(*select_changed)	(CamelIMAPXServer *server, const gchar *selected_folder);
	void	(*shutdown)		(CamelIMAPXServer *server);

	gchar tagprefix;
};

GType		camel_imapx_server_get_type	(void);
CamelIMAPXServer *
		camel_imapx_server_new		(CamelStore *store,
						 CamelURL *url);
gboolean	camel_imapx_server_connect	(CamelIMAPXServer *is,
						 GError **error);
gboolean	imapx_connect_to_server		(CamelIMAPXServer *is,
						 GError **error);
GPtrArray *	camel_imapx_server_list		(CamelIMAPXServer *is,
						 const gchar *top,
						 guint32 flags,
						 const gchar *ext,
						 GError **error);
gboolean	camel_imapx_server_refresh_info	(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 GError **error);
gboolean	camel_imapx_server_sync_changes	(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 GError **error);
gboolean	camel_imapx_server_expunge	(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 GError **error);
gboolean	camel_imapx_server_noop		(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 GError **error);
CamelStream *	camel_imapx_server_get_message	(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 const gchar *uid,
						 GError **error);
gboolean	camel_imapx_server_copy_message	(CamelIMAPXServer *is,
						 CamelFolder *source,
						 CamelFolder *dest,
						 GPtrArray *uids,
						 gboolean delete_originals,
						 GError **error);
gboolean	camel_imapx_server_append_message
						(CamelIMAPXServer *is,
						 CamelFolder *folder,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *mi,
						 GError **error);
gboolean	camel_imapx_server_sync_message (CamelIMAPXServer *is,
						 CamelFolder *folder,
						 const gchar *uid,
						 GError **error);
gboolean	camel_imapx_server_manage_subscription
						(CamelIMAPXServer *is,
						 const gchar *folder_name,
						 gboolean subscribe,
						 GError **error);
gboolean	camel_imapx_server_create_folder(CamelIMAPXServer *is,
						 const gchar *folder_name,
						 GError **error);
gboolean	camel_imapx_server_delete_folder(CamelIMAPXServer *is,
						 const gchar *folder_name,
						 GError **error);
gboolean	camel_imapx_server_rename_folder(CamelIMAPXServer *is,
						 const gchar *old_name,
						 const gchar *new_name,
						 GError **error);
struct _IMAPXJobQueueInfo *
		camel_imapx_server_get_job_queue_info
						(CamelIMAPXServer *is);

G_END_DECLS

#endif /* CAMEL_IMAPX_SERVER_H */
