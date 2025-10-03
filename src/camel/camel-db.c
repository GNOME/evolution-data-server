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
 * Authors: Sankar P <psankar@novell.com>
 *          Srinivasa Ragavan <sragavan@novell.com>
 */

#include "evolution-data-server-config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <sqlite3.h>

#include "camel-debug.h"
#include "camel-enums.h"
#include "camel-operation.h"
#include "camel-search-utils.h"
#include "camel-store-search-private.h"
#include "camel-string-utils.h"
#include "camel-utils.h"

#include "camel-db.h"

/* how long to wait before invoking sync on the file */
#define SYNC_TIMEOUT_SECONDS 5

#define CAMEL_DB_FREE_CACHE_SIZE 2 * 1024 * 1024
#define CAMEL_DB_SLEEP_INTERVAL 1 * 10 * 10

G_DEFINE_QUARK (camel-db-error-quark, camel_db_error)

static sqlite3_vfs *old_vfs = NULL;
static GThreadPool *sync_pool = NULL;

typedef struct {
	sqlite3_file parent;
	sqlite3_file *old_vfs_file; /* pointer to old_vfs' file */
	GRecMutex sync_mutex;
	guint timeout_id;
	gint flags;

	/* Do know how many syncs are pending, to not close
	   the file before the last sync is over */
	guint pending_syncs;
	GMutex pending_syncs_lock;
	GCond pending_syncs_cond;
} CamelSqlite3File;

static gint
call_old_file_Sync (CamelSqlite3File *cFile,
                    gint flags)
{
	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (cFile != NULL, SQLITE_ERROR);

	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);
	return cFile->old_vfs_file->pMethods->xSync (cFile->old_vfs_file, flags);
}

typedef struct {
	GCond cond;
	GMutex mutex;
	gboolean is_set;
} SyncDone;

struct SyncRequestData
{
	CamelSqlite3File *cFile;
	guint32 flags;
	SyncDone *done; /* not NULL when waiting for a finish; will be freed by the caller */
};

static void
sync_request_thread_cb (gpointer task_data,
                        gpointer null_data)
{
	struct SyncRequestData *sync_data = task_data;
	SyncDone *done;

	g_return_if_fail (sync_data != NULL);
	g_return_if_fail (sync_data->cFile != NULL);

	call_old_file_Sync (sync_data->cFile, sync_data->flags);

	g_mutex_lock (&sync_data->cFile->pending_syncs_lock);
	g_warn_if_fail (sync_data->cFile->pending_syncs > 0);
	sync_data->cFile->pending_syncs--;
	if (!sync_data->cFile->pending_syncs)
		g_cond_signal (&sync_data->cFile->pending_syncs_cond);
	g_mutex_unlock (&sync_data->cFile->pending_syncs_lock);

	done = sync_data->done;
	g_slice_free (struct SyncRequestData, sync_data);

	if (done != NULL) {
		g_mutex_lock (&done->mutex);
		done->is_set = TRUE;
		g_cond_broadcast (&done->cond);
		g_mutex_unlock (&done->mutex);
	}
}

static void
sync_push_request (CamelSqlite3File *cFile,
                   gboolean wait_for_finish)
{
	struct SyncRequestData *data;
	SyncDone *done = NULL;
	GError *error = NULL;

	g_return_if_fail (cFile != NULL);
	g_return_if_fail (sync_pool != NULL);

	g_rec_mutex_lock (&cFile->sync_mutex);

	if (!cFile->flags) {
		/* nothing to sync, might be when xClose is called
		 * without any pending xSync request */
		g_rec_mutex_unlock (&cFile->sync_mutex);
		return;
	}

	if (wait_for_finish) {
		done = g_slice_new (SyncDone);
		g_cond_init (&done->cond);
		g_mutex_init (&done->mutex);
		done->is_set = FALSE;
	}

	data = g_slice_new0 (struct SyncRequestData);
	data->cFile = cFile;
	data->flags = cFile->flags;
	data->done = done;

	cFile->flags = 0;

	g_mutex_lock (&cFile->pending_syncs_lock);
	cFile->pending_syncs++;
	g_mutex_unlock (&cFile->pending_syncs_lock);

	g_rec_mutex_unlock (&cFile->sync_mutex);

	g_thread_pool_push (sync_pool, data, &error);

	if (error) {
		g_warning ("%s: Failed to push to thread pool: %s\n", G_STRFUNC, error->message);
		g_error_free (error);

		if (done != NULL) {
			g_cond_clear (&done->cond);
			g_mutex_clear (&done->mutex);
			g_slice_free (SyncDone, done);
		}

		return;
	}

	if (done != NULL) {
		g_mutex_lock (&done->mutex);
		while (!done->is_set)
			g_cond_wait (&done->cond, &done->mutex);
		g_mutex_unlock (&done->mutex);

		g_cond_clear (&done->cond);
		g_mutex_clear (&done->mutex);
		g_slice_free (SyncDone, done);
	}
}

