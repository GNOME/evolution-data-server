/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * camel-folder.h: Abstract class for an email folder
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_FOLDER_H
#define CAMEL_FOLDER_H

#include <camel/camel-folder-summary.h>

/* Standard GObject macros */
#define CAMEL_TYPE_FOLDER \
	(camel_folder_get_type ())
#define CAMEL_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_FOLDER, CamelFolder))
#define CAMEL_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_FOLDER, CamelFolderClass))
#define CAMEL_IS_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_FOLDER))
#define CAMEL_IS_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_FOLDER))
#define CAMEL_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_FOLDER, CamelFolderClass))

/**
 * CAMEL_FOLDER_ERROR:
 *
 * Since: 2.32
 **/
#define CAMEL_FOLDER_ERROR \
	(camel_folder_error_quark ())

G_BEGIN_DECLS

struct _CamelStore;

typedef struct _CamelFolderChangeInfo CamelFolderChangeInfo;
typedef struct _CamelFolderChangeInfoPrivate CamelFolderChangeInfoPrivate;

typedef struct _CamelFolder CamelFolder;
typedef struct _CamelFolderClass CamelFolderClass;
typedef struct _CamelFolderPrivate CamelFolderPrivate;

/**
 * CamelFolderError:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_FOLDER_ERROR_INVALID,
	CAMEL_FOLDER_ERROR_INVALID_STATE,
	CAMEL_FOLDER_ERROR_NON_EMPTY,
	CAMEL_FOLDER_ERROR_NON_UID,
	CAMEL_FOLDER_ERROR_INSUFFICIENT_PERMISSION,
	CAMEL_FOLDER_ERROR_INVALID_PATH,
	CAMEL_FOLDER_ERROR_INVALID_UID,
	CAMEL_FOLDER_ERROR_SUMMARY_INVALID
} CamelFolderError;

/**
 * CamelFolderLock:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_FOLDER_CHANGE_LOCK,
	CAMEL_FOLDER_REC_LOCK
} CamelFolderLock;

struct _CamelFolderChangeInfo {
	GPtrArray *uid_added;
	GPtrArray *uid_removed;
	GPtrArray *uid_changed;
	GPtrArray *uid_recent;

	CamelFolderChangeInfoPrivate *priv;
};

typedef struct _CamelFolderQuotaInfo CamelFolderQuotaInfo;

/**
 * CamelFolderQuotaInfo:
 *
 * Since: 2.24
 **/
struct _CamelFolderQuotaInfo {
	gchar *name;
	guint64 used;
	guint64 total;

	struct _CamelFolderQuotaInfo *next;
};

struct _CamelFolder {
	CamelObject parent;
	CamelFolderPrivate *priv;

	CamelFolderSummary *summary;

	guint32 folder_flags;
	guint32 permanent_flags;

	/* Future ABI expansion */
	gpointer later[4];
};

#define CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY (1<<0)
#define CAMEL_FOLDER_HAS_SEARCH_CAPABILITY  (1<<1)
#define CAMEL_FOLDER_FILTER_RECENT          (1<<2)
#define CAMEL_FOLDER_HAS_BEEN_DELETED       (1<<3)
#define CAMEL_FOLDER_IS_TRASH               (1<<4)
#define CAMEL_FOLDER_IS_JUNK                (1<<5)
#define CAMEL_FOLDER_FILTER_JUNK	    (1<<6)

struct _CamelFolderClass {
	CamelObjectClass parent_class;

