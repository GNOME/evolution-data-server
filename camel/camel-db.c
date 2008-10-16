/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c: class for an imap folder */

/* 
 * Authors:
 *   Sankar P <psankar@novell.com>
 *   Srinivasa Ragavan <sragavan@novell.com> 
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

#include "camel-db.h"
#include "camel-string-utils.h"

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-debug.h"

#define d(x) if (camel_debug("sqlite")) x
#define START(stmt) 	if (camel_debug("dbtime")) { g_print ("\n===========\nDB SQL operation [%s] started\n", stmt); if (!cdb->priv->timer) { cdb->priv->timer = g_timer_new (); } else { g_timer_reset(cdb->priv->timer);} }
#define END 	if (camel_debug("dbtime")) { g_timer_stop (cdb->priv->timer); g_print ("DB Operation ended. Time Taken : %f\n###########\n", g_timer_elapsed (cdb->priv->timer, NULL)); }

struct _CamelDBPrivate {
	GTimer *timer;
	char *file_name;
};

static GStaticRecMutex trans_lock = G_STATIC_REC_MUTEX_INIT;	

static int 
cdb_sql_exec (sqlite3 *db, const char* stmt, CamelException *ex) 
{
	char *errmsg = NULL;
	int   ret = -1;

	d(g_print("Camel SQL Exec:\n%s\n", stmt));

	ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);
	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED || ret == -1) {
		if (errmsg) {
			sqlite3_free (errmsg);
			errmsg = NULL;
		}
		ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);
	}


	if (ret != SQLITE_OK) {
		d(g_print ("Error in SQL EXEC statement: %s [%s].\n", stmt, errmsg));
		if (ex)	
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _(errmsg));
		sqlite3_free (errmsg);
		errmsg = NULL;
		return -1;
	}

	if (errmsg) {
		sqlite3_free (errmsg);
		errmsg = NULL;
	}

	return 0;
}

CamelDB *
camel_db_open (const char *path, CamelException *ex)
{
	CamelDB *cdb;
	sqlite3 *db;
	char *cache;
	int ret;

	CAMEL_DB_USE_SHARED_CACHE;
	
	ret = sqlite3_open(path, &db);
	if (ret) {

		if (!db) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Insufficient memory"));
		} else {
			const char *error;
			error = sqlite3_errmsg (db);
			d(g_print("Can't open database %s: %s\n", path, error));
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _(error));
			sqlite3_close(db);
		}
		return NULL;
	}



	cdb = g_new (CamelDB, 1);
	cdb->db = db;
	cdb->lock = g_mutex_new ();
	cdb->priv = g_new(CamelDBPrivate, 1);
	cdb->priv->file_name = g_strdup(path);
	cdb->priv->timer = NULL;
	d(g_print ("\nDatabase succesfully opened  \n"));

	/* Which is big / costlier ? A Stack frame or a pointer */
	if(g_getenv("CAMEL_SQLITE_DEFAULT_CACHE_SIZE")!=NULL)
		cache = g_strdup_printf ("PRAGMA cache_size=%s", g_getenv("CAMEL_SQLITE_DEFAULT_CACHE_SIZE"));
	else 
		cache = g_strdup ("PRAGMA cache_size=100");

	camel_db_command (cdb, cache, NULL);

	g_free (cache);

	sqlite3_busy_timeout (cdb->db, CAMEL_DB_SLEEP_INTERVAL);

	return cdb;
}

CamelDB *
camel_db_clone (CamelDB *cdb, CamelException *ex)
{
	return camel_db_open(cdb->priv->file_name, ex);
}

void
camel_db_close (CamelDB *cdb)
{
	if (cdb) {
		sqlite3_close (cdb->db);
		g_mutex_free (cdb->lock);
		g_free (cdb);
		d(g_print ("\nDatabase succesfully closed \n"));
	}
}

int
camel_db_set_collate (CamelDB *cdb, const char *col, const char *collate, CamelDBCollate func)
{
		int ret = 0;

		if (!cdb)
			return 0;

		g_mutex_lock (cdb->lock);
		d(g_print("Creating Collation %s on %s with %p\n", collate, col, func));
		if (collate && func)
			ret = sqlite3_create_collation(cdb->db, collate, SQLITE_UTF8,  NULL, func);
		g_mutex_unlock (cdb->lock);

		return ret;
}

