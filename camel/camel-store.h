/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-store.h : Abstract class for an email store */

/*
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *          Michael Zucchi <NotZed@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
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

#ifndef CAMEL_STORE_H
#define CAMEL_STORE_H

/* for mode_t */
#include <sys/types.h>

#include <camel/camel-folder.h>
#include <camel/camel-service.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STORE \
	(camel_store_get_type ())
#define CAMEL_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STORE, CamelStore))
#define CAMEL_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STORE, CamelStoreClass))
#define CAMEL_IS_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STORE))
#define CAMEL_IS_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STORE))
#define CAMEL_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STORE, CamelStoreClass))

/**
 * CAMEL_STORE_ERROR:
 *
 * Since: 2.32
 **/
#define CAMEL_STORE_ERROR \
	(camel_store_error_quark ())

G_BEGIN_DECLS

/**
 * CamelStoreError:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_STORE_ERROR_INVALID,
	CAMEL_STORE_ERROR_NO_FOLDER
} CamelStoreError;

/**
 * CamelStoreLock:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_STORE_FOLDER_LOCK
} CamelStoreLock;

#define CAMEL_FOLDER_TYPE_MASK (7 << 10)
#define CAMEL_FOLDER_TYPE_BIT (10)

/**
 * CamelFolderInfoFlags:
 * @CAMEL_FOLDER_NOSELECT:
 *    The folder cannot contain messages.
 * @CAMEL_FOLDER_NOINFERIORS:
 *    The folder cannot have child folders.
 * @CAMEL_FOLDER_CHILDREN:
 *    The folder has children (not yet fully implemented).
 * @CAMEL_FOLDER_NOCHILDREN:
 *    The folder does not have children (not yet fully implemented).
 * @CAMEL_FOLDER_SUBSCRIBED:
 *    The folder is subscribed.
 * @CAMEL_FOLDER_VIRTUAL:
 *    The folder is virtual.  Messages cannot be copied or moved to
 *    virtual folders since they are only queries of other folders.
 * @CAMEL_FOLDER_SYSTEM:
 *    The folder is a built-in "system" folder.  System folders
 *    cannot be renamed or deleted.
 * @CAMEL_FOLDER_VTRASH:
 *    The folder is a virtual trash folder.  It cannot be copied to,
 *    and can only be moved to if in an existing folder.
 * @CAMEL_FOLDER_SHARED_TO_ME:
 *    A folder being shared by someone else.
 * @CAMEL_FOLDER_SHARED_BY_ME:
 *    A folder being shared by the user.
 * @CAMEL_FOLDER_TYPE_NORMAL:
 *    The folder is a normal folder.
 * @CAMEL_FOLDER_TYPE_INBOX:
 *    The folder is an inbox folder.
 * @CAMEL_FOLDER_TYPE_OUTBOX:
 *    The folder is an outbox folder.
 * @CAMEL_FOLDER_TYPE_TRASH:
 *    The folder shows deleted messages.
 * @CAMEL_FOLDER_TYPE_JUNK:
 *    The folder shows junk messages.
 * @CAMEL_FOLDER_TYPE_SENT:
 *    The folder shows sent messages.
 *
 * These flags are abstractions.  It's up to the CamelProvider to give
 * them suitable interpretations.  Use #CAMEL_FOLDER_TYPE_MASK to isolate
 * the folder's type.
 **/
typedef enum {
	CAMEL_FOLDER_NOSELECT     = 1 << 0,
	CAMEL_FOLDER_NOINFERIORS  = 1 << 1,
	CAMEL_FOLDER_CHILDREN     = 1 << 2,
	CAMEL_FOLDER_NOCHILDREN   = 1 << 3,
	CAMEL_FOLDER_SUBSCRIBED   = 1 << 4,
	CAMEL_FOLDER_VIRTUAL      = 1 << 5,
	CAMEL_FOLDER_SYSTEM       = 1 << 6,
	CAMEL_FOLDER_VTRASH       = 1 << 7,
	CAMEL_FOLDER_SHARED_TO_ME = 1 << 8,
	CAMEL_FOLDER_SHARED_BY_ME = 1 << 9,
	CAMEL_FOLDER_TYPE_NORMAL  = 0 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_INBOX   = 1 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_OUTBOX  = 2 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_TRASH   = 3 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_JUNK    = 4 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_SENT    = 5 << CAMEL_FOLDER_TYPE_BIT
} CamelFolderInfoFlags;

