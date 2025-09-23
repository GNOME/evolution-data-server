/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <sqlite3.h>

#include "camel-db.h"
#include "camel-enums.h"
#include "camel-operation.h"
#include "camel-search-private.h"
#include "camel-store-search.h"
#include "camel-store-search-private.h"
#include "camel-string-utils.h"

#include "camel-store-db.h"

#define FOLDERS_VERSION_KEY "csdb::folders_version"
#define FOLDERS_TABLE_VERSION 1

#define MESSAGES_VERSION_KEY "csdb::messages_version"
#define MESSAGES_TABLE_VERSION 1

#define LOCK(_self) g_rec_mutex_lock (&(_self)->priv->lock)
#define UNLOCK(_self) g_rec_mutex_unlock (&(_self)->priv->lock)

#define get_num(_cl) ((_cl) ? (guint32) g_ascii_strtoull ((_cl), NULL, 10) : 0)
#define get_num64(_cl) ((_cl) ? g_ascii_strtoll ((_cl), NULL, 10) : 0)

/**
 * SECTION: camel-store-db
 * @include: camel/camel.h
 * @short_description: a #CamelStore database
 *
 * The #CamelStoreDB is a descendant of the #CamelDB, which takes care
 * of the save of the message information in an internal database.
 *
 * Since: 3.58
 **/

struct _CamelStoreDBPrivate {
	GRecMutex lock;
	GHashTable *folder_ids; /* gchar *foldername ~> GUINT_TO_POINTER(folder_id) */
	GHashTable *searches; /* gchar *ident ~> CamelStoreSearch * */
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelStoreDB, camel_store_db, CAMEL_TYPE_DB)

/**
 * camel_store_db_folder_record_clear:
 * @self: (nullable): a #CamelStoreDBFolderRecord
 *
 * Frees dynamically allocated data in the @self, but not the @self itself,
 * and sets all members to zeros or equivalent. Does nothing when @self is %NULL.
 * It can be called on the structure passed to the camel_store_db_read_folder().
 *
 * Since: 3.58
 **/
void
camel_store_db_folder_record_clear (CamelStoreDBFolderRecord *self)
{
	if (self) {
		g_free (self->folder_name);
		g_free (self->bdata);
		memset (self, 0, sizeof (*self));
	}
}

/**
 * camel_store_db_message_record_clear:
 * @self: a #CamelStoreDBFolderRecord
 *
 * Frees dynamically allocated data in the @self, but not the @self itself,
 * and sets all members to zeros or equivalent. Does nothing when @self is %NULL.
 * It can be called on the structure passed to the camel_store_db_read_message().
 *
 * Since: 3.58
 **/
void
camel_store_db_message_record_clear (CamelStoreDBMessageRecord *self)
{
	if (self) {
		camel_pstring_free (self->uid);
		camel_pstring_free (self->subject);
		camel_pstring_free (self->from);
		camel_pstring_free (self->to);
		camel_pstring_free (self->cc);
		camel_pstring_free (self->mlist);
		g_free (self->part);
		g_free (self->labels);
		g_free (self->usertags);
		g_free (self->cinfo);
		g_free (self->bdata);
		g_free (self->userheaders);
		g_free (self->preview);
		memset (self, 0, sizeof (*self));
	}
}

static void
camel_store_db_finalize (GObject *object)
{
	CamelStoreDB *self = CAMEL_STORE_DB (object);

	g_hash_table_destroy (self->priv->folder_ids);
	g_hash_table_destroy (self->priv->searches);
	g_rec_mutex_clear (&self->priv->lock);

	G_OBJECT_CLASS (camel_store_db_parent_class)->finalize (object);
}

static void
camel_store_db_class_init (CamelStoreDBClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = camel_store_db_finalize;
}

static void
camel_store_db_init (CamelStoreDB *self)
{
	self->priv = camel_store_db_get_instance_private (self);
	g_rec_mutex_init (&self->priv->lock);
	self->priv->folder_ids = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);
	self->priv->searches = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/* bool camelcmptext(string context, string uid, string header_name, int cmp_kind, string haystack, string needle) */