static gboolean
sync_push_request_timeout (gpointer user_data)
{
	CamelSqlite3File *cFile = user_data;

	g_rec_mutex_lock (&cFile->sync_mutex);

	if (cFile->timeout_id != 0) {
		sync_push_request (cFile, FALSE);
		cFile->timeout_id = 0;
	}

	g_rec_mutex_unlock (&cFile->sync_mutex);

	return FALSE;
}

#define def_subclassed(_nm, _params, _call) \
static gint \
camel_sqlite3_file_ ## _nm _params \
{ \
	CamelSqlite3File *cFile; \
 \
	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR); \
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR); \
 \
	cFile = (CamelSqlite3File *) pFile; \
	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR); \
	return cFile->old_vfs_file->pMethods->_nm _call; \
}

#define def_subclassed_void(_nm, _params, _call) \
static void \
camel_sqlite3_file_ ## _nm _params \
{ \
	CamelSqlite3File *cFile; \
 \
	g_return_if_fail (old_vfs != NULL); \
	g_return_if_fail (pFile != NULL); \
 \
	cFile = (CamelSqlite3File *) pFile; \
	g_return_if_fail (cFile->old_vfs_file->pMethods != NULL); \
	cFile->old_vfs_file->pMethods->_nm _call; \
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
def_subclassed (xShmMap, (sqlite3_file *pFile, gint iPg, gint pgsz, gint n, void volatile **arr), (cFile->old_vfs_file, iPg, pgsz, n, arr))
def_subclassed (xShmLock, (sqlite3_file *pFile, gint offset, gint n, gint flags), (cFile->old_vfs_file, offset, n, flags))
def_subclassed_void (xShmBarrier, (sqlite3_file *pFile), (cFile->old_vfs_file))
def_subclassed (xShmUnmap, (sqlite3_file *pFile, gint deleteFlag), (cFile->old_vfs_file, deleteFlag))
def_subclassed (xFetch, (sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp), (cFile->old_vfs_file, iOfst, iAmt, pp))
def_subclassed (xUnfetch, (sqlite3_file *pFile, sqlite3_int64 iOfst, void *p), (cFile->old_vfs_file, iOfst, p))

#undef def_subclassed

static gint
camel_sqlite3_file_xCheckReservedLock (sqlite3_file *pFile,
                                       gint *pResOut)
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

	g_rec_mutex_lock (&cFile->sync_mutex);

	/* Cancel any pending sync requests. */
	if (cFile->timeout_id > 0) {
		g_source_remove (cFile->timeout_id);
		cFile->timeout_id = 0;
	}

	g_rec_mutex_unlock (&cFile->sync_mutex);

	/* Make the last sync. */
	sync_push_request (cFile, TRUE);

	g_mutex_lock (&cFile->pending_syncs_lock);
	while (cFile->pending_syncs > 0) {
		g_cond_wait (&cFile->pending_syncs_cond, &cFile->pending_syncs_lock);
	}
	g_mutex_unlock (&cFile->pending_syncs_lock);

	if (cFile->old_vfs_file->pMethods)
		res = cFile->old_vfs_file->pMethods->xClose (cFile->old_vfs_file);
	else
		res = SQLITE_OK;

	g_free (cFile->old_vfs_file);
	cFile->old_vfs_file = NULL;

	g_rec_mutex_clear (&cFile->sync_mutex);
	g_mutex_clear (&cFile->pending_syncs_lock);
	g_cond_clear (&cFile->pending_syncs_cond);

	return res;
}

static gint
camel_sqlite3_file_xSync (sqlite3_file *pFile,
                          gint flags)
{
	CamelSqlite3File *cFile;
	GSource *source;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (CamelSqlite3File *) pFile;

	g_rec_mutex_lock (&cFile->sync_mutex);

	/* If a sync request is already scheduled, accumulate flags. */
	cFile->flags |= flags;

	/* Cancel any pending sync requests. */
	if (cFile->timeout_id > 0)
		g_source_remove (cFile->timeout_id);

	/* Wait SYNC_TIMEOUT_SECONDS before we actually sync. */
	source = g_timeout_source_new_seconds (SYNC_TIMEOUT_SECONDS);
	g_source_set_callback (source, sync_push_request_timeout, cFile, NULL);
	g_source_set_name (source, "[camel] sync_push_request_timeout");
	cFile->timeout_id = g_source_attach (source, NULL);
	g_source_unref (source);

	g_rec_mutex_unlock (&cFile->sync_mutex);

	return SQLITE_OK;
}