/* Should this be really exposed ? */
int
camel_db_command (CamelDB *cdb, const char *stmt, CamelException *ex)
{
		int ret;

		if (!cdb)
			return TRUE;
		g_mutex_lock (cdb->lock);

		START(stmt);
		ret = cdb_sql_exec (cdb->db, stmt, ex);
		END;
		g_mutex_unlock (cdb->lock);

		return ret;
}


int 
camel_db_begin_transaction (CamelDB *cdb, CamelException *ex)
{
	if (!cdb)
		return -1;
	g_static_rec_mutex_lock (&trans_lock);

	g_mutex_lock (cdb->lock);
	return (cdb_sql_exec (cdb->db, "BEGIN", ex));
}

int 
camel_db_end_transaction (CamelDB *cdb, CamelException *ex)
{
	int ret;
	if (!cdb)
		return -1;

	START("COMMIT");
	ret = cdb_sql_exec (cdb->db, "COMMIT", ex);
	END;
	g_mutex_unlock (cdb->lock);
	g_static_rec_mutex_unlock (&trans_lock);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	
	return ret;
}

int
camel_db_abort_transaction (CamelDB *cdb, CamelException *ex)
{
	int ret;
	
	ret = cdb_sql_exec (cdb->db, "ROLLBACK", ex);
	g_mutex_unlock (cdb->lock);
	g_static_rec_mutex_unlock (&trans_lock);	
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	
	return ret;
}

int
camel_db_add_to_transaction (CamelDB *cdb, const char *stmt, CamelException *ex)
{
	if (!cdb)
		return -1;

	return (cdb_sql_exec (cdb->db, stmt, ex));
}

int 
camel_db_transaction_command (CamelDB *cdb, GSList *qry_list, CamelException *ex)
{
	int ret;
	const char *query;

	if (!cdb)
		return -1;

	g_mutex_lock (cdb->lock);
	START("BEGIN");
	ret = cdb_sql_exec (cdb->db, "BEGIN", ex);
	if (ret)
		goto end;


	while (qry_list) {
		query = qry_list->data;
		ret = cdb_sql_exec (cdb->db, query, ex);
		if (ret)
			goto end;
		qry_list = g_slist_next (qry_list);
	}

	ret = cdb_sql_exec (cdb->db, "COMMIT", ex);
	END;
end:
	g_mutex_unlock (cdb->lock);
	return ret;
}

static int 
count_cb (void *data, int argc, char **argv, char **azColName)
{
  	int i;

  	for(i=0; i<argc; i++) {
		if (strstr(azColName[i], "COUNT")) {
			*(guint32 *)data = argv [i] ? strtoul (argv [i], NULL, 10) : 0;
		}
  	}

  	return 0;
}

int
camel_db_count_message_info (CamelDB *cdb, const char *query, guint32 *count, CamelException *ex)
{
	int ret = -1;
	char *errmsg = NULL;

	g_mutex_lock (cdb->lock);

	START(query);
	ret = sqlite3_exec(cdb->db, query, count_cb, count, &errmsg);
	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED) {
		if (errmsg) {
			sqlite3_free (errmsg);
			errmsg = NULL;
		}

		ret = sqlite3_exec (cdb->db, query, count_cb, count, &errmsg);
	}

	END;

	g_mutex_unlock (cdb->lock);
	
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
		
	if (ret != SQLITE_OK) {
		d(g_print ("Error in SQL SELECT statement: %s [%s]\n", query, errmsg));
		if (ex)
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _(errmsg));
		sqlite3_free (errmsg);
		errmsg = NULL;
	}

	if (errmsg) {
		sqlite3_free (errmsg);
		errmsg = NULL;
	}

	return ret;
}

int
camel_db_count_junk_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;
	char *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE junk = 1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_count_unread_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;
	char *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE read = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_count_visible_unread_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;
	char *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE read = 0 AND junk = 0 AND deleted = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_count_visible_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;
	char *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE junk = 0 AND deleted = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_count_junk_not_deleted_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;
	char *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE junk = 1 AND deleted = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_count_deleted_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{
	int ret;
	char *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE deleted = 1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}


