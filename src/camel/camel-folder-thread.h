/*
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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_FOLDER_THREAD_H
#define CAMEL_FOLDER_THREAD_H

#include <camel/camel-folder-summary.h>
#include <camel/camel-folder.h>
#include <camel/camel-memchunk.h>
#include <camel/camel-enums.h>

G_BEGIN_DECLS

typedef struct _CamelFolderThreadNode CamelFolderThreadNode;

CamelFolderThreadNode *
		camel_folder_thread_node_get_next	(CamelFolderThreadNode *self);
CamelFolderThreadNode *
		camel_folder_thread_node_get_parent	(CamelFolderThreadNode *self);
CamelFolderThreadNode *
		camel_folder_thread_node_get_child	(CamelFolderThreadNode *self);
gpointer	camel_folder_thread_node_get_item	(CamelFolderThreadNode *self);

#define CAMEL_TYPE_FOLDER_THREAD (camel_folder_thread_get_type ())
G_DECLARE_FINAL_TYPE (CamelFolderThread, camel_folder_thread, CAMEL, FOLDER_THREAD, GObject)

typedef void		(* CamelFolderThreadVoidFunc) (gconstpointer item);
typedef const gchar *	(* CamelFolderThreadStrFunc) (gconstpointer item);
typedef gint64		(* CamelFolderThreadInt64Func) (gconstpointer item);
typedef guint64		(* CamelFolderThreadUint64Func) (gconstpointer item);
typedef const GArray *	(* CamelFolderThreadArrayFunc) (gconstpointer item);

CamelFolderThread *
		camel_folder_thread_new		(CamelFolder *folder,
						 GPtrArray *uids,
						 CamelFolderThreadFlags flags);
CamelFolderThread *
		camel_folder_thread_new_items	(GPtrArray *items, /* caller data with items understood by the below functions */
						 CamelFolderThreadFlags flags,
						 CamelFolderThreadStrFunc get_uid_func,
						 CamelFolderThreadStrFunc get_subject_func,
						 CamelFolderThreadUint64Func get_message_id_func,
						 CamelFolderThreadArrayFunc get_references_func,
						 CamelFolderThreadInt64Func get_date_sent_func,
						 CamelFolderThreadInt64Func get_date_received_func,
						 CamelFolderThreadVoidFunc lock_func,
						 CamelFolderThreadVoidFunc unlock_func);
CamelFolderThreadNode *
		camel_folder_thread_get_tree	(CamelFolderThread *self);

/* debugging function only */
guint		camel_folder_thread_dump	(CamelFolderThread *self);

G_END_DECLS

#endif /* CAMEL_FOLDER_THREAD_H */