static gint
camel_sqlite3_vfs_xOpen (sqlite3_vfs *pVfs,
                         const gchar *zPath,
                         sqlite3_file *pFile,
                         gint flags,
                         gint *pOutFlags)
{
	static GRecMutex only_once_lock;
	static sqlite3_io_methods io_methods = {0};
	CamelSqlite3File *cFile;
	gint res;

	g_return_val_if_fail (old_vfs != NULL, -1);
	g_return_val_if_fail (pFile != NULL, -1);

	cFile = (CamelSqlite3File *) pFile;
	cFile->old_vfs_file = g_malloc0 (old_vfs->szOsFile);

	res = old_vfs->xOpen (old_vfs, zPath, cFile->old_vfs_file, flags, pOutFlags);
	if (res != SQLITE_OK) {
		g_free (cFile->old_vfs_file);
		return res;
	}

	g_rec_mutex_init (&cFile->sync_mutex);
	g_mutex_init (&cFile->pending_syncs_lock);
	g_cond_init (&cFile->pending_syncs_cond);

	cFile->pending_syncs = 0;

	g_rec_mutex_lock (&only_once_lock);

	if (!sync_pool)
		sync_pool = g_thread_pool_new (sync_request_thread_cb, NULL, 2, FALSE, NULL);

	/* cFile->old_vfs_file->pMethods is NULL when open failed for some reason,
	 * thus do not initialize our structure when do not know the version */
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

		if (io_methods.iVersion > 1) {
			use_subclassed (xShmMap);
			use_subclassed (xShmLock);
			use_subclassed (xShmBarrier);
			use_subclassed (xShmUnmap);
		}

		if (io_methods.iVersion > 2) {
			use_subclassed (xFetch);
			use_subclassed (xUnfetch);
		}

		if (io_methods.iVersion > 3) {
			g_warning ("%s: Unchecked IOMethods version %d, downgrading to version 3", G_STRFUNC, io_methods.iVersion);
			io_methods.iVersion = 3;
		}
		#undef use_subclassed
	}

	g_rec_mutex_unlock (&only_once_lock);

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
#define START(stmt) \
	if (camel_debug ("dbtime")) { \
		g_print ( \
			"\n===========\n" \
			"DB SQL operation [%s] started\n", stmt); \
		if (!cdb->priv->timer) { \
			cdb->priv->timer = g_timer_new (); \
		} else { \
			g_timer_reset (cdb->priv->timer); \
		} \
	}
#define END \
	if (camel_debug ("dbtime")) { \
		g_timer_stop (cdb->priv->timer); \
		g_print ( \
			"DB Operation ended. " \
			"Time Taken : %f\n###########\n", \
			g_timer_elapsed (cdb->priv->timer, NULL)); \
	}
#define STARTTS(stmt) \
	if (camel_debug ("dbtimets")) { \
		g_print ( \
			"\n===========\n" \
			"DB SQL operation [%s] started\n", stmt); \
		if (!cdb->priv->timer) { \
			cdb->priv->timer = g_timer_new (); \
		} else { \
			g_timer_reset (cdb->priv->timer); \
		} \
	}
#define ENDTS(_explanation) \
	if (camel_debug ("dbtimets")) { \
		g_timer_stop (cdb->priv->timer); \
		g_print ( \
			"DB Operation ended. " _explanation \
			"Time Taken : %f\n###########\n", \
			g_timer_elapsed (cdb->priv->timer, NULL)); \
	}

struct _CamelDBPrivate {
	sqlite3 *db;
	GTimer *timer;
	GRWLock rwlock;
	gchar *filename;
	GMutex transaction_lock;
	GThread *transaction_thread;
	guint32 transaction_level;
	gboolean is_foldersdb;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelDB, camel_db, G_TYPE_OBJECT)

static void
camel_db_finalize (GObject *object)
{
	CamelDB *cdb = CAMEL_DB (object);

	sqlite3_close (cdb->priv->db);
	g_rw_lock_clear (&cdb->priv->rwlock);
	g_mutex_clear (&cdb->priv->transaction_lock);
	g_free (cdb->priv->filename);

	d (g_print ("\nDatabase successfully closed \n"));

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_db_parent_class)->finalize (object);
}

static void
camel_db_class_init (CamelDBClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = camel_db_finalize;
}

static void
camel_db_init (CamelDB *cdb)
{
	cdb->priv = camel_db_get_instance_private (cdb);

	g_rw_lock_init (&cdb->priv->rwlock);
	g_mutex_init (&cdb->priv->transaction_lock);
	cdb->priv->transaction_thread = NULL;
	cdb->priv->transaction_level = 0;
	cdb->priv->timer = NULL;
}

/*
 * cdb_sql_exec 
 * @cdb:
 * @stmt:
 * @error:
 *
 * Callers should hold the lock
 */