static void
csdb_camel_cmp_text_func (sqlite3_context *ctx,
			  gint nArgs,
			  sqlite3_value **values)
{
	gboolean matches = FALSE;
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	const gchar *context, *uid, *header_name, *haystack, *needle;
	gint cmp_kind;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 6);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	header_name = (const gchar *) sqlite3_value_text (values[2]);
	cmp_kind = (gint) sqlite3_value_int64 (values[3]);
	haystack = (const gchar *) sqlite3_value_text (values[4]);
	needle = (const gchar *) sqlite3_value_text (values[5]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_compare_text (search, uid, NULL, header_name, cmp_kind, haystack, needle);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelsearchbody(string context, string uid, int cmp_kind, string encoded_words) */
static void
csdb_camel_search_body_func (sqlite3_context *ctx,
			     gint nArgs,
			     sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context, *uid, *encoded_words;
	gint cmp_kind;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 4);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	cmp_kind = (gint) sqlite3_value_int64 (values[2]);
	encoded_words = (const gchar *) sqlite3_value_text (values[3]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_search_body (search, uid, cmp_kind, encoded_words);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelsearchheader(string context, string uid, string header_name, int cmp_kind, string needle, string db_value) */
static void
csdb_camel_search_header_func (sqlite3_context *ctx,
			       gint nArgs,
			       sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	const gchar *context, *uid, *header_name, *needle, *db_value;
	gint cmp_kind;
	gboolean matches = FALSE;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 6);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	header_name = (const gchar *) sqlite3_value_text (values[2]);
	cmp_kind = (gint) sqlite3_value_int64 (values[3]);
	needle = (const gchar *) sqlite3_value_text (values[4]);
	db_value = (const gchar *) sqlite3_value_text (values[5]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_search_header (search, uid, header_name, cmp_kind, needle, db_value);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* string camelgetusertag(string context, string uid, string tag_name, string dbvalue) */
static void
csdb_camel_get_user_tag_func (sqlite3_context *ctx,
			      gint nArgs,
			      sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gchar *value = NULL;
	const gchar *context, *uid, *tag_name, *dbvalue;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 4);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	tag_name = (const gchar *) sqlite3_value_text (values[2]);
	dbvalue = (const gchar *) sqlite3_value_text (values[3]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		value = _camel_store_search_dup_user_tag (search, uid, tag_name, dbvalue);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	if (value)
		sqlite3_result_text (ctx, value, -1, g_free);
	else
		sqlite3_result_null (ctx);
}

/* string camelfromloadedinfoordb(string context, string uid, string column_name, string dbvalue) */
static void
csdb_camel_from_loaded_info_or_db_func (sqlite3_context *ctx,
					gint nArgs,
					sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gchar *value = NULL;
	const gchar *context, *uid, *column_name, *dbvalue;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 4);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	column_name = (const gchar *) sqlite3_value_text (values[2]);
	dbvalue = (const gchar *) sqlite3_value_text (values[3]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		value = _camel_store_search_from_loaded_info_or_db (search, uid, column_name, dbvalue);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	if (value)
		sqlite3_result_text (ctx, value, -1, g_free);
	else
		sqlite3_result_null (ctx);
}

/* bool cameladdressbookcontains(string context, string book_uid, string email) */
static void
csdb_camel_addressbook_contains_func (sqlite3_context *ctx,
				      gint nArgs,
				      sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context, *book_uid, *email;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 3);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	book_uid = (const gchar *) sqlite3_value_text (values[1]);
	email = (const gchar *) sqlite3_value_text (values[2]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_addressbook_contains (search, book_uid, email);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelchecklabels(string context, string uid, string label_to_check, string dbvalue) */
static void
csdb_camel_check_labels_func (sqlite3_context *ctx,
			      gint nArgs,
			      sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context, *uid, *label_to_check, *dbvalue;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 4);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	label_to_check = (const gchar *) sqlite3_value_text (values[2]);
	dbvalue = (const gchar *) sqlite3_value_text (values[3]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_check_labels (search, uid, label_to_check, dbvalue);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelcheckflags(string context, string uid, uint flags_to_check, uint dbvalue) */
static void
csdb_camel_check_flags_func (sqlite3_context *ctx,
			     gint nArgs,
			     sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context, *uid;
	guint32 flags_to_check, dbvalue;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 4);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);
	flags_to_check = (guint32) sqlite3_value_int64 (values[2]);
	if (sqlite3_value_type (values[3]) == SQLITE_NULL)
		dbvalue = 0;
	else
		dbvalue = (guint32) sqlite3_value_int64 (values[3]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_check_flags (search, uid, flags_to_check, dbvalue);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelsearchinresultindex(string context, string uid) */
static void
csdb_camel_search_in_result_index_func (sqlite3_context *ctx,
					gint nArgs,
					sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context, *uid;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 2);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	uid = (const gchar *) sqlite3_value_text (values[1]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_in_result_index (search, uid);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelsearchinmatchindex(string context, string index_id, string uid) */
static void
csdb_camel_search_in_match_index_func (sqlite3_context *ctx,
				       gint nArgs,
				       sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context, *index_id, *uid;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 3);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	index_id = (const gchar *) sqlite3_value_text (values[1]);
	uid = (const gchar *) sqlite3_value_text (values[2]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_in_match_index (search, index_id, uid);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* bool camelisfolderid(string context, int folder_id) */
static void
csdb_camel_is_folder_id_func (sqlite3_context *ctx,
			      gint nArgs,
			      sqlite3_value **values)
{
	CamelStoreDB *self = sqlite3_user_data (ctx);
	CamelStoreSearch *search;
	gboolean matches = FALSE;
	const gchar *context;
	guint32 folder_id;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 2);
	g_return_if_fail (values != NULL);

	context = (const gchar *) sqlite3_value_text (values[0]);
	folder_id = (guint32) sqlite3_value_int (values[1]);

	LOCK (self);
	search = g_hash_table_lookup (self->priv->searches, context);
	if (search)
		g_object_ref (search);
	UNLOCK (self);

	if (search)
		matches = _camel_store_search_is_folder_id (search, folder_id);
	else
		g_warn_if_reached ();

	g_clear_object (&search);

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/* int64 camelmaketime(string str) */
static void
csdb_camel_make_time_func (sqlite3_context *ctx,
			   gint nArgs,
			   sqlite3_value **values)
{
	const gchar *str;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 1);
	g_return_if_fail (values != NULL);

	str = (const gchar *) sqlite3_value_text (values[0]);

	sqlite3_result_int64 (ctx, camel_search_util_str_to_time (str));
}

static void
camel_store_db_init_sqlite_functions (CamelStoreDB *self)
{
	gint flags = SQLITE_UTF8 | SQLITE_DETERMINISTIC;
	sqlite3 *sdb = _camel_db_get_sqlite_db (CAMEL_DB (self));

	g_return_if_fail (sdb != NULL);

	/* bool camelcmptext(string context, string uid, string header_name, int cmp_kind, string haystack, string needle) */
	sqlite3_create_function (sdb, "camelcmptext", 6, flags, self, csdb_camel_cmp_text_func, NULL, NULL);

	/* bool camelsearchbody(string context, string uid, int cmp_kind, string encoded_words) */
	sqlite3_create_function (sdb, "camelsearchbody", 4, flags, self, csdb_camel_search_body_func, NULL, NULL);

	/* bool camelsearchheader(string context, string uid, string header_name, int cmp_kind, string needle, string db_value) */
	sqlite3_create_function (sdb, "camelsearchheader", 6, flags, self, csdb_camel_search_header_func, NULL, NULL);

	/* string camelgetusertag(string context, string uid, string tag_name, string db_tags) */
	sqlite3_create_function (sdb, "camelgetusertag", 4, flags, self, csdb_camel_get_user_tag_func, NULL, NULL);

	/* string camelfromloadedinfoordb(string context, string uid, string column_name, string dbvalue) */
	sqlite3_create_function (sdb, "camelfromloadedinfoordb", 4, flags, self, csdb_camel_from_loaded_info_or_db_func, NULL, NULL);

	/* bool cameladdressbookcontains(string context, string book_uid, string email) */
	sqlite3_create_function (sdb, "cameladdressbookcontains", 3, flags, self, csdb_camel_addressbook_contains_func, NULL, NULL);

	/* bool camelchecklabels(string context, string uid, string label_to_check, string dbvalue) */
	sqlite3_create_function (sdb, "camelchecklabels", 4, flags, self, csdb_camel_check_labels_func, NULL, NULL);

	/* bool camelcheckflags(string context, string uid, uint flags_to_check, uint dbvalue) */
	sqlite3_create_function (sdb, "camelcheckflags", 4, flags, self, csdb_camel_check_flags_func, NULL, NULL);

	/* bool camelsearchinresultindex(string context, string uid) */
	sqlite3_create_function (sdb, "camelsearchinresultindex", 2, flags, self, csdb_camel_search_in_result_index_func, NULL, NULL);

	/* bool camelsearchinmatchindex(string context, string index_id, string uid) */
	sqlite3_create_function (sdb, "camelsearchinmatchindex", 3, flags, self, csdb_camel_search_in_match_index_func, NULL, NULL);

	/* bool camelisfolderid(string context, int folder_id) */
	sqlite3_create_function (sdb, "camelisfolderid", 2, flags, self, csdb_camel_is_folder_id_func, NULL, NULL);

	/* int64 camelmaketime(string str) */
	sqlite3_create_function (sdb, "camelmaketime", 1, flags, self, csdb_camel_make_time_func, NULL, NULL);
}

static gboolean
camel_store_db_read_int_cb (gpointer user_data,
			    gint ncol,
			    gchar **cols,
			    gchar **names)
{
	gint *version = user_data;

	if (cols[0])
		*version = (gint) g_ascii_strtoll (cols[0], NULL, 10);

	return TRUE;
}

static gboolean
camel_store_db_read_uint_cb (gpointer user_data,
			     gint ncol,
			     gchar **cols,
			     gchar **names)
{
	guint32 *value = user_data;

	if (cols[0])
		*value = (guint32) g_ascii_strtoull (cols[0], NULL, 10);

	return TRUE;
}

static gboolean
camel_store_db_read_string_cb (gpointer user_data,
			       gint ncol,
			       gchar **cols,
			       gchar **names)
{
	gchar **pvalue = user_data;

	*pvalue = g_strdup (cols[0]);

	return TRUE;
}

static gint
camel_store_db_get_int_key_internal (CamelStoreDB *self,
				     const gchar *key,
				     gint def_value)
{
	gint value = def_value;
	gchar *stmt;

	stmt = sqlite3_mprintf ("SELECT value FROM keys WHERE key=%Q", key);
	if (!camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_int_cb, &value, NULL))
		value = def_value;
	sqlite3_free (stmt);

	return value;
}

static gboolean
camel_store_db_set_int_key_internal (CamelStoreDB *self,
				     const gchar *key,
				     gint value,
				     GError **error)
{
	gboolean success;
	gchar *stmt;

	stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO keys (key, value) VALUES (%Q, %d)", key, value);
	success = camel_db_exec_statement (CAMEL_DB (self), stmt, error);
	sqlite3_free (stmt);

	return success;
}

static gchar *
camel_store_db_dup_string_key_internal (CamelStoreDB *self,
					const gchar *key)
{
	gchar *value = NULL;
	gchar *stmt;

	stmt = sqlite3_mprintf ("SELECT value FROM keys WHERE key=%Q", key);
	if (!camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_string_cb, &value, NULL))
		value = NULL;
	sqlite3_free (stmt);

	return value;
}

static gboolean
camel_store_db_set_string_key_internal (CamelStoreDB *self,
					const gchar *key,
					const gchar *value,
					GError **error)
{
	gboolean success;
	gchar *stmt;

	stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO keys (key, value) VALUES (%Q, %Q)", key, value);
	success = camel_db_exec_statement (CAMEL_DB (self), stmt, error);
	sqlite3_free (stmt);

	return success;
}

static gboolean
camel_store_db_migrate_message_info_to_tmp_messages (CamelDB *cdb,
						     const gchar *folder_name,
						     guint folder_id,
						     gint current_version,
						     GError **error)
{
	gchar *stmt;
	gboolean success = TRUE;

	if (current_version >= 0 && current_version < 3) {
		/* Between version 0-1 the following things are changed
		 * ADDED: created: time
		 * ADDED: modified: time
		 * RENAMED: msg_security to dirty
		 *
		 * Between version 2-3 the following things are changed
		 * ADDED: userheaders: text
		 * ADDED: preview: text
		 */

		stmt = sqlite3_mprintf (
			"INSERT INTO 'mem.messages' SELECT "
			"uid, flags, msg_type, %s, size, dsent, dreceived, "
			"subject, mail_from, mail_to, mail_cc, mlist, "
			"part, labels, usertags, cinfo, bdata, '', '', %u"
			" FROM %Q",
			current_version == 0 ? "msg_security" : "dirty", folder_id, folder_name);
		success = camel_db_exec_statement (cdb, stmt, error);
		sqlite3_free (stmt);
	} else if (current_version == 3) {
		/* only fill the folder_id column */
		stmt = sqlite3_mprintf (
			"INSERT INTO 'mem.messages' SELECT "
			"uid, flags, msg_type, dirty, "
			"size, dsent, dreceived, subject, "
			"mail_from, mail_to, mail_cc, mlist, "
			"part, labels, usertags, "
			"cinfo, bdata, userheaders, preview, %u"
			" FROM %Q",
			folder_id, folder_name);
		success = camel_db_exec_statement (cdb, stmt, error);
		sqlite3_free (stmt);
	}

	return success;
}

static gboolean
camel_store_db_create_messages_table (CamelDB *cdb,
				      guint32 folder_id,
				      GError **error)
{
	const gchar *prefix;
	gchar *table_name = NULL;
	gchar *stmt;
	gboolean success;

	if (folder_id) {
		table_name = g_strdup_printf ("messages_%u", folder_id);
		prefix = "CREATE TABLE ";
	} else {
		prefix = "CREATE TEMP TABLE 'mem.messages'";
	}

	stmt = g_strconcat (prefix,
		table_name ? table_name : "",
		" ( ",
		"uid TEXT, "
		"flags INTEGER, "
		"msg_type INTEGER, "
		"dirty INTEGER, "
		"size INTEGER, "
		"dsent NUMERIC, "
		"dreceived NUMERIC, "
		"subject TEXT, "
		"mail_from TEXT, "
		"mail_to TEXT, "
		"mail_cc TEXT, "
		"mlist TEXT, "
		"part TEXT, "
		"labels TEXT, "
		"usertags TEXT, "
		"cinfo TEXT, "
		"bdata TEXT, "
		"userheaders TEXT, "
		"preview TEXT",
		folder_id ? "" : ", folder_id INTEGER",
		folder_id ? ", PRIMARY KEY (uid)" : "",
		")",
		NULL);

	success = camel_db_exec_statement (cdb, stmt, error);

	g_free (stmt);

	if (success && folder_id) {
		g_return_val_if_fail (table_name != NULL, FALSE);

		stmt = g_strdup_printf ("CREATE INDEX %s_uid_flags ON %s (uid, flags)", table_name, table_name);
		success = camel_db_exec_statement (cdb, stmt, error);
		g_free (stmt);

		if (!success) {
			g_free (table_name);
			return success;
		}
	}

	g_free (table_name);

	return success;
}

static gboolean
camel_store_db_migrate_folders_table (CamelStoreDB *self,
				      gint current_version,
				      GError **error)
{
	if (current_version >= FOLDERS_TABLE_VERSION)
		return TRUE;

	return camel_store_db_set_int_key_internal (self, FOLDERS_VERSION_KEY, FOLDERS_TABLE_VERSION, error);
}

static gboolean
camel_store_db_read_folders_folder_ids_cb (gpointer user_data,
					   gint ncol,
					   gchar **cols,
					   gchar **name)
{
	GHashTable *folder_ids = user_data;
	const gchar *folder_id_str = cols[0];

	if (folder_id_str && *folder_id_str) {
		guint32 folder_id = (guint32) g_ascii_strtoll (folder_id_str, NULL, 10);

		if (folder_id)
			g_hash_table_add (folder_ids, GUINT_TO_POINTER (folder_id));
	}

	return TRUE;
}

static gboolean
camel_store_db_migrate_one_messages_table (CamelStoreDB *self,
					   const gchar *table_name,
					   gint current_version,
					   GError **error)
{
	gboolean success = TRUE;

	/* migrate the table_name, aka 'messages_X', to the new version from the currect_version */

	return success;
}

static gboolean
camel_store_db_migrate_messages_tables (CamelStoreDB *self,
					gint current_version,
					GError **error)
{
	CamelDB *cdb;
	GHashTable *folder_ids;
	gboolean success;

	if (current_version >= MESSAGES_TABLE_VERSION)
		return TRUE;

	cdb = CAMEL_DB (self);

	folder_ids = g_hash_table_new (g_direct_hash, g_direct_equal);
	success = camel_db_exec_select (cdb, "SELECT folder_id FROM folders", camel_store_db_read_folders_folder_ids_cb, folder_ids, NULL);
	if (success) {
		GHashTableIter iter;
		gpointer key;

		g_hash_table_iter_init (&iter, folder_ids);
		while (success && g_hash_table_iter_next (&iter, &key, NULL)) {
			guint32 folder_id = GPOINTER_TO_UINT (key);
			gchar *table_name = g_strdup_printf ("messages_%u", folder_id);

			if (camel_db_has_table (cdb, table_name))
				success = camel_store_db_migrate_one_messages_table (self, table_name, current_version, error);

			g_free (table_name);
		}
	}

	g_hash_table_unref (folder_ids);

	return success && camel_store_db_set_int_key_internal (self, MESSAGES_VERSION_KEY, MESSAGES_TABLE_VERSION, error);
}

static gboolean
camel_store_db_create_keys_table_with_defauls (CamelStoreDB *self,
					       GError **error)
{
	gboolean success;

	success = camel_db_exec_statement (CAMEL_DB (self), "CREATE TABLE keys (key TEXT PRIMARY KEY, value TEXT)", error);
	if (!success)
		return success;

	success = camel_store_db_set_int_key_internal (self, FOLDERS_VERSION_KEY, FOLDERS_TABLE_VERSION, error);
	if (success)
		success = camel_store_db_set_int_key_internal (self, MESSAGES_VERSION_KEY, MESSAGES_TABLE_VERSION, error);

	return success;
}

typedef struct _SplitTableNames {
	GHashTable *known_folders;
	GPtrArray *disposable_table_names;
	GPtrArray *disposable_indexes;
} SplitTableNames;

static gboolean
camel_store_db_read_folders_folder_name_cb (gpointer user_data,
					    gint ncol,
					    gchar **cols,
					    gchar **name)
{
	SplitTableNames *stn = user_data;
	const gchar *table_name = cols[0];

	if (table_name && *table_name)
		g_hash_table_insert (stn->known_folders, g_strdup (table_name), GUINT_TO_POINTER (g_hash_table_size (stn->known_folders) + 1));

	return TRUE;
}

static gboolean
camel_store_db_read_disposable_table_names_cb (gpointer user_data,
					       gint ncol,
					       gchar **cols,
					       gchar **name)
{
	SplitTableNames *stn = user_data;
	const gchar *table_name = cols[0];

	if (table_name && *table_name && !g_str_equal (table_name, "folders") && !g_str_equal (table_name, "sqlite_sequence"))
		g_ptr_array_add (stn->disposable_table_names, g_strdup (table_name));

	return TRUE;
}

static gboolean
camel_store_db_read_disposable_indexes_cb (gpointer user_data,
					   gint ncol,
					   gchar **cols,
					   gchar **names)
{
	SplitTableNames *stn = user_data;
	const gchar *index_name = cols[0];

	if (index_name && *index_name)
		g_ptr_array_add (stn->disposable_indexes, g_strdup (index_name));

	return TRUE;
}

static gboolean
camel_store_db_maybe_migrate (CamelStoreDB *self,
			      GCancellable *cancellable,
			      GError **error)
{
	const gchar *query = "CREATE TABLE folders ( "
		"folder_name TEXT PRIMARY KEY, "
		"version INTEGER, "
		"flags INTEGER, "
		"nextuid INTEGER, "
		"time NUMERIC, "
		"saved_count INTEGER, "
		"unread_count INTEGER, "
		"deleted_count INTEGER, "
		"junk_count INTEGER, "
		"visible_count INTEGER, "
		"jnd_count INTEGER, "
		"bdata TEXT, "
		"folder_id INTEGER )";
	CamelDB *cdb = CAMEL_DB (self);
	SplitTableNames stn;
	GHashTableIter iter;
	gpointer key, value;
	gchar *stmt;
	guint ii, n_ops, current_op;
	gboolean success = TRUE;

	camel_db_release_cache_memory ();

	if (!camel_db_has_table (cdb, "folders")) {
		success = camel_db_exec_statement (cdb, query, error);
		if (!success)
			return success;

		return camel_store_db_create_keys_table_with_defauls (self, error);
	}

	/* 3.58.0 adds a new column 'folders.folder_id' and renames all the message info tables into a 'messages_X' table;
	   consider existing 'folder_id' column as an already migrated state */
	if (camel_db_has_table_with_column (cdb, "folders", "folder_id")) {
		gint current_version;

		/* if the the 'keys' table is not here, then something really bad happened */
		if (!camel_db_has_table (cdb, "keys")) {
			success = camel_store_db_create_keys_table_with_defauls (self, error);
			if (!success)
				return success;
		}

		current_version = camel_store_db_get_int_key_internal (self, FOLDERS_VERSION_KEY, -1);
		if (current_version < FOLDERS_TABLE_VERSION) {
			success = camel_db_begin_transaction (cdb, error);
			if (!success)
				return success;

			success = camel_store_db_migrate_folders_table (self, current_version, error);
			if (!success) {
				camel_db_abort_transaction (cdb, NULL);
				return success;
			}

			success = camel_db_end_transaction (cdb, error);
			if (!success)
				return success;
		}

		current_version = camel_store_db_get_int_key_internal (self, MESSAGES_VERSION_KEY, -1);
		if (current_version < MESSAGES_TABLE_VERSION) {
			success = camel_db_begin_transaction (cdb, error);
			if (!success)
				return success;

			success = camel_store_db_migrate_messages_tables (self, current_version, error);
			if (!success) {
				camel_db_abort_transaction (cdb, NULL);
				return success;
			}

			success = camel_db_end_transaction (cdb, error);
			if (!success)
				return success;
		}

		return success;
	}

	/* migrate the data */
	success = camel_db_begin_transaction (cdb, error);
	if (!success)
		return success;

	camel_operation_push_message (cancellable, "%s", _("Gathering existing summary data…"));

	stn.known_folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL); /* gchar *folder_name ~> gint folder_id */
	stn.disposable_table_names = g_ptr_array_new_with_free_func (g_free); /* gchar * */
	stn.disposable_indexes = g_ptr_array_new_with_free_func (g_free); /* gchar * */

	success = camel_db_exec_select (cdb, "SELECT folder_name FROM folders", camel_store_db_read_folders_folder_name_cb, &stn, NULL);
	if (!success)
		goto exit;

	success = camel_db_exec_select (cdb, "SELECT tbl_name FROM sqlite_master WHERE type='table'", camel_store_db_read_disposable_table_names_cb, &stn, NULL);
	if (!success)
		goto exit;

	success = camel_db_exec_select (cdb, "SELECT name FROM sqlite_master WHERE type='index' and name NOT LIKE 'sqlite_%'", camel_store_db_read_disposable_indexes_cb, &stn, NULL);
	if (!success)
		goto exit;

	/* it adds the column as the last */
	success = camel_db_exec_statement (cdb, "ALTER TABLE folders ADD COLUMN folder_id INTEGER", error);
	if (!success)
		goto exit;

	success = camel_db_exec_statement (cdb, "DROP TABLE IF EXISTS 'mem.messages'", error);
	if (!success)
		goto exit;

	success = camel_store_db_create_messages_table (cdb, 0, error);
	if (!success)
		goto exit;

	camel_operation_pop_message (cancellable);
	camel_operation_push_message (cancellable, "%s", _("Migrating folder data…"));

	n_ops = (2 * g_hash_table_size (stn.known_folders)) + stn.disposable_table_names->len + stn.disposable_indexes->len + 1;
	current_op = 0;

	g_hash_table_iter_init (&iter, stn.known_folders);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *folder_name = key;
		guint folder_id = GPOINTER_TO_UINT (value);

		camel_operation_progress (cancellable, (current_op++) * 100.0 / n_ops);

		stmt = sqlite3_mprintf ("UPDATE folders SET folder_id=%u WHERE folder_name=%Q", folder_id, folder_name);
		success = camel_db_exec_statement (cdb, stmt, error);
		sqlite3_free (stmt);
		if (!success)
			goto exit;

		if (camel_db_has_table (cdb, folder_name)) {
			gint current_version = -1;

			stmt = sqlite3_mprintf ("SELECT version FROM '%q_version'", folder_name);
			if (!camel_db_exec_select (cdb, stmt, camel_store_db_read_int_cb, &current_version, NULL))
				current_version = -1;
			sqlite3_free (stmt);

			success = camel_store_db_migrate_message_info_to_tmp_messages (cdb, folder_name, folder_id, current_version, error);
			if (!success)
				goto exit;

			stmt = sqlite3_mprintf ("DROP TABLE %Q", folder_name);
			success = camel_db_exec_statement (cdb, stmt, error);
			sqlite3_free (stmt);
			if (!success)
				goto exit;
		}
	}

	camel_operation_pop_message (cancellable);
	camel_operation_push_message (cancellable, "%s", _("Removing obsolete data…"));
	camel_operation_progress (cancellable, current_op * 100.0 / n_ops);

	for (ii = 0; ii < stn.disposable_table_names->len; ii++) {
		const gchar *table_name = g_ptr_array_index (stn.disposable_table_names, ii);
		GError *local_error = NULL;

		camel_operation_progress (cancellable, (current_op++) * 100.0 / n_ops);
		stmt = sqlite3_mprintf ("DROP TABLE IF EXISTS %Q", table_name);
		if (!camel_db_exec_statement (cdb, stmt, &local_error)) {
			g_warning ("%s: Failed to drop table '%s' in '%s': %s", G_STRFUNC, table_name, camel_db_get_filename (cdb),
				local_error ? local_error->message : "Unknown error");
		}
		g_clear_error (&local_error);
		sqlite3_free (stmt);
	}

	for (ii = 0; ii < stn.disposable_indexes->len; ii++) {
		const gchar *name = g_ptr_array_index (stn.disposable_indexes, ii);
		GError *local_error = NULL;

		camel_operation_progress (cancellable, (current_op++) * 100.0 / n_ops);
		stmt = sqlite3_mprintf ("DROP INDEX IF EXISTS %Q", name);
		if (!camel_db_exec_statement (cdb, stmt, &local_error)) {
			g_warning ("%s: Failed to drop index '%s' in '%s': %s", G_STRFUNC, name, camel_db_get_filename (cdb),
				local_error ? local_error->message : "Unknown error");
		}
		g_clear_error (&local_error);
		sqlite3_free (stmt);
	}

	success = camel_store_db_create_keys_table_with_defauls (self, error);
	if (!success)
		goto exit;

	camel_operation_pop_message (cancellable);
	camel_operation_push_message (cancellable, "%s", _("Moving data…"));
	camel_operation_progress (cancellable, current_op * 100.0 / n_ops);

	g_hash_table_iter_init (&iter, stn.known_folders);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint folder_id = GPOINTER_TO_UINT (value);

		camel_operation_progress (cancellable, (current_op++) * 100.0 / n_ops);

		success = camel_store_db_create_messages_table (cdb, folder_id, error);
		if (!success)
			goto exit;

		stmt = g_strdup_printf ("INSERT INTO messages_%u SELECT uid, flags, msg_type, "
			"dirty, size, dsent, dreceived, subject, mail_from, "
			"mail_to, mail_cc, mlist, part, labels, usertags, "
			"cinfo, bdata, userheaders, preview "
			"FROM 'mem.messages' WHERE folder_id=%u", folder_id, folder_id);
		/* copy data from the temporary table to the new (permanent) messages table */
		success = camel_db_exec_statement (cdb, stmt, error);
		g_free (stmt);

		if (!success)
			goto exit;
	}

	camel_operation_pop_message (cancellable);
	camel_operation_push_message (cancellable, "%s", _("Clean up data…"));
	camel_operation_progress (cancellable, current_op * 100.0 / n_ops);

	success = camel_db_exec_statement (cdb, "DROP TABLE IF EXISTS 'mem.messages'", error);
	if (!success)
		goto exit;

 exit:
	camel_operation_pop_message (cancellable);

	if (success) {
		success = camel_db_end_transaction (cdb, error);
		camel_operation_progress (cancellable, (current_op++) * 100.0 / n_ops);

		if (success) {
			/* ignore errors from the vacuum */
			(void) camel_db_maybe_run_maintenance (cdb, NULL);
		}
	} else {
		camel_db_abort_transaction (cdb, NULL);
	}

	camel_operation_progress (cancellable, 0);

	g_hash_table_unref (stn.known_folders);
	g_ptr_array_unref (stn.disposable_table_names);
	g_ptr_array_unref (stn.disposable_indexes);

	return success;
}

static gboolean
camel_store_db_read_folder_ids_cb (gpointer user_data,
				   gint ncol,
				   gchar **cols,
				   gchar **names)
{
	GHashTable *folder_ids = user_data;
	const gchar *folder_name;
	const gchar *folder_id_str;

	if (ncol != 2)
		return FALSE;

	folder_name = cols[0];
	folder_id_str = cols[1];

	if (folder_name && *folder_name && folder_id_str && *folder_id_str) {
		guint32 folder_id;

		folder_id = (guint32) g_ascii_strtoll (folder_id_str, NULL, 10);

		if (folder_id != 0)
			g_hash_table_insert (folder_ids, g_strdup (folder_name), GUINT_TO_POINTER (folder_id));
	}

	return TRUE;
}

static gboolean
camel_store_db_reread_folder_ids (CamelStoreDB *self,
				  GError **error)
{
	CamelDB *cdb;
	gboolean success;

	cdb = CAMEL_DB (self);

	camel_db_writer_lock (cdb);
	LOCK (self);

	g_hash_table_remove_all (self->priv->folder_ids);

	success = camel_db_exec_select (cdb, "SELECT folder_name, folder_id FROM folders",
		camel_store_db_read_folder_ids_cb, self->priv->folder_ids, error);

	if (success) {
		GHashTableIter iter;
		gpointer value = NULL;

		/* ensure respective 'messages_X' tables exist */
		g_hash_table_iter_init (&iter, self->priv->folder_ids);
		while (success && g_hash_table_iter_next (&iter, NULL, &value)) {
			guint32 folder_id = GPOINTER_TO_UINT (value);

			if (folder_id) {
				gchar *table_name = g_strdup_printf ("messages_%u", folder_id);

				if (!camel_db_has_table (cdb, table_name))
					success = camel_store_db_create_messages_table (cdb, folder_id, error);

				g_free (table_name);
			}
		}
	}

	UNLOCK (self);
	camel_db_writer_unlock (cdb);

	return success;
}

/**
 * camel_store_db_new:
 * @filename: a file name of the database to use
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CamelStoreDB instance, which uses @filename as
 * its storage.
 *
 * It also migrates existing data, if needed, providing feedback
 * through the @cancellable, if it's a #CamelOperation instance.
 *
 * Returns: (transfer full): a new #CamelStoreDB, or %NULL on error
 *
 * Since: 3.58
 **/
CamelStoreDB *
camel_store_db_new (const gchar *filename,
		    GCancellable *cancellable,
		    GError **error)
{
	CamelStoreDB *self;

	g_return_val_if_fail (filename != NULL, NULL);

	self = g_object_new (CAMEL_TYPE_STORE_DB, NULL);

	if (!camel_db_open (CAMEL_DB (self), filename, error)) {
		g_clear_object (&self);
		return NULL;
	}

	if (!camel_store_db_maybe_migrate (self, cancellable, error)) {
		g_clear_object (&self);
		return NULL;
	}

	if (!camel_store_db_reread_folder_ids (self, error)) {
		g_clear_object (&self);
		return NULL;
	}

	camel_store_db_init_sqlite_functions (self);

	return self;
}

static gchar *
camel_store_db_construct_user_key_name (const gchar *key)
{
	return g_strconcat ("user::", key, NULL);
}

/**
 * camel_store_db_get_int_key:
 * @self: a #CamelStoreDB
 * @key: a user key to read
 * @def_value: a default value to return, when the key not stored yet
 *
 * Reads an integer value for the @key. If such does not exists, the @def_value
 * is returned.
 *
 * Returns: an integer value of the @key, or @def_value when does not exist
 *   or any other error occurred
 *
 * See also camel_store_db_set_int_key(), camel_store_db_dup_string_key()
 *
 * Since: 3.58
 **/
gint
camel_store_db_get_int_key (CamelStoreDB *self,
			    const gchar *key,
			    gint def_value)
{
	gchar *user_key;
	gint value;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), def_value);
	g_return_val_if_fail (key && *key, def_value);

	user_key = camel_store_db_construct_user_key_name (key);
	value = camel_store_db_get_int_key_internal (self, user_key, def_value);
	g_free (user_key);

	return value;
}

