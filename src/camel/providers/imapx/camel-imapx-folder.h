/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_IMAPX_FOLDER_H
#define CAMEL_IMAPX_FOLDER_H

#include <camel/camel.h>

#include "camel-imapx-mailbox.h"

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_FOLDER \
	(camel_imapx_folder_get_type ())
#define CAMEL_IMAPX_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolder))
#define CAMEL_IMAPX_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolderClass))
#define CAMEL_IS_IMAPX_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_FOLDER))
#define CAMEL_IS_IMAPX_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_FOLDER))
#define CAMEL_IMAPX_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolderClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXFolder CamelIMAPXFolder;
typedef struct _CamelIMAPXFolderClass CamelIMAPXFolderClass;
typedef struct _CamelIMAPXFolderPrivate CamelIMAPXFolderPrivate;

struct _CamelIMAPXFolder {
	CamelOfflineFolder parent;
	CamelIMAPXFolderPrivate *priv;

	CamelDataCache *cache;

	GMutex search_lock;
	GMutex stream_lock;

	gboolean apply_filters;		/* persistent property */
};

struct _CamelIMAPXFolderClass {
	CamelOfflineFolderClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_imapx_folder_get_type	(void);
CamelFolder *	camel_imapx_folder_new		(CamelStore *parent,
						 const gchar *path,
						 const gchar *raw,
						 GError **error);
CamelIMAPXMailbox *
		camel_imapx_folder_ref_mailbox	(CamelIMAPXFolder *folder);
void		camel_imapx_folder_set_mailbox	(CamelIMAPXFolder *folder,
						 CamelIMAPXMailbox *mailbox);
CamelIMAPXMailbox *
		camel_imapx_folder_list_mailbox	(CamelIMAPXFolder *folder,
						 GCancellable *cancellable,
						 GError **error);
GSequence *	camel_imapx_folder_copy_message_map
						(CamelIMAPXFolder *folder);
void		camel_imapx_folder_add_move_to_real_junk
						(CamelIMAPXFolder *folder,
						 const gchar *message_uid);
void		camel_imapx_folder_add_move_to_real_trash
						(CamelIMAPXFolder *folder,
						 const gchar *message_uid);
void		camel_imapx_folder_add_move_to_not_junk
						(CamelIMAPXFolder *folder,
						 const gchar *message_uid);
void		camel_imapx_folder_invalidate_local_cache
						(CamelIMAPXFolder *folder,
						 guint64 new_uidvalidity);
gboolean	camel_imapx_folder_get_check_folder
						(CamelIMAPXFolder *folder);
void		camel_imapx_folder_set_check_folder
						(CamelIMAPXFolder *folder,
						 gboolean check_folder);
gint64		camel_imapx_folder_get_last_full_update
						(CamelIMAPXFolder *folder);
void		camel_imapx_folder_set_last_full_update
						(CamelIMAPXFolder *folder,
						 gint64 last_full_update);
void		camel_imapx_folder_claim_move_to_real_junk_uids
						(CamelIMAPXFolder *folder,
						 GPtrArray *out_uids_to_copy);
void		camel_imapx_folder_claim_move_to_real_trash_uids
						(CamelIMAPXFolder *folder,
						 GPtrArray *out_uids_to_copy);
void		camel_imapx_folder_claim_move_to_not_junk_uids
						(CamelIMAPXFolder *folder,
						 GPtrArray *out_uids_to_copy);
void		camel_imapx_folder_clear_move_to_real_trash_uids
						(CamelIMAPXFolder *folder);
void		camel_imapx_folder_clear_move_to_real_junk_uids
						(CamelIMAPXFolder *folder);
void		camel_imapx_folder_update_cache_expire
						(CamelFolder *folder,
						 time_t expire_when);

G_END_DECLS

#endif /* CAMEL_IMAPX_FOLDER_H */