static gboolean
cdb_sql_exec (CamelDB *cdb,
              const gchar *stmt,
              gint (*callback)(gpointer ,gint,gchar **,gchar **),
              gpointer data,
	      gint *out_sqlite_error_code,
              GError **error)
{
	sqlite3 *db = cdb->priv->db;
	gchar *errmsg = NULL;
	gint   ret, retries = 0;

	g_return_val_if_fail (stmt != NULL, FALSE);

	d (g_print ("Camel SQL Exec:\n%s\n", stmt));

	ret = sqlite3_exec (db, stmt, callback, data, &errmsg);
	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED || ret == -1) {
		/* try for ~15 seconds, then give up */
		if (retries > 150)
			break;
		retries++;

		g_clear_pointer (&errmsg, sqlite3_free);
		g_thread_yield ();
		g_usleep (100 * 1000); /* Sleep for 100 ms */

		ret = sqlite3_exec (db, stmt, callback, data, &errmsg);
	}

	if (out_sqlite_error_code)
		*out_sqlite_error_code = ret;

	/* the abort can happen when the callback returns to stop further processing,
	   which is not a problem, it's requested to stop, thus do not error out */
	if (ret != SQLITE_OK && ret != SQLITE_ABORT) {
		d (g_print ("Error in SQL EXEC statement: %s [%s].\n", stmt, errmsg));
		if (ret == SQLITE_CORRUPT) {
			if (cdb->priv->filename && *cdb->priv->filename) {
				g_set_error (error, CAMEL_DB_ERROR,
					CAMEL_DB_ERROR_CORRUPT, "%s (%s)", errmsg, cdb->priv->filename);
			} else {
				g_set_error (error, CAMEL_DB_ERROR,
					CAMEL_DB_ERROR_CORRUPT, "%s", errmsg);
			}
		} else {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC, "%s", errmsg);
		}
		sqlite3_free (errmsg);
		return FALSE;
	}

	g_clear_pointer (&errmsg, sqlite3_free);

	return TRUE;
}

static void
cdb_camel_compare_date_func (sqlite3_context *ctx,
			     gint nArgs,
			     sqlite3_value **values)
{
	sqlite3_int64 v1, v2;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 2);
	g_return_if_fail (values != NULL);

	v1 = sqlite3_value_int64 (values[0]);
	v2 = sqlite3_value_int64 (values[1]);

	sqlite3_result_int (ctx, camel_search_util_compare_date (v1, v2));
}

/**
 * camel_db_writer_lock:
 * @cdb: a #CamelDB
 *
 * Acquires a writer lock on the @cdb. It can be called multiple times.
 * Call pair function camel_db_writer_unlock() to release it.
 *
 * Note: This adds a transaction on the DB. Not all statements can be executed
 *   in the transaction.
 *
 * Since: 3.58
 **/
void
camel_db_writer_lock (CamelDB *cdb)
{
	g_return_if_fail (cdb != NULL);

	g_mutex_lock (&cdb->priv->transaction_lock);
	if (cdb->priv->transaction_thread != g_thread_self ()) {
		g_mutex_unlock (&cdb->priv->transaction_lock);

		g_rw_lock_writer_lock (&cdb->priv->rwlock);

		g_mutex_lock (&cdb->priv->transaction_lock);

		g_warn_if_fail (cdb->priv->transaction_thread == NULL);
		g_warn_if_fail (cdb->priv->transaction_level == 0);

		cdb->priv->transaction_thread = g_thread_self ();
	}

	cdb->priv->transaction_level++;

	g_mutex_unlock (&cdb->priv->transaction_lock);
}

/**
 * camel_db_writer_unlock:
 * @cdb: a @CamelDB
 *
 * Releases a write lock on the @cdb previously acquired
 * by calling camel_db_writer_lock().
 *
 * Since: 3.58
 **/
void
camel_db_writer_unlock (CamelDB *cdb)
{
	g_return_if_fail (cdb != NULL);

	g_mutex_lock (&cdb->priv->transaction_lock);

	g_warn_if_fail (cdb->priv->transaction_thread == g_thread_self ());
	g_warn_if_fail (cdb->priv->transaction_level > 0);

	cdb->priv->transaction_level--;

	if (!cdb->priv->transaction_level) {
		cdb->priv->transaction_thread = NULL;
		g_mutex_unlock (&cdb->priv->transaction_lock);

		g_rw_lock_writer_unlock (&cdb->priv->rwlock);
	} else {
		g_mutex_unlock (&cdb->priv->transaction_lock);
	}
}

/**
 * camel_db_reader_lock:
 * @cdb: a #CamelDB
 *
 * Acquires a reader lock on the @cdb. It can be called multiple times.
 * Call pair function camel_db_reader_unlock() to release it. it's okay
 * to call this function when a writer lock is already acquired by the
 * calling thread.
 *
 * Since: 3.58
 **/
void
camel_db_reader_lock (CamelDB *cdb)
{
	g_return_if_fail (cdb != NULL);

	g_mutex_lock (&cdb->priv->transaction_lock);
	if (cdb->priv->transaction_thread == g_thread_self ()) {
		/* already holding write lock */
		g_mutex_unlock (&cdb->priv->transaction_lock);
	} else {
		g_mutex_unlock (&cdb->priv->transaction_lock);

		g_rw_lock_reader_lock (&cdb->priv->rwlock);
	}
}