/**
 * camel_store_db_set_int_key:
 * @self: a #CamelStoreDB
 * @key: a user key to set
 * @value: a value to set
 * @error: a return location for a #GError, or %NULL
 *
 * Sets an integer value for the @key to @value.
 *
 * Returns: whether succeeded
 *
 * See also camel_store_db_get_int_key(), camel_store_db_set_string_key()
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_set_int_key (CamelStoreDB *self,
			    const gchar *key,
			    gint value,
			    GError **error)
{
	gchar *user_key;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (key && *key, FALSE);

	user_key = camel_store_db_construct_user_key_name (key);
	success = camel_store_db_set_int_key_internal (self, user_key, value, error);
	g_free (user_key);

	return success;
}

/**
 * camel_store_db_dup_string_key:
 * @self: a #CamelStoreDB
 * @key: a user key to read
 *
 * Reads a string value for the @key. If such does not exists, the %NULL
 * is returned.
 *
 * Returns: (transfer full) (nullable): a string value of the @key,
 *   or %NULL when does not exist or any other error occurred
 *
 * See also camel_store_db_set_string_key(), camel_store_db_get_int_key()
 *
 * Since: 3.58
 **/
gchar *
camel_store_db_dup_string_key (CamelStoreDB *self,
			       const gchar *key)
{
	gchar *user_key;
	gchar *value;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), NULL);
	g_return_val_if_fail (key && *key, NULL);

	user_key = camel_store_db_construct_user_key_name (key);
	value = camel_store_db_dup_string_key_internal (self, user_key);
	g_free (user_key);

	return value;
}