int
camel_db_count_total_message_info (CamelDB *cdb, const char *table_name, guint32 *count, CamelException *ex)
{

	int ret;
	char *query;

	if (!cdb)
		return -1;
	
	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

int
camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data, CamelException *ex) 
{
  	char *errmsg = NULL;
  	//int nrecs = 0;
	int ret = -1;

	if (!cdb)
		return ret;
	
	d(g_print ("\n%s:\n%s \n", __FUNCTION__, stmt));
	g_mutex_lock (cdb->lock);

	START(stmt);
	ret = sqlite3_exec(cdb->db, stmt, callback, data, &errmsg);
	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED) {
		if (errmsg) {
			sqlite3_free (errmsg);
			errmsg = NULL;
		}

		ret = sqlite3_exec (cdb->db, stmt, callback, data, &errmsg);
	}

	END;

	g_mutex_unlock (cdb->lock);
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
		
  	if (ret != SQLITE_OK) {
    		d(g_warning ("Error in select statement '%s' [%s].\n", stmt, errmsg));
		if (ex)
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, errmsg);
		sqlite3_free (errmsg);
		errmsg = NULL;
  	}

	if (errmsg) {
		sqlite3_free (errmsg);
		errmsg = NULL;
	}

	return ret;
}

int
camel_db_create_vfolder (CamelDB *db, const char *folder_name, CamelException *ex)
{
	int ret;
	char *table_creation_query, *safe_index;
	
	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q (  vuid TEXT PRIMARY KEY)", folder_name);

	ret = camel_db_command (db, table_creation_query, ex);	

	sqlite3_free (table_creation_query);


	safe_index = g_strdup_printf("VINDEX-%s", folder_name);
	table_creation_query = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (vuid)", safe_index, folder_name);
	ret = camel_db_command (db, table_creation_query, ex);

	sqlite3_free (table_creation_query);
	g_free (safe_index);
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	
	return ret;	 
}

int
camel_db_recreate_vfolder (CamelDB *db, const char *folder_name, CamelException *ex)
{
	int ret;
	char *table_query;
	
	table_query = sqlite3_mprintf ("DROP TABLE %Q", folder_name);

	ret = camel_db_command (db, table_query, ex);	

	sqlite3_free (table_query);

	
	return camel_db_create_vfolder (db, folder_name, ex); 
}

int
camel_db_delete_uid_from_vfolder (CamelDB *db, char *folder_name, char *vuid, CamelException *ex)
{
	 char *del_query;
	 int ret;
	 
	 del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE vuid = %Q", folder_name, vuid);

	 ret = camel_db_command (db, del_query, ex);
	 
	 sqlite3_free (del_query);
	 CAMEL_DB_RELEASE_SQLITE_MEMORY;
	 return ret;
}

int
camel_db_delete_uid_from_vfolder_transaction (CamelDB *db, char *folder_name, char *vuid, CamelException *ex)
{
	char *del_query;
	int ret;
	 
	del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE vuid = %Q", folder_name, vuid);

	ret = camel_db_add_to_transaction (db, del_query, ex);
	 
	sqlite3_free (del_query);

	return ret;
}

static int
read_uids_callback (void *ref, int ncol, char ** cols, char ** name)
{
	GPtrArray *array = (GPtrArray *) ref;
	 
	#if 0
	int i;
	for (i = 0; i < ncol; ++i) {
		if (!strcmp (name [i], "uid"))
			g_ptr_array_add (array, (char *) (camel_pstring_strdup(cols [i])));
	}
	#else
			g_ptr_array_add (array, (char *) (camel_pstring_strdup(cols [0])));
	#endif
	 
	 return 0;
}

int
camel_db_get_folder_uids (CamelDB *db, char *folder_name, char *sort_by, char *collate, GPtrArray *array, CamelException *ex)
{
	 char *sel_query;
	 int ret;

	 sel_query = sqlite3_mprintf("SELECT uid FROM %Q%s%s%s%s", folder_name, sort_by ? " order by " : "", sort_by ? sort_by: "", (sort_by && collate) ? " collate " : "", (sort_by && collate) ? collate : "");

	 ret = camel_db_select (db, sel_query, read_uids_callback, array, ex);
	 sqlite3_free (sel_query);

	 return ret;
}

