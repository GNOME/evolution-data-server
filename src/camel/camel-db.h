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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DB_H
#define CAMEL_DB_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <camel/camel-enums.h>

/* Standard GObject macros */
#define CAMEL_TYPE_DB \
	(camel_db_get_type ())
#define CAMEL_DB(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_DB, CamelDB))
#define CAMEL_DB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_DB, CamelDBClass))
#define CAMEL_IS_DB(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_DB))
#define CAMEL_IS_DB_CLASS(obj) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_DB))
#define CAMEL_DB_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_DB, CamelDBClass))

/**
 * CAMEL_DB_ERROR:
 *
 * Since: 3.44
 **/
#define CAMEL_DB_ERROR \
	(camel_db_error_quark ())

G_BEGIN_DECLS

/**
 * CamelDBError:
 * @CAMEL_DB_ERROR_CORRUPT: database is corrupt
 *
 * Since: 3.44
 **/
typedef enum {
	CAMEL_DB_ERROR_CORRUPT
} CamelDBError;

typedef struct _CamelDB CamelDB;
typedef struct _CamelDBClass CamelDBClass;
typedef struct _CamelDBPrivate CamelDBPrivate;

/**
 * CamelDB:
 *
 * Since: 2.24
 **/
struct _CamelDB {
	/*< private >*/
	GObject parent;
	CamelDBPrivate *priv;
};

struct _CamelDBClass {
	/*< private >*/
	GObjectClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

/**
 * CamelDBCollate:
 * @enc: a used encoding (SQLITE_UTF8)
 * @length1: length of the @data1
 * @data1: the first value, of lenth @length1
 * @length2: length of the @data2
 * @data2: the second value, of lenth @length2
 *
 * A collation callback function.
 *
 * Returns: less than zero, zero, or greater than zero value, the same as for example strcmp() does.
 *
 * Since: 2.24
 **/
typedef gint (* CamelDBCollate)(gpointer enc, gint length1, gconstpointer data1, gint length2, gconstpointer data2);

/**
 * CamelDBSelectCB:
 * @user_data: a callback user data
 * @ncol: how many columns is provided
 * @colvalues: (array length=ncol): array of column values, as UTF-8 strings
 * @colnames: (array length=ncol): array of column names
 *
 * A callback called for the SELECT statements. The items at the same index of @colvalues
 * and @colnames correspond to each other.
 *
 * Returns: %TRUE to continue, %FALSE to abort the execution.
 *
 * Since: 3.58
 **/
typedef gboolean (* CamelDBSelectCB) (gpointer user_data, gint ncol, gchar **colvalues, gchar **colnames);

GQuark		camel_db_error_quark		(void) G_GNUC_CONST;
GType		camel_db_get_type		(void) G_GNUC_CONST;

CamelDB *	camel_db_new			(const gchar *filename,
						 GError **error);
gboolean	camel_db_open			(CamelDB *cdb,
						 const gchar *filename,
						 GError **error);
const gchar *	camel_db_get_filename		(CamelDB *cdb);
void		camel_db_writer_lock		(CamelDB *cdb);
void		camel_db_writer_unlock		(CamelDB *cdb);
void		camel_db_reader_lock		(CamelDB *cdb);
void		camel_db_reader_unlock		(CamelDB *cdb);
gboolean	camel_db_has_table		(CamelDB *cdb,
						 const gchar *table_name);
gboolean	camel_db_has_table_with_column	(CamelDB *cdb,
						 const gchar *table_name,
						 const gchar *column_name);
gboolean	camel_db_exec_select		(CamelDB *cdb,
						 const gchar *stmt,
						 CamelDBSelectCB callback,
						 gpointer user_data,
						 GError **error);
gboolean	camel_db_exec_statement		(CamelDB *cdb,
						 const gchar *stmt,
						 GError **error);
gboolean	camel_db_begin_transaction	(CamelDB *cdb,
						 GError **error);
gboolean	camel_db_end_transaction	(CamelDB *cdb,
						 GError **error);
gboolean	camel_db_abort_transaction	(CamelDB *cdb,
						 GError **error);
gboolean	camel_db_set_collate		(CamelDB *cdb,
						 const gchar *col,
						 const gchar *collate,
						 CamelDBCollate func);
gboolean	camel_db_maybe_run_maintenance	(CamelDB *cdb,
						 GError **error);

void		camel_db_release_cache_memory	(void);

gchar *		camel_db_sqlize_string		(const gchar *string);
void		camel_db_free_sqlized_string	(gchar *string);
void		camel_db_sqlize_to_statement	(GString *stmt,
						 const gchar *str,
						 CamelDBSqlizeFlags flags);

G_END_DECLS

#endif