/**
 * camel_db_reader_unlock:
 * @cdb: a #CamelDB
 *
 * Releases a reader lock on the @cdb previously acquired by
 * calling camel_db_reader_lock().
 *
 * Since: 3.58
 **/
void
camel_db_reader_unlock (CamelDB *cdb)
{
	g_return_if_fail (cdb != NULL);

	g_mutex_lock (&cdb->priv->transaction_lock);
	if (cdb->priv->transaction_thread == g_thread_self ()) {
		/* already holding write lock */
		g_mutex_unlock (&cdb->priv->transaction_lock);
	} else {
		g_mutex_unlock (&cdb->priv->transaction_lock);

		g_rw_lock_reader_unlock (&cdb->priv->rwlock);
	}
}

static gchar *
cdb_construct_transaction_stmt (CamelDB *cdb,
				const gchar *prefix)
{
	gchar *name;

	g_return_val_if_fail (cdb != NULL, NULL);

	g_mutex_lock (&cdb->priv->transaction_lock);
	g_warn_if_fail (cdb->priv->transaction_thread == g_thread_self ());
	name = g_strdup_printf ("%sTN%d", prefix ? prefix : "", cdb->priv->transaction_level);
	g_mutex_unlock (&cdb->priv->transaction_lock);

	return name;
}

static gboolean
camel_db_command_internal (CamelDB *cdb,
			   const gchar *stmt,
			   gint *out_sqlite_error_code,
			   GError **error)
{
	gboolean success;

	camel_db_writer_lock (cdb);

	START (stmt);
	success = cdb_sql_exec (cdb, stmt, NULL, NULL, out_sqlite_error_code, error);
	END;

	camel_db_writer_unlock (cdb);

	return success;
}

/**
 * camel_db_new:
 * @filename: A file name with the database to open/create
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CamelDB instance and calls camel_db_open() to
 * open a database file. Free the returned object with
 * g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): A new #CamelDB instance,
 *    or %NULL on error
 *
 * Since: 3.58
 **/
CamelDB *
camel_db_new (const gchar *filename,
	      GError **error)
{
	CamelDB *self;

	self = g_object_new (CAMEL_TYPE_DB, NULL);

	if (!camel_db_open (self, filename, error))
		g_clear_object (&self);

	return self;
}

/**
 * camel_db_open:
 * @cdb: a #CamelDB
 * @filename: A file name with the database to open/create
 * @error: return location for a #GError, or %NULL
 *
 * Opens the database stored as @filename. The function can be called
 * only once, all following calls will result into failures.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_db_open (CamelDB *cdb,
	       const gchar *filename,
	       GError **error)
{
	static GOnce vfs_once = G_ONCE_INIT;
	sqlite3 *db;
	gint ret, cdb_sqlite_error_code = SQLITE_OK;
	gboolean reopening = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	camel_db_writer_lock (cdb);

	if (cdb->priv->db) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("There is already opened file “%s”"), cdb->priv->filename);
		camel_db_writer_unlock (cdb);
		return FALSE;
	}

	g_once (&vfs_once, (GThreadFunc) init_sqlite_vfs, NULL);

 reopen:
	ret = sqlite3_open (filename, &db);
	if (ret) {
		if (!db) {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Insufficient memory"));
		} else {
			const gchar *errmsg;
			errmsg = sqlite3_errmsg (db);
			d (g_print ("Can't open database %s: %s\n", filename, errmsg));
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC, "%s", errmsg);
			sqlite3_close (db);
		}
		camel_db_writer_unlock (cdb);

		return FALSE;
	}

	cdb->priv->db = db;
	cdb->priv->filename = g_strdup (filename);
	d (g_print ("\nDatabase successfully opened  \n"));

	sqlite3_create_function (db, "CAMELCOMPAREDATE", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL, cdb_camel_compare_date_func, NULL, NULL);

	/* Which is big / costlier ? A Stack frame or a pointer */
	if (g_getenv ("CAMEL_SQLITE_DEFAULT_CACHE_SIZE") != NULL) {
		gchar *cache = NULL;

		cache = g_strdup_printf ("PRAGMA cache_size=%s", g_getenv ("CAMEL_SQLITE_DEFAULT_CACHE_SIZE"));
		camel_db_command_internal (cdb, cache, &cdb_sqlite_error_code, &local_error);
		g_free (cache);
	}

	if (cdb_sqlite_error_code == SQLITE_OK)
		camel_db_command_internal (cdb, "ATTACH DATABASE ':memory:' AS mem", &cdb_sqlite_error_code, &local_error);

	if (cdb_sqlite_error_code == SQLITE_OK && g_getenv ("CAMEL_SQLITE_IN_MEMORY") != NULL) {
		/* Optionally turn off Journaling, this gets over fsync issues, but could be risky */
		camel_db_command_internal (cdb, "PRAGMA main.journal_mode = off", &cdb_sqlite_error_code, &local_error);
		if (cdb_sqlite_error_code == SQLITE_OK)
			camel_db_command_internal (cdb, "PRAGMA temp_store = memory", &cdb_sqlite_error_code, &local_error);
	}

	if (!reopening && (
	    cdb_sqlite_error_code == SQLITE_CANTOPEN ||
	    cdb_sqlite_error_code == SQLITE_CORRUPT ||
	    cdb_sqlite_error_code == SQLITE_NOTADB)) {
		gchar *second_filename;

		reopening = TRUE;
		g_clear_pointer (&cdb->priv->db, sqlite3_close);
		g_clear_pointer (&cdb->priv->filename, g_free);

		second_filename = g_strconcat (filename, ".corrupt", NULL);
		if (g_rename (filename, second_filename) == -1) {
			if (!local_error) {
				g_set_error (&local_error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
					_("Could not rename “%s” to %s: %s"),
					filename, second_filename, g_strerror (errno));
			}

			camel_db_writer_unlock (cdb);
			g_propagate_error (error, local_error);

			g_free (second_filename);

			return FALSE;
		}

		g_free (second_filename);

		g_warning ("%s: Failed to open '%s', renamed old file to .corrupt; code:%s (%d) error:%s", G_STRFUNC, filename,
			cdb_sqlite_error_code == SQLITE_CANTOPEN ? "SQLITE_CANTOPEN" :
			cdb_sqlite_error_code == SQLITE_CORRUPT ? "SQLITE_CORRUPT" :
			cdb_sqlite_error_code == SQLITE_NOTADB ? "SQLITE_NOTADB" : "???",
			cdb_sqlite_error_code, local_error ? local_error->message : "Unknown error");

		g_clear_error (&local_error);

		goto reopen;
	}

	if (local_error) {
		camel_db_writer_unlock (cdb);

		g_propagate_error (error, local_error);
		g_clear_pointer (&cdb->priv->db, sqlite3_close);
		g_clear_pointer (&cdb->priv->filename, g_free);
		return FALSE;
	}

	sqlite3_busy_timeout (cdb->priv->db, CAMEL_DB_SLEEP_INTERVAL);

	camel_db_writer_unlock (cdb);

	return TRUE;
}