GPtrArray *
camel_db_get_folder_junk_uids (CamelDB *db, char *folder_name, CamelException *ex)
{
	 char *sel_query;
	 int ret;
	 GPtrArray *array = g_ptr_array_new();
	 
	 sel_query = sqlite3_mprintf("SELECT uid FROM %Q where junk=1", folder_name);

	 ret = camel_db_select (db, sel_query, read_uids_callback, array, ex);

	 sqlite3_free (sel_query);

	 if (!array->len || ret != 0) {
		 g_ptr_array_free (array, TRUE);
		 array = NULL;
	 } 
	 return array;
}

GPtrArray *
camel_db_get_folder_deleted_uids (CamelDB *db, char *folder_name, CamelException *ex)
{
	 char *sel_query;
	 int ret;
	 GPtrArray *array = g_ptr_array_new();
	 
	 sel_query = sqlite3_mprintf("SELECT uid FROM %Q where deleted=1", folder_name);

	 ret = camel_db_select (db, sel_query, read_uids_callback, array, ex);
	 sqlite3_free (sel_query);

	 if (!array->len || ret != 0) {
		 g_ptr_array_free (array, TRUE);
		 array = NULL;
	 }
		 
	 return array;
}

static int
read_vuids_callback (void *ref, int ncol, char ** cols, char ** name)
{
	 GPtrArray *array = (GPtrArray *)ref;
	 
	 #if 0
	 int i;
	 

	 for (i = 0; i < ncol; ++i) {
		  if (!strcmp (name [i], "vuid"))
			   g_ptr_array_add (array, (char *) (camel_pstring_strdup(cols [i]+8)));
	 }
	 #else
			   g_ptr_array_add (array, (char *) (camel_pstring_strdup(cols [0]+8)));
	 #endif

	 return 0;
}

GPtrArray *
camel_db_get_vuids_from_vfolder (CamelDB *db, char *folder_name, char *filter, CamelException *ex)
{
	 char *sel_query;
	 char *cond = NULL;
	 GPtrArray *array;
	 char *tmp = g_strdup_printf("%s%%", filter ? filter:"");
	 if(filter) 
		  cond = sqlite3_mprintf(" WHERE vuid LIKE %Q", tmp);
	 g_free(tmp);
	 sel_query = sqlite3_mprintf("SELECT vuid FROM %Q%s", folder_name, filter ? cond : "");

	 if (cond)
		  sqlite3_free (cond);
	 #warning "handle return values"
	 #warning "No The caller should parse the ex in case of NULL returns" 
	 array = g_ptr_array_new ();
	 camel_db_select (db, sel_query, read_vuids_callback, array, ex);
	 sqlite3_free (sel_query);
	 /* We make sure to return NULL if we don't get anything. Be good to your caller */ 
	 if (!array->len) {
		  g_ptr_array_free (array, TRUE);
		  array = NULL;
	 }

	 return array;
}

int
camel_db_add_to_vfolder (CamelDB *db, char *folder_name, char *vuid, CamelException *ex)
{
	 char *ins_query;
	 int ret;
	 
	 ins_query = sqlite3_mprintf ("INSERT INTO %Q VALUES (%Q)", folder_name, vuid);

	 ret = camel_db_command (db, ins_query, ex);
	 
	 sqlite3_free (ins_query);
	 CAMEL_DB_RELEASE_SQLITE_MEMORY;
	 return ret;
}

int
camel_db_add_to_vfolder_transaction (CamelDB *db, char *folder_name, char *vuid, CamelException *ex)
{
	 char *ins_query;
	 int ret;
	 
	 ins_query = sqlite3_mprintf ("INSERT INTO %Q VALUES (%Q)", folder_name, vuid);

	 ret = camel_db_add_to_transaction (db, ins_query, ex);
	 
	 sqlite3_free (ins_query);

	 return ret;
}


int
camel_db_create_folders_table (CamelDB *cdb, CamelException *ex)
{
	char *query = "CREATE TABLE IF NOT EXISTS folders ( folder_name TEXT PRIMARY KEY, version REAL, flags INTEGER, nextuid INTEGER, time NUMERIC, saved_count INTEGER, unread_count INTEGER, deleted_count INTEGER, junk_count INTEGER, visible_count INTEGER, jnd_count INTEGER, bdata TEXT )";
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ((camel_db_command (cdb, query, ex)));
}