/**
 * camel_store_db_set_string_key:
 * @self: a #CamelStoreDB
 * @key: a user key to set
 * @value: a value to set
 * @error: a return location for a #GError, or %NULL
 *
 * Sets a string value for the @key to @value.
 *
 * Returns: whether succeeded
 *
 * See also camel_store_db_dup_string_key(), camel_store_db_set_int_key()
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_set_string_key (CamelStoreDB *self,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	gchar *user_key;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (key && *key, FALSE);

	user_key = camel_store_db_construct_user_key_name (key);
	success = camel_store_db_set_string_key_internal (self, user_key, value ? value : "", error);
	g_free (user_key);

	return success;
}

/**
 * camel_store_db_write_folder:
 * @self: a #CamelStoreDB
 * @folder_name: name of the folder to write the record to
 * @record: an #CamelStoreDBFolderRecord
 * @error: a return location for a #GError, or %NULL
 *
 * Writes information about a folder as set in the @record.
 * The "folder_id" member of the @record is ignored, the same
 * as the "folder_name" member, the folder is identified by
 * the @folder_name argument.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_write_folder (CamelStoreDB *self,
			     const gchar *folder_name,
			     const CamelStoreDBFolderRecord *record,
			     GError **error)
{
	CamelDB *cdb;
	guint32 folder_id;
	gboolean folder_id_changed = FALSE;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	cdb = CAMEL_DB (self);

	camel_db_writer_lock (cdb);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (!folder_id) {
		success = camel_db_exec_select (cdb, "SELECT MAX(folder_id) FROM folders", camel_store_db_read_uint_cb, &folder_id, error);
		if (success) {
			folder_id++;
			folder_id_changed = TRUE;
		}
	}

	if (success) {
		gchar *stmt;

		stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO folders "
			"(folder_name, version, flags, nextuid, time, saved_count, unread_count, "
			"deleted_count, junk_count, visible_count, jnd_count, bdata, folder_id) "
			"VALUES "
			"(%Q, %d, %d, %d, %lld, %d, %d, %d, %d, %d, %d, %Q, %d)",
			folder_name,
			record->version,
			record->flags,
			record->nextuid,
			record->timestamp,
			record->saved_count,
			record->unread_count,
			record->deleted_count,
			record->junk_count,
			record->visible_count,
			record->jnd_count,
			record->bdata,
			folder_id);
		success = camel_db_exec_statement (cdb, stmt, error);
		sqlite3_free (stmt);
	}

	if (success && folder_id_changed) {
		LOCK (self);
		g_hash_table_insert (self->priv->folder_ids, g_strdup (folder_name), GUINT_TO_POINTER (folder_id));
		UNLOCK (self);

		success = camel_store_db_create_messages_table (cdb, folder_id, error);
	}

	camel_db_writer_unlock (cdb);

	return success;
}

static gboolean
camel_store_db_read_folder_record_cb (gpointer user_data,
				      gint ncol,
				      gchar **cols,
				      gchar **names)
{
	CamelStoreDBFolderRecord *record = user_data;

	/* make sure the SELECT uses the expected order and count of the cols */
	g_return_val_if_fail (ncol == 13, FALSE);

	record->folder_name = g_strdup (cols[0]);
	record->version = get_num (cols[1]);
	record->flags = get_num (cols[2]);
	record->nextuid = get_num (cols[3]);
	record->timestamp = get_num64 (cols[4]);
	record->saved_count = get_num (cols[5]);
	record->unread_count = get_num (cols[6]);
	record->deleted_count = get_num (cols[7]);
	record->junk_count = get_num (cols[8]);
	record->visible_count = get_num (cols[9]);
	record->jnd_count = get_num (cols[10]);
	record->bdata = g_strdup (cols[11]);
	record->folder_id = get_num (cols[12]);

	return TRUE;
}

