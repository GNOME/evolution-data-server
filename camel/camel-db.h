/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#ifndef __CAMEL_DB_H
#define __CAMEL_DB_H
#include <sqlite3.h>
#include <glib.h>
#define CAMEL_DB_FILE "folders.db"

#include "camel-exception.h"

typedef struct _CamelDBPrivate CamelDBPrivate;

typedef int(*CamelDBCollate)(void*,int,const void*,int,const void*);

struct _CamelDB {
	sqlite3 *db;
	GMutex *lock;
	CamelDBPrivate *priv;
};

#define CAMEL_DB_FREE_CACHE_SIZE 2 * 1024 * 1024
#define CAMEL_DB_SLEEP_INTERVAL 1*10*10
#define CAMEL_DB_RELEASE_SQLITE_MEMORY if(!g_getenv("CAMEL_SQLITE_FREE_CACHE")) sqlite3_release_memory(CAMEL_DB_FREE_CACHE_SIZE);
#define CAMEL_DB_USE_SHARED_CACHE if(g_getenv("CAMEL_SQLITE_SHARED_CACHE")) sqlite3_enable_shared_cache(TRUE);


/* The extensive DB format, supporting basic searching and sorting
  uid, - Message UID
  flags, - Camel Message info flags
  unread/read, - boolean read/unread status
  deleted, - boolean deleted status
  replied, - boolean replied status
  imp, - boolean important status
  junk, - boolean junk status
  size, - size of the mail
  attachment, boolean attachment status
  dsent, - sent date
  dreceived, - received date
  subject, - subject of the mail
  from, - sender
  to, - recipient
  cc, - CC members
  mlist, - message list headers
  follow-up-flag, - followup flag / also can be queried to see for followup or not
  completed-on-set, - completed date, can be used to see if completed
  due-by,  - to see the due by date
  Location - This can be derived from the database location/name. No need to store.
  label, - labels of mails also called as userflags
  usertags, composite string of user tags
  cinfo, content info string - composite string
  bdata, provider specific data
  part, part/references/thread id
*/

typedef struct _CamelMIRecord {
	char *uid;
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
	char *subject;
	char *from;
	char *to;
	char *cc;
	char *mlist;
	char *followup_flag;
	char *followup_completed_on;
	char *followup_due_by;
	char *part;
	char *labels;
	char *usertags;
	char *cinfo;
	char *bdata;
} CamelMIRecord;

typedef struct _CamelFIRecord {
	char *folder_name;
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
	char *bdata;
} CamelFIRecord;




typedef struct _CamelDB CamelDB;
typedef int (*CamelDBSelectCB) (gpointer data, int ncol, char **colvalues, char **colnames);


CamelDB * camel_db_open (const char *path, CamelException *ex);
CamelDB * camel_db_clone (CamelDB *cdb, CamelException *ex);
void camel_db_close (CamelDB *cdb);
int camel_db_command (CamelDB *cdb, const char *stmt, CamelException *ex);

int camel_db_transaction_command (CamelDB *cdb, GSList *qry_list, CamelException *ex);

int camel_db_begin_transaction (CamelDB *cdb, CamelException *ex);
int camel_db_add_to_transaction (CamelDB *cdb, const char *query, CamelException *ex);
int camel_db_end_transaction (CamelDB *cdb, CamelException *ex);
int camel_db_abort_transaction (CamelDB *cdb, CamelException *ex);
int camel_db_clear_folder_summary (CamelDB *cdb, char *folder, CamelException *ex);
int camel_db_rename_folder (CamelDB *cdb, const char *old_folder, const char *new_folder, CamelException *ex);

int camel_db_delete_folder (CamelDB *cdb, const char *folder, CamelException *ex);
int camel_db_delete_uid (CamelDB *cdb, const char *folder, const char *uid, CamelException *ex);
/*int camel_db_delete_uids (CamelDB *cdb, CamelException *ex, int nargs, ... );*/
int camel_db_delete_uids (CamelDB *cdb, const char* folder_name, GSList *uids, CamelException *ex);
int camel_db_delete_vuids (CamelDB *cdb, const char* folder_name, char *shash, GSList *uids, CamelException *ex);

int camel_db_create_folders_table (CamelDB *cdb, CamelException *ex);
int camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data, CamelException *ex);

int camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex);
int camel_db_read_folder_info_record (CamelDB *cdb, const char *folder_name, CamelFIRecord **record, CamelException *ex);

int camel_db_prepare_message_info_table (CamelDB *cdb, const char *folder_name, CamelException *ex);

int camel_db_write_message_info_record (CamelDB *cdb, const char *folder_name, CamelMIRecord *record, CamelException *ex);
int camel_db_read_message_info_records (CamelDB *cdb, const char *folder_name, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex);
int camel_db_read_message_info_record_with_uid (CamelDB *cdb, const char *folder_name, const char *uid, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex);

int camel_db_count_junk_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);
int camel_db_count_unread_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);
int camel_db_count_deleted_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);
int camel_db_count_total_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);

int camel_db_count_visible_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);
int camel_db_count_visible_unread_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);

int camel_db_count_junk_not_deleted_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex);
int camel_db_count_message_info (CamelDB *cdb, const char *query, guint32 *count, CamelException *ex);
void camel_db_camel_mir_free (CamelMIRecord *record);

int camel_db_create_vfolder (CamelDB *db, const char *folder_name, CamelException *ex);
int camel_db_recreate_vfolder (CamelDB *db, const char *folder_name, CamelException *ex);
int camel_db_delete_uid_from_vfolder (CamelDB *db, char *folder_name, char *vuid, CamelException *ex);
int camel_db_delete_uid_from_vfolder_transaction (CamelDB *db, char *folder_name, char *vuid, CamelException *ex);
GPtrArray * camel_db_get_vuids_from_vfolder (CamelDB *db, char *folder_name, char *filter, CamelException *ex);
int camel_db_add_to_vfolder (CamelDB *db, char *folder_name, char *vuid, CamelException *ex);
int camel_db_add_to_vfolder_transaction (CamelDB *db, char *folder_name, char *vuid, CamelException *ex);

int camel_db_get_folder_uids (CamelDB *db, char *folder_name, char *sort_by, char *collate, GPtrArray *array, CamelException *ex);

GPtrArray * camel_db_get_folder_junk_uids (CamelDB *db, char *folder_name, CamelException *ex);
GPtrArray * camel_db_get_folder_deleted_uids (CamelDB *db, char *folder_name, CamelException *ex);

char * camel_db_sqlize_string (const char *string);
void camel_db_free_sqlized_string (char *string);

char * camel_db_get_column_name (const char *raw_name);
int camel_db_set_collate (CamelDB *cdb, const char *col, const char *collate, CamelDBCollate func);
/* Migration APIS */
int camel_db_migrate_vfolders_to_14(CamelDB *cdb, const char *folder, CamelException *ex);
#endif

