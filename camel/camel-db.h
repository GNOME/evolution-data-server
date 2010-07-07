/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#ifndef __CAMEL_DB_H
#define __CAMEL_DB_H
#include <sqlite3.h>
#include <glib.h>

/**
 * CAMEL_DB_FILE:
 *
 * Since: 2.24
 **/
#define CAMEL_DB_FILE "folders.db"

/* Hopefully no one will create a folder named EVO_IN_meM_hAnDlE */

/**
 * CAMEL_DB_IN_MEMORY_TABLE:
 *
 * Since: 2.26
 **/
#define CAMEL_DB_IN_MEMORY_TABLE "EVO_IN_meM_hAnDlE.temp"

/**
 * CAMEL_DB_IN_MEMORY_DB:
 *
 * Since: 2.26
 **/
#define CAMEL_DB_IN_MEMORY_DB "EVO_IN_meM_hAnDlE"

/**
 * CAMEL_DB_IN_MEMORY_TABLE_LIMIT:
 *
 * Since: 2.26
 **/
#define CAMEL_DB_IN_MEMORY_TABLE_LIMIT 100000

#include "camel-exception.h"

typedef struct _CamelDBPrivate CamelDBPrivate;

/**
 * CamelDBCollate:
 *
 * Since: 2.24
 **/
typedef gint(*CamelDBCollate)(gpointer ,int,gconstpointer ,int,gconstpointer );

/**
 * CamelDB:
 *
 * Since: 2.24
 **/
struct _CamelDB {
	sqlite3 *db;
	GMutex *lock;
	CamelDBPrivate *priv;
};

/**
 * CAMEL_DB_FREE_CACHE_SIZE:
 *
 * Since: 2.24
 **/
#define CAMEL_DB_FREE_CACHE_SIZE 2 * 1024 * 1024

/**
 * CAMEL_DB_SLEEP_INTERVAL:
 *
 * Since: 2.24
 **/
#define CAMEL_DB_SLEEP_INTERVAL 1*10*10

/**
 * CAMEL_DB_RELEASE_SQLITE_MEMORY:
 *
 * Since: 2.24
 **/
#define CAMEL_DB_RELEASE_SQLITE_MEMORY if(!g_getenv("CAMEL_SQLITE_FREE_CACHE")) sqlite3_release_memory(CAMEL_DB_FREE_CACHE_SIZE);

/**
 * CAMEL_DB_USE_SHARED_CACHE:
 *
 * Since: 2.24
 **/
#define CAMEL_DB_USE_SHARED_CACHE if(g_getenv("CAMEL_SQLITE_SHARED_CACHE")) sqlite3_enable_shared_cache(TRUE);

/**
 * CamelMIRecord:
 * @uid:
 *	Message UID
 * @flags:
 *	Camel Message info flags
 * @msg_type:
 * @dirty:
 * @read:
 *	boolean read status
 * @deleted:
 *	boolean deleted status
 * @replied:
 *	boolean replied status
 * @important:
 *	boolean important status
 * @junk:
 *	boolean junk status
 * @attachment:
 *	boolean attachment status
 * @size:
 *	size of the mail
 * @dsent:
 *	date sent
 * @dreceived:
 *	date received
 * @subject:
 *	subject of the mail
 * @from:
 *	sender
 * @to:
 *	recipient
 * @cc:
 *	CC members
 * @mlist:
 *	message list headers
 * @followup_flag:
 *	followup flag / also can be queried to see for followup or not
 * @followup_completed_on:
 *	completed date, can be used to see if completed
 * @followup_due_by:
 *	to see the due by date
 * @part:
 *	part / references / thread id
 * @labels:
 *	labels of mails also called as userflags
 * @usertags:
 *	composite string of user tags
 * @cinfo:
 *	content info string - composite string
 * @bdata:
 *	provider specific data
 * @bodystructure:
 *
 * The extensive DB format, supporting basic searching and sorting.
 *
 * Since: 2.24
 **/
typedef struct _CamelMIRecord {
	gchar *uid;
	guint32 flags;
	guint32 msg_type;
	guint32 dirty;
	gboolean read;
	gboolean deleted;
	gboolean replied;
	gboolean important;
	gboolean junk;
	gboolean attachment;
	guint32 size;
	time_t dsent;
	time_t dreceived;
	gchar *subject;
	gchar *from;
	gchar *to;
	gchar *cc;
	gchar *mlist;
	gchar *followup_flag;
	gchar *followup_completed_on;
	gchar *followup_due_by;
	gchar *part;
	gchar *labels;
	gchar *usertags;
	gchar *cinfo;
	gchar *bdata;
	gchar *bodystructure;
} CamelMIRecord;

/**
 * CamelFIRecord:
 *
 * Since: 2.24
 **/
typedef struct _CamelFIRecord {
	gchar *folder_name;
	guint32 version;
	guint32 flags;
	guint32 nextuid;
	time_t time;
	guint32 saved_count;
	guint32 unread_count;
	guint32 deleted_count;
	guint32 junk_count;
	guint32 visible_count;
	guint32 jnd_count;  /* Junked not deleted */
	gchar *bdata;
} CamelFIRecord;

typedef struct _CamelDB CamelDB;

/**
 * CamelDBSelectCB:
 *
 * Since: 2.24
 **/
typedef gint (*CamelDBSelectCB) (gpointer data, gint ncol, gchar **colvalues, gchar **colnames);