int 
camel_db_prepare_message_info_table (CamelDB *cdb, const char *folder_name, CamelException *ex)
{
	int ret;
	char *table_creation_query, *safe_index;

	/* README: It is possible to compress all system flags into a single column and use just as userflags but that makes querying for other applications difficult an d bloats the parsing code. Instead, it is better to bloat the tables. Sqlite should have some optimizations for sparse columns etc. */

	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q (  uid TEXT PRIMARY KEY , flags INTEGER , msg_type INTEGER , read INTEGER , deleted INTEGER , replied INTEGER , important INTEGER , junk INTEGER , attachment INTEGER , msg_security INTEGER , size INTEGER , dsent NUMERIC , dreceived NUMERIC , subject TEXT , mail_from TEXT , mail_to TEXT , mail_cc TEXT , mlist TEXT , followup_flag TEXT , followup_completed_on TEXT , followup_due_by TEXT , part TEXT , labels TEXT , usertags TEXT , cinfo TEXT , bdata TEXT )", folder_name);

	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);

	sqlite3_free (table_creation_query);

	/* FIXME: sqlize folder_name before you create the index */
	safe_index = g_strdup_printf("SINDEX-%s", folder_name);
	table_creation_query = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (uid, flags, size, dsent, dreceived, subject, mail_from, mail_to, mail_cc, mlist, part, labels, usertags, cinfo, bdata)", safe_index, folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	g_free (safe_index);
	sqlite3_free (table_creation_query);
	
	return ret;
}

int
camel_db_write_message_info_record (CamelDB *cdb, const char *folder_name, CamelMIRecord *record, CamelException *ex)
{
	int ret;
	char *del_query;
	char *ins_query;

	/* NB: UGLIEST Hack. We can't modify the schema now. We are using msg_security (an unsed one to notify of FLAGGED/Dirty infos */

	ins_query = sqlite3_mprintf ("INSERT INTO %Q VALUES (%Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %ld, %ld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q )", 
			folder_name, record->uid, record->flags,
			record->msg_type, record->read, record->deleted, record->replied,
			record->important, record->junk, record->attachment, record->dirty,
			record->size, record->dsent, record->dreceived,
			record->subject, record->from, record->to,
			record->cc, record->mlist, record->followup_flag,
			record->followup_completed_on, record->followup_due_by, 
			record->part, record->labels, record->usertags,
			record->cinfo, record->bdata);

	del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", folder_name, record->uid);

#if 0
	char *upd_query;

	upd_query = g_strdup_printf ("IMPLEMENT AND THEN TRY");
	camel_db_command (cdb, upd_query, ex);
	g_free (upd_query);
#else

	ret = camel_db_add_to_transaction (cdb, del_query, ex);
	ret = camel_db_add_to_transaction (cdb, ins_query, ex);

#endif

	sqlite3_free (del_query);
	sqlite3_free (ins_query);

	return ret;
}

int
camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex)
{
	int ret;

	char *del_query;
	char *ins_query;

	ins_query = sqlite3_mprintf ("INSERT INTO folders VALUES ( %Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %Q ) ", 
			record->folder_name, record->version,
								 record->flags, record->nextuid, record->time,
			record->saved_count, record->unread_count,
								 record->deleted_count, record->junk_count, record->visible_count, record->jnd_count, record->bdata); 

	del_query = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", record->folder_name);


#if 0
	char *upd_query;
	
	upd_query = g_strdup_printf ("UPDATE folders SET version = %d, flags = %d, nextuid = %d, time = 143, saved_count = %d, unread_count = %d, deleted_count = %d, junk_count = %d, bdata = %s, WHERE folder_name = %Q", record->version, record->flags, record->nextuid, record->saved_count, record->unread_count, record->deleted_count, record->junk_count, "PROVIDER SPECIFIC DATA", record->folder_name );
	camel_db_command (cdb, upd_query, ex);
	g_free (upd_query);
#else

	ret = camel_db_add_to_transaction (cdb, del_query, ex);
	ret = camel_db_add_to_transaction (cdb, ins_query, ex);

#endif

	sqlite3_free (del_query);
	sqlite3_free (ins_query);

	return ret;
}