/* next bit is 1 << 13 */

typedef struct _CamelFolderInfo {
	struct _CamelFolderInfo *next;
	struct _CamelFolderInfo *parent;
	struct _CamelFolderInfo *child;

	gchar *uri;
	gchar *name;
	gchar *full_name;

	CamelFolderInfoFlags flags;
	gint32 unread;
	gint32 total;
} CamelFolderInfo;

/* Flags for store flags */
typedef enum {
	CAMEL_STORE_SUBSCRIPTIONS    = 1 << 0,
	CAMEL_STORE_VTRASH           = 1 << 1,
	CAMEL_STORE_FILTER_INBOX     = 1 << 2,
	CAMEL_STORE_VJUNK            = 1 << 3,
	CAMEL_STORE_PROXY            = 1 << 4,
	CAMEL_STORE_IS_MIGRATING     = 1 << 5,
	CAMEL_STORE_ASYNC            = 1 << 6,
	CAMEL_STORE_REAL_JUNK_FOLDER = 1 << 7,
} CamelStoreFlags;

/* store premissions */
typedef enum {
	CAMEL_STORE_READ  = 1 << 0,
	CAMEL_STORE_WRITE = 1 << 1
} CamelStorePermissionFlags;

struct _CamelDB;

typedef struct _CamelStore CamelStore;
typedef struct _CamelStoreClass CamelStoreClass;
typedef struct _CamelStorePrivate CamelStorePrivate;

/* open mode for folder */
typedef enum {
	CAMEL_STORE_FOLDER_CREATE     = 1 << 0,
	CAMEL_STORE_FOLDER_EXCL       = 1 << 1,
	CAMEL_STORE_FOLDER_BODY_INDEX = 1 << 2,
	CAMEL_STORE_FOLDER_PRIVATE    = 1 << 3  /* a private folder that
                                                   should not show up in
                                                   unmatched, folder
                                                   info's, etc. */
} CamelStoreGetFolderFlags;

#define CAMEL_STORE_FOLDER_CREATE_EXCL \
	(CAMEL_STORE_FOLDER_CREATE | CAMEL_STORE_FOLDER_EXCL)

/**
 * CamelStoreGetFolderInfoFlags:
 * @CAMEL_STORE_FOLDER_INFO_FAST:
 * @CAMEL_STORE_FOLDER_INFO_RECURSIVE:
 * @CAMEL_STORE_FOLDER_INFO_SUBSCRIBED:
 * @CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL:
 *   Do not include virtual trash or junk folders.
 * @CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST:
 *   Fetch only the subscription list. Clients should use this
 *   flag for requesting the list of folders available for
 *   subscription. Used in Exchange / IMAP connectors for public
 *   folder fetching.
 **/
typedef enum {
	CAMEL_STORE_FOLDER_INFO_FAST              = 1 << 0,
	CAMEL_STORE_FOLDER_INFO_RECURSIVE         = 1 << 1,
	CAMEL_STORE_FOLDER_INFO_SUBSCRIBED        = 1 << 2,
	CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL        = 1 << 3,
	CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST = 1 << 4
} CamelStoreGetFolderInfoFlags;

struct _CamelStore {
	CamelService parent;
	CamelStorePrivate *priv;

	CamelObjectBag *folders;
	struct _CamelDB *cdb_r;
	struct _CamelDB *cdb_w;

	CamelStoreFlags flags;
	CamelStorePermissionFlags mode;

	/* Future ABI expansion */
	gpointer later[4];
};

struct _CamelStoreClass {
	CamelServiceClass parent_class;

	GHashFunc hash_folder_name;
	GCompareFunc compare_folder_name;