/*
 * _camel_db_get_sqlite_db:
 * @self: a #CamelDB
 *
 * Returns the SQLite database object. It should be called only after
 * a database is opened, otherwise a %NULL is returned.
 *
 * Returns: (transfer none) (nullable): the SQLite database object
 *
 * Since: 3.58
 **/
sqlite3 *
_camel_db_get_sqlite_db (CamelDB *self)
{
	g_return_val_if_fail (CAMEL_IS_DB (self), NULL);

	return self->priv->db;
}

/**
 * camel_db_get_filename:
 * @cdb: a #CamelDB
 *
 * Returns: (transfer none): A filename associated with @cdb.
 *
 * Since: 3.24
 **/
const gchar *
camel_db_get_filename (CamelDB *cdb)
{
	g_return_val_if_fail (CAMEL_IS_DB (cdb), NULL);

	return cdb->priv->filename;
}

static gboolean
read_integer_callback (gpointer user_data,
                       gint ncol,
                       gchar **cols,
                       gchar **name)
{
	gint *version = user_data;

	if (cols[0])
		*version = (gint) g_ascii_strtoll (cols [0], NULL, 10);

	return TRUE;
}

static gboolean
read_any_found_callback (gpointer ref,
			 gint ncol,
			 gchar **cols,
			 gchar **name)
{
	gboolean *any_found = ref;

	*any_found = TRUE;

	return TRUE;
}

/**
 * camel_db_has_table:
 * @cdb: a #CamelDB
 * @table_name: a table name
 *
 * Checks whether the @table_name exists in the @cdb.
 *
 * Returns: %TRUE, when the @table_name exists, %FALSE when not
 *    or when any other error occurred
 *
 * Since: 3.58
 **/
gboolean
camel_db_has_table (CamelDB *cdb,
		    const gchar *table_name)
{
	gchar *stmt;
	gint count = -1;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);
	g_return_val_if_fail (table_name != NULL, FALSE);

	stmt = sqlite3_mprintf ("SELECT COUNT(tbl_name) FROM sqlite_master WHERE type='table' AND tbl_name=%Q", table_name);

	if (!camel_db_exec_select (cdb, stmt, read_integer_callback, &count, NULL))
		count = -1;

	sqlite3_free (stmt);

	return count == 1;
}

/**
 * camel_db_has_table_with_column:
 * @cdb: a #CamelDB
 * @table_name: a table name
 * @column_name: a table name
 *
 * Checks whether the @table_name exists in the @cdb and contains
 * column named @column_name.
 *
 * Returns: %TRUE, when the @table_name exists and contains @column_name column,
 *    %FALSE when not or when any other error occurred
 *
 * Since: 3.58
 **/