/**
 * camel_store_db_read_folder:
 * @self: a #CamelStoreDB
 * @folder_name: name of the folder to read the record for
 * @out_record: (out caller-allocates): a #CamelStoreDBFolderRecord to read the values to
 * @error: a return location for a #GError, or %NULL
 *
 * Reads information about a folder named @folder_name, previously stored by
 * the camel_store_db_write_folder(). The data in the @out_record should be
 * cleared by the camel_store_db_folder_record_clear(), when no longer needed.
 * The function returns success also when the folder information was not saved
 * yet. It can be checked by the folder_id value, which is never zero for those
 * existing tables.
 *
 * Returns: whether succeeded with the read, but check the non-zero-ness of
 *    the folder_id member of the @out_record to recognize whether it was
 *    found
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_read_folder (CamelStoreDB *self,
			    const gchar *folder_name,
			    CamelStoreDBFolderRecord *out_record,
			    GError **error)
{
	gchar *stmt;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (out_record != NULL, FALSE);

	memset (out_record, 0, sizeof (*out_record));

	stmt = sqlite3_mprintf ("SELECT folder_name, version, flags, nextuid, time, saved_count, unread_count, "
		"deleted_count, junk_count, visible_count, jnd_count, bdata, folder_id FROM folders "
		"WHERE folder_name=%Q", folder_name);
	success = camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_folder_record_cb, out_record, error);
	sqlite3_free (stmt);

	return success;
}

/**
 * camel_store_db_get_folder_id:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 *
 * Gets ID of a folder named @folder_name.
 *
 * Returns: ID of a folder named @folder_name, 0 when not found
 *
 * Since: 3.58
 **/