	/* Non-Blocking Methods */
	gboolean	(*can_refresh_folder)	(CamelStore *store,
						 CamelFolderInfo *info,
						 GError **error);
	gboolean	(*folder_is_subscribed)	(CamelStore *store,
						 const gchar *folder_name);
	void		(*free_folder_info)	(CamelStore *store,
						 CamelFolderInfo *fi);

	/* Synchronous I/O Methods */
	CamelFolder *	(*get_folder_sync)	(CamelStore *store,
						 const gchar *folder_name,
						 CamelStoreGetFolderFlags flags,
						 GCancellable *cancellable,
						 GError **error);
	CamelFolderInfo *
			(*get_folder_info_sync)	(CamelStore *store,
						 const gchar *top,
						 CamelStoreGetFolderInfoFlags flags,
						 GCancellable *cancellable,
						 GError **error);
	CamelFolder *	(*get_inbox_folder_sync)
						(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
	CamelFolder *	(*get_junk_folder_sync)	(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
	CamelFolder *	(*get_trash_folder_sync)
						(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
	CamelFolderInfo *
			(*create_folder_sync)	(CamelStore *store,
						 const gchar *parent_name,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*delete_folder_sync)	(CamelStore *store,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*rename_folder_sync)	(CamelStore *store,
						 const gchar *old_name,
						 const gchar *new_name,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*subscribe_folder_sync)
						(CamelStore *store,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*unsubscribe_folder_sync)
						(CamelStore *store,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*synchronize_sync)	(CamelStore *store,
						 gboolean expunge,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*noop_sync)		(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);

	/* Asyncrhonous I/O Methods (all have defaults) */
	void		(*get_folder)		(CamelStore *store,
						 const gchar *folder_name,
						 CamelStoreGetFolderFlags flags,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	CamelFolder *	(*get_folder_finish)	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*get_folder_info)	(CamelStore *store,
						 const gchar *top,
						 CamelStoreGetFolderInfoFlags flags,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	CamelFolderInfo *
			(*get_folder_info_finish)
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*get_inbox_folder)	(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	CamelFolder *	(*get_inbox_folder_finish)
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*get_junk_folder)	(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	CamelFolder *	(*get_junk_folder_finish)
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*get_trash_folder)	(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	CamelFolder *	(*get_trash_folder_finish)
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*create_folder)	(CamelStore *store,
						 const gchar *parent_name,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	CamelFolderInfo *
			(*create_folder_finish)	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*delete_folder)	(CamelStore *store,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*delete_folder_finish)	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*rename_folder)	(CamelStore *store,
						 const gchar *old_name,
						 const gchar *new_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*rename_folder_finish)	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*subscribe_folder)	(CamelStore *store,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*subscribe_folder_finish)
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*unsubscribe_folder)	(CamelStore *store,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*unsubscribe_folder_finish)
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*synchronize)		(CamelStore *store,
						 gboolean expunge,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*synchronize_finish)	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
	void		(*noop)			(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*noop_finish)		(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);

	/* Signals */
	void		(*folder_created)	(CamelStore *store,
						 CamelFolderInfo *folder_info);
	void		(*folder_deleted)	(CamelStore *store,
						 CamelFolderInfo *folder_info);
	void		(*folder_opened)	(CamelStore *store,
						 CamelFolder *folder);
	void		(*folder_renamed)	(CamelStore *store,
						 const gchar *old_name,
						 CamelFolderInfo *folder_info);
	void		(*folder_subscribed)	(CamelStore *store,
						 CamelFolderInfo *folder_info);
	void		(*folder_unsubscribed)	(CamelStore *store,
						 CamelFolderInfo *folder_info);
};

GType		camel_store_get_type		(void);
GQuark		camel_store_error_quark		(void) G_GNUC_CONST;
void		camel_store_folder_created	(CamelStore *store,
						 CamelFolderInfo *folder_info);
void		camel_store_folder_deleted	(CamelStore *store,
						 CamelFolderInfo *folder_info);
void		camel_store_folder_opened	(CamelStore *store,
						 CamelFolder *folder);
void		camel_store_folder_renamed	(CamelStore *store,
						 const gchar *old_name,
						 CamelFolderInfo *folder_info);
void		camel_store_folder_subscribed	(CamelStore *store,
						 CamelFolderInfo *folder_info);
