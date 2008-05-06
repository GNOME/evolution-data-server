/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#include <glib.h>
#include "camel-db.h"

#define d(x)
static gboolean
cdb_sql_exec (sqlite3 *db, const char* stmt) {
  	char *errmsg;
  	int   ret;

  	ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);

  	if(ret != SQLITE_OK) {
    		d(g_warning ("Error in statement: %s [%s].\n", stmt, errmsg));
		sqlite3_free (errmsg);
 	}

	return ret == SQLITE_OK;
}

CamelDB *
camel_db_open (const char *path)
{
	CamelDB *cdb;
	sqlite3 *db;
	int ret;

	ret = sqlite3_open(path, &db);
	if(ret) {
    		d(g_warning("Can't open database %s: %s\n", path, sqlite3_errmsg(db)));
    		sqlite3_close(db);
		return NULL;
  	}

	cdb = g_new (CamelDB, 1);
	cdb->db = db;
	cdb->lock = g_mutex_new ();
	return cdb;
}

void
camel_db_close (CamelDB *cdb)
{
	if (cdb) {
		sqlite3_close (cdb->db);
		g_mutex_free (cdb->lock);
		g_free (cdb);
	}
}

gboolean
camel_db_command (CamelDB *cdb, const char *stmt)
{
	gboolean ret;
	
	if (!cdb)
		return TRUE;
	g_mutex_lock (cdb->lock);
	d(printf("Executing: %s\n", stmt));
	ret = cdb_sql_exec (cdb->db, stmt);
	g_mutex_unlock (cdb->lock);
	return ret;
}

/* We enforce it to be count and not COUNT just to speed up */
static int 
count_cb (void *data, int argc, char **argv, char **azColName)
{
  	int i;

  	for(i=0; i<argc; i++) {
		if (strstr(azColName[i], "count")) {
			*(int **)data = atol(argv[i]);
		}
  	}

  	return 0;
}

guint32
camel_db_count (CamelDB *cdb, const char *stmt)
{
	int count=0;
	char *errmsg;
	int ret;

	if (!cdb)
		return 0;
	g_mutex_lock (cdb->lock);
	ret = sqlite3_exec(cdb->db, stmt, count_cb, &count, &errmsg);
  	if(ret != SQLITE_OK) {
    		d(g_warning ("Error in select statement %s [%s].\n", stmt, errmsg));
		sqlite3_free (errmsg);
  	}
	g_mutex_unlock (cdb->lock);
	d(printf("count of '%s' is %d\n", stmt, count));
	return count;
}


int
camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data) 
{
  	char *errmsg;
  	int nrecs = 0;
	int ret;

	if (!cdb)
		return TRUE;
	g_mutex_lock (cdb->lock);	
  	ret = sqlite3_exec(cdb->db, stmt, callback, data, &errmsg);

  	if(ret != SQLITE_OK) {
    		d(g_warning ("Error in select statement '%s' [%s].\n", stmt, errmsg));
		sqlite3_free (errmsg);
  	}
	g_mutex_unlock (cdb->lock);

	return ret;
}


gboolean
camel_db_delete_folder (CamelDB *cdb, char *folder)
{
	char *tab = g_strdup_printf ("delete from folders where folder='%s'", folder);
	gboolean ret;

	ret = camel_db_command (cdb, tab);
	g_free (tab);

	return ret;
}

gboolean
camel_db_delete_uid (CamelDB *cdb, char *folder, char *uid)
{
	char *tab = g_strdup_printf ("delete from %s where uid='%s'", folder, uid);
	gboolean ret;

	ret = camel_db_command (cdb, tab);
	g_free (tab);

	return ret;
}
