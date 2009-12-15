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

/* how long to wait before invoking sync on the file */
#define SYNC_TIMEOUT_SECONDS 5

static sqlite3_vfs *old_vfs = NULL;

typedef struct {
	sqlite3_file parent;
	sqlite3_file *old_vfs_file; /* pointer to old_vfs' file */
	GAsyncQueue *queue;
	GThread *thread;
	guint timeout_id;
	gint flags;
} CamelSqlite3File;

static gint
call_old_file_Sync (CamelSqlite3File *cFile, gint flags)
{
	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (cFile != NULL, SQLITE_ERROR);

	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);
	return cFile->old_vfs_file->pMethods->xSync (cFile->old_vfs_file, flags);
}

/* This special flag tells the sync request thread to exit.
 * Just have to make sure it does not collide with SQLite's
 * own synchronization flags (SQLITE_SYNC_xxx). */
#define SYNC_THREAD_EXIT 0x100000

static gpointer
sync_request_thread_cb (CamelSqlite3File *cFile)
{
	gpointer data;
	gint flags = 0;

	g_async_queue_ref (cFile->queue);

	while (TRUE) {
		/* Block until a request arrives. */
		data = g_async_queue_pop (cFile->queue);

		/* Make sure we can safely deference. */
		if (data == NULL)
			continue;

		/* Extract flags and discard request. */
		flags = *((gint *) data);
		g_slice_free (gint, data);

		/* Check for exit request. */
		if (flags & SYNC_THREAD_EXIT)
			break;

		/* Got a boneafide sync request.
		 * Do it, but ignore errors. */
		call_old_file_Sync (cFile, flags);
	}

	/* Clear the exit flag. */
	flags &= ~SYNC_THREAD_EXIT;

	/* One more for the road? */
	if (flags != 0 && getenv ("CAMEL_NO_SYNC_ON_CLOSE") == NULL)
		call_old_file_Sync (cFile, flags);

	g_async_queue_unref (cFile->queue);

	return NULL;
}

static gboolean
sync_push_request (CamelSqlite3File *cFile)
{
	gint *data;

	/* The queue itself does not need to be locked yet,
	 * but we use its mutex to safely manipulate flags. */
	g_async_queue_lock (cFile->queue);

	/* We can't just cast the flags to a pointer because
	 * g_async_queue_push() won't take NULLs, and flags
	 * may be zero.  So we have to allocate memory to
	 * send an integer.  Bother. */
	data = g_slice_new (gint);
	*data = cFile->flags;
	cFile->flags = 0;

	g_async_queue_push_unlocked (cFile->queue, data);

	cFile->timeout_id = 0;

	g_async_queue_unlock (cFile->queue);

	return FALSE;
}

#define def_subclassed(_nm, _params, _call)			\
static gint							\
camel_sqlite3_file_ ## _nm _params				\
{								\
	CamelSqlite3File *cFile;				\
								\
	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);	\
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);	\
								\
	cFile = (CamelSqlite3File *) pFile;		\
	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);	\
	return cFile->old_vfs_file->pMethods->_nm _call;	\
}

def_subclassed (xRead, (sqlite3_file *pFile, gpointer pBuf, gint iAmt, sqlite3_int64 iOfst), (cFile->old_vfs_file, pBuf, iAmt, iOfst))
def_subclassed (xWrite, (sqlite3_file *pFile, gconstpointer pBuf, gint iAmt, sqlite3_int64 iOfst), (cFile->old_vfs_file, pBuf, iAmt, iOfst))
def_subclassed (xTruncate, (sqlite3_file *pFile, sqlite3_int64 size), (cFile->old_vfs_file, size))
def_subclassed (xFileSize, (sqlite3_file *pFile, sqlite3_int64 *pSize), (cFile->old_vfs_file, pSize))
def_subclassed (xLock, (sqlite3_file *pFile, gint lockType), (cFile->old_vfs_file, lockType))
def_subclassed (xUnlock, (sqlite3_file *pFile, gint lockType), (cFile->old_vfs_file, lockType))
def_subclassed (xFileControl, (sqlite3_file *pFile, gint op, gpointer pArg), (cFile->old_vfs_file, op, pArg))
def_subclassed (xSectorSize, (sqlite3_file *pFile), (cFile->old_vfs_file))
def_subclassed (xDeviceCharacteristics, (sqlite3_file *pFile), (cFile->old_vfs_file))

#undef def_subclassed

static gint
camel_sqlite3_file_xCheckReservedLock (sqlite3_file *pFile, gint *pResOut)
{
	CamelSqlite3File *cFile;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (CamelSqlite3File *) pFile;
	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);

	/* check version in runtime */
	if (sqlite3_libversion_number () < 3006000)
		return ((gint (*)(sqlite3_file *)) (cFile->old_vfs_file->pMethods->xCheckReservedLock)) (cFile->old_vfs_file);
	else
		return ((gint (*)(sqlite3_file *, gint *)) (cFile->old_vfs_file->pMethods->xCheckReservedLock)) (cFile->old_vfs_file, pResOut);
}

static gint
camel_sqlite3_file_xClose (sqlite3_file *pFile)
{
	CamelSqlite3File *cFile;
	gint res;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (CamelSqlite3File *) pFile;

	/* The queue itself does not need to be locked yet,
	 * but we use its mutex to safely manipulate flags. */
	g_async_queue_lock (cFile->queue);

	/* Tell the sync request thread to exit.  It may do
	 * one last sync before exiting, so preserve any sync
	 * flags that have accumulated. */
	cFile->flags |= SYNC_THREAD_EXIT;

	/* Cancel any pending sync requests. */
	if (cFile->timeout_id > 0)
		g_source_remove (cFile->timeout_id);

	/* Unlock the queue before pushing the exit request. */
	g_async_queue_unlock (cFile->queue);

	/* Push the exit request. */
	sync_push_request (cFile);

	/* Wait for the thread to exit. */
	g_thread_join (cFile->thread);
	cFile->thread = NULL;

	/* Now we can safely destroy the queue. */
	g_async_queue_unref (cFile->queue);
	cFile->queue = NULL;

	if (cFile->old_vfs_file->pMethods)
		res = cFile->old_vfs_file->pMethods->xClose (cFile->old_vfs_file);
	else
		res = SQLITE_OK;

	g_free (cFile->old_vfs_file);
	cFile->old_vfs_file = NULL;

	return res;
}