gboolean
camel_db_has_table_with_column (CamelDB *cdb,
				const gchar *table_name,
				const gchar *column_name)
{
	gchar *stmt;
	gboolean any_found = TRUE;

	stmt = sqlite3_mprintf ("SELECT %w FROM %Q LIMIT 1", column_name, table_name);

	if (!camel_db_exec_select (cdb, stmt, read_any_found_callback, &any_found, NULL))
		any_found = FALSE;

	sqlite3_free (stmt);

	return any_found;
}

/**
 * camel_db_set_collate:
 * @cdb: a #CamelDB
 * @col: a column name; currently unused
 * @collate: collation name
 * @func: (scope call): a #CamelDBCollate collation function
 *
 * Defines a collation @collate, which can be used in SQL (SQLite)
 * statement as a collation function. The @func is called when
 * colation is used.
 *
 * Returns: whether succeeded
 *
 * Since: 2.24
 **/
gboolean
camel_db_set_collate (CamelDB *cdb,
                      const gchar *col,
                      const gchar *collate,
                      CamelDBCollate func)
{
	gint ret = 0;

	if (!cdb)
		return TRUE;

	camel_db_writer_lock (cdb);
	d (g_print ("Creating Collation %s on %s with %p\n", collate, col, (gpointer) func));
	if (collate && func)
		ret = sqlite3_create_collation (cdb->priv->db, collate, SQLITE_UTF8,  NULL, func);
	camel_db_writer_unlock (cdb);

	return ret == 0;
}

/**
 * camel_db_exec_statement:
 * @cdb: a #CamelDB
 * @stmt: an SQL (SQLite) statement to execute
 * @error: return location for a #GError, or %NULL
 *
 * Executes an SQLite statement.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_db_exec_statement (CamelDB *cdb,
			 const gchar *stmt,
			 GError **error)
{
	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);
	g_return_val_if_fail (stmt != NULL, FALSE);

	return camel_db_command_internal (cdb, stmt, NULL, error);
}

/**
 * camel_db_begin_transaction:
 * @cdb: a #CamelDB
 * @error: return location for a #GError, or %NULL
 *
 * Begins transaction. End it with camel_db_end_transaction() or camel_db_abort_transaction().
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_db_begin_transaction (CamelDB *cdb,
                            GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);

	camel_db_writer_lock (cdb);

	stmt = cdb_construct_transaction_stmt (cdb, "SAVEPOINT ");

	STARTTS (stmt);
	success = cdb_sql_exec (cdb, stmt, NULL, NULL, NULL, error);
	g_free (stmt);

	if (!success) {
		ENDTS ("Transaction failed to start. ");
		camel_db_writer_unlock (cdb);
	}

	return success;
}

/**
 * camel_db_end_transaction:
 * @cdb: a #CamelDB
 * @error: return location for a #GError, or %NULL
 *
 * Ends an ongoing transaction by committing the changes.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_db_end_transaction (CamelDB *cdb,
                          GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);

	stmt = cdb_construct_transaction_stmt (cdb, "RELEASE SAVEPOINT ");
	success = cdb_sql_exec (cdb, stmt, NULL, NULL, NULL, error);
	g_free (stmt);

	ENDTS ("");
	camel_db_writer_unlock (cdb);
	camel_db_release_cache_memory ();

	return success;
}

/**
 * camel_db_abort_transaction:
 * @cdb: a #CamelDB
 * @error: return location for a #GError, or %NULL
 *
 * Ends an ongoing transaction by ignoring the changes.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_db_abort_transaction (CamelDB *cdb,
                            GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);

	stmt = cdb_construct_transaction_stmt (cdb, "ROLLBACK TO SAVEPOINT ");
	success = cdb_sql_exec (cdb, stmt, NULL, NULL, NULL, error);
	g_free (stmt);

	if (success) {
		stmt = cdb_construct_transaction_stmt (cdb, "RELEASE SAVEPOINT ");
		success = cdb_sql_exec (cdb, stmt, NULL, NULL, NULL, error);
		g_free (stmt);
	}

	ENDTS ("Transaction aborted. ");
	camel_db_writer_unlock (cdb);
	camel_db_release_cache_memory ();

	return success;
}

typedef struct _SelectData {
	CamelDBSelectCB callback;
	gpointer user_data;
} SelectData;

static gint
camel_db_select_cb (gpointer user_data,
		    gint ncol,
		    gchar **colvalues,
		    gchar **colnames)
{
	SelectData *sd = user_data;

	if (sd->callback (sd->user_data, ncol, colvalues, colnames))
		return 0;

	return -1;
}

/**
 * camel_db_exec_select:
 * @cdb: a #CamelDB
 * @stmt: a SELECT statement to execute
 * @callback: (scope call) (closure user_data): a callback to call for each row
 * @user_data: user data for the @callback
 * @error: return location for a #GError, or %NULL
 *
 * Executes a SELECT statement and calls the @callback for each selected row.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_db_exec_select (CamelDB *cdb,
		      const gchar *stmt,
		      CamelDBSelectCB callback,
		      gpointer user_data,
		      GError **error)
{
	SelectData sd = { 0, };
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);
	g_return_val_if_fail (stmt != NULL, FALSE);

	d (g_print ("\n%s:\n%s \n", G_STRFUNC, stmt));
	camel_db_reader_lock (cdb);

	sd.callback = callback;
	sd.user_data = user_data;

	START (stmt);
	success = cdb_sql_exec (cdb, stmt, camel_db_select_cb, &sd, NULL, error);
	END;

	camel_db_reader_unlock (cdb);
	camel_db_release_cache_memory ();

	return success;
}

/**
 * camel_db_sqlize_string:
 * @string: a string to "sqlize"
 *
 * Converts the @string to be usable in the SQLite statements.
 *
 * Returns: (transfer full): A newly allocated sqlized @string. The returned
 *    value should be freed with camel_db_sqlize_string(), when no longer needed.
 *
 * Since: 2.24
 **/