CamelDB * camel_db_open (const gchar *path, CamelException *ex);
CamelDB * camel_db_clone (CamelDB *cdb, CamelException *ex);
void camel_db_close (CamelDB *cdb);
gint camel_db_command (CamelDB *cdb, const gchar *stmt, CamelException *ex);

gint camel_db_transaction_command (CamelDB *cdb, GSList *qry_list, CamelException *ex);

gint camel_db_begin_transaction (CamelDB *cdb, CamelException *ex);
gint camel_db_add_to_transaction (CamelDB *cdb, const gchar *query, CamelException *ex);
gint camel_db_end_transaction (CamelDB *cdb, CamelException *ex);
gint camel_db_abort_transaction (CamelDB *cdb, CamelException *ex);
gint camel_db_clear_folder_summary (CamelDB *cdb, gchar *folder, CamelException *ex);
gint camel_db_rename_folder (CamelDB *cdb, const gchar *old_folder, const gchar *new_folder, CamelException *ex);

gint camel_db_delete_folder (CamelDB *cdb, const gchar *folder, CamelException *ex);
gint camel_db_delete_uid (CamelDB *cdb, const gchar *folder, const gchar *uid, CamelException *ex);
/*int camel_db_delete_uids (CamelDB *cdb, CamelException *ex, gint nargs, ... );*/
gint camel_db_delete_uids (CamelDB *cdb, const gchar * folder_name, GSList *uids, CamelException *ex);
gint camel_db_delete_vuids (CamelDB *cdb, const gchar * folder_name, const gchar *shash, GSList *uids, CamelException *ex);

gint camel_db_create_folders_table (CamelDB *cdb, CamelException *ex);
gint camel_db_select (CamelDB *cdb, const gchar * stmt, CamelDBSelectCB callback, gpointer data, CamelException *ex);

gint camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex);
gint camel_db_read_folder_info_record (CamelDB *cdb, const gchar *folder_name, CamelFIRecord **record, CamelException *ex);

gint camel_db_prepare_message_info_table (CamelDB *cdb, const gchar *folder_name, CamelException *ex);

gint camel_db_write_message_info_record (CamelDB *cdb, const gchar *folder_name, CamelMIRecord *record, CamelException *ex);
gint camel_db_write_fresh_message_info_record (CamelDB *cdb, const gchar *folder_name, CamelMIRecord *record, CamelException *ex);
gint camel_db_read_message_info_records (CamelDB *cdb, const gchar *folder_name, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex);
gint camel_db_read_message_info_record_with_uid (CamelDB *cdb, const gchar *folder_name, const gchar *uid, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex);

gint camel_db_count_junk_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);
gint camel_db_count_unread_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);
gint camel_db_count_deleted_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);
gint camel_db_count_total_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);

gint camel_db_count_visible_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);
gint camel_db_count_visible_unread_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);

gint camel_db_count_junk_not_deleted_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex);
gint camel_db_count_message_info (CamelDB *cdb, const gchar *query, guint32 *count, CamelException *ex);
void camel_db_camel_mir_free (CamelMIRecord *record);

gint camel_db_create_vfolder (CamelDB *db, const gchar *folder_name, CamelException *ex);
gint camel_db_recreate_vfolder (CamelDB *db, const gchar *folder_name, CamelException *ex);
gint camel_db_delete_uid_from_vfolder (CamelDB *db, gchar *folder_name, gchar *vuid, CamelException *ex);
gint camel_db_delete_uid_from_vfolder_transaction (CamelDB *db, const gchar *folder_name, const gchar *vuid, CamelException *ex);
GPtrArray * camel_db_get_vuids_from_vfolder (CamelDB *db, gchar *folder_name, gchar *filter, CamelException *ex);
gint camel_db_add_to_vfolder (CamelDB *db, gchar *folder_name, gchar *vuid, CamelException *ex);
gint camel_db_add_to_vfolder_transaction (CamelDB *db, const gchar *folder_name, const gchar *vuid, CamelException *ex);

gint camel_db_get_folder_uids (CamelDB *db, const gchar *folder_name, const gchar *sort_by, const gchar *collate, GPtrArray *array, CamelException *ex);
gint camel_db_get_folder_uids_flags (CamelDB *db, const gchar *folder_name, const gchar *sort_by, const gchar *collate, GPtrArray *summary, GHashTable *table, CamelException *ex);

GPtrArray * camel_db_get_folder_junk_uids (CamelDB *db, gchar *folder_name, CamelException *ex);
GPtrArray * camel_db_get_folder_deleted_uids (CamelDB *db, gchar *folder_name, CamelException *ex);

gchar * camel_db_sqlize_string (const gchar *string);
void camel_db_free_sqlized_string (gchar *string);

gchar * camel_db_get_column_name (const gchar *raw_name);
gint camel_db_set_collate (CamelDB *cdb, const gchar *col, const gchar *collate, CamelDBCollate func);
/* Migration APIS */
gint camel_db_migrate_vfolders_to_14(CamelDB *cdb, const gchar *folder, CamelException *ex);

gint camel_db_start_in_memory_transactions (CamelDB *cdb, CamelException *ex);
gint camel_db_flush_in_memory_transactions (CamelDB *cdb, const gchar * folder_name, CamelException *ex);

GHashTable *
camel_db_get_folder_preview (CamelDB *db, gchar *folder_name, CamelException *ex);
gint camel_db_write_preview_record (CamelDB *db, gchar *folder_name, const gchar *uid, const gchar *msg, CamelException *ex);

gint
camel_db_reset_folder_version (CamelDB *cdb, const gchar *folder_name, gint reset_version, CamelException *ex);
#endif