guint32
camel_store_db_get_folder_id (CamelStoreDB *self,
			      const gchar *folder_name)
{
	guint32 folder_id;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), 0);
	g_return_val_if_fail (folder_name != NULL, 0);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	return folder_id;
}

/**
 * camel_store_db_rename_folder:
 * @self: a #CamelStoreDB
 * @old_folder_name: an existing folder name
 * @new_folder_name: a folder name to rename to
 * @error: a return location for a #GError, or %NULL
 *
 * Renames folder @old_folder_name to @new_folder_name. Returns
 * failure and sets %G_IO_ERROR_NOT_FOUND error when the @old_folder_name
 * does not exist, and %G_IO_ERROR_EXISTS, when the @new_folder_name
 * already exists.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_rename_folder (CamelStoreDB *self,
			      const gchar *old_folder_name,
			      const gchar *new_folder_name,
			      GError **error)
{
	CamelDB *cdb;
	guint32 folder_id;
	gboolean success = FALSE, exists;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (old_folder_name != NULL, FALSE);
	g_return_val_if_fail (new_folder_name != NULL, FALSE);

	cdb = CAMEL_DB (self);

	camel_db_writer_lock (cdb);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, old_folder_name));
	exists = folder_id && g_hash_table_contains (self->priv->folder_ids, new_folder_name);
	UNLOCK (self);

	if (!folder_id) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Folder '%s' does not exist", old_folder_name);
	} else if (exists) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Folder '%s' already exists", new_folder_name);
	} else {
		gchar *stmt;

		stmt = sqlite3_mprintf ("UPDATE folders SET folder_name=%Q WHERE folder_name=%Q", new_folder_name, old_folder_name);
		success = camel_db_exec_statement (cdb, stmt, error);
		sqlite3_free (stmt);

		if (success) {
			LOCK (self);
			g_hash_table_remove (self->priv->folder_ids, old_folder_name);
			g_hash_table_insert (self->priv->folder_ids, g_strdup (new_folder_name), GUINT_TO_POINTER (folder_id));
			UNLOCK (self);
		}
	}

	camel_db_writer_unlock (cdb);

	return success;
}

/**
 * camel_store_db_delete_folder:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @error: a return location for a #GError, or %NULL
 *
 * Deletes all information about the @folder_name. It does nothing
 * when the folder does not exist.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_delete_folder (CamelStoreDB *self,
			      const gchar *folder_name,
			      GError **error)
{
	CamelDB *cdb;
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	cdb = CAMEL_DB (self);

	camel_db_writer_lock (cdb);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		success = camel_db_begin_transaction (cdb, error);
		if (success) {
			gchar *stmt;

			stmt = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name=%Q", folder_name);
			success = camel_db_exec_statement (cdb, stmt, error);
			sqlite3_free (stmt);

			if (success) {
				stmt = g_strdup_printf ("DROP TABLE IF EXISTS messages_%u", folder_id);
				success = camel_db_exec_statement (cdb, stmt, error);
				g_free (stmt);
			}

			if (success)
				success = camel_db_end_transaction (cdb, error);
			else
				camel_db_abort_transaction (cdb, NULL);

			if (success) {
				LOCK (self);
				g_hash_table_remove (self->priv->folder_ids, folder_name);
				UNLOCK (self);
			}
		}
	}

	camel_db_writer_unlock (cdb);

	return success;
}

/**
 * camel_store_db_clear_folder:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @error: a return location for a #GError, or %NULL
 *
 * Clears content of the @folder_name. It does nothing
 * when the folder does not exist.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_clear_folder (CamelStoreDB *self,
			     const gchar *folder_name,
			     GError **error)
{
	CamelDB *cdb;
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	cdb = CAMEL_DB (self);

	camel_db_writer_lock (cdb);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		success = camel_db_begin_transaction (cdb, error);
		if (success) {
			gchar *stmt;

			stmt = g_strdup_printf ("DELETE FROM messages_%u", folder_id);
			success = camel_db_exec_statement (cdb, stmt, error);
			g_free (stmt);

			if (success) {
				CamelStoreDBFolderRecord record = { 0, };

				success = camel_store_db_read_folder (self, folder_name, &record, error);
				if (success) {
					record.saved_count = 0;
					record.unread_count = 0;
					record.deleted_count = 0;
					record.junk_count = 0;
					record.visible_count = 0;
					record.jnd_count = 0;

					success = camel_store_db_write_folder (self, folder_name, &record, error);

					camel_store_db_folder_record_clear (&record);
				}
			}

			if (success)
				success = camel_db_end_transaction (cdb, error);
			else
				camel_db_abort_transaction (cdb, NULL);
		}
	}

	camel_db_writer_unlock (cdb);

	return success;
}

/**
 * camel_store_db_write_message:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @record: a #CamelStoreDBMessageRecord
 * @error: a return location for a #GError, or %NULL
 *
 * Writes information about a single message into the @self. The message
 * in the @record is identified by the @folder_name argument and the "uid" member
 * of the structure. The "folder_id" member of the @record is ignored.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_write_message (CamelStoreDB *self,
			      const gchar *folder_name,
			      const CamelStoreDBMessageRecord *record,
			      GError **error)
{
	CamelDB *cdb;
	gchar *stmt;
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (record != NULL, FALSE);

	cdb = CAMEL_DB (self);

	camel_db_writer_lock (cdb);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (!folder_id) {
		camel_db_writer_unlock (cdb);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot write message information: Folder “%s” not found"), folder_name);
		return FALSE;
	}

	stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO messages_%u "
		"(uid, flags, msg_type, dirty, size, dsent, dreceived, subject, mail_from, "
		"mail_to, mail_cc, mlist, part, labels, usertags, "
		"cinfo, bdata, userheaders, preview) "
		"VALUES "
		"(%Q, %d, %d, %d, %d, %lld, %lld, %Q, %Q,"
		"%Q, %Q, %Q, %Q, %Q, %Q, "
		"%Q, %Q, %Q, %Q)",
		folder_id,
		record->uid,
		record->flags,
		record->msg_type,
		record->dirty,
		record->size,
		record->dsent,
		record->dreceived,
		record->subject,
		record->from,
		record->to,
		record->cc,
		record->mlist,
		record->part,
		record->labels,
		record->usertags,
		record->cinfo,
		record->bdata,
		record->userheaders,
		record->preview);
	success = camel_db_exec_statement (cdb, stmt, error);
	sqlite3_free (stmt);

	camel_db_writer_unlock (cdb);

	return success;
}

static gboolean
camel_store_db_read_single_message_record_cb (CamelStoreDB *storedb,
					      const CamelStoreDBMessageRecord *record,
					      gpointer user_data)
{
	CamelStoreDBMessageRecord *out_record = user_data;

	g_return_val_if_fail (out_record->folder_id == 0, FALSE);

	out_record->folder_id = record->folder_id;
	out_record->uid = camel_pstring_strdup (record->uid);
	out_record->flags = record->flags;
	out_record->msg_type = record->msg_type;
	out_record->dirty = record->dirty;
	out_record->size = record->size;
	out_record->dsent = record->dsent;
	out_record->dreceived = record->dreceived;
	out_record->subject = camel_pstring_strdup (record->subject);
	out_record->from = camel_pstring_strdup (record->from);
	out_record->to = camel_pstring_strdup (record->to);
	out_record->cc = camel_pstring_strdup (record->cc);
	out_record->mlist = camel_pstring_strdup (record->mlist);
	out_record->part = g_strdup (record->part);
	out_record->labels = g_strdup (record->labels);
	out_record->usertags = g_strdup (record->usertags);
	out_record->cinfo = g_strdup (record->cinfo);
	out_record->bdata = g_strdup (record->bdata);
	out_record->userheaders = g_strdup (record->userheaders);
	out_record->preview = g_strdup (record->preview);

	return TRUE;
}

typedef struct _ReadMessagesData {
	CamelStoreDB *self;
	CamelStoreDBReadMessagesFunc func;
	gpointer user_data;
	guint32 folder_id;
} ReadMessagesData;

static gboolean
camel_store_db_read_messages_cb (gpointer user_data,
				 gint ncol,
				 gchar **colvalues,
				 gchar **colnames)
{
	ReadMessagesData *rmd = user_data;
	CamelStoreDBMessageRecord record = { 0, };

	g_return_val_if_fail (rmd != NULL, FALSE);
	g_return_val_if_fail (ncol == 19, FALSE);

	record.uid = colvalues[0];
	record.flags = get_num (colvalues[1]);
	record.msg_type = get_num (colvalues[2]);
	record.dirty = get_num (colvalues[3]);
	record.size = get_num (colvalues[4]);
	record.dsent = get_num64 (colvalues[5]);
	record.dreceived = get_num64 (colvalues[6]);
	record.subject = colvalues[7];
	record.from = colvalues[8];
	record.to = colvalues[9];
	record.cc = colvalues[10];
	record.mlist = colvalues[11];
	record.part = colvalues[12];
	record.labels = colvalues[13];
	record.usertags = colvalues[14];
	record.cinfo = colvalues[15];
	record.bdata = colvalues[16];
	record.userheaders = colvalues[17];
	record.preview = colvalues[18];
	record.folder_id = rmd->folder_id;

	return rmd->func (rmd->self, &record, rmd->user_data);
}

static gboolean
camel_store_db_read_messages_internal (CamelStoreDB *self,
				       const gchar *folder_name,
				       const gchar *where_clause,
				       CamelStoreDBReadMessagesFunc func,
				       gpointer user_data,
				       GError **error)
{
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		ReadMessagesData rmd = { 0, };
		GString *stmt;

		stmt = g_string_new ("SELECT uid, flags, msg_type, "
			"dirty, size, dsent, dreceived, subject, mail_from, "
			"mail_to, mail_cc, mlist, part, labels, usertags, cinfo, "
			"bdata, userheaders, preview FROM ");

		g_string_append_printf (stmt, "messages_%u", folder_id);

		if (where_clause) {
			g_string_append (stmt, " WHERE ");
			g_string_append (stmt, where_clause);
		}

		rmd.self = self;
		rmd.func = func;
		rmd.user_data = user_data;
		rmd.folder_id = folder_id;

		success = camel_db_exec_select (CAMEL_DB (self), stmt->str, camel_store_db_read_messages_cb, &rmd, error);

		g_string_free (stmt, TRUE);
	} else {
		success = FALSE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot read message information: Folder “%s” not found"), folder_name);
	}

	return success;
}

/**
 * camel_store_db_read_message:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @uid: message UID
 * @out_record: (out caller-allocates): a #CamelStoreDBMessageRecord to read the information to
 * @error: a return location for a #GError, or %NULL
 *
 * Reads information about a single message stored in the @self. The message
 * in the @out_record is identified by the folder ID and the UID members
 * of the structure.
 *
 * Call camel_store_db_message_record_clear() on the @out_record
 * structure to clear dynamically allocated memory in it.
 *
 * See also camel_store_db_read_messages().
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_read_message (CamelStoreDB *self,
			     const gchar *folder_name,
			     const gchar *uid,
			     CamelStoreDBMessageRecord *out_record,
			     GError **error)
{
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (uid != NULL && *uid != '\0', FALSE);
	g_return_val_if_fail (out_record != NULL, FALSE);

	memset (out_record, 0, sizeof (*out_record));

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		gchar *where_clause;

		where_clause = sqlite3_mprintf ("uid=%Q", uid);
		success = camel_store_db_read_messages_internal (self, folder_name, where_clause, camel_store_db_read_single_message_record_cb, out_record, error);
		sqlite3_free (where_clause);

		if (success && !out_record->folder_id) {
			success = FALSE;
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				_("Cannot read message information in folder “%s”: Message “%s” not found"), folder_name, uid);
		}
	} else {
		success = FALSE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot read message information: Folder “%s” not found"), folder_name);
	}

	return success;
}

/**
 * camel_store_db_read_messages:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name to read the data from
 * @func: (scope call) (closure user_data): a #CamelStoreDBReadMessagesFunc to be called
 * @user_data: user data for the @func
 * @error: a return location for a #GError, or %NULL
 *
 * Reads information about all messages for the folder @folder_name
 * and calls the @func with its @user_data for each such message information.
 *
 * See also camel_store_db_read_message().
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_read_messages (CamelStoreDB *self,
			      const gchar *folder_name,
			      CamelStoreDBReadMessagesFunc func,
			      gpointer user_data,
			      GError **error)
{
	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	return camel_store_db_read_messages_internal (self, folder_name, NULL, func, user_data, error);
}

/**
 * camel_store_db_delete_message:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @uid: message UID
 * @error: a return location for a #GError, or %NULL
 *
 * Deletes single message with UID @uid from folder @folder_name.
 * It's okay when such @uid does not exist, but the folder is
 * required to exist.
 *
 * See also camel_store_db_delete_messages().
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_delete_message (CamelStoreDB *self,
			       const gchar *folder_name,
			       const gchar *uid,
			       GError **error)
{
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (uid != NULL && *uid != '\0', FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		gchar *stmt;

		stmt = sqlite3_mprintf ("DELETE FROM messages_%u WHERE uid=%Q", folder_id, uid);
		success = camel_db_exec_statement (CAMEL_DB (self), stmt, error);
		sqlite3_free (stmt);
	} else {
		success = FALSE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot delete message: Folder “%s” not found"), folder_name);
	}

	return success;
}

/**
 * camel_store_db_delete_messages:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @uids: (element-type utf8): a #GPtrArray of strings with message UID-s
 * @error: a return location for a #GError, or %NULL
 *
 * Deletes multiple messages with UID @uids from folder @folder_name.
 * It's okay when such @uids do not exist, but the folder is
 * required to exist.
 *
 * See also camel_store_db_delete_message().
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_delete_messages (CamelStoreDB *self,
				const gchar *folder_name,
				/* const */ GPtrArray *uids,
				GError **error)
{
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		GString *stmt;
		gchar *tmp;
		guint ii;

		stmt = g_string_sized_new (256);

		for (ii = 0; ii < uids->len; ii++) {
			const gchar *uid = g_ptr_array_index (uids, ii);

			if (uid && *uid) {
				if (stmt->len)
					g_string_append_c (stmt, ',');
				tmp = sqlite3_mprintf ("%Q", uid);
				g_string_append (stmt, tmp);
				sqlite3_free (tmp);
			}
		}

		if (stmt->len) {
			tmp = sqlite3_mprintf ("DELETE FROM messages_%u WHERE uid IN (", folder_id);
			g_string_prepend (stmt, tmp);
			sqlite3_free (tmp);
			g_string_append_c (stmt, ')');

			success = camel_db_exec_statement (CAMEL_DB (self), stmt->str, error);
		}

		g_string_free (stmt, TRUE);
	} else {
		success = FALSE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot delete messages: Folder “%s” not found"), folder_name);
	}

	return success;
}