	/* Methods */
	gboolean	(*refresh_info)		(CamelFolder *folder,
						 GError **error);
	gboolean	(*sync)			(CamelFolder *folder,
						 gboolean expunge,
						 GError **error);
	gboolean	(*expunge)		(CamelFolder *folder,
						 GError **error);
	gint		(*get_message_count)	(CamelFolder *folder);
	gboolean	(*append_message)	(CamelFolder *folder,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *info,
						 gchar **appended_uid,
						 GError **error);
	guint32		(*get_permanent_flags)	(CamelFolder *folder);
	guint32		(*get_message_flags)	(CamelFolder *folder,
						 const gchar *uid);
	gboolean	(*set_message_flags)	(CamelFolder *folder,
						 const gchar *uid,
						 guint32 flags, guint32 set);
	gboolean	(*get_message_user_flag)(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name);
	void		(*set_message_user_flag)(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name,
						 gboolean value);
	const gchar *	(*get_message_user_tag)	(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name);
	void		(*set_message_user_tag)	(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name,
						 const gchar *value);
	CamelMimeMessage *
			(*get_message)		(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);
	GPtrArray *	(*get_uids)		(CamelFolder *folder);
	void		(*free_uids)		(CamelFolder *folder,
						 GPtrArray *array);
	gint		(*cmp_uids)		(CamelFolder *folder,
						 const gchar *uid1,
						 const gchar *uid2);
	void		(*sort_uids)		(CamelFolder *folder,
						 GPtrArray *uids);
	GPtrArray *	(*get_summary)		(CamelFolder *folder);
	void		(*free_summary)		(CamelFolder *folder,
						 GPtrArray *summary);
	gboolean	(*has_search_capability)(CamelFolder *folder);
	GPtrArray *	(*search_by_expression)	(CamelFolder *folder,
						 const gchar *expression,
						 GError **error);
	GPtrArray *	(*search_by_uids)	(CamelFolder *folder,
						 const gchar *expression,
						 GPtrArray *uids,
						 GError **error);
	void		(*search_free)		(CamelFolder *folder,
						 GPtrArray *result);
	gboolean	(*fetch_old_messages)   (CamelFolder *folder,
						 int count,
						 GError **error);
        gboolean	(*purge_old_messages)   (CamelFolder *folder,
						 GError **error);
	CamelMessageInfo *
			(*get_message_info)	(CamelFolder *folder,
						 const gchar *uid);
	void		(*ref_message_info)	(CamelFolder *folder,
						 CamelMessageInfo *info);
	void		(*free_message_info)	(CamelFolder *folder,
						 CamelMessageInfo *info);
	gboolean	(*transfer_messages_to)	(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *destination,
						 GPtrArray **transferred_uids,
						 gboolean delete_originals,
						 GError **error);
	void		(*delete)		(CamelFolder *folder);
	void		(*rename)		(CamelFolder *folder,
						 const gchar *newname);
	void		(*freeze)		(CamelFolder *folder);
	void		(*thaw)			(CamelFolder *folder);
	gboolean	(*is_frozen)		(CamelFolder *folder);
	CamelFolderQuotaInfo *
			(*get_quota_info)	(CamelFolder *folder);
	guint32		(*count_by_expression)	(CamelFolder *folder,
						 const gchar *expression,
						 GError **error);
	gboolean	(*sync_message)		(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);
	GPtrArray *	(*get_uncached_uids)	(CamelFolder *folder,
						 GPtrArray *uids,
						 GError **error);
	gchar *		(*get_filename)		(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);

	/* Signals */
	void		(*changed)		(CamelFolder *folder,
						 CamelFolderChangeInfo *info);
	void		(*deleted)		(CamelFolder *folder);
	void		(*renamed)		(CamelFolder *folder,
						 const gchar *old_name);
};

GType		camel_folder_get_type		(void);
GQuark		camel_folder_error_quark	(void) G_GNUC_CONST;
gboolean	camel_folder_refresh_info	(CamelFolder *folder,
						 GError **error);
gboolean	camel_folder_sync		(CamelFolder *folder,
						 gboolean expunge,
						 GError **error);
void		camel_folder_set_lock_async	(CamelFolder *folder,
						 gboolean skip_folder_lock);

struct _CamelStore *
		camel_folder_get_parent_store	(CamelFolder *folder);

/* delete operations */
gboolean	camel_folder_expunge		(CamelFolder *folder,
						 GError **error);

/* folder name operations */
const gchar *	camel_folder_get_name		(CamelFolder *folder);
void		camel_folder_set_name		(CamelFolder *folder,
						 const gchar *name);
const gchar *	camel_folder_get_full_name	(CamelFolder *folder);
void		camel_folder_set_full_name	(CamelFolder *folder,
						 const gchar *full_name);
const gchar *	camel_folder_get_description	(CamelFolder *folder);
void		camel_folder_set_description	(CamelFolder *folder,
						 const gchar *description);

/* various properties accessors */
guint32		camel_folder_get_permanent_flags(CamelFolder *folder);

#ifndef CAMEL_DISABLE_DEPRECATED
guint32		camel_folder_get_message_flags	(CamelFolder *folder,
						 const gchar *uid);

gboolean	camel_folder_set_message_flags	(CamelFolder *folder,
						 const gchar *uid,
						 guint32 flags,
						 guint32 set);

gboolean	camel_folder_get_message_user_flag
						(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name);

void		camel_folder_set_message_user_flag
						(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name,
						 gboolean value);

const gchar *	camel_folder_get_message_user_tag
						(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name);

void		camel_folder_set_message_user_tag
						(CamelFolder *folder,
						 const gchar *uid,
						 const gchar *name,
						 const gchar *value);
#endif /* CAMEL_DISABLE_DEPRECATED */

/* message manipulation */
gboolean	camel_folder_append_message	(CamelFolder *folder,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *info,
						 gchar **appended_uid,
						 GError **error);

/* summary related operations */
gboolean	camel_folder_has_summary_capability
						(CamelFolder *folder);

gint		camel_folder_get_message_count	(CamelFolder *folder);

#ifndef CAMEL_DISABLE_DEPRECATED
gint		camel_folder_get_unread_message_count
						(CamelFolder *folder);
#endif

gint		camel_folder_get_deleted_message_count
						(CamelFolder *folder);

GPtrArray *	camel_folder_get_summary	(CamelFolder *folder);
void		camel_folder_free_summary	(CamelFolder *folder,
						 GPtrArray *array);
gboolean	camel_folder_fetch_old_messages (CamelFolder *folder,
						 int count,
                          			 GError **error);
gboolean	camel_folder_purge_old_messages (CamelFolder *folder,
						 GError **error);

/* uid based access operations */
CamelMimeMessage *
		camel_folder_get_message	(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);
gboolean	camel_folder_sync_message	(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);