static int 
read_fir_callback (void * ref, int ncol, char ** cols, char ** name)
{
	CamelFIRecord *record = *(CamelFIRecord **) ref;
	int i;

	d(g_print ("\nread_fir_callback called \n"));
#if 0
	record->folder_name = cols [0];
	record->version = cols [1];
	/* Just a sequential mapping of struct members to columns is enough I guess. 
	Needs some checking */
#else
	
	for (i = 0; i < ncol; ++i) {
		if (!strcmp (name [i], "folder_name"))
			record->folder_name = g_strdup(cols [i]);

		else if (!strcmp (name [i], "version"))
			record->version = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "flags"))
			record->flags = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "nextuid"))
			record->nextuid = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "time"))
			record->time = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "saved_count"))
			record->saved_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "unread_count"))
			record->unread_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "deleted_count"))
			record->deleted_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;

		else if (!strcmp (name [i], "junk_count"))
			record->junk_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "visible_count"))
			record->visible_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "jnd_count"))
			record->jnd_count = cols [i] ? strtoul (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "bdata"))
			record->bdata = g_strdup (cols [i]);
	
	}
#endif 
	return 0;
}

int
camel_db_read_folder_info_record (CamelDB *cdb, const char *folder_name, CamelFIRecord **record, CamelException *ex)
{
	char *query;
	int ret;

	query = sqlite3_mprintf ("SELECT * FROM folders WHERE folder_name = %Q", folder_name);
	ret = camel_db_select (cdb, query, read_fir_callback, record, ex);

	sqlite3_free (query);
	return (ret);
}

int
camel_db_read_message_info_record_with_uid (CamelDB *cdb, const char *folder_name, const char *uid, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex)
{
	char *query;
	int ret;

	query = sqlite3_mprintf ("SELECT uid, flags, size, dsent, dreceived, subject, mail_from, mail_to, mail_cc, mlist, part, labels, usertags, cinfo, bdata FROM %Q WHERE uid = %Q", folder_name, uid);
	ret = camel_db_select (cdb, query, read_mir_callback, p, ex);
	sqlite3_free (query);

	return (ret);
}

int
camel_db_read_message_info_records (CamelDB *cdb, const char *folder_name, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex)
{
	char *query;
	int ret;

	query = sqlite3_mprintf ("SELECT uid, flags, size, dsent, dreceived, subject, mail_from, mail_to, mail_cc, mlist, part, labels, usertags, cinfo, bdata FROM %Q ", folder_name);
	ret = camel_db_select (cdb, query, read_mir_callback, p, ex);
	sqlite3_free (query);

	return (ret);
}

int
camel_db_delete_uid (CamelDB *cdb, const char *folder, const char *uid, CamelException *ex)
{
	char *tab = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", folder, uid);
	int ret;

	ret = camel_db_command (cdb, tab, ex);
	sqlite3_free (tab);
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;
}

static int
cdb_delete_ids (CamelDB *cdb, const char * folder_name, GSList *uids, char *uid_prefix, const char *field, CamelException *ex)
{
	char *tmp;
	int ret;
	gboolean first = TRUE;
	GString *str = g_string_new ("DELETE FROM ");
	GSList *iterator;

	tmp = sqlite3_mprintf ("%Q WHERE %s IN (", folder_name, field); 
	g_string_append_printf (str, "%s ", tmp);
	sqlite3_free (tmp);

	iterator = uids;

	while (iterator) {
		char *foo = g_strdup_printf("%s%s", uid_prefix, (char *) iterator->data);
		tmp = sqlite3_mprintf ("%Q", foo);
		g_free(foo);
		iterator = iterator->next;

		if (first == TRUE) {
			g_string_append_printf (str, " %s ", tmp);
			first = FALSE;
		} else
			g_string_append_printf (str, ", %s ", tmp);

		sqlite3_free (tmp);
	}

	g_string_append (str, ")");

	ret = camel_db_command (cdb, str->str, ex);
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	g_string_free (str, TRUE);

	return ret;
}

int
camel_db_delete_uids (CamelDB *cdb, const char * folder_name, GSList *uids, CamelException *ex)
{
	return cdb_delete_ids (cdb, folder_name, uids, "", "uid", ex);
}

int
camel_db_delete_vuids (CamelDB *cdb, const char * folder_name, char *hash, GSList *uids, CamelException *ex)
{
	return cdb_delete_ids (cdb, folder_name, uids, hash, "vuid", ex);
}