/**
 * camel_store_db_count_messages:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @kind: a #CamelStoreDBCountKind
 * @out_count: (out): a return location to store the count to
 * @error: a return location for a #GError, or %NULL
 *
 * Counts @kind messages in folder @folder_name.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_store_db_count_messages (CamelStoreDB *self,
			       const gchar *folder_name,
			       CamelStoreDBCountKind kind,
			       guint32 *out_count,
			       GError **error)
{
	guint32 folder_id;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (out_count != NULL, FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		gchar *where_clause = NULL;
		gchar *stmt;

		switch (kind) {
		case CAMEL_STORE_DB_COUNT_KIND_TOTAL:
			break;
		case CAMEL_STORE_DB_COUNT_KIND_UNREAD:
			where_clause = g_strdup_printf ("(flags & 0x%x)=0", CAMEL_MESSAGE_SEEN);
			break;
		case CAMEL_STORE_DB_COUNT_KIND_JUNK:
			where_clause = g_strdup_printf ("(flags & 0x%x)!=0", CAMEL_MESSAGE_JUNK);
			break;
		case CAMEL_STORE_DB_COUNT_KIND_DELETED:
			where_clause = g_strdup_printf ("(flags & 0x%x)!=0", CAMEL_MESSAGE_DELETED);
			break;
		case CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED:
			where_clause = g_strdup_printf ("(flags & 0x%x)=0", CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_DELETED);
			break;
		case CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED_UNREAD:
			where_clause = g_strdup_printf ("(flags & 0x%x)=0", CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_DELETED);
			break;
		case CAMEL_STORE_DB_COUNT_KIND_JUNK_NOT_DELETED:
			where_clause = g_strdup_printf ("(flags & 0x%x)=0x%x", CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_JUNK);
			break;
		default:
			g_warn_if_reached ();
			break;
		}

		stmt = sqlite3_mprintf ("SELECT COUNT(*) FROM messages_%u%s%s", folder_id,
			where_clause ? " WHERE " : "",
			where_clause ? where_clause : "");
		success = camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_uint_cb, out_count, error);
		sqlite3_free (stmt);
		g_free (where_clause);
	} else {
		success = FALSE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot count messages: Folder “%s” not found"), folder_name);
	}

	return success;
}

static gboolean
camel_store_db_read_uids_flags_cb (gpointer user_data,
				   gint ncol,
				   gchar **colvalues,
				   gchar **colnames)
{
	GHashTable *uids_flags = user_data;
	const gchar *uid;
	guint32 flags;

	g_return_val_if_fail (uids_flags != NULL, FALSE);
	g_return_val_if_fail (ncol == 2, FALSE);

	uid = colvalues[0];
	flags = get_num (colvalues[1]);

	if (uid && *uid)
		g_hash_table_insert (uids_flags, (gpointer) camel_pstring_strdup (uid), GUINT_TO_POINTER (flags));

	return TRUE;
}


/**
 * camel_store_db_dup_uids_with_flags:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @error: a return location for a #GError, or %NULL
 *
 * Reads message UID-s with their flags in folder @folder_name and
 * returns it as a hash table with UID-s as a key and the flags
 * as a value.
 *
 * Free the returned #GHashTable with g_hash_table_unref(), when
 * no longer needed.
 *
 * Returns: (transfer container) (element-type utf8 guint32): a #GHashTable of
 *    the message UID-s and their flags, or %NULL on error
 *
 * Since: 3.58
 **/