void		camel_store_folder_unsubscribed	(CamelStore *store,
						 CamelFolderInfo *folder_info);
void		camel_store_free_folder_info	(CamelStore *store,
						 CamelFolderInfo *fi);
void		camel_store_free_folder_info_full
						(CamelStore *store,
						 CamelFolderInfo *fi);
void		camel_store_free_folder_info_nop (CamelStore *store,
						 CamelFolderInfo *fi);
CamelFolderInfo *
		camel_folder_info_new		(void);
void		camel_folder_info_free		(CamelFolderInfo *fi);
#ifndef CAMEL_DISABLE_DEPRECATED
CamelFolderInfo *
		camel_folder_info_build		(GPtrArray *folders,
						 const gchar *namespace,
						 gchar separator,
						 gboolean short_names);
#endif /* CAMEL_DISABLE_DEPRECATED */
CamelFolderInfo *
		camel_folder_info_clone		(CamelFolderInfo *fi);
gboolean	camel_store_supports_subscriptions
						(CamelStore *store);
gboolean	camel_store_folder_is_subscribed (CamelStore *store,
						 const gchar *folder_name);
gboolean	camel_store_can_refresh_folder	(CamelStore *store,
						 CamelFolderInfo *info,
						 GError **error);
void		camel_store_lock		(CamelStore *store,
						 CamelStoreLock lock);
void		camel_store_unlock		(CamelStore *store,
						 CamelStoreLock lock);

CamelFolder *	camel_store_get_folder_sync	(CamelStore *store,
						 const gchar *folder_name,
						 CamelStoreGetFolderFlags flags,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_get_folder		(CamelStore *store,
						 const gchar *folder_name,
						 CamelStoreGetFolderFlags flags,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
CamelFolder *	camel_store_get_folder_finish	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
CamelFolderInfo *
		camel_store_get_folder_info_sync
						(CamelStore *store,
						 const gchar *top,
						 CamelStoreGetFolderInfoFlags flags,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_get_folder_info	(CamelStore *store,
						 const gchar *top,
						 CamelStoreGetFolderInfoFlags flags,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
CamelFolderInfo *
		camel_store_get_folder_info_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
CamelFolder *	camel_store_get_inbox_folder_sync
						(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_get_inbox_folder	(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
CamelFolder *	camel_store_get_inbox_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
CamelFolder *	camel_store_get_junk_folder_sync
						(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_get_junk_folder	(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
CamelFolder *	camel_store_get_junk_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
CamelFolder *	camel_store_get_trash_folder_sync
						(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_get_trash_folder	(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
CamelFolder *	camel_store_get_trash_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
CamelFolderInfo *
		camel_store_create_folder_sync	(CamelStore *store,
						 const gchar *parent_name,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_create_folder	(CamelStore *store,
						 const gchar *parent_name,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
CamelFolderInfo *
		camel_store_create_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
gboolean	camel_store_delete_folder_sync	(CamelStore *store,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_delete_folder	(CamelStore *store,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_store_delete_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
gboolean	camel_store_rename_folder_sync	(CamelStore *store,
						 const gchar *old_name,
						 const gchar *new_name,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_rename_folder	(CamelStore *store,
						 const gchar *old_name,
						 const gchar *new_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_store_rename_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
gboolean	camel_store_subscribe_folder_sync
						(CamelStore *store,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_subscribe_folder	(CamelStore *store,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_store_subscribe_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
gboolean	camel_store_unsubscribe_folder_sync
						(CamelStore *store,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_unsubscribe_folder	(CamelStore *store,
						 const gchar *folder_name,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_store_unsubscribe_folder_finish
						(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
gboolean	camel_store_synchronize_sync	(CamelStore *store,
						 gboolean expunge,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_synchronize		(CamelStore *store,
						 gboolean expunge,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_store_synchronize_finish	(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);
gboolean	camel_store_noop_sync		(CamelStore *store,
						 GCancellable *cancellable,
						 GError **error);
void		camel_store_noop		(CamelStore *store,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_store_noop_finish		(CamelStore *store,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_STORE_H */