int
camel_db_clear_folder_summary (CamelDB *cdb, char *folder, CamelException *ex)
{
	int ret;

	char *folders_del;
	char *msginfo_del;

	folders_del = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", folder);
	msginfo_del = sqlite3_mprintf ("DELETE FROM %Q ", folder);

	camel_db_begin_transaction (cdb, ex);
	camel_db_add_to_transaction (cdb, msginfo_del, ex);
	camel_db_add_to_transaction (cdb, folders_del, ex);
	ret = camel_db_end_transaction (cdb, ex);

	sqlite3_free (folders_del);
	sqlite3_free (msginfo_del);

	return ret;
}

int
camel_db_delete_folder (CamelDB *cdb, const char *folder, CamelException *ex)
{
	int ret;
	char *del;

	del = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", folder);
	ret = camel_db_command (cdb, del, ex);
	sqlite3_free (del);
	
	del = sqlite3_mprintf ("DROP TABLE %Q ", folder);
	ret = camel_db_command (cdb, del, ex);
	sqlite3_free (del);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;	
}

int
camel_db_rename_folder (CamelDB *cdb, const char *old_folder, const char *new_folder, CamelException *ex)
{
	int ret;
	char *cmd;

	cmd = sqlite3_mprintf ("ALTER TABLE %Q RENAME TO  %Q", old_folder, new_folder);
	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("UPDATE folders SET folder_name = %Q WHERE folder_name = %Q", new_folder, old_folder);
	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);
	
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;	
}

void
camel_db_camel_mir_free (CamelMIRecord *record)
{
	if (record) {
		camel_pstring_free (record->uid);
		camel_pstring_free (record->subject);
		camel_pstring_free (record->from);
		camel_pstring_free (record->to);
		camel_pstring_free (record->cc);
		camel_pstring_free (record->mlist);
		camel_pstring_free (record->followup_flag);
		camel_pstring_free (record->followup_completed_on);
		camel_pstring_free (record->followup_due_by);
		g_free (record->part);
		g_free (record->labels);
		g_free (record->usertags);
		g_free (record->cinfo);
		g_free (record->bdata);

		g_free (record);
	}
}

char * 
camel_db_sqlize_string (const char *string)
{
	return sqlite3_mprintf ("%Q", string);
}

void 
camel_db_free_sqlized_string (char *string)
{
	sqlite3_free (string);
	string = NULL;
}

/*
"(  uid TEXT PRIMARY KEY ,
flags INTEGER ,
msg_type INTEGER ,
replied INTEGER ,
msg_security INTEGER ,
size INTEGER ,
dsent NUMERIC ,
dreceived NUMERIC ,
mlist TEXT ,
followup_flag TEXT ,
followup_completed_on TEXT ,
followup_due_by TEXT ," */

char *
camel_db_get_column_name (const char *raw_name)
{
	if (!g_ascii_strcasecmp (raw_name, "Subject"))
		return g_strdup ("subject");
	else if (!g_ascii_strcasecmp (raw_name, "from"))
		return g_strdup ("mail_from");
	else if (!g_ascii_strcasecmp (raw_name, "Cc"))
		return g_strdup ("mail_cc");
	else if (!g_ascii_strcasecmp (raw_name, "To"))
		return g_strdup ("mail_to");
	else if (!g_ascii_strcasecmp (raw_name, "Flagged"))
		return g_strdup ("important");
	else if (!g_ascii_strcasecmp (raw_name, "deleted"))
		return g_strdup ("deleted");
	else if (!g_ascii_strcasecmp (raw_name, "junk"))
		return g_strdup ("junk");
	else if (!g_ascii_strcasecmp (raw_name, "Answered"))
		return g_strdup ("replied");	
	else if (!g_ascii_strcasecmp (raw_name, "Seen"))
		return g_strdup ("read");
	else if (!g_ascii_strcasecmp (raw_name, "user-tag"))
		return g_strdup ("usertags");	
	else if (!g_ascii_strcasecmp (raw_name, "user-flag"))
		return g_strdup ("labels");	
	else if (!g_ascii_strcasecmp (raw_name, "Attachments"))
		return g_strdup ("attachment");
	else if (!g_ascii_strcasecmp (raw_name, "x-camel-mlist"))
		return g_strdup ("mlist");	
	else
		return g_strdup (raw_name);

}

int
camel_db_migrate_vfolders_to_14 (CamelDB *cdb, const char *folder, CamelException *ex)
{
	char *cmd = sqlite3_mprintf ("ALTER TABLE %Q ADD COLUMN flags INTEGER", folder);
	int ret;
	
	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);
	
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;		
}