GHashTable * /* gchar *uid ~> guint32 flags */
camel_store_db_dup_uids_with_flags (CamelStoreDB *self,
				    const gchar *folder_name,
				    GError **error)
{
	guint32 folder_id;
	GHashTable *uids_flags = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		gchar *stmt;

		uids_flags = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);

		stmt = sqlite3_mprintf ("SELECT uid,flags FROM messages_%u", folder_id);
		if (!camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_uids_flags_cb, uids_flags, error)) {
			g_clear_pointer (&uids_flags, g_hash_table_unref);
		}
		sqlite3_free (stmt);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot get UIDs and flags: Folder “%s” not found"), folder_name);
	}

	return uids_flags;
}

static gboolean
camel_store_db_read_uids_cb (gpointer user_data,
			     gint ncol,
			     gchar **colvalues,
			     gchar **colnames)
{
	GPtrArray *uids = user_data;
	const gchar *uid;

	g_return_val_if_fail (uids != NULL, FALSE);
	g_return_val_if_fail (ncol == 1, FALSE);

	uid = colvalues[0];
	if (uid && *uid)
		g_ptr_array_add (uids, (gpointer) camel_pstring_strdup (uid));

	return TRUE;
}

/**
 * camel_store_db_dup_junk_uids:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @error: a return location for a #GError, or %NULL
 *
 * Gets junk message UID-s in folder @folder_name. Free the returned array
 * with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: (transfer container) (element-type utf8): a #GPtrArray of message UID-s
 *    in folder @folder_name, which are marked as junk, or %NULL on error
 *
 * Since: 3.58
 **/
GPtrArray *
camel_store_db_dup_junk_uids (CamelStoreDB *self,
			      const gchar *folder_name,
			      GError **error)
{
	guint32 folder_id;
	GPtrArray *uids = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		gchar *stmt;

		uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);

		stmt = sqlite3_mprintf ("SELECT uid FROM messages_%u WHERE (flags & 0x%x)!=0", folder_id, CAMEL_MESSAGE_JUNK);

		if (!camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_uids_cb, uids, error)) {
			g_clear_pointer (&uids, g_ptr_array_unref);
		}

		sqlite3_free (stmt);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot get junk UIDs: Folder “%s” not found"), folder_name);
	}

	return uids;
}

/**
 * camel_store_db_dup_deleted_uids:
 * @self: a #CamelStoreDB
 * @folder_name: a folder name
 * @error: a return location for a #GError, or %NULL
 *
 * Gets deleted message UID-s in folder @folder_name. Free the returned array
 * with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: (transfer container) (element-type utf8): a #GPtrArray of message UID-s
 *    in folder @folder_name, which are marked as deleted, or %NULL on error
 *
 * Since: 3.58
 **/
GPtrArray *
camel_store_db_dup_deleted_uids (CamelStoreDB *self,
				 const gchar *folder_name,
				 GError **error)
{
	guint32 folder_id;
	GPtrArray *uids = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE_DB (self), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	LOCK (self);
	folder_id = GPOINTER_TO_UINT (g_hash_table_lookup (self->priv->folder_ids, folder_name));
	UNLOCK (self);

	if (folder_id) {
		gchar *stmt;

		uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);

		stmt = sqlite3_mprintf ("SELECT uid FROM messages_%u WHERE (flags & 0x%x) != 0", folder_id, CAMEL_MESSAGE_DELETED);

		if (!camel_db_exec_select (CAMEL_DB (self), stmt, camel_store_db_read_uids_cb, uids, error)) {
			g_clear_pointer (&uids, g_ptr_array_unref);
		}

		sqlite3_free (stmt);
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot get deleted UIDs: Folder “%s” not found"), folder_name);
	}

	return uids;
}

/**
 * camel_store_db_util_get_column_for_header_name:
 * @header_name: name of a header to get a column for
 *
 * Gets a corresponding messages table column name for the @header_name.
 *
 * Returns: (nullable): corresponding messages table column name for the @header_name,
 *    or %NULL, when the @header_name does not have a corresponding column name
 *    in the messages table
 *
 * Since: 3.58
 **/
const gchar *
camel_store_db_util_get_column_for_header_name (const gchar *header_name)
{
	g_return_val_if_fail (header_name != NULL, NULL);

	/* when changing the columns list, update also _camel_store_search_from_loaded_info_or_db() */
	if (!g_ascii_strcasecmp (header_name, "Subject"))
		return "subject";
	else if (!g_ascii_strcasecmp (header_name, "from"))
		return "mail_from";
	else if (!g_ascii_strcasecmp (header_name, "Cc"))
		return "mail_cc";
	else if (!g_ascii_strcasecmp (header_name, "To"))
		return "mail_to";
	else if (!g_ascii_strcasecmp (header_name, "user-tag"))
		return "usertags";
	else if (!g_ascii_strcasecmp (header_name, "user-flag"))
		return "labels";
	else if (!g_ascii_strcasecmp (header_name, "x-camel-mlist"))
		return "mlist";

	return NULL;
}

/*
 * _camel_store_db_register_search:
 * @self: a #CamelStoreDB
 * @search: (transfer none): a #CamelStoreSearch
 *
 * Registers the @search to be called for custom SQL functions where
 * it's referenced as a context. The @search is not referenced, it's
 * responsible to call _camel_store_db_unregister_search() before it
 * is destroyed.
 *
 * Since: 3.58
 **/
void
_camel_store_db_register_search (CamelStoreDB *self,
				 CamelStoreSearch *search)
{
	gchar *ident;

	g_return_if_fail (CAMEL_IS_STORE_DB (self));
	g_return_if_fail (CAMEL_IS_STORE_SEARCH (search));

	ident = g_strdup_printf ("%p", search);

	LOCK (self);
	g_hash_table_insert (self->priv->searches, ident, search);
	UNLOCK (self);
}

/*
 * _camel_store_db_unregister_search:
 * @self: a #CamelStoreDB
 * @search: (transfer none): a #CamelStoreSearch
 *
 * Unregisters the previously registered @search by a call
 * to _camel_store_db_register_search().
 *
 * Since: 3.58
 **/
void
_camel_store_db_unregister_search (CamelStoreDB *self,
				   CamelStoreSearch *search)
{
	gchar *ident;

	g_return_if_fail (CAMEL_IS_STORE_DB (self));

	ident = g_strdup_printf ("%p", search);

	LOCK (self);
	g_warn_if_fail (g_hash_table_remove (self->priv->searches, ident));
	UNLOCK (self);

	g_free (ident);
}