#define camel_folder_delete_message(folder, uid) \
	(camel_folder_set_message_flags \
	(folder, uid, CAMEL_MESSAGE_DELETED | \
	CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN))

GPtrArray *	camel_folder_get_uids		(CamelFolder *folder);
void		camel_folder_free_uids		(CamelFolder *folder,
						 GPtrArray *array);
GPtrArray *	camel_folder_get_uncached_uids	(CamelFolder *folder,
						 GPtrArray *uids,
						 GError **error);
gint		camel_folder_cmp_uids		(CamelFolder *folder,
						 const gchar *uid1,
						 const gchar *uid2);
void		camel_folder_sort_uids		(CamelFolder *folder,
						 GPtrArray *uids);

/* search api */
gboolean	camel_folder_has_search_capability
						(CamelFolder *folder);
GPtrArray *	camel_folder_search_by_expression
						(CamelFolder *folder,
						 const gchar *expr,
						 GError **error);
GPtrArray *	camel_folder_search_by_uids	(CamelFolder *folder,
						 const gchar *expr,
						 GPtrArray *uids,
						 GError **error);
void		camel_folder_search_free	(CamelFolder *folder,
						 GPtrArray *result);
guint32		camel_folder_count_by_expression(CamelFolder *folder,
						 const gchar *expression,
						 GError **error);

/* summary info */
CamelMessageInfo *
		camel_folder_get_message_info	(CamelFolder *folder,
						 const gchar *uid);
void		camel_folder_free_message_info	(CamelFolder *folder,
						 CamelMessageInfo *info);
#ifndef CAMEL_DISABLE_DEPRECATED
void		camel_folder_ref_message_info	(CamelFolder *folder,
						 CamelMessageInfo *info);
#endif

gboolean	camel_folder_transfer_messages_to
						(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *dest,
						 GPtrArray **transferred_uids,
						 gboolean delete_originals,
						 GError **error);

void		camel_folder_delete		(CamelFolder *folder);
void		camel_folder_rename		(CamelFolder *folder,
						 const gchar *new);
void		camel_folder_changed		(CamelFolder *folder,
						 CamelFolderChangeInfo *changes);

/* stop/restart getting events */
void		camel_folder_freeze		(CamelFolder *folder);
void		camel_folder_thaw		(CamelFolder *folder);
gboolean	camel_folder_is_frozen		(CamelFolder *folder);
gint		camel_folder_get_frozen_count	(CamelFolder *folder);

/* quota support */
CamelFolderQuotaInfo *
		camel_folder_get_quota_info	(CamelFolder *folder);
CamelFolderQuotaInfo *
		camel_folder_quota_info_new	(const gchar *name,
						 guint64 used,
						 guint64 total);
CamelFolderQuotaInfo *
		camel_folder_quota_info_clone	(const CamelFolderQuotaInfo *info);
void		camel_folder_quota_info_free	(CamelFolderQuotaInfo *info);

/* For use by subclasses (for free_{uids,summary,subfolder_names}) */
void		camel_folder_free_nop		(CamelFolder *folder,
						 GPtrArray *array);
void		camel_folder_free_shallow	(CamelFolder *folder,
						 GPtrArray *array);
void		camel_folder_free_deep		(CamelFolder *folder,
						 GPtrArray *array);

gchar *		camel_folder_get_filename	(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);

/* update functions for change info */
CamelFolderChangeInfo *
		camel_folder_change_info_new	(void);
void		camel_folder_change_info_clear	(CamelFolderChangeInfo *info);
void		camel_folder_change_info_free	(CamelFolderChangeInfo *info);
gboolean	camel_folder_change_info_changed(CamelFolderChangeInfo *info);

/* for building diff's automatically */
void		camel_folder_change_info_add_source
						(CamelFolderChangeInfo *info,
						 const gchar *uid);
void		camel_folder_change_info_add_source_list
						(CamelFolderChangeInfo *info,
						 const GPtrArray *list);
void		camel_folder_change_info_add_update
						(CamelFolderChangeInfo *info,
						 const gchar *uid);
void		camel_folder_change_info_add_update_list
						(CamelFolderChangeInfo *info,
						 const GPtrArray *list);
void		camel_folder_change_info_build_diff
						(CamelFolderChangeInfo *info);

/* for manipulating diff's directly */
void		camel_folder_change_info_cat	(CamelFolderChangeInfo *info,
						 CamelFolderChangeInfo *src);
void		camel_folder_change_info_add_uid(CamelFolderChangeInfo *info,
						 const gchar *uid);
void		camel_folder_change_info_remove_uid
						(CamelFolderChangeInfo *info,
						 const gchar *uid);
void		camel_folder_change_info_change_uid
						(CamelFolderChangeInfo *info,
						 const gchar *uid);
void		camel_folder_change_info_recent_uid
						(CamelFolderChangeInfo *info,
						 const gchar *uid);
void		camel_folder_lock		(CamelFolder *folder,
						 CamelFolderLock lock);
void		camel_folder_unlock		(CamelFolder *folder,
						 CamelFolderLock lock);

G_END_DECLS

#endif /* CAMEL_FOLDER_H */
