/* Srinivasa Ragavan - <sragavan@novell.com> - GPL v2 or later */

#ifndef __CAMEL_DB_H
#define __CAMEL_DB_H
#include <sqlite3.h>
#include <glib.h>
#define CAMEL_DB_FILE "folders.db"
struct _CamelDB {
	sqlite3 *db;
	GMutex *lock;
};

typedef struct _CamelDB CamelDB;
typedef int (*CamelDBSelectCB) (void *data, int ncol, char **colvalues, char **colnames);


CamelDB * camel_db_open (const char *path);
void camel_db_close (CamelDB *cdb);
gboolean camel_db_command (CamelDB *cdb, const char *stmt);
int camel_db_select (CamelDB *cdb, const char* stmt, CamelDBSelectCB callback, gpointer data);

guint32 camel_db_count (CamelDB *cdb, const char *stmt);

#endif

