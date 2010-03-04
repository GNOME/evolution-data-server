/*
 *  Copyright (C) 2005 Novell Inc.
 *
 *  Authors:
 *    Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _CAMEL_IMAPX_SERVER_H
#define _CAMEL_IMAPX_SERVER_H

#include <camel/camel-msgport.h>
#include <camel/camel-list-utils.h>
#include <libedataserver/e-flag.h>

struct _CamelFolder;
struct _CamelException;
struct _CamelMimeMessage;
struct _CamelMessageInfo;

#define CAMEL_IMAPX_SERVER(obj)         CAMEL_CHECK_CAST (obj, camel_imapx_server_get_type (), CamelIMAPPServer)
#define CAMEL_IMAPX_SERVER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapx_server_get_type (), CamelIMAPPServerClass)
#define CAMEL_IS_IMAPX_SERVER(obj)      CAMEL_CHECK_TYPE (obj, camel_imapx_server_get_type ())

typedef struct _CamelIMAPXServer CamelIMAPXServer;
typedef struct _CamelIMAPXServerClass CamelIMAPXServerClass;

#define IMAPX_MODE_READ (1<<0)
#define IMAPX_MODE_WRITE (1<<1)

struct _CamelIMAPXServer {
	CamelObject cobject;

	struct _CamelStore *store;
	struct _CamelSession *session;

	/* Info about the current connection */
	struct _CamelURL *url;
	struct _CamelIMAPXStream *stream;
	struct _capability_info *cinfo;
	gboolean is_ssl_stream;

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
	struct _CamelIMAPXCommand *literal;
	CamelDList queue;
	CamelDList active;
	CamelDList done;

	/* info on currently selected folder */
	struct _CamelFolder *select_folder;
	gchar *select;
	struct _CamelFolderChangeInfo *changes;
	struct _CamelFolder *select_pending;
	guint32 permanentflags;
	guint32 uidvalidity;
	guint32 unseen;
	guint32 exists;
	guint32 recent;
	guint32 mode;
	guint32 unread;

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
	struct _CamelIMAPXIdle *idle;
	gboolean use_idle;
};

struct _CamelIMAPXServerClass {
	CamelObjectClass cclass;

	gchar tagprefix;
};

CamelType               camel_imapx_server_get_type     (void);
CamelIMAPXServer *camel_imapx_server_new(struct _CamelStore *store, struct _CamelURL *url);

gboolean camel_imapx_server_connect(CamelIMAPXServer *is, gint state);

GPtrArray *camel_imapx_server_list(CamelIMAPXServer *is, const gchar *top, guint32 flags, CamelException *ex);

void camel_imapx_server_refresh_info(CamelIMAPXServer *is, CamelFolder *folder, struct _CamelException *ex);
void camel_imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex);
void camel_imapx_server_expunge(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex);
void camel_imapx_server_noop (CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex);

CamelStream *camel_imapx_server_get_message(CamelIMAPXServer *is, CamelFolder *folder, const gchar *uid, struct _CamelException *ex);
void camel_imapx_server_copy_message (CamelIMAPXServer *is, CamelFolder *source, CamelFolder *dest, GPtrArray *uids, gboolean delete_originals, CamelException *ex);
void camel_imapx_server_append_message(CamelIMAPXServer *is, CamelFolder *folder, struct _CamelMimeMessage *message, const struct _CamelMessageInfo *mi, CamelException *ex);
void camel_imapx_server_sync_message (CamelIMAPXServer *is, CamelFolder *folder, const gchar *uid, CamelException *ex);

#endif /* _CAMEL_IMAPX_SERVER_H */