static gint
camel_sqlite3_file_xSync (sqlite3_file *pFile, gint flags)
{
	CamelSqlite3File *cFile;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (CamelSqlite3File *) pFile;

	/* The queue itself does not need to be locked yet,
	 * but we use its mutex to safely manipulate flags. */
	g_async_queue_lock (cFile->queue);

	/* If a sync request is already scheduled, accumulate flags. */
	cFile->flags |= flags;

	/* Cancel any pending sync requests. */
	if (cFile->timeout_id > 0)
		g_source_remove (cFile->timeout_id);

	/* Wait SYNC_TIMEOUT_SECONDS before we actually sync. */
	cFile->timeout_id = g_timeout_add_seconds (
		SYNC_TIMEOUT_SECONDS, (GSourceFunc)
		sync_push_request, cFile);

	g_async_queue_unlock (cFile->queue);

	return SQLITE_OK;
}

static gint
camel_sqlite3_vfs_xOpen (sqlite3_vfs *pVfs, const gchar *zPath, sqlite3_file *pFile, gint flags, gint *pOutFlags)
{
	static GStaticRecMutex only_once_lock = G_STATIC_REC_MUTEX_INIT;
	static sqlite3_io_methods io_methods = {0};
	CamelSqlite3File *cFile;
	GError *error = NULL;
	gint res;

	g_return_val_if_fail (old_vfs != NULL, -1);
	g_return_val_if_fail (pFile != NULL, -1);

	cFile = (CamelSqlite3File *)pFile;
	cFile->old_vfs_file = g_malloc0 (old_vfs->szOsFile);

	res = old_vfs->xOpen (old_vfs, zPath, cFile->old_vfs_file, flags, pOutFlags);
	if (res != SQLITE_OK) {
		g_free (cFile->old_vfs_file);
		return res;
	}

	cFile->queue = g_async_queue_new ();

	/* Spawn a joinable thread to listen for sync requests. */
	cFile->thread = g_thread_create (
		(GThreadFunc) sync_request_thread_cb, cFile, TRUE, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	g_static_rec_mutex_lock (&only_once_lock);

	/* cFile->old_vfs_file->pMethods is NULL when open failed for some reason,
	   thus do not initialize our structure when do not know the version */
	if (io_methods.xClose == NULL && cFile->old_vfs_file->pMethods) {
		/* initialize our subclass function only once */
		io_methods.iVersion = cFile->old_vfs_file->pMethods->iVersion;

		/* check version in compile time */
		#if SQLITE_VERSION_NUMBER < 3006000
		io_methods.xCheckReservedLock = (gint (*)(sqlite3_file *)) camel_sqlite3_file_xCheckReservedLock;
		#else
		io_methods.xCheckReservedLock = camel_sqlite3_file_xCheckReservedLock;
		#endif

		#define use_subclassed(x) io_methods.x = camel_sqlite3_file_ ## x
		use_subclassed (xClose);
		use_subclassed (xRead);
		use_subclassed (xWrite);
		use_subclassed (xTruncate);
		use_subclassed (xSync);
		use_subclassed (xFileSize);
		use_subclassed (xLock);
		use_subclassed (xUnlock);
		use_subclassed (xFileControl);
		use_subclassed (xSectorSize);
		use_subclassed (xDeviceCharacteristics);
		#undef use_subclassed
	}

	g_static_rec_mutex_unlock (&only_once_lock);

	cFile->parent.pMethods = &io_methods;

	return res;
}

static gpointer
init_sqlite_vfs (void)
{
	static sqlite3_vfs vfs = { 0 };

	old_vfs = sqlite3_vfs_find (NULL);
	g_return_val_if_fail (old_vfs != NULL, NULL);

	memcpy (&vfs, old_vfs, sizeof (sqlite3_vfs));

	vfs.szOsFile = sizeof (CamelSqlite3File);
	vfs.zName = "camel_sqlite3_vfs";
	vfs.xOpen = camel_sqlite3_vfs_xOpen;

	sqlite3_vfs_register (&vfs, 1);

	return NULL;
}

#define d(x) if (camel_debug("sqlite")) x
#define START(stmt)	if (camel_debug("dbtime")) { g_print ("\n===========\nDB SQL operation [%s] started\n", stmt); if (!cdb->priv->timer) { cdb->priv->timer = g_timer_new (); } else { g_timer_reset(cdb->priv->timer);} }
#define END	if (camel_debug("dbtime")) { g_timer_stop (cdb->priv->timer); g_print ("DB Operation ended. Time Taken : %f\n###########\n", g_timer_elapsed (cdb->priv->timer, NULL)); }
#define STARTTS(stmt)	if (camel_debug("dbtimets")) { g_print ("\n===========\nDB SQL operation [%s] started\n", stmt); if (!cdb->priv->timer) { cdb->priv->timer = g_timer_new (); } else { g_timer_reset(cdb->priv->timer);} }
#define ENDTS	if (camel_debug("dbtimets")) { g_timer_stop (cdb->priv->timer); g_print ("DB Operation ended. Time Taken : %f\n###########\n", g_timer_elapsed (cdb->priv->timer, NULL)); }

struct _CamelDBPrivate {
	GTimer *timer;
	gchar *file_name;
};

static GStaticRecMutex trans_lock = G_STATIC_REC_MUTEX_INIT;

static gint write_mir (CamelDB *cdb, const gchar *folder_name, CamelMIRecord *record, CamelException *ex, gboolean delete_old_record);

static gint
cdb_sql_exec (sqlite3 *db, const gchar * stmt, CamelException *ex)
{
	gchar *errmsg = NULL;
	gint   ret = -1;

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
camel_db_open (const gchar *path, CamelException *ex)
{
	static GOnce vfs_once = G_ONCE_INIT;
	CamelDB *cdb;
	sqlite3 *db;
	gint ret;

	g_once (&vfs_once, (GThreadFunc) init_sqlite_vfs, NULL);

	CAMEL_DB_USE_SHARED_CACHE;

	ret = sqlite3_open(path, &db);
	if (ret) {

		if (!db) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Insufficient memory"));
		} else {
			const gchar *error;
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
	if (g_getenv("CAMEL_SQLITE_DEFAULT_CACHE_SIZE")!=NULL) {
		gchar *cache = NULL;

		cache = g_strdup_printf ("PRAGMA cache_size=%s", g_getenv("CAMEL_SQLITE_DEFAULT_CACHE_SIZE"));
		camel_db_command (cdb, cache, NULL);
		g_free (cache);
	}

	camel_db_command (cdb, "ATTACH DATABASE ':memory:' AS mem", NULL);

	if (g_getenv("CAMEL_SQLITE_IN_MEMORY") != NULL) {
		/* Optionally turn off Journaling, this gets over fsync issues, but could be risky */
		camel_db_command (cdb, "PRAGMA main.journal_mode = off", NULL);
		camel_db_command (cdb, "PRAGMA temp_store = memory", NULL);
	}

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

gint
camel_db_set_collate (CamelDB *cdb, const gchar *col, const gchar *collate, CamelDBCollate func)
{
		gint ret = 0;

		if (!cdb)
			return 0;

		g_mutex_lock (cdb->lock);
		d(g_print("Creating Collation %s on %s with %p\n", collate, col, (gpointer) func));
		if (collate && func)
			ret = sqlite3_create_collation(cdb->db, collate, SQLITE_UTF8,  NULL, func);
		g_mutex_unlock (cdb->lock);

		return ret;
}

/* Should this be really exposed ? */
gint
camel_db_command (CamelDB *cdb, const gchar *stmt, CamelException *ex)
{
		gint ret;

		if (!cdb)
			return TRUE;
		g_mutex_lock (cdb->lock);

		START(stmt);
		ret = cdb_sql_exec (cdb->db, stmt, ex);
		END;
		g_mutex_unlock (cdb->lock);

		return ret;
}

gint
camel_db_begin_transaction (CamelDB *cdb, CamelException *ex)
{
	if (!cdb)
		return -1;
	if (g_getenv("SQLITE_TRANSLOCK"))
		g_static_rec_mutex_lock (&trans_lock);

	g_mutex_lock (cdb->lock);
	STARTTS("BEGIN");

	return (cdb_sql_exec (cdb->db, "BEGIN", ex));
}

gint
camel_db_end_transaction (CamelDB *cdb, CamelException *ex)
{
	gint ret;
	if (!cdb)
		return -1;

	ret = cdb_sql_exec (cdb->db, "COMMIT", ex);
	ENDTS;
	g_mutex_unlock (cdb->lock);
	if (g_getenv("SQLITE_TRANSLOCK"))
		g_static_rec_mutex_unlock (&trans_lock);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;

	return ret;
}

gint
camel_db_abort_transaction (CamelDB *cdb, CamelException *ex)
{
	gint ret;

	ret = cdb_sql_exec (cdb->db, "ROLLBACK", ex);
	g_mutex_unlock (cdb->lock);
	if (g_getenv("SQLITE_TRANSLOCK"))
		g_static_rec_mutex_unlock (&trans_lock);
	CAMEL_DB_RELEASE_SQLITE_MEMORY;

	return ret;
}

gint
camel_db_add_to_transaction (CamelDB *cdb, const gchar *stmt, CamelException *ex)
{
	if (!cdb)
		return -1;

	return (cdb_sql_exec (cdb->db, stmt, ex));
}

gint
camel_db_transaction_command (CamelDB *cdb, GSList *qry_list, CamelException *ex)
{
	gint ret;
	const gchar *query;

	if (!cdb)
		return -1;

	g_mutex_lock (cdb->lock);
	STARTTS("BEGIN");
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
	ENDTS;
end:
	g_mutex_unlock (cdb->lock);
	return ret;
}

static gint
count_cb (gpointer data, gint argc, gchar **argv, gchar **azColName)
{
	gint i;

	for (i=0; i<argc; i++) {
		if (strstr(azColName[i], "COUNT")) {
			*(guint32 *)data = argv [i] ? strtoul (argv [i], NULL, 10) : 0;
		}
	}

	return 0;
}

gint
camel_db_count_message_info (CamelDB *cdb, const gchar *query, guint32 *count, CamelException *ex)
{
	gint ret = -1;
	gchar *errmsg = NULL;

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

gint
camel_db_count_junk_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{
	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE junk = 1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_count_unread_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{
	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE read = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_count_visible_unread_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{
	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE read = 0 AND junk = 0 AND deleted = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_count_visible_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{
	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE junk = 0 AND deleted = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_count_junk_not_deleted_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{
	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE junk = 1 AND deleted = 0", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_count_deleted_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{
	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q WHERE deleted = 1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_count_total_message_info (CamelDB *cdb, const gchar *table_name, guint32 *count, CamelException *ex)
{

	gint ret;
	gchar *query;

	if (!cdb)
		return -1;

	query = sqlite3_mprintf ("SELECT COUNT (*) FROM %Q where read=0 or read=1", table_name);

	ret = camel_db_count_message_info (cdb, query, count, ex);
	sqlite3_free (query);

	return ret;
}

gint
camel_db_select (CamelDB *cdb, const gchar * stmt, CamelDBSelectCB callback, gpointer data, CamelException *ex)
{
	gchar *errmsg = NULL;
	/*int nrecs = 0;*/
	gint ret = -1;

	if (!cdb)
		return ret;

	d(g_print ("\n%s:\n%s \n", G_STRFUNC, stmt));
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

gint
camel_db_create_vfolder (CamelDB *db, const gchar *folder_name, CamelException *ex)
{
	gint ret;
	gchar *table_creation_query, *safe_index;

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

gint
camel_db_recreate_vfolder (CamelDB *db, const gchar *folder_name, CamelException *ex)
{
	gint ret;
	gchar *table_query;

	table_query = sqlite3_mprintf ("DROP TABLE %Q", folder_name);

	ret = camel_db_command (db, table_query, ex);

	sqlite3_free (table_query);

	return camel_db_create_vfolder (db, folder_name, ex);
}

gint
camel_db_delete_uid_from_vfolder (CamelDB *db, gchar *folder_name, gchar *vuid, CamelException *ex)
{
	 gchar *del_query;
	 gint ret;

	 del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE vuid = %Q", folder_name, vuid);

	 ret = camel_db_command (db, del_query, ex);

	 sqlite3_free (del_query);
	 CAMEL_DB_RELEASE_SQLITE_MEMORY;
	 return ret;
}

gint
camel_db_delete_uid_from_vfolder_transaction (CamelDB *db, const gchar *folder_name, const gchar *vuid, CamelException *ex)
{
	gchar *del_query;
	gint ret;

	del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE vuid = %Q", folder_name, vuid);

	ret = camel_db_add_to_transaction (db, del_query, ex);

	sqlite3_free (del_query);

	return ret;
}

struct _db_data_uids_flags {
	GPtrArray *uids;
	GPtrArray *flags;
};
static gint
read_uids_flags_callback (gpointer ref, gint ncol, gchar ** cols, gchar ** name)
{
	struct _db_data_uids_flags *data= (struct _db_data_uids_flags *) ref;

	gint i;
	for (i = 0; i < ncol; ++i) {
		if (!strcmp (name [i], "uid"))
			g_ptr_array_add (data->uids, (gchar *) (camel_pstring_strdup(cols [i])));
		else if (!strcmp (name [i], "flags"))
			g_ptr_array_add (data->flags, GUINT_TO_POINTER(strtoul (cols [i], NULL, 10)));
	}

	 return 0;
}

gint
camel_db_get_folder_uids_flags (CamelDB *db, const gchar *folder_name, const gchar *sort_by, const gchar *collate, GPtrArray *summary, GHashTable *table, CamelException *ex)
{
	 GPtrArray *uids = summary;
	 GPtrArray *flags = g_ptr_array_new ();
	 gchar *sel_query;
	 gint ret;
	 struct _db_data_uids_flags data;
	 gint i;

	 data.uids = uids;
	 data.flags = flags;

	 sel_query = sqlite3_mprintf("SELECT uid,flags FROM %Q%s%s%s%s", folder_name, sort_by ? " order by " : "", sort_by ? sort_by: "", (sort_by && collate) ? " collate " : "", (sort_by && collate) ? collate : "");

	 ret = camel_db_select (db, sel_query, read_uids_flags_callback, &data, ex);
	 sqlite3_free (sel_query);

	 for (i=0; i<uids->len; i++) {
		 g_hash_table_insert (table, uids->pdata[i], flags->pdata[i]);
	 }

	 g_ptr_array_free (flags, TRUE);
	 return ret;
}

static gint
read_uids_callback (gpointer ref, gint ncol, gchar ** cols, gchar ** name)
{
	GPtrArray *array = (GPtrArray *) ref;

	#if 0
	gint i;
	for (i = 0; i < ncol; ++i) {
		if (!strcmp (name [i], "uid"))
			g_ptr_array_add (array, (gchar *) (camel_pstring_strdup(cols [i])));
	}
	#else
			g_ptr_array_add (array, (gchar *) (camel_pstring_strdup(cols [0])));
	#endif

	 return 0;
}

gint
camel_db_get_folder_uids (CamelDB *db, const gchar *folder_name, const gchar *sort_by, const gchar *collate, GPtrArray *array, CamelException *ex)
{
	 gchar *sel_query;
	 gint ret;

	 sel_query = sqlite3_mprintf("SELECT uid FROM %Q%s%s%s%s", folder_name, sort_by ? " order by " : "", sort_by ? sort_by: "", (sort_by && collate) ? " collate " : "", (sort_by && collate) ? collate : "");

	 ret = camel_db_select (db, sel_query, read_uids_callback, array, ex);
	 sqlite3_free (sel_query);

	 return ret;
}

GPtrArray *
camel_db_get_folder_junk_uids (CamelDB *db, gchar *folder_name, CamelException *ex)
{
	 gchar *sel_query;
	 gint ret;
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
camel_db_get_folder_deleted_uids (CamelDB *db, gchar *folder_name, CamelException *ex)
{
	 gchar *sel_query;
	 gint ret;
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

static gint
read_preview_callback (gpointer ref, gint ncol, gchar ** cols, gchar ** name)
{
	GHashTable *hash = (GHashTable *)ref;
	const gchar *uid=NULL;
	gchar *msg=NULL;
	gint i;

	for (i = 0; i < ncol; ++i) {
		if (!strcmp (name [i], "uid"))
			uid = camel_pstring_strdup(cols [i]);
		else if (!strcmp (name [i], "preview"))
			msg = g_strdup(cols[i]);
	}

	g_hash_table_insert(hash, (gchar *)uid, msg);

	return 0;
}

GHashTable *
camel_db_get_folder_preview (CamelDB *db, gchar *folder_name, CamelException *ex)
{
	 gchar *sel_query;
	 gint ret;
	 GHashTable *hash = g_hash_table_new (g_str_hash, g_str_equal);

	 sel_query = sqlite3_mprintf("SELECT uid, preview FROM '%q_preview'", folder_name);

	 ret = camel_db_select (db, sel_query, read_preview_callback, hash, ex);
	 sqlite3_free (sel_query);

	 if (!g_hash_table_size (hash) || ret != 0) {
		 g_hash_table_destroy (hash);
		 hash = NULL;
	 }

	 return hash;
}

gint
camel_db_write_preview_record (CamelDB *db, gchar *folder_name, const gchar *uid, const gchar *msg, CamelException *ex)
{
	gchar *query;
	gint ret;

	query = sqlite3_mprintf("INSERT OR REPLACE INTO '%q_preview' VALUES(%Q,%Q)", folder_name, uid, msg);

	ret = camel_db_add_to_transaction (db, query, ex);
	sqlite3_free (query);

	return ret;
}

static gint
read_vuids_callback (gpointer ref, gint ncol, gchar ** cols, gchar ** name)
{
	 GPtrArray *array = (GPtrArray *)ref;

	 #if 0
	 gint i;

	 for (i = 0; i < ncol; ++i) {
		  if (!strcmp (name [i], "vuid"))
			   g_ptr_array_add (array, (gchar *) (camel_pstring_strdup(cols [i]+8)));
	 }
	 #else
			   g_ptr_array_add (array, (gchar *) (camel_pstring_strdup(cols [0]+8)));
	 #endif

	 return 0;
}

GPtrArray *
camel_db_get_vuids_from_vfolder (CamelDB *db, gchar *folder_name, gchar *filter, CamelException *ex)
{
	 gchar *sel_query;
	 gchar *cond = NULL;
	 GPtrArray *array;
	 gchar *tmp = g_strdup_printf("%s%%", filter ? filter:"");
	 if (filter)
		  cond = sqlite3_mprintf(" WHERE vuid LIKE %Q", tmp);
	 g_free(tmp);
	 sel_query = sqlite3_mprintf("SELECT vuid FROM %Q%s", folder_name, filter ? cond : "");

	 if (cond)
		  sqlite3_free (cond);
	 /* FIXME[disk-summary] handle return values */
	 /* FIXME[disk-summary] No The caller should parse the ex in case
	 *                      of NULL returns */
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

gint
camel_db_add_to_vfolder (CamelDB *db, gchar *folder_name, gchar *vuid, CamelException *ex)
{
	 gchar *ins_query;
	 gint ret;

	 ins_query = sqlite3_mprintf ("INSERT INTO %Q VALUES (%Q)", folder_name, vuid);

	 ret = camel_db_command (db, ins_query, ex);

	 sqlite3_free (ins_query);
	 CAMEL_DB_RELEASE_SQLITE_MEMORY;
	 return ret;
}

gint
camel_db_add_to_vfolder_transaction (CamelDB *db, const gchar *folder_name, const gchar *vuid, CamelException *ex)
{
	 gchar *ins_query;
	 gint ret;

	 ins_query = sqlite3_mprintf ("INSERT INTO %Q VALUES (%Q)", folder_name, vuid);

	 ret = camel_db_add_to_transaction (db, ins_query, ex);

	 sqlite3_free (ins_query);

	 return ret;
}

gint
camel_db_create_folders_table (CamelDB *cdb, CamelException *ex)
{
	const gchar *query = "CREATE TABLE IF NOT EXISTS folders ( folder_name TEXT PRIMARY KEY, version REAL, flags INTEGER, nextuid INTEGER, time NUMERIC, saved_count INTEGER, unread_count INTEGER, deleted_count INTEGER, junk_count INTEGER, visible_count INTEGER, jnd_count INTEGER, bdata TEXT )";
	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ((camel_db_command (cdb, query, ex)));
}

static gint
camel_db_create_message_info_table (CamelDB *cdb, const gchar *folder_name, CamelException *ex)
{
	gint ret;
	gchar *table_creation_query, *safe_index;

	/* README: It is possible to compress all system flags into a single column and use just as userflags but that makes querying for other applications difficult an d bloats the parsing code. Instead, it is better to bloat the tables. Sqlite should have some optimizations for sparse columns etc. */
	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q (  uid TEXT PRIMARY KEY , flags INTEGER , msg_type INTEGER , read INTEGER , deleted INTEGER , replied INTEGER , important INTEGER , junk INTEGER , attachment INTEGER , dirty INTEGER , size INTEGER , dsent NUMERIC , dreceived NUMERIC , subject TEXT , mail_from TEXT , mail_to TEXT , mail_cc TEXT , mlist TEXT , followup_flag TEXT , followup_completed_on TEXT , followup_due_by TEXT , part TEXT , labels TEXT , usertags TEXT , cinfo TEXT , bdata TEXT, created TEXT, modified TEXT)", folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	sqlite3_free (table_creation_query);

	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS '%q_bodystructure' (  uid TEXT PRIMARY KEY , bodystructure TEXT )", folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	sqlite3_free (table_creation_query);

	/* Create message preview table. */
	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS '%q_preview' (  uid TEXT PRIMARY KEY , preview TEXT)", folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	sqlite3_free (table_creation_query);

	/* FIXME: sqlize folder_name before you create the index */
	safe_index = g_strdup_printf("SINDEX-%s", folder_name);
	table_creation_query = sqlite3_mprintf ("DROP INDEX IF EXISTS %Q", safe_index);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	g_free (safe_index);
	sqlite3_free (table_creation_query);

	/* INDEX on preview */
	safe_index = g_strdup_printf("SINDEX-%s-preview", folder_name);
	table_creation_query = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON '%q_preview' (uid, preview)", safe_index, folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	g_free (safe_index);
	sqlite3_free (table_creation_query);

	/* Index on deleted*/
	safe_index = g_strdup_printf("DELINDEX-%s", folder_name);
	table_creation_query = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (deleted)", safe_index, folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	g_free (safe_index);
	sqlite3_free (table_creation_query);

	/* Index on Junk*/
	safe_index = g_strdup_printf("JUNKINDEX-%s", folder_name);
	table_creation_query = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (junk)", safe_index, folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	g_free (safe_index);
	sqlite3_free (table_creation_query);

	/* Index on unread*/
	safe_index = g_strdup_printf("READINDEX-%s", folder_name);
	table_creation_query = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (read)", safe_index, folder_name);
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	g_free (safe_index);
	sqlite3_free (table_creation_query);

	return ret;
}

static gint
camel_db_migrate_folder_prepare (CamelDB *cdb, const gchar *folder_name, gint version, CamelException *ex)
{
	gint ret = 0;
	gchar *table_creation_query;

	/* Migration stage one: storing the old data */

	if (version < 1) {

		/* Between version 0-1 the following things are changed
		 * ADDED: created: time
		 * ADDED: modified: time
		 * RENAMED: msg_security to dirty
		 * */

		table_creation_query = sqlite3_mprintf ("DROP TABLE IF EXISTS 'mem.%q'", folder_name);
		ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
		sqlite3_free (table_creation_query);

		table_creation_query = sqlite3_mprintf ("CREATE TEMP TABLE IF NOT EXISTS 'mem.%q' (  uid TEXT PRIMARY KEY , flags INTEGER , msg_type INTEGER , read INTEGER , deleted INTEGER , replied INTEGER , important INTEGER , junk INTEGER , attachment INTEGER , dirty INTEGER , size INTEGER , dsent NUMERIC , dreceived NUMERIC , subject TEXT , mail_from TEXT , mail_to TEXT , mail_cc TEXT , mlist TEXT , followup_flag TEXT , followup_completed_on TEXT , followup_due_by TEXT , part TEXT , labels TEXT , usertags TEXT , cinfo TEXT , bdata TEXT, created TEXT, modified TEXT )", folder_name);
		ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
		sqlite3_free (table_creation_query);

		table_creation_query = sqlite3_mprintf ("INSERT INTO 'mem.%q' SELECT uid , flags , msg_type , read , deleted , replied , important , junk , attachment , msg_security , size , dsent , dreceived , subject , mail_from , mail_to , mail_cc , mlist , followup_flag , followup_completed_on , followup_due_by , part , labels , usertags , cinfo , bdata , strftime(\"%%s\", 'now'), strftime(\"%%s\", 'now') FROM %Q", folder_name, folder_name);
		ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
		sqlite3_free (table_creation_query);

		table_creation_query = sqlite3_mprintf ("DROP TABLE IF EXISTS %Q", folder_name);
		ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
		sqlite3_free (table_creation_query);

		ret = camel_db_create_message_info_table (cdb, folder_name, ex);
		camel_exception_clear (ex);
	}

	/* Add later version migrations here */

	return ret;
}

static gint
camel_db_migrate_folder_recreate (CamelDB *cdb, const gchar *folder_name, gint version, CamelException *ex)
{
	gint ret = 0;
	gchar *table_creation_query;

	/* Migration stage two: writing back the old data */

	if (version < 2) {
		table_creation_query = sqlite3_mprintf ("INSERT INTO %Q SELECT uid , flags , msg_type , read , deleted , replied , important , junk , attachment , dirty , size , dsent , dreceived , subject , mail_from , mail_to , mail_cc , mlist , followup_flag , followup_completed_on , followup_due_by , part , labels , usertags , cinfo , bdata, created, modified FROM 'mem.%q'", folder_name, folder_name);
		ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
		sqlite3_free (table_creation_query);

		table_creation_query = sqlite3_mprintf ("DROP TABLE 'mem.%q'", folder_name);
		ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
		sqlite3_free (table_creation_query);
	}

	/* Add later version migrations here */

	return ret;
}

gint
camel_db_reset_folder_version (CamelDB *cdb, const gchar *folder_name, gint reset_version, CamelException *ex)
{
	gint ret = 0;
	gchar *version_creation_query;
	gchar *version_insert_query;
	gchar *drop_folder_query;

	drop_folder_query = sqlite3_mprintf ("DROP TABLE IF EXISTS '%q_version'", folder_name);
	version_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS '%q_version' ( version TEXT )", folder_name);

	version_insert_query = sqlite3_mprintf ("INSERT INTO '%q_version' VALUES ('%d')", folder_name, reset_version);

	ret = camel_db_add_to_transaction (cdb, drop_folder_query, ex);
	ret = camel_db_add_to_transaction (cdb, version_creation_query, ex);
	ret = camel_db_add_to_transaction (cdb, version_insert_query, ex);

	sqlite3_free (drop_folder_query);
	sqlite3_free (version_creation_query);
	sqlite3_free (version_insert_query);

	return ret;
}

static gint
camel_db_write_folder_version (CamelDB *cdb, const gchar *folder_name, gint old_version, CamelException *ex)
{
	gint ret = 0;
	gchar *version_creation_query;
	gchar *version_insert_query;

	version_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS '%q_version' ( version TEXT )", folder_name);

	if (old_version == -1)
		version_insert_query = sqlite3_mprintf ("INSERT INTO '%q_version' VALUES ('2')", folder_name);
	else
		version_insert_query = sqlite3_mprintf ("UPDATE '%q_version' SET version='2'", folder_name);

	ret = camel_db_add_to_transaction (cdb, version_creation_query, ex);
	ret = camel_db_add_to_transaction (cdb, version_insert_query, ex);

	sqlite3_free (version_creation_query);
	sqlite3_free (version_insert_query);

	return ret;
}

static gint
camel_db_get_folder_version (CamelDB *cdb, const gchar *folder_name, CamelException *ex)
{
	gint version = -1, ret;
	gchar *query;
	sqlite3_stmt *stmt = NULL;

	query = sqlite3_mprintf ("SELECT version FROM '%q_version'", folder_name);

	ret = sqlite3_prepare_v2 (cdb->db, query, -1, &stmt, NULL);

	if (ret == SQLITE_OK)
		ret = sqlite3_step (stmt);
	if (ret == SQLITE_ROW || ret == SQLITE_OK)
		version = sqlite3_column_int (stmt, 0);

	sqlite3_finalize (stmt);

	sqlite3_free (query);

	return version;
}

gint
camel_db_prepare_message_info_table (CamelDB *cdb, const gchar *folder_name, CamelException *ex)
{
	gint ret, current_version;

	/* Make sure we have the table already */
	ret = camel_db_create_message_info_table (cdb, folder_name, ex);

	/* Migration stage zero: version fetch */
	current_version = camel_db_get_folder_version (cdb, folder_name, ex);

	/* Migration stage one: storing the old data if necessary */
	ret = camel_db_migrate_folder_prepare (cdb, folder_name, current_version, ex);

	/* Migration stage two: rewriting the old data if necessary */
	ret = camel_db_migrate_folder_recreate (cdb, folder_name, current_version, ex);

	/* Final step: (over)write the current version label */
	ret = camel_db_write_folder_version (cdb, folder_name, current_version, ex);

	return ret;
}

gint
camel_db_write_fresh_message_info_record (CamelDB *cdb, const gchar *folder_name, CamelMIRecord *record, CamelException *ex)
{
	return write_mir (cdb, folder_name, record, ex, FALSE);
}

gint
camel_db_write_message_info_record (CamelDB *cdb, const gchar *folder_name, CamelMIRecord *record, CamelException *ex)
{
	return write_mir (cdb, folder_name, record, ex, TRUE);
}

static gint
write_mir (CamelDB *cdb, const gchar *folder_name, CamelMIRecord *record, CamelException *ex, gboolean delete_old_record)
{
	gint ret;
	/*char *del_query;*/
	gchar *ins_query;

	/* FIXME: We should migrate from this DELETE followed by INSERT model to an INSERT OR REPLACE model as pointed out by pvanhoof */

	/* NB: UGLIEST Hack. We can't modify the schema now. We are using dirty (an unsed one to notify of FLAGGED/Dirty infos */

	ins_query = sqlite3_mprintf ("INSERT OR REPLACE INTO %Q VALUES (%Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %lld, %lld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, strftime(\"%%s\", 'now'), strftime(\"%%s\", 'now') )",
			folder_name, record->uid, record->flags,
			record->msg_type, record->read, record->deleted, record->replied,
			record->important, record->junk, record->attachment, record->dirty,
			record->size, (gint64) record->dsent, (gint64) record->dreceived,
			record->subject, record->from, record->to,
			record->cc, record->mlist, record->followup_flag,
			record->followup_completed_on, record->followup_due_by,
			record->part, record->labels, record->usertags,
			record->cinfo, record->bdata);

	/* if (delete_old_record)
			del_query = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", folder_name, record->uid); */

#if 0
	gchar *upd_query;

	upd_query = g_strdup_printf ("IMPLEMENT AND THEN TRY");
	camel_db_command (cdb, upd_query, ex);
	g_free (upd_query);
#else

	/* if (delete_old_record)
			ret = camel_db_add_to_transaction (cdb, del_query, ex); */
	ret = camel_db_add_to_transaction (cdb, ins_query, ex);

#endif

	/* if (delete_old_record)
			sqlite3_free (del_query); */
	sqlite3_free (ins_query);

	if (ret == 0) {
		ins_query = sqlite3_mprintf ("INSERT OR REPLACE INTO '%q_bodystructure' VALUES (%Q, %Q )",
				folder_name, record->uid, record->bodystructure);
		ret = camel_db_add_to_transaction (cdb, ins_query, ex);
		sqlite3_free (ins_query);
	}

	return ret;
}

gint
camel_db_write_folder_info_record (CamelDB *cdb, CamelFIRecord *record, CamelException *ex)
{
	gint ret;

	gchar *del_query;
	gchar *ins_query;

	ins_query = sqlite3_mprintf ("INSERT INTO folders VALUES ( %Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %Q ) ",
			record->folder_name, record->version,
								 record->flags, record->nextuid, record->time,
			record->saved_count, record->unread_count,
								 record->deleted_count, record->junk_count, record->visible_count, record->jnd_count, record->bdata);

	del_query = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", record->folder_name);

#if 0
	gchar *upd_query;

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

static gint
read_fir_callback (gpointer  ref, gint ncol, gchar ** cols, gchar ** name)
{
	CamelFIRecord *record = *(CamelFIRecord **) ref;
	gint i;

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

gint
camel_db_read_folder_info_record (CamelDB *cdb, const gchar *folder_name, CamelFIRecord **record, CamelException *ex)
{
	gchar *query;
	gint ret;

	query = sqlite3_mprintf ("SELECT * FROM folders WHERE folder_name = %Q", folder_name);
	ret = camel_db_select (cdb, query, read_fir_callback, record, ex);

	sqlite3_free (query);
	return (ret);
}

gint
camel_db_read_message_info_record_with_uid (CamelDB *cdb, const gchar *folder_name, const gchar *uid, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex)
{
	gchar *query;
	gint ret;

	query = sqlite3_mprintf ("SELECT uid, flags, size, dsent, dreceived, subject, mail_from, mail_to, mail_cc, mlist, part, labels, usertags, cinfo, bdata FROM %Q WHERE uid = %Q", folder_name, uid);
	ret = camel_db_select (cdb, query, read_mir_callback, p, ex);
	sqlite3_free (query);

	return (ret);
}

gint
camel_db_read_message_info_records (CamelDB *cdb, const gchar *folder_name, gpointer p, CamelDBSelectCB read_mir_callback, CamelException *ex)
{
	gchar *query;
	gint ret;

	query = sqlite3_mprintf ("SELECT uid, flags, size, dsent, dreceived, subject, mail_from, mail_to, mail_cc, mlist, part, labels, usertags, cinfo, bdata FROM %Q ", folder_name);
	ret = camel_db_select (cdb, query, read_mir_callback, p, ex);
	sqlite3_free (query);

	return (ret);
}

static gint
camel_db_create_deleted_table (CamelDB *cdb, CamelException *ex)
{
	gint ret;
	gchar *table_creation_query;
	table_creation_query = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS Deletes (id INTEGER primary key AUTOINCREMENT not null, uid TEXT, time TEXT, mailbox TEXT)");
	ret = camel_db_add_to_transaction (cdb, table_creation_query, ex);
	sqlite3_free (table_creation_query);
	return ret;
}

static gint
camel_db_trim_deleted_table (CamelDB *cdb, CamelException *ex)
{
	gint ret = 0;

	/* TODO: We need a mechanism to get rid of very old deletes, or something
	 * that keeps the list trimmed at a certain max (deleting upfront when
	 * appending at the back) */

	return ret;
}

gint
camel_db_delete_uid (CamelDB *cdb, const gchar *folder, const gchar *uid, CamelException *ex)
{
	gchar *tab;
	gint ret;

	camel_db_begin_transaction (cdb, ex);

	ret = camel_db_create_deleted_table (cdb, ex);

	tab = sqlite3_mprintf ("INSERT OR REPLACE INTO Deletes (uid, mailbox, time) SELECT uid, %Q, strftime(\"%%s\", 'now') FROM %Q WHERE uid = %Q", folder, folder, uid);
	ret = camel_db_add_to_transaction (cdb, tab, ex);
	sqlite3_free (tab);

	ret = camel_db_trim_deleted_table (cdb, ex);

	tab = sqlite3_mprintf ("DELETE FROM '%q_bodystructure' WHERE uid = %Q", folder, uid);
	ret = camel_db_add_to_transaction (cdb, tab, ex);
	sqlite3_free (tab);

	tab = sqlite3_mprintf ("DELETE FROM %Q WHERE uid = %Q", folder, uid);
	ret = camel_db_add_to_transaction (cdb, tab, ex);
	sqlite3_free (tab);

	ret = camel_db_end_transaction (cdb, ex);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;
}

static gint
cdb_delete_ids (CamelDB *cdb, const gchar * folder_name, GSList *uids, const gchar *uid_prefix, const gchar *field, CamelException *ex)
{
	gchar *tmp;
	gint ret;
	gchar *tab;
	gboolean first = TRUE;
	GString *str = g_string_new ("DELETE FROM ");
	GSList *iterator;
	GString *ins_str = NULL;

	if (strcmp (field, "vuid") != 0)
		ins_str = g_string_new ("INSERT OR REPLACE INTO Deletes (uid, mailbox, time) SELECT uid, ");

	camel_db_begin_transaction (cdb, ex);

	if (ins_str)
		ret = camel_db_create_deleted_table (cdb, ex);

	if (ins_str) {
		tab = sqlite3_mprintf ("%Q, strftime(\"%%s\", 'now') FROM %Q WHERE %s IN (", folder_name, folder_name, field);
		g_string_append_printf (ins_str, "%s ", tab);
		sqlite3_free (tab);
	}

	tmp = sqlite3_mprintf ("%Q WHERE %s IN (", folder_name, field);
	g_string_append_printf (str, "%s ", tmp);
	sqlite3_free (tmp);

	iterator = uids;

	while (iterator) {
		gchar *foo = g_strdup_printf("%s%s", uid_prefix, (gchar *) iterator->data);
		tmp = sqlite3_mprintf ("%Q", foo);
		g_free(foo);
		iterator = iterator->next;

		if (first == TRUE) {
			g_string_append_printf (str, " %s ", tmp);
			if (ins_str)
				g_string_append_printf (ins_str, " %s ", tmp);
			first = FALSE;
		} else {
			g_string_append_printf (str, ", %s ", tmp);
			if (ins_str)
				g_string_append_printf (ins_str, ", %s ", tmp);
		}

		sqlite3_free (tmp);
	}

	g_string_append (str, ")");
	if (ins_str) {
		g_string_append (ins_str, ")");
		ret = camel_db_add_to_transaction (cdb, ins_str->str, ex);
		ret = camel_db_trim_deleted_table (cdb, ex);
	}

	ret = camel_db_add_to_transaction (cdb, str->str, ex);

	ret = camel_db_end_transaction (cdb, ex);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;

	if (ins_str)
		g_string_free (ins_str, TRUE);
	g_string_free (str, TRUE);

	return ret;
}

gint
camel_db_delete_uids (CamelDB *cdb, const gchar * folder_name, GSList *uids, CamelException *ex)
{
	if (!uids || !uids->data)
		return 0;

	return cdb_delete_ids (cdb, folder_name, uids, "", "uid", ex);
}

gint
camel_db_delete_vuids (CamelDB *cdb, const gchar * folder_name, const gchar *hash, GSList *uids, CamelException *ex)
{
	return cdb_delete_ids (cdb, folder_name, uids, hash, "vuid", ex);
}

gint
camel_db_clear_folder_summary (CamelDB *cdb, gchar *folder, CamelException *ex)
{
	gint ret;

	gchar *folders_del;
	gchar *msginfo_del;
	gchar *bstruct_del;
	gchar *tab;

	folders_del = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", folder);
	msginfo_del = sqlite3_mprintf ("DELETE FROM %Q ", folder);
	bstruct_del = sqlite3_mprintf ("DELETE FROM '%q_bodystructure' ", folder);

	camel_db_begin_transaction (cdb, ex);

	ret = camel_db_create_deleted_table (cdb, ex);

	tab = sqlite3_mprintf ("INSERT OR REPLACE INTO Deletes (uid, mailbox, time) SELECT uid, %Q, strftime(\"%%s\", 'now') FROM %Q", folder, folder);
	ret = camel_db_add_to_transaction (cdb, tab, ex);
	sqlite3_free (tab);

	ret = camel_db_trim_deleted_table (cdb, ex);

	camel_db_add_to_transaction (cdb, msginfo_del, ex);
	camel_db_add_to_transaction (cdb, folders_del, ex);
	camel_db_add_to_transaction (cdb, bstruct_del, ex);

	ret = camel_db_end_transaction (cdb, ex);

	sqlite3_free (folders_del);
	sqlite3_free (msginfo_del);
	sqlite3_free (bstruct_del);

	return ret;
}

gint
camel_db_delete_folder (CamelDB *cdb, const gchar *folder, CamelException *ex)
{
	gint ret;
	gchar *del;
	gchar *tab;

	camel_db_begin_transaction (cdb, ex);

	ret = camel_db_create_deleted_table (cdb, ex);

	tab = sqlite3_mprintf ("INSERT OR REPLACE INTO Deletes (uid, mailbox, time) SELECT uid, %Q, strftime(\"%%s\", 'now') FROM %Q", folder, folder);
	ret = camel_db_add_to_transaction (cdb, tab, ex);
	sqlite3_free (tab);

	ret = camel_db_trim_deleted_table (cdb, ex);

	del = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", folder);
	ret = camel_db_add_to_transaction (cdb, del, ex);
	sqlite3_free (del);

	del = sqlite3_mprintf ("DROP TABLE %Q ", folder);
	ret = camel_db_add_to_transaction (cdb, del, ex);
	sqlite3_free (del);

	del = sqlite3_mprintf ("DROP TABLE '%q_bodystructure' ", folder);
	ret = camel_db_add_to_transaction (cdb, del, ex);
	sqlite3_free (del);

	ret = camel_db_end_transaction (cdb, ex);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;
}

gint
camel_db_rename_folder (CamelDB *cdb, const gchar *old_folder, const gchar *new_folder, CamelException *ex)
{
	gint ret;
	gchar *cmd, *tab;

	camel_db_begin_transaction (cdb, ex);

	ret = camel_db_create_deleted_table (cdb, ex);

	tab = sqlite3_mprintf ("INSERT OR REPLACE INTO Deletes (uid, mailbox, time) SELECT uid, %Q, strftime(\"%%s\", 'now') FROM %Q", old_folder, old_folder);
	ret = camel_db_add_to_transaction (cdb, tab, ex);
	sqlite3_free (tab);

	ret = camel_db_trim_deleted_table (cdb, ex);

	cmd = sqlite3_mprintf ("ALTER TABLE %Q RENAME TO  %Q", old_folder, new_folder);
	ret = camel_db_add_to_transaction (cdb, cmd, ex);
	sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("ALTER TABLE '%q_version' RENAME TO  '%q_version'", old_folder, new_folder);
        ret = camel_db_add_to_transaction (cdb, cmd, ex);
        sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("UPDATE %Q SET modified=strftime(\"%%s\", 'now'), created=strftime(\"%%s\", 'now')", new_folder);
	ret = camel_db_add_to_transaction (cdb, cmd, ex);
	sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("UPDATE folders SET folder_name = %Q WHERE folder_name = %Q", new_folder, old_folder);
	ret = camel_db_add_to_transaction (cdb, cmd, ex);
	sqlite3_free (cmd);

	ret = camel_db_end_transaction (cdb, ex);

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
		g_free (record->bodystructure);

		g_free (record);
	}
}

gchar *
camel_db_sqlize_string (const gchar *string)
{
	return sqlite3_mprintf ("%Q", string);
}

void
camel_db_free_sqlized_string (gchar *string)
{
	sqlite3_free (string);
	string = NULL;
}

/*
"(  uid TEXT PRIMARY KEY ,
flags INTEGER ,
msg_type INTEGER ,
replied INTEGER ,
dirty INTEGER ,
size INTEGER ,
dsent NUMERIC ,
dreceived NUMERIC ,
mlist TEXT ,
followup_flag TEXT ,
followup_completed_on TEXT ,
followup_due_by TEXT ," */

gchar *
camel_db_get_column_name (const gchar *raw_name)
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

gint
camel_db_migrate_vfolders_to_14 (CamelDB *cdb, const gchar *folder, CamelException *ex)
{
	gchar *cmd = sqlite3_mprintf ("ALTER TABLE %Q ADD COLUMN flags INTEGER", folder);
	gint ret;

	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);

	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	return ret;
}

gint camel_db_start_in_memory_transactions (CamelDB *cdb, CamelException *ex)
{
	gint ret;
	gchar *cmd = sqlite3_mprintf ("ATTACH DATABASE ':memory:' AS %s", CAMEL_DB_IN_MEMORY_DB);

	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("CREATE TEMPORARY TABLE %Q (  uid TEXT PRIMARY KEY , flags INTEGER , msg_type INTEGER , read INTEGER , deleted INTEGER , replied INTEGER , important INTEGER , junk INTEGER , attachment INTEGER , dirty INTEGER , size INTEGER , dsent NUMERIC , dreceived NUMERIC , subject TEXT , mail_from TEXT , mail_to TEXT , mail_cc TEXT , mlist TEXT , followup_flag TEXT , followup_completed_on TEXT , followup_due_by TEXT , part TEXT , labels TEXT , usertags TEXT , cinfo TEXT , bdata TEXT )", CAMEL_DB_IN_MEMORY_TABLE);
	ret = camel_db_command (cdb, cmd, ex);
	if (ret != 0 )
		abort ();
	sqlite3_free (cmd);

	return ret;
}

gint camel_db_flush_in_memory_transactions (CamelDB *cdb, const gchar * folder_name, CamelException *ex)
{
	gint ret;
	gchar *cmd = sqlite3_mprintf ("INSERT INTO %Q SELECT * FROM %Q", folder_name, CAMEL_DB_IN_MEMORY_TABLE);

	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("DROP TABLE %Q", CAMEL_DB_IN_MEMORY_TABLE);
	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);

	cmd = sqlite3_mprintf ("DETACH %Q", CAMEL_DB_IN_MEMORY_DB);
	ret = camel_db_command (cdb, cmd, ex);
	sqlite3_free (cmd);

	return ret;
}
