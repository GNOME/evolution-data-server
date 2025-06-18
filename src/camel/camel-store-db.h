/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STORE_DB_H
#define CAMEL_STORE_DB_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <camel/camel-db.h>
#include <camel/camel-enums.h>

G_BEGIN_DECLS

/**
 * CamelStoreDBFolderRecord:
 * @folder_name: name of the folder
 * @version: version of the saved information
 * @flags: folder flags
 * @nextuid: next free uid
 * @timestamp: timestamp of the summary
 * @saved_count: count of all messages
 * @unread_count: count of unread messages
 * @deleted_count: count of deleted messages
 * @junk_count: count of junk messages
 * @visible_count: count of visible (not deleted and not junk) messages
 * @jnd_count: count of junk and not deleted messages
 * @bdata: custom data of the #CamelFolderSummary descendants
 * @folder_id: ID of the folder
 *
 * A folder record, with values stored in a #CamelStoreDB.
 *
 * Since: 3.58
 **/
typedef struct _CamelStoreDBFolderRecord {
	gchar *folder_name;
	guint32 version;
	guint32 flags;
	guint32 nextuid;
	gint64 timestamp;
	guint32 saved_count;
	guint32 unread_count;
	guint32 deleted_count;
	guint32 junk_count;
	guint32 visible_count;
	guint32 jnd_count;  /* Junked not deleted */
	gchar *bdata;
	guint32 folder_id;
} CamelStoreDBFolderRecord;

void		camel_store_db_folder_record_clear	(CamelStoreDBFolderRecord *self);

/**
 * CamelStoreDBMessageRecord:
 * @folder_id: ID of the folder the message belongs to
 * @uid: Message UID
 * @flags: Camel Message info flags
 * @msg_type: unused
 * @dirty: whether the message info requires upload to the server; it corresponds to #CAMEL_MESSAGE_FOLDER_FLAGGED
 * @size: size of the mail
 * @dsent: date sent
 * @dreceived: date received
 * @subject: subject of the mail
 * @from: sender
 * @to: recipient
 * @cc: CC members
 * @mlist: message list headers
 * @part: part / references / thread id
 * @labels: labels of mails also called as userflags
 * @usertags: composite string of user tags
 * @cinfo: content info string - composite string
 * @bdata: provider specific data
 * @userheaders: value for user-defined message headers
 * @preview: message body preview
 *
 * A message record, with values stored in a #CamelStoreDB.
 *
 * Since: 3.58
 **/
typedef struct _CamelStoreDBMessageRecord {
	guint32 folder_id;
	const gchar *uid; /* stored in the string pool */
	guint32 flags;
	guint32 msg_type;
	guint32 dirty;
	guint32 size;
	gint64 dsent; /* time_t */
	gint64 dreceived; /* time_t */
	const gchar *subject;	/* stored in the string pool */
	const gchar *from;	/* stored in the string pool */
	const gchar *to;	/* stored in the string pool */
	const gchar *cc;	/* stored in the string pool */
	const gchar *mlist;	/* stored in the string pool */
	gchar *part;
	gchar *labels;
	gchar *usertags;
	gchar *cinfo;
	gchar *bdata;
	gchar *userheaders;
	gchar *preview;
} CamelStoreDBMessageRecord;

void		camel_store_db_message_record_clear	(CamelStoreDBMessageRecord *self);

/**
 * CAMEL_STORE_DB_FILE:
 *
 * File name used by the #CamelStore for the #CamelStoreDB.
 *
 * Since: 3.58
 **/
#define CAMEL_STORE_DB_FILE "folders.db"

/* Standard GObject macros */
#define CAMEL_TYPE_STORE_DB camel_store_db_get_type ()
#define CAMEL_STORE_DB(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CAMEL_TYPE_STORE_DB, CamelStoreDB))
#define CAMEL_STORE_DB_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), CAMEL_TYPE_STORE_DB, CamelStoreDBClass))
#define CAMEL_IS_STORE_DB(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CAMEL_TYPE_STORE_DB))
#define CAMEL_IS_STORE_DB_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE ((cls), CAMEL_TYPE_STORE_DB))
#define CAMEL_STORE_DB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), CAMEL_TYPE_STORE_DB, CamelStoreDBClass))

typedef struct _CamelStoreDB CamelStoreDB;
typedef struct _CamelStoreDBClass CamelStoreDBClass;
typedef struct _CamelStoreDBPrivate CamelStoreDBPrivate;