gchar *
camel_db_sqlize_string (const gchar *string)
{
	return sqlite3_mprintf ("%Q", string);
}

/**
 * camel_db_free_sqlized_string:
 * @string: (nullable): a string to free
 *
 * Frees a string previously returned by camel_db_sqlize_string().
 *
 * Since: 2.24
 **/
void
camel_db_free_sqlized_string (gchar *string)
{
	if (string)
		sqlite3_free (string);
}

/**
 * camel_db_sqlize_to_statement:
 * @stmt: a #GString with an SQL statement
 * @str: (nullable): a string to sqlize
 * @flags: bit-or of #CamelDBSqlizeFlags
 *
 * Encodes the @str to be safe to use as a string
 * in an SQL statement and appends it to the @stmt.
 * When @str is %NULL, a NULL word is appended.
 *
 * Since: 3.58
 **/
void
camel_db_sqlize_to_statement (GString *stmt,
			      const gchar *str,
			      CamelDBSqlizeFlags flags)
{
	gchar *tmp;

	g_return_if_fail (stmt != NULL);

	if (!str) {
		g_string_append (stmt, "NULL");
		return;
	}

	if ((flags & CAMEL_DB_SQLIZE_FLAG_ESCAPE_ONLY) != 0)
		tmp = sqlite3_mprintf ("%q", str);
	else
		tmp = sqlite3_mprintf ("%Q", str);

	g_string_append (stmt, tmp);
	sqlite3_free (tmp);
}

static gint
get_number_cb (gpointer data,
	       gint argc,
	       gchar **argv,
	       gchar **azColName)
{
	guint64 *pui64 = data;

	if (argc == 1) {
		*pui64 = argv[0] ? g_ascii_strtoull (argv[0], NULL, 10) : 0;
	} else {
		*pui64 = 0;
	}

	return 0;
}

/**
 * camel_db_maybe_run_maintenance:
 * @cdb: a #CamelDB
 * @error: a #GError or %NULL
 *
 * Runs a @cdb maintenance, which includes vacuum, if necessary.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.16
 **/
gboolean
camel_db_maybe_run_maintenance (CamelDB *cdb,
				GError **error)
{
	GError *local_error = NULL;
	guint64 page_count = 0, page_size = 0, freelist_count = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_DB (cdb), FALSE);

	/* Do not call camel_db_writer_lock() here, because it adds transaction, but
	   the VACUUM cannot run in the transaction. */
	g_rw_lock_writer_lock (&cdb->priv->rwlock);

	if (cdb_sql_exec (cdb, "PRAGMA page_count;", get_number_cb, &page_count, NULL, &local_error) == SQLITE_OK &&
	    cdb_sql_exec (cdb, "PRAGMA page_size;", get_number_cb, &page_size, NULL, &local_error) == SQLITE_OK &&
	    cdb_sql_exec (cdb, "PRAGMA freelist_count;", get_number_cb, &freelist_count, NULL, &local_error) == SQLITE_OK) {
		/* Vacuum, if there's more than 5% of the free pages, or when free pages use more than 10MB */
		success = !page_count || !freelist_count || (freelist_count * page_size < 1024 * 1024 * 10 && freelist_count * 1000 / page_count <= 50) ||
		    cdb_sql_exec (cdb, "vacuum;", NULL, NULL, NULL, &local_error) == SQLITE_OK;
	}

	/* Do not call camel_db_writer_unlock() here, because ... see above */
	g_rw_lock_writer_unlock (&cdb->priv->rwlock);

	if (local_error) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	return success;
}

/**
 * camel_db_release_cache_memory:
 *
 * Instructs sqlite to release its memory, if possible. This can be avoided
 * when CAMEL_SQLITE_FREE_CACHE environment variable is set.
 *
 * Since: 3.24
 **/
void
camel_db_release_cache_memory (void)
{
	static gint env_set = -1;

	if (env_set == -1)
		env_set = g_getenv("CAMEL_SQLITE_FREE_CACHE") ? 1 : 0;

	if (!env_set)
		sqlite3_release_memory (CAMEL_DB_FREE_CACHE_SIZE);
}