/**
 * CamelStoreDB:
 *
 * Since: 3.58
 **/
struct _CamelStoreDB {
	/*< private >*/
	CamelDB parent;
	CamelStoreDBPrivate *priv;
};

struct _CamelStoreDBClass {
	CamelDBClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

/**
 * CamelStoreDBReadMessagesFunc:
 * @storedb: a #CamelStoreDB
 * @record: a #CamelStoreDBMessageRecord which had been read
 * @user_data: callback user data
 *
 * A callback called in the camel_store_db_read_messages() for
 * each read message information. The members of the @record
 * are valid only in time of the callback being called.
 *
 * Returns: %TRUE to continue, %FALSE to stop further reading
 *
 * Since: 3.58
 **/
typedef gboolean (* CamelStoreDBReadMessagesFunc)	(CamelStoreDB *storedb,
							 const CamelStoreDBMessageRecord *record,
							 gpointer user_data);

GType		camel_store_db_get_type		(void) G_GNUC_CONST;
CamelStoreDB *	camel_store_db_new		(const gchar *filename,
						 GCancellable *cancellable,
						 GError **error);
gint		camel_store_db_get_int_key	(CamelStoreDB *self,
						 const gchar *key,
						 gint def_value);
gboolean	camel_store_db_set_int_key	(CamelStoreDB *self,
						 const gchar *key,
						 gint value,
						 GError **error);
gchar *		camel_store_db_dup_string_key	(CamelStoreDB *self,
						 const gchar *key);
gboolean	camel_store_db_set_string_key	(CamelStoreDB *self,
						 const gchar *key,
						 const gchar *value,
						 GError **error);
gboolean	camel_store_db_write_folder	(CamelStoreDB *self,
						 const gchar *folder_name,
						 const CamelStoreDBFolderRecord *record,
						 GError **error);
gboolean	camel_store_db_read_folder	(CamelStoreDB *self,
						 const gchar *folder_name,
						 CamelStoreDBFolderRecord *out_record,
						 GError **error);
guint32		camel_store_db_get_folder_id	(CamelStoreDB *self,
						 const gchar *folder_name);
gboolean	camel_store_db_rename_folder	(CamelStoreDB *self,
						 const gchar *old_folder_name,
						 const gchar *new_folder_name,
						 GError **error);
gboolean	camel_store_db_delete_folder	(CamelStoreDB *self,
						 const gchar *folder_name,
						 GError **error);
gboolean	camel_store_db_clear_folder	(CamelStoreDB *self,
						 const gchar *folder_name,
						 GError **error);
gboolean	camel_store_db_write_message	(CamelStoreDB *self,
						 const gchar *folder_name,
						 const CamelStoreDBMessageRecord *record,
						 GError **error);
gboolean	camel_store_db_read_message	(CamelStoreDB *self,
						 const gchar *folder_name,
						 const gchar *uid,
						 CamelStoreDBMessageRecord *out_record,
						 GError **error);
gboolean	camel_store_db_read_messages	(CamelStoreDB *self,
						 const gchar *folder_name,
						 CamelStoreDBReadMessagesFunc func,
						 gpointer user_data,
						 GError **error);
gboolean	camel_store_db_delete_message	(CamelStoreDB *self,
						 const gchar *folder_name,
						 const gchar *uid,
						 GError **error);
gboolean	camel_store_db_delete_messages	(CamelStoreDB *self,
						 const gchar *folder_name,
						 /* const */ GPtrArray *uids,
						 GError **error);
gboolean	camel_store_db_count_messages	(CamelStoreDB *self,
						 const gchar *folder_name,
						 CamelStoreDBCountKind kind,
						 guint32 *out_count,
						 GError **error);
GHashTable *	camel_store_db_dup_uids_with_flags /* gchar *uid ~> guint32 flags */
						(CamelStoreDB *self,
						 const gchar *folder_name,
						 GError **error);
GPtrArray *	camel_store_db_dup_junk_uids	(CamelStoreDB *self,
						 const gchar *folder_name,
						 GError **error);
GPtrArray *	camel_store_db_dup_deleted_uids	(CamelStoreDB *self,
						 const gchar *folder_name,
						 GError **error);
const gchar *	camel_store_db_util_get_column_for_header_name
						(const gchar *header_name);

G_END_DECLS

#endif /* CAMEL_STORE_DB_H */
