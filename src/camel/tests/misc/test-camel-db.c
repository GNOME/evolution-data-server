/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include "camel/camel.h"

#define LATEST_VERSION 999

static gchar *
test_create_tmp_file (void)
{
	GString *path;
	gint fd;

	path = g_string_new (g_get_tmp_dir ());
	g_string_append_c (path, G_DIR_SEPARATOR);
	g_string_append (path, "camel-test-XXXXXX.db");

	fd = g_mkstemp (path->str);
	g_assert_cmpint (fd, !=, -1);

	close (fd);
	g_assert_cmpint (g_unlink (path->str), ==, 0);

	return g_string_free (path, FALSE);
}

static gboolean
test_read_integer_cb (gpointer user_data,
		      gint ncol,
		      gchar **cols,
		      gchar **name)
{
	gint *version = user_data;

	g_assert_nonnull (cols[0]);
	*version = strtoul (cols[0], NULL, 10);

	return TRUE;
}

static gint
test_count_tables (CamelDB *cdb)
{
	gint count = -1;
	gboolean success;
	GError *error = NULL;

	success = camel_db_exec_select (cdb, "SELECT COUNT(tbl_name) FROM sqlite_master WHERE type='table'", test_read_integer_cb, &count, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (count, !=, -1);

	return count;
}

#define test_count_table_rows(_a,_b) test_count_table_rows_full (_a, _b, NULL)

static gint
test_count_table_rows_full (CamelDB *cdb,
			    const gchar *table_name,
			    const gchar *where_clause)
{
	gchar *stmt;
	gint count = 0;
	gboolean success;
	GError *error = NULL;

	if (!camel_db_has_table (cdb, table_name))
		return 0;

	stmt = sqlite3_mprintf ("SELECT COUNT(*) FROM %Q%s%s", table_name,
		where_clause ? " WHERE " : "", where_clause ? where_clause : "");

	success = camel_db_exec_select (cdb, stmt, test_read_integer_cb, &count, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	sqlite3_free (stmt);

	return count;
}

typedef struct _ReadCompareData {
	guint n_read;
	const gchar *expected_value;
} ReadCompareData;

static gboolean
test_read_compare_column0_cb (gpointer user_data,
			      gint ncol,
			      gchar **cols,
			      gchar **name)
{
	ReadCompareData *rcd = user_data;

	rcd->n_read++;
	g_assert_cmpstr (rcd->expected_value, ==, cols[0]);

	return TRUE;
}

static void
test_has_table_with_column_value (CamelDB *cdb,
				  const gchar *table_name,
				  const gchar *field_name,
				  const gchar *value,
				  const gchar *where_clause)
{
	gchar *stmt;
	gboolean success;
	ReadCompareData rcd = { 0, NULL };
	GError *error = NULL;

	stmt = sqlite3_mprintf ("SELECT %w FROM %Q WHERE %s", field_name, table_name, where_clause);

	rcd.expected_value = value;
	success = camel_db_exec_select (cdb, stmt, test_read_compare_column0_cb, &rcd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (rcd.n_read, ==, 1);

	sqlite3_free (stmt);
}

static void
test_create_old_folder_data (CamelDB *cdb,
			     const CamelStoreDBFolderRecord *fir,
			     const CamelStoreDBMessageRecord *mirs)
{
	GError *error = NULL;
	gchar *index_name;
	gchar *tab_name;
	gchar *stmt;
	gboolean success;

	g_assert_nonnull (fir);
	g_assert_cmpint (fir->version, <=, 3);

	success = camel_db_begin_transaction (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_db_exec_statement (cdb, "CREATE TABLE IF NOT EXISTS folders ( "
		"folder_name TEXT PRIMARY KEY, "
		"version REAL, "
		"flags INTEGER, "
		"nextuid INTEGER, "
		"time NUMERIC, "
		"saved_count INTEGER, "
		"unread_count INTEGER, "
		"deleted_count INTEGER, "
		"junk_count INTEGER, "
		"visible_count INTEGER, "
		"jnd_count INTEGER, "
		"bdata TEXT)", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	stmt = sqlite3_mprintf ("DELETE FROM folders WHERE folder_name = %Q", fir->folder_name);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);

	stmt = sqlite3_mprintf ("INSERT INTO folders VALUES (%Q, %d, %d, %d, %lld, %d, %d, %d, %d, %d, %d, %Q) ",
		fir->folder_name,
		fir->version,
		fir->flags,
		fir->nextuid,
		fir->timestamp,
		fir->saved_count,
		fir->unread_count,
		fir->deleted_count,
		fir->junk_count,
		fir->visible_count,
		fir->jnd_count,
		fir->bdata);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);

	if (fir->version == 0) {
		stmt = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q ( "
			"uid TEXT PRIMARY KEY , "
			"flags INTEGER , "
			"msg_type INTEGER , "
			"read INTEGER , "
			"deleted INTEGER , "
			"replied INTEGER , "
			"important INTEGER , "
			"junk INTEGER , "
			"attachment INTEGER , "
			"msg_security INTEGER , "
			"size INTEGER , "
			"dsent NUMERIC , "
			"dreceived NUMERIC , "
			"subject TEXT , "
			"mail_from TEXT , "
			"mail_to TEXT , "
			"mail_cc TEXT , "
			"mlist TEXT , "
			"followup_flag TEXT , "
			"followup_completed_on TEXT , "
			"followup_due_by TEXT , "
			"part TEXT , "
			"labels TEXT , "
			"usertags TEXT , "
			"cinfo TEXT , "
			"bdata TEXT)",
			fir->folder_name);
	} else if (fir->version <= 2) {
		stmt = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q ( "
			"uid TEXT PRIMARY KEY , "
			"flags INTEGER , "
			"msg_type INTEGER , "
			"read INTEGER , "
			"deleted INTEGER , "
			"replied INTEGER , "
			"important INTEGER , "
			"junk INTEGER , "
			"attachment INTEGER , "
			"dirty INTEGER , "
			"size INTEGER , "
			"dsent NUMERIC , "
			"dreceived NUMERIC , "
			"subject TEXT , "
			"mail_from TEXT , "
			"mail_to TEXT , "
			"mail_cc TEXT , "
			"mlist TEXT , "
			"followup_flag TEXT , "
			"followup_completed_on TEXT , "
			"followup_due_by TEXT , "
			"part TEXT , "
			"labels TEXT , "
			"usertags TEXT , "
			"cinfo TEXT , "
			"bdata TEXT, "
			"created TEXT, "
			"modified TEXT)",
			fir->folder_name);
	} else {
		stmt = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q ( "
			"uid TEXT PRIMARY KEY , "
			"flags INTEGER , "
			"msg_type INTEGER , "
			"read INTEGER , "
			"deleted INTEGER , "
			"replied INTEGER , "
			"important INTEGER , "
			"junk INTEGER , "
			"attachment INTEGER , "
			"dirty INTEGER , "
			"size INTEGER , "
			"dsent NUMERIC , "
			"dreceived NUMERIC , "
			"subject TEXT , "
			"mail_from TEXT , "
			"mail_to TEXT , "
			"mail_cc TEXT , "
			"mlist TEXT , "
			"followup_flag TEXT , "
			"followup_completed_on TEXT , "
			"followup_due_by TEXT , "
			"part TEXT , "
			"labels TEXT , "
			"usertags TEXT , "
			"cinfo TEXT , "
			"bdata TEXT, "
			"userheaders TEXT, "
			"preview TEXT, "
			"created TEXT, "
			"modified TEXT)",
			fir->folder_name);
	}
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);

	index_name = g_strdup_printf ("DELINDEX-%s", fir->folder_name);
	stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (deleted)", index_name, fir->folder_name);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);
	g_free (index_name);

	index_name = g_strdup_printf ("JUNKINDEX-%s", fir->folder_name);
	stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (junk)", index_name, fir->folder_name);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);
	g_free (index_name);

	index_name = g_strdup_printf ("READINDEX-%s", fir->folder_name);
	stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (read)", index_name, fir->folder_name);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);
	g_free (index_name);

	tab_name = g_strconcat (fir->folder_name, "_version", NULL);
	stmt = sqlite3_mprintf ("CREATE TABLE %Q ( version TEXT )", tab_name);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);

	stmt = sqlite3_mprintf ("INSERT INTO %Q VALUES (%d)", tab_name, fir->version);
	success = camel_db_exec_statement (cdb, stmt, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	sqlite3_free (stmt);

	g_free (tab_name);

	/* this version checks are not when the tables were added, but
	   to not have the extra disposable tables available always */
	if (fir->version >= 2) {
		tab_name = g_strconcat (fir->folder_name, "_preview", NULL);
		stmt = sqlite3_mprintf ("CREATE TABLE %Q (uid TEXT PRIMARY KEY, preview TEXT)", tab_name);
		success = camel_db_exec_statement (cdb, stmt, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		sqlite3_free (stmt);
		g_free (tab_name);

		tab_name = g_strconcat (fir->folder_name, "_bodystructure", NULL);
		stmt = sqlite3_mprintf ("CREATE TABLE %Q (uid TEXT PRIMARY KEY, bodystructure TEXT)", tab_name);
		success = camel_db_exec_statement (cdb, stmt, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		sqlite3_free (stmt);
		g_free (tab_name);
	}

	if (mirs) {
		guint ii;

		for (ii = 0; mirs[ii].uid; ii++) {
			const CamelStoreDBMessageRecord *mir = &(mirs[ii]);

			if (fir->version == 0) {
				stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO %Q VALUES ("
					"%Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "
					"%lld, %lld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, "
					"%Q, %Q, %Q, %Q, %Q)",
					fir->folder_name,
					mir->uid,
					mir->flags,
					mir->msg_type,
					(mir->flags & CAMEL_MESSAGE_SEEN) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_DELETED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_ANSWERED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_FLAGGED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_JUNK) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_ATTACHMENTS) != 0 ? 1 : 0,
					mir->dirty,
					mir->size,
					mir->dsent,
					mir->dreceived,
					mir->subject,
					mir->from,
					mir->to,
					mir->cc,
					mir->mlist,
					"",
					"",
					"",
					mir->part,
					mir->labels,
					mir->usertags,
					mir->cinfo,
					mir->bdata);
			} else if (fir->version <= 2) {
				stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO %Q VALUES ("
					"%Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "
					"%lld, %lld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, "
					"%Q, %Q, %Q, %Q, %Q, "
					"strftime(\"%%s\", 'now'), "
					"strftime(\"%%s\", 'now') )",
					fir->folder_name,
					mir->uid,
					mir->flags,
					mir->msg_type,
					(mir->flags & CAMEL_MESSAGE_SEEN) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_DELETED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_ANSWERED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_FLAGGED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_JUNK) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_ATTACHMENTS) != 0 ? 1 : 0,
					mir->dirty,
					mir->size,
					mir->dsent,
					mir->dreceived,
					mir->subject,
					mir->from,
					mir->to,
					mir->cc,
					mir->mlist,
					"",
					"",
					"",
					mir->part,
					mir->labels,
					mir->usertags,
					mir->cinfo,
					mir->bdata);
			} else {
				stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO %Q VALUES ("
					"%Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "
					"%lld, %lld, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %Q, "
					"%Q, %Q, %Q, %Q, %Q, %Q, %Q, "
					"strftime(\"%%s\", 'now'), "
					"strftime(\"%%s\", 'now') )",
					fir->folder_name,
					mir->uid,
					mir->flags,
					mir->msg_type,
					(mir->flags & CAMEL_MESSAGE_SEEN) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_DELETED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_ANSWERED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_FLAGGED) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_JUNK) != 0 ? 1 : 0,
					(mir->flags & CAMEL_MESSAGE_ATTACHMENTS) != 0 ? 1 : 0,
					mir->dirty,
					mir->size,
					mir->dsent,
					mir->dreceived,
					mir->subject,
					mir->from,
					mir->to,
					mir->cc,
					mir->mlist,
					"",
					"",
					"",
					mir->part,
					mir->labels,
					mir->usertags,
					mir->cinfo,
					mir->bdata,
					mir->userheaders,
					mir->preview);
			}

			success = camel_db_exec_statement (cdb, stmt, &error);
			g_assert_no_error (error);
			g_assert_true (success);
			sqlite3_free (stmt);
		}
	}

	success = camel_db_end_transaction (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);
}

static void
test_add_field_value_test_str (GString *stmt,
			       const gchar *opr, /* NULL, "AND" or "OR" */
			       const gchar *field_name,
			       const gchar *value)
{
	if (opr)
		g_string_append_printf (stmt, " %s ", opr);

	if (value) {
		gchar *tmp;

		tmp = sqlite3_mprintf ("%s=%Q", field_name, value);
		g_string_append (stmt, tmp);
		sqlite3_free (tmp);
	} else {
		g_string_append_printf (stmt, "%s IS NULL", field_name);
	}
}

static void
test_add_field_value_test_uint32 (GString *stmt,
				  const gchar *opr, /* NULL, "AND" or "OR" */
				  const gchar *field_name,
				  guint32 value)
{
	if (opr)
		g_string_append_printf (stmt, " %s ", opr);
	g_string_append_printf (stmt, "%s=%u", field_name, value);
}

static void
test_add_field_value_test_int64 (GString *stmt,
				 const gchar *opr, /* NULL, "AND" or "OR" */
				 const gchar *field_name,
				 gint64 value)
{
	if (opr)
		g_string_append_printf (stmt, " %s ", opr);
	g_string_append_printf (stmt, "%s=%" G_GINT64_FORMAT, field_name, value);
}

static void
test_verify_old_folder_data (CamelDB *cdb,
			     const CamelStoreDBFolderRecord *fir,
			     const CamelStoreDBMessageRecord *mirs)
{
	GString *stmt;
	gint count = 0, n_mirs = 0, ii;
	gboolean success;
	GError *error = NULL;

	stmt = g_string_new ("SELECT COUNT(*) FROM folders WHERE ");
	test_add_field_value_test_str (stmt, NULL, "folder_name", fir->folder_name);
	test_add_field_value_test_uint32 (stmt, "AND", "version", fir->version);
	test_add_field_value_test_uint32 (stmt, "AND", "flags", fir->flags);
	test_add_field_value_test_uint32 (stmt, "AND", "nextuid", fir->nextuid);
	test_add_field_value_test_int64 (stmt, "AND", "time", fir->timestamp);
	test_add_field_value_test_uint32 (stmt, "AND", "saved_count", fir->saved_count);
	test_add_field_value_test_uint32 (stmt, "AND", "unread_count", fir->unread_count);
	test_add_field_value_test_uint32 (stmt, "AND", "deleted_count", fir->deleted_count);
	test_add_field_value_test_uint32 (stmt, "AND", "junk_count", fir->junk_count);
	test_add_field_value_test_uint32 (stmt, "AND", "visible_count", fir->visible_count);
	test_add_field_value_test_uint32 (stmt, "AND", "jnd_count", fir->jnd_count);
	test_add_field_value_test_str (stmt, "AND", "bdata", fir->bdata);
	success = camel_db_exec_select (cdb, stmt->str, test_read_integer_cb, &count, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (count, ==, 1);
	g_string_free (stmt, TRUE);

	if (mirs) {
		for (ii = 0; mirs[ii].uid; ii++) {
			n_mirs++;
		}
	}

	g_assert_cmpint (test_count_table_rows (cdb, fir->folder_name), ==, n_mirs);

	if (mirs) {
		for (ii = 0; mirs[ii].uid; ii++) {
			const CamelStoreDBMessageRecord *mir = &(mirs[ii]);
			gchar *tmp;

			tmp = sqlite3_mprintf ("SELECT COUNT(*) FROM %Q WHERE ", fir->folder_name);
			stmt = g_string_new (tmp);
			sqlite3_free (tmp);

			test_add_field_value_test_str (stmt, NULL, "uid", mir->uid);
			test_add_field_value_test_uint32 (stmt, "AND", "flags", mir->flags);
			test_add_field_value_test_uint32 (stmt, "AND", "msg_type", mir->msg_type);
			test_add_field_value_test_uint32 (stmt, "AND", fir->version == 0 ? "msg_security" : "dirty", mir->dirty);
			test_add_field_value_test_uint32 (stmt, "AND", "size", mir->size);
			test_add_field_value_test_int64 (stmt, "AND", "dsent", mir->dsent);
			test_add_field_value_test_int64 (stmt, "AND", "dreceived", mir->dreceived);
			test_add_field_value_test_str (stmt, "AND", "subject", mir->subject);
			test_add_field_value_test_str (stmt, "AND", "mail_from", mir->from);
			test_add_field_value_test_str (stmt, "AND", "mail_to", mir->to);
			test_add_field_value_test_str (stmt, "AND", "mail_cc", mir->cc);
			test_add_field_value_test_str (stmt, "AND", "mlist", mir->mlist);
			test_add_field_value_test_str (stmt, "AND", "part", mir->part);
			test_add_field_value_test_str (stmt, "AND", "labels", mir->labels);
			test_add_field_value_test_str (stmt, "AND", "usertags", mir->usertags);
			test_add_field_value_test_str (stmt, "AND", "cinfo", mir->cinfo);
			test_add_field_value_test_str (stmt, "AND", "bdata", mir->bdata);

			if (fir->version >= 3) {
				test_add_field_value_test_str (stmt, "AND", "userheaders", mir->userheaders);
				test_add_field_value_test_str (stmt, "AND", "preview", mir->preview);
			}

			success = camel_db_exec_select (cdb, stmt->str, test_read_integer_cb, &count, &error);
			g_assert_no_error (error);
			g_assert_true (success);
			g_assert_cmpint (count, ==, 1);
			g_string_free (stmt, TRUE);
		}
	}
}

static void
test_verify_folder_records_equal (const CamelStoreDBFolderRecord *loaded,
				  guint32 expected_folder_id,
				  const CamelStoreDBFolderRecord *expected)
{
	g_assert_nonnull (loaded);
	g_assert_cmpint (expected_folder_id, >, 0);
	g_assert_nonnull (expected);

	g_assert_cmpstr (loaded->folder_name, ==, expected->folder_name);
	g_assert_cmpint (loaded->folder_id, ==, expected_folder_id);
	g_assert_cmpint (loaded->version, ==, expected->version);
	g_assert_cmpint (loaded->flags, ==, expected->flags);
	g_assert_cmpint (loaded->nextuid, ==, expected->nextuid);
	g_assert_cmpint (loaded->timestamp, ==, expected->timestamp);
	g_assert_cmpint (loaded->saved_count, ==, expected->saved_count);
	g_assert_cmpint (loaded->unread_count, ==, expected->unread_count);
	g_assert_cmpint (loaded->deleted_count, ==, expected->deleted_count);
	g_assert_cmpint (loaded->junk_count, ==, expected->junk_count);
	g_assert_cmpint (loaded->visible_count, ==, expected->visible_count);
	g_assert_cmpint (loaded->jnd_count, ==, expected->jnd_count);
	g_assert_cmpstr (loaded->bdata, ==, expected->bdata);
}

static void
test_verify_message_records_equal (const CamelStoreDBMessageRecord *loaded,
				   guint32 expected_folder_id,
				   const CamelStoreDBMessageRecord *expected,
				   gint version)
{
	g_assert_nonnull (loaded);
	g_assert_nonnull (expected);

	if (expected_folder_id == 0)
		expected_folder_id = expected->folder_id;

	g_assert_cmpint (expected_folder_id, >, 0);

	g_assert_cmpint (loaded->folder_id, ==, expected_folder_id);
	g_assert_cmpstr (loaded->uid, ==, expected->uid);
	g_assert_cmpint (loaded->flags, ==, expected->flags);
	g_assert_cmpint (loaded->msg_type, ==, expected->msg_type);
	g_assert_cmpint (loaded->dirty, ==, expected->dirty);
	g_assert_cmpint (loaded->size, ==, expected->size);
	g_assert_cmpint (loaded->dsent, ==, expected->dsent);
	g_assert_cmpint (loaded->dreceived, ==, expected->dreceived);
	g_assert_cmpstr (loaded->subject, ==, expected->subject);
	g_assert_cmpstr (loaded->from, ==, expected->from);
	g_assert_cmpstr (loaded->to, ==, expected->to);
	g_assert_cmpstr (loaded->cc, ==, expected->cc);
	g_assert_cmpstr (loaded->mlist, ==, expected->mlist);
	g_assert_cmpstr (loaded->part, ==, expected->part);
	g_assert_cmpstr (loaded->labels, ==, expected->labels);
	g_assert_cmpstr (loaded->usertags, ==, expected->usertags);
	g_assert_cmpstr (loaded->cinfo, ==, expected->cinfo);
	g_assert_cmpstr (loaded->bdata, ==, expected->bdata);
	if (version < 3)
		g_assert_cmpstr (loaded->userheaders, ==, "");
	else
		g_assert_cmpstr (loaded->userheaders, ==, expected->userheaders);
	if (version < 3)
		g_assert_cmpstr (loaded->preview, ==, "");
	else
		g_assert_cmpstr (loaded->preview, ==, expected->preview);
}

typedef struct _ReadMirData {
	const CamelStoreDBFolderRecord *fir;
	GHashTable *known_mirs; /* gchar *key { "filder_id:uid" } ~> const CamelStoreDBMessageRecord * */
	guint32 n_called;
} ReadMirData;

static gboolean
test_read_mirs_cb (CamelStoreDB *storedb,
		   const CamelStoreDBMessageRecord *record,
		   gpointer user_data)
{
	ReadMirData *rmd = user_data;
	const CamelStoreDBMessageRecord *expected;
	gchar *key;

	rmd->n_called++;

	key = g_strdup_printf ("%u:%s", record->folder_id, record->uid);

	g_assert_cmpint (record->folder_id, ==, rmd->fir->folder_id);
	g_assert_true (g_hash_table_contains (rmd->known_mirs, key));

	expected = g_hash_table_lookup (rmd->known_mirs, key);
	test_verify_message_records_equal (record, rmd->fir->folder_id, expected, rmd->fir->version);

	g_free (key);

	return TRUE;
}

static void
test_verify_new_folder_data (CamelDB *cdb,
			     const CamelStoreDBFolderRecord *fir,
			     const CamelStoreDBMessageRecord *mirs)
{
	CamelStoreDBFolderRecord saved_fir = { 0, };
	GString *stmt;
	gint count = 0, n_mirs = 0, ii;
	gboolean success;
	GError *error = NULL;

	g_assert_cmpint (fir->folder_id, !=, 0);

	stmt = g_string_new ("SELECT COUNT(*) FROM folders WHERE ");
	test_add_field_value_test_str (stmt, NULL, "folder_name", fir->folder_name);
	test_add_field_value_test_uint32 (stmt, "AND", "folder_id", fir->folder_id);
	test_add_field_value_test_uint32 (stmt, "AND", "version", fir->version);
	test_add_field_value_test_uint32 (stmt, "AND", "flags", fir->flags);
	test_add_field_value_test_uint32 (stmt, "AND", "nextuid", fir->nextuid);
	test_add_field_value_test_int64 (stmt, "AND", "time", fir->timestamp);
	test_add_field_value_test_uint32 (stmt, "AND", "saved_count", fir->saved_count);
	test_add_field_value_test_uint32 (stmt, "AND", "unread_count", fir->unread_count);
	test_add_field_value_test_uint32 (stmt, "AND", "deleted_count", fir->deleted_count);
	test_add_field_value_test_uint32 (stmt, "AND", "junk_count", fir->junk_count);
	test_add_field_value_test_uint32 (stmt, "AND", "visible_count", fir->visible_count);
	test_add_field_value_test_uint32 (stmt, "AND", "jnd_count", fir->jnd_count);
	test_add_field_value_test_str (stmt, "AND", "bdata", fir->bdata);
	success = camel_db_exec_select (cdb, stmt->str, test_read_integer_cb, &count, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (count, ==, 1);
	g_string_free (stmt, TRUE);

	if (mirs) {
		for (ii = 0; mirs[ii].uid; ii++) {
			n_mirs++;
		}
	}

	count = 0;
	stmt = g_string_new ("SELECT COUNT(*) FROM ");
	g_string_append_printf (stmt, "messages_%u", fir->folder_id);
	success = camel_db_exec_select (cdb, stmt->str, test_read_integer_cb, &count, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (count, ==, n_mirs);
	g_string_free (stmt, TRUE);

	if (mirs) {
		for (ii = 0; mirs[ii].uid; ii++) {
			const CamelStoreDBMessageRecord *mir = &(mirs[ii]);

			stmt = g_string_new ("SELECT COUNT(*) FROM ");
			g_string_append_printf (stmt, "messages_%u WHERE ", fir->folder_id);

			test_add_field_value_test_str (stmt, NULL, "uid", mir->uid);
			test_add_field_value_test_uint32 (stmt, "AND", "flags", mir->flags);
			test_add_field_value_test_uint32 (stmt, "AND", "msg_type", mir->msg_type);
			test_add_field_value_test_uint32 (stmt, "AND", "dirty", mir->dirty);
			test_add_field_value_test_uint32 (stmt, "AND", "size", mir->size);
			test_add_field_value_test_int64 (stmt, "AND", "dsent", mir->dsent);
			test_add_field_value_test_int64 (stmt, "AND", "dreceived", mir->dreceived);
			test_add_field_value_test_str (stmt, "AND", "subject", mir->subject);
			test_add_field_value_test_str (stmt, "AND", "mail_from", mir->from);
			test_add_field_value_test_str (stmt, "AND", "mail_to", mir->to);
			test_add_field_value_test_str (stmt, "AND", "mail_cc", mir->cc);
			test_add_field_value_test_str (stmt, "AND", "mlist", mir->mlist);
			test_add_field_value_test_str (stmt, "AND", "part", mir->part);
			test_add_field_value_test_str (stmt, "AND", "labels", mir->labels);
			test_add_field_value_test_str (stmt, "AND", "usertags", mir->usertags);
			test_add_field_value_test_str (stmt, "AND", "cinfo", mir->cinfo);
			test_add_field_value_test_str (stmt, "AND", "bdata", mir->bdata);
			test_add_field_value_test_str (stmt, "AND", "userheaders", fir->version == 3 ? mir->userheaders : "");
			test_add_field_value_test_str (stmt, "AND", "preview", fir->version == 3 ? mir->preview : "");

			success = camel_db_exec_select (cdb, stmt->str, test_read_integer_cb, &count, &error);
			g_assert_no_error (error);
			g_assert_true (success);
			g_assert_cmpint (count, ==, 1);
			g_string_free (stmt, TRUE);
		}
	}

	/* also use the public API */
	success = camel_store_db_read_folder (CAMEL_STORE_DB (cdb), fir->folder_name, &saved_fir, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_folder_records_equal (&saved_fir, fir->folder_id, fir);
	camel_store_db_folder_record_clear (&saved_fir);

	if (mirs) {
		CamelStoreDB *sdb = CAMEL_STORE_DB (cdb);
		ReadMirData rmd = { 0, };

		rmd.fir = fir;
		rmd.known_mirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		rmd.n_called = 0;

		for (ii = 0; mirs[ii].uid; ii++) {
			const CamelStoreDBMessageRecord *mir = &(mirs[ii]);
			gchar *tmp;

			tmp = g_strdup_printf ("%u:%s", fir->folder_id, mir->uid);
			g_hash_table_insert (rmd.known_mirs, tmp, (gpointer) mir);
		}

		success = camel_store_db_read_messages (sdb, fir->folder_name, test_read_mirs_cb, &rmd, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_cmpint (rmd.n_called, ==, g_hash_table_size (rmd.known_mirs));

		for (ii = 0; mirs[ii].uid; ii++) {
			const CamelStoreDBMessageRecord *expected = &(mirs[ii]);
			CamelStoreDBMessageRecord loaded = { 0, };
			gchar *tmp;

			success = camel_store_db_read_message (sdb, fir->folder_name, expected->uid, &loaded, &error);
			g_assert_no_error (error);
			g_assert_true (success);

			test_verify_message_records_equal (&loaded, fir->folder_id, expected, fir->version);

			tmp = g_strdup_printf ("%u:%s", fir->folder_id, expected->uid);
			g_assert_true (g_hash_table_remove (rmd.known_mirs, tmp));
			g_free (tmp);

			camel_store_db_message_record_clear (&loaded);
		}

		/* all had been read */
		g_assert_cmpint (g_hash_table_size (rmd.known_mirs), ==, 0);
		g_hash_table_destroy (rmd.known_mirs);
	} else {
		CamelStoreDB *sdb = CAMEL_STORE_DB (cdb);
		ReadMirData rmd = { 0, };

		rmd.fir = fir;
		rmd.known_mirs = NULL;
		rmd.n_called = 0;

		success = camel_store_db_read_messages (sdb, fir->folder_name, test_read_mirs_cb, &rmd, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_cmpint (rmd.n_called, ==, 0);
	}
}

static void
test_camel_store_db_folder_ops (void)
{
	CamelStoreDBFolderRecord fir1_expected = {
		.folder_id = 0,
		.folder_name = (gchar *) "Inbox/folder1",
		.version = 3,
		.flags = 11,
		.nextuid = 12,
		.timestamp = 13,
		.saved_count = 14,
		.unread_count = 15,
		.deleted_count = 16,
		.junk_count = 17,
		.visible_count = 18,
		.jnd_count = 19,
		.bdata = (gchar *) "fir1bdata"
	};
	const CamelStoreDBFolderRecord fir2_expected = {
		.folder_id = 0,
		.folder_name = (gchar *) "Inbox/folder2",
		.version = 3,
		.flags = 21,
		.nextuid = 22,
		.timestamp = 23,
		.saved_count = 24,
		.unread_count = 25,
		.deleted_count = 26,
		.junk_count = 27,
		.visible_count = 28,
		.jnd_count = 29,
		.bdata = (gchar *) "fir2bdata"
	};
	const CamelStoreDBFolderRecord fir3_expected = {
		.folder_id = 0,
		.folder_name = (gchar *) "Inbox/folder3",
		.version = 3,
		.flags = 31,
		.nextuid = 32,
		.timestamp = 33,
		.saved_count = 34,
		.unread_count = 35,
		.deleted_count = 36,
		.junk_count = 37,
		.visible_count = 38,
		.jnd_count = 39,
		.bdata = (gchar *) "fir3bdata"
	};
	CamelStoreDBMessageRecord mir_expected = {
		.uid = "10",
		.folder_id = 0,
		.flags = 101,
		.msg_type = 102,
		.dirty = 103,
		.size = 110,
		.dsent = 111,
		.dreceived = 112,
		.subject = "subject 10",
		.from = "from 10",
		.to = "to 10",
		.cc = "cc 10",
		.mlist = "mlist 10",
		.part = (gchar *) "part 10",
		.labels = (gchar *) "labels 10",
		.usertags = (gchar *) "usertags 10",
		.cinfo = (gchar *) "cinfo 10",
		.bdata = (gchar *) "bdata 10",
		.userheaders = (gchar *) "userheaders 10",
		.preview = (gchar *) "preview 10"
	};

	CamelStoreDB *sdb;
	CamelStoreDBFolderRecord folder_record;
	CamelDB *cdb;
	GError *error = NULL;
	gchar *filename;
	gboolean success;

	filename = test_create_tmp_file ();

	sdb = camel_store_db_new (filename, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);

	cdb = CAMEL_DB (sdb);

	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 0);

	success = camel_store_db_write_folder (sdb, fir1_expected.folder_name, &fir1_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);

	success = camel_store_db_read_folder (sdb, "unknown", &folder_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (folder_record.folder_id, ==, 0);
	camel_store_db_folder_record_clear (&folder_record);

	success = camel_store_db_read_folder (sdb, fir1_expected.folder_name, &folder_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_folder_records_equal (&folder_record, 1, &fir1_expected);
	camel_store_db_folder_record_clear (&folder_record);

	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);

	success = camel_store_db_rename_folder (sdb, "unknown", "renamed-folder", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (success);
	g_clear_error (&error);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, "unknown"), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, "renamed-folder"), ==, 0);

	success = camel_store_db_rename_folder (sdb, fir1_expected.folder_name, "renamed-folder", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, "renamed-folder"), ==, 1);

	fir1_expected.folder_name = (gchar *) "renamed-folder";
	success = camel_store_db_read_folder (sdb, fir1_expected.folder_name, &folder_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_folder_records_equal (&folder_record, 1, &fir1_expected);
	camel_store_db_folder_record_clear (&folder_record);

	success = camel_store_db_write_folder (sdb, fir2_expected.folder_name, &fir2_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 2);

	success = camel_store_db_rename_folder (sdb, fir1_expected.folder_name, fir2_expected.folder_name, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
	g_assert_false (success);
	g_clear_error (&error);

	success = camel_store_db_write_folder (sdb, fir3_expected.folder_name, &fir3_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 2);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	success = camel_store_db_delete_folder (sdb, "unknown", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 2);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, "unknown"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	success = camel_store_db_delete_folder (sdb, fir2_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	success = camel_store_db_write_folder (sdb, fir2_expected.folder_name, &fir2_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 4);

	success = camel_store_db_delete_folder (sdb, fir2_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	/* re-uses max folder ID, because it's not used anymore */
	success = camel_store_db_write_folder (sdb, fir2_expected.folder_name, &fir2_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 4);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);

	success = camel_store_db_clear_folder (sdb, "unknown", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 4);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	success = camel_store_db_clear_folder (sdb, fir3_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 4);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	/* overwrite information in existing folder */
	fir1_expected.timestamp = 987;
	success = camel_store_db_write_folder (sdb, fir1_expected.folder_name, &fir1_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 4);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);

	success = camel_store_db_read_folder (sdb, fir1_expected.folder_name, &folder_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_folder_records_equal (&folder_record, 1, &fir1_expected);
	camel_store_db_folder_record_clear (&folder_record);

	mir_expected.folder_id = 0;
	mir_expected.uid = "11";
	success = camel_store_db_write_message (sdb, fir1_expected.folder_name, &mir_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	mir_expected.folder_id = 0;
	mir_expected.uid = "33";
	success = camel_store_db_write_message (sdb, fir3_expected.folder_name, &mir_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 1);

	mir_expected.folder_id = 0;
	mir_expected.uid = "44";
	success = camel_store_db_write_message (sdb, fir3_expected.folder_name, &mir_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 2);

	success = camel_store_db_write_folder (sdb, fir2_expected.folder_name, &fir2_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 4);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 2);

	success = camel_store_db_rename_folder (sdb, fir1_expected.folder_name, "again-renamed-folder", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, "again-renamed-folder"), ==, 1);

	fir1_expected.folder_name = (gchar *) "again-renamed-folder";
	success = camel_store_db_read_folder (sdb, fir1_expected.folder_name, &folder_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_folder_records_equal (&folder_record, 1, &fir1_expected);
	camel_store_db_folder_record_clear (&folder_record);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 2);

	success = camel_store_db_delete_folder (sdb, fir2_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 2);

	success = camel_store_db_clear_folder (sdb, fir1_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 3);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 2);

	mir_expected.folder_id = 0;
	mir_expected.uid = "55";
	success = camel_store_db_write_message (sdb, fir1_expected.folder_name, &mir_expected, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 2);

	success = camel_store_db_delete_folder (sdb, fir3_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 0);

	success = camel_store_db_delete_folder (sdb, fir1_expected.folder_name, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_3"), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2_expected.folder_name), ==, 0);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir3_expected.folder_name), ==, 0);

	g_object_unref (sdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_free (filename);
}

typedef struct _ReadMessagesData {
	guint n_called;
	const CamelStoreDBMessageRecord *expected[3];
} ReadMessagesData;

static gboolean
test_read_messages_cb (CamelStoreDB *storedb,
		       const CamelStoreDBMessageRecord *record,
		       gpointer user_data)
{
	ReadMessagesData *rmd = user_data;
	gboolean found = FALSE;
	guint ii;

	rmd->n_called++;

	for (ii = 0; ii < G_N_ELEMENTS (rmd->expected); ii++) {
		if (rmd->expected[ii] &&
		    g_strcmp0 (rmd->expected[ii]->uid, record->uid) == 0) {
			found = TRUE;
			test_verify_message_records_equal (record, 0, rmd->expected[ii], LATEST_VERSION);
			break;
		}
	}

	g_assert_true (found);

	return TRUE;
}

static void
test_camel_store_db_message_ops (void)
{
	const CamelStoreDBFolderRecord fir1 = {
		.folder_id = 1,
		.folder_name = (gchar *) "Inbox/folder1",
		.version = 3,
		.flags = 11,
		.nextuid = 12,
		.timestamp = 13,
		.saved_count = 14,
		.unread_count = 15,
		.deleted_count = 16,
		.junk_count = 17,
		.visible_count = 18,
		.jnd_count = 19,
		.bdata = (gchar *) "fir1bdata"
	};
	const CamelStoreDBFolderRecord fir2 = {
		.folder_id = 2,
		.folder_name = (gchar *) "Inbox/folder2",
		.version = 3,
		.flags = 21,
		.nextuid = 22,
		.timestamp = 23,
		.saved_count = 24,
		.unread_count = 25,
		.deleted_count = 26,
		.junk_count = 27,
		.visible_count = 28,
		.jnd_count = 29,
		.bdata = (gchar *) "fir2bdata"
	};
	CamelStoreDBMessageRecord mir1 = {
		.uid = "10",
		.folder_id = 0,
		.flags = CAMEL_MESSAGE_DRAFT,
		.msg_type = 102,
		.dirty = 103,
		.size = 110,
		.dsent = 111,
		.dreceived = 112,
		.subject = "subject 10",
		.from = "from 10",
		.to = "to 10",
		.cc = "cc 10",
		.mlist = "mlist 10",
		.part = (gchar *) "part 10",
		.labels = (gchar *) "labels 10",
		.usertags = (gchar *) "usertags 10",
		.cinfo = (gchar *) "cinfo 10",
		.bdata = (gchar *) "bdata 10",
		.userheaders = (gchar *) "userheaders 10",
		.preview = (gchar *) "preview 10"
	};
	CamelStoreDBMessageRecord mir2 = {
		.uid = "20",
		.folder_id = 0,
		.flags = CAMEL_MESSAGE_SEEN,
		.msg_type = 202,
		.dirty = 0,
		.size = 210,
		.dsent = 0,
		.dreceived = 0,
		.subject = "subject 20 - read",
		.from = "from 20",
		.to = "to 20",
		.cc = "cc 20",
		.mlist = NULL,
		.part = (gchar *) "part 20",
		.labels = (gchar *) NULL,
		.usertags = NULL,
		.cinfo = NULL,
		.bdata = NULL,
		.userheaders = NULL,
		.preview = NULL
	};
	CamelStoreDBMessageRecord mir3 = {
		.uid = "30",
		.folder_id = 0,
		.flags = 0,
		.msg_type = 302,
		.dirty = 0,
		.size = 310,
		.dsent = 0,
		.dreceived = 0,
		.subject = "subject 30 - unread",
		.from = "from 30",
		.to = "to 30",
		.cc = "cc 30",
		.mlist = NULL,
		.part = (gchar *) "part 30",
		.labels = (gchar *) NULL,
		.usertags = NULL,
		.cinfo = NULL,
		.bdata = NULL,
		.userheaders = NULL,
		.preview = NULL
	};

	CamelStoreDB *sdb;
	CamelStoreDBMessageRecord message_record;
	CamelDB *cdb;
	GHashTable *uid_flags;
	GPtrArray *array;
	ReadMessagesData rmd = { 0, };
	GError *error = NULL;
	gchar *filename;
	guint count;
	gboolean success;

	filename = test_create_tmp_file ();

	sdb = camel_store_db_new (filename, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);

	cdb = CAMEL_DB (sdb);

	success = camel_store_db_write_folder (sdb, fir1.folder_name, &fir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	success = camel_store_db_write_folder (sdb, fir2.folder_name, &fir2, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir1.folder_name), ==, 1);
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, fir2.folder_name), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);

	mir1.folder_id = 0;
	success = camel_store_db_write_message (sdb, fir1.folder_name, &mir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);

	success = camel_store_db_read_message (sdb, "unknown", mir1.uid, &message_record, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (success);
	g_clear_error (&error);

	success = camel_store_db_read_message (sdb, fir1.folder_name, mir1.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir1.folder_id, &mir1, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	success = camel_store_db_read_message (sdb, fir1.folder_name, mir2.uid, &message_record, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (success);
	g_clear_error (&error);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);

	mir1.flags = CAMEL_MESSAGE_SEEN;

	success = camel_store_db_write_message (sdb, fir1.folder_name, &mir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 0);

	success = camel_store_db_read_message (sdb, fir1.folder_name, mir1.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir1.folder_id, &mir1, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	mir2.folder_id = 0;
	success = camel_store_db_write_message (sdb, fir2.folder_name, &mir2, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 1);

	mir3.folder_id = 0;
	success = camel_store_db_write_message (sdb, fir2.folder_name, &mir3, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	mir1.folder_id = 1;
	mir2.folder_id = 2;
	mir3.folder_id = 2;

	#define init_rmd(_exp0, _exp1, _exp2) \
		rmd.n_called = 0; \
		rmd.expected[0] = _exp0; \
		rmd.expected[1] = _exp1; \
		rmd.expected[2] = _exp2;

	init_rmd (&mir1, NULL, NULL);

	success = camel_store_db_read_messages (sdb, fir1.folder_name, test_read_messages_cb, &rmd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (rmd.n_called, ==, 1);

	init_rmd (&mir2, &mir3, NULL);
	success = camel_store_db_read_messages (sdb, fir2.folder_name, test_read_messages_cb, &rmd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (rmd.n_called, ==, 2);

	mir1.flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_SEEN;
	mir1.folder_id = 0;
	success = camel_store_db_write_message (sdb, fir1.folder_name, &mir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	success = camel_store_db_read_message (sdb, fir1.folder_name, mir1.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir1.folder_id, &mir1, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	mir1.folder_id = 1;
	mir2.folder_id = 2;
	mir3.folder_id = 2;

	init_rmd (&mir1, NULL, NULL);
	success = camel_store_db_read_messages (sdb, fir1.folder_name, test_read_messages_cb, &rmd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (rmd.n_called, ==, 1);

	init_rmd (&mir2, &mir3, NULL);
	success = camel_store_db_read_messages (sdb, fir2.folder_name, test_read_messages_cb, &rmd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (rmd.n_called, ==, 2);

	success = camel_store_db_count_messages (sdb, "unknown", CAMEL_STORE_DB_COUNT_KIND_UNREAD, &count, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (success);
	g_clear_error (&error);

	uid_flags = camel_store_db_dup_uids_with_flags (sdb, "unknown", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_null (uid_flags);
	g_clear_error (&error);

	array = camel_store_db_dup_junk_uids (sdb, "unknown", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_null (array);
	g_clear_error (&error);

	array = camel_store_db_dup_deleted_uids (sdb, "unknown", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_null (array);
	g_clear_error (&error);

	#define check_counts(_kind, _expect1, _expect2) \
		count = ~0U; \
		success = camel_store_db_count_messages (sdb, fir1.folder_name, _kind, &count, &error); \
		g_assert_no_error (error); \
		g_assert_true (success); \
		g_assert_cmpint (count, ==, _expect1); \
		count = ~0U; \
		success = camel_store_db_count_messages (sdb, fir2.folder_name, _kind, &count, &error); \
		g_assert_no_error (error); \
		g_assert_true (success); \
		g_assert_cmpint (count, ==, _expect2);

	check_counts (CAMEL_STORE_DB_COUNT_KIND_TOTAL, 1, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK, 0, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_DELETED, 0, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED, 1, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK_NOT_DELETED, 0, 0);

	uid_flags = camel_store_db_dup_uids_with_flags (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (g_hash_table_size (uid_flags), ==, 1);
	g_assert_true (g_hash_table_contains (uid_flags, mir1.uid));
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (uid_flags, mir1.uid)), ==, mir1.flags);
	g_hash_table_destroy (uid_flags);

	uid_flags = camel_store_db_dup_uids_with_flags (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (g_hash_table_size (uid_flags), ==, 2);
	g_assert_true (g_hash_table_contains (uid_flags, mir2.uid));
	g_assert_true (g_hash_table_contains (uid_flags, mir3.uid));
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (uid_flags, mir2.uid)), ==, mir2.flags);
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (uid_flags, mir3.uid)), ==, mir3.flags);
	g_hash_table_destroy (uid_flags);

	array = camel_store_db_dup_junk_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_junk_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	mir1.flags = CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_SEEN;
	success = camel_store_db_write_message (sdb, fir1.folder_name, &mir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	check_counts (CAMEL_STORE_DB_COUNT_KIND_TOTAL, 1, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK, 1, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_DELETED, 0, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED, 0, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK_NOT_DELETED, 1, 0);

	array = camel_store_db_dup_junk_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 1);
	g_assert_true (g_ptr_array_find_with_equal_func (array, mir1.uid, g_str_equal, NULL));
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_junk_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	mir1.flags = CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN;
	success = camel_store_db_write_message (sdb, fir1.folder_name, &mir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	check_counts (CAMEL_STORE_DB_COUNT_KIND_TOTAL, 1, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK, 0, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_DELETED, 1, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED, 0, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK_NOT_DELETED, 0, 0);

	array = camel_store_db_dup_junk_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 1);
	g_assert_true (g_ptr_array_find_with_equal_func (array, mir1.uid, g_str_equal, NULL));
	g_ptr_array_unref (array);

	array = camel_store_db_dup_junk_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	mir1.flags = CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_SEEN;
	success = camel_store_db_write_message (sdb, fir1.folder_name, &mir1, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	check_counts (CAMEL_STORE_DB_COUNT_KIND_TOTAL, 1, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK, 1, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_DELETED, 1, 0);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED, 0, 2);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_NOT_JUNK_NOT_DELETED_UNREAD, 0, 1);
	check_counts (CAMEL_STORE_DB_COUNT_KIND_JUNK_NOT_DELETED, 0, 0);

	array = camel_store_db_dup_junk_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 1);
	g_assert_true (g_ptr_array_find_with_equal_func (array, mir1.uid, g_str_equal, NULL));
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir1.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 1);
	g_assert_true (g_ptr_array_find_with_equal_func (array, mir1.uid, g_str_equal, NULL));
	g_ptr_array_unref (array);

	array = camel_store_db_dup_junk_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (array);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	array = camel_store_db_dup_deleted_uids (sdb, fir2.folder_name, &error);
	g_assert_no_error (error);
	g_assert_nonnull (uid_flags);
	g_assert_cmpint (array->len, ==, 0);
	g_ptr_array_unref (array);

	success = camel_store_db_delete_message (sdb, "unknown", mir2.uid, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (success);
	g_clear_error (&error);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	success = camel_store_db_delete_message (sdb, fir2.folder_name, "unknown", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	success = camel_store_db_delete_message (sdb, fir2.folder_name, mir2.uid, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 1);

	success = camel_store_db_read_message (sdb, fir1.folder_name, mir1.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir1.folder_id, &mir1, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	success = camel_store_db_read_message (sdb, fir2.folder_name, mir3.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir2.folder_id, &mir3, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	mir2.uid = "mir2-uid-1";
	success = camel_store_db_write_message (sdb, fir2.folder_name, &mir2, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	mir2.uid = "mir2-uid-2";
	success = camel_store_db_write_message (sdb, fir2.folder_name, &mir2, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	mir2.uid = "mir2-uid-3";
	success = camel_store_db_write_message (sdb, fir2.folder_name, &mir2, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	mir2.uid = "mir2-uid-4";
	success = camel_store_db_write_message (sdb, fir2.folder_name, &mir2, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 5);

	array = g_ptr_array_new ();
	g_ptr_array_add (array, (gpointer) "mir2-uid-1");
	g_ptr_array_add (array, (gpointer) "mir2-uid-3");
	g_ptr_array_add (array, (gpointer) "unknown-mir2-uid-'\"%X");
	g_ptr_array_add (array, (gpointer) "mir2-uid-2");
	success = camel_store_db_delete_messages (sdb, "unknown", array, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (success);
	g_clear_error (&error);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 5);
	success = camel_store_db_delete_messages (sdb, fir2.folder_name, array, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_ptr_array_unref (array);
	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 2);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_1"), ==, 1);
	g_assert_cmpint (test_count_table_rows (cdb, "messages_2"), ==, 2);

	success = camel_store_db_read_message (sdb, fir1.folder_name, mir1.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir1.folder_id, &mir1, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	success = camel_store_db_read_message (sdb, fir2.folder_name, mir2.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir2.folder_id, &mir2, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	success = camel_store_db_read_message (sdb, fir2.folder_name, mir3.uid, &message_record, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	test_verify_message_records_equal (&message_record, fir2.folder_id, &mir3, LATEST_VERSION);
	camel_store_db_message_record_clear (&message_record);

	#undef check_counts
	#undef init_rmd

	g_object_unref (sdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_free (filename);
}

static const CamelStoreDBFolderRecord fir0_base = {
	.folder_id = 0,
	.folder_name = (gchar *) "without-messages",
	.version = -1,
	.flags = 1,
	.nextuid = 2,
	.timestamp = 3,
	.saved_count = 4,
	.unread_count = 5,
	.deleted_count = 6,
	.junk_count = 7,
	.visible_count = 8,
	.jnd_count = 9,
	.bdata = (gchar *) "fir0bdata"
};
static const CamelStoreDBFolderRecord fir1_base = {
	.folder_id = 0,
	.folder_name = (gchar *) "with/one/Message",
	.version = -1,
	.flags = 11,
	.nextuid = 12,
	.timestamp = 13,
	.saved_count = 14,
	.unread_count = 15,
	.deleted_count = 16,
	.junk_count = 17,
	.visible_count = 18,
	.jnd_count = 19,
	.bdata = (gchar *) "fir1bdata"
};
static const CamelStoreDBFolderRecord fir2_base = {
	.folder_id = 0,
	.folder_name = (gchar *) "with two %/mess ''/ages",
	.version = -1,
	.flags = 21,
	.nextuid = 22,
	.timestamp = 23,
	.saved_count = 24,
	.unread_count = 25,
	.deleted_count = 26,
	.junk_count = 27,
	.visible_count = 28,
	.jnd_count = 29,
	.bdata = (gchar *) "fir2bdata"
};
static const CamelStoreDBFolderRecord fir3_base = {
	.folder_id = 0,
	.folder_name = (gchar *) "Inbox/three/messages",
	.version = -1,
	.flags = 31,
	.nextuid = 32,
	.timestamp = 33,
	.saved_count = 34,
	.unread_count = 35,
	.deleted_count = 36,
	.junk_count = 37,
	.visible_count = 38,
	.jnd_count = 39,
	.bdata = (gchar *) "fir3bdata"
};
static const CamelStoreDBMessageRecord mirs1[] = {
	{ .uid = "10",
	  .folder_id = 0,
	  .flags = 101,
	  .msg_type = 102,
	  .dirty = 103,
	  .size = 110,
	  .dsent = 111,
	  .dreceived = 112,
	  .subject = "subject 10",
	  .from = "from 10",
	  .to = "to 10",
	  .cc = "cc 10",
	  .mlist = "mlist 10",
	  .part = (gchar *) "part 10",
	  .labels = (gchar *) "labels 10",
	  .usertags = (gchar *) "usertags 10",
	  .cinfo = (gchar *) "cinfo 10",
	  .bdata = (gchar *) "bdata 10",
	  .userheaders = (gchar *) "userheaders 10",
	  .preview = (gchar *) "preview 10" },
	{ .uid = NULL }
};
static const CamelStoreDBMessageRecord mirs2[] = {
	{ .uid = "20",
	  .folder_id = 0,
	  .flags = 201,
	  .msg_type = 202,
	  .dirty = 203,
	  .size = 210,
	  .dsent = 211,
	  .dreceived = 212,
	  .subject = "subject 20",
	  .from = "from 20",
	  .to = "to 20",
	  .cc = "cc 20",
	  .mlist = "mlist 20",
	  .part = (gchar *) "part 20",
	  .labels = (gchar *) "labels 20",
	  .usertags = (gchar *) "usertags 20",
	  .cinfo = (gchar *) "cinfo 20",
	  .bdata = (gchar *) "bdata 20",
	  .userheaders = (gchar *) "userheaders 20",
	  .preview = (gchar *) "preview 20" },
	{ .uid = "21",
	  .folder_id = 0,
	  .flags = 301,
	  .msg_type = 302,
	  .dirty = 303,
	  .size = 310,
	  .dsent = 311,
	  .dreceived = 312,
	  .subject = "subject 21",
	  .from = "from 21",
	  .to = "to 21",
	  .cc = "cc 21",
	  .mlist = "mlist 21",
	  .part = (gchar *) "part 21",
	  .labels = (gchar *) "labels 21",
	  .usertags = (gchar *) "usertags 21",
	  .cinfo = (gchar *) "cinfo 21",
	  .bdata = (gchar *) "bdata 21",
	  .userheaders = (gchar *) "userheaders 21",
	  .preview = (gchar *) "preview 21" },
	{ .uid = NULL }
};
static const CamelStoreDBMessageRecord mirs3[] = {
	{ .uid = "30",
	  .folder_id = 0,
	  .flags = 401,
	  .msg_type = 402,
	  .dirty = 403,
	  .size = 410,
	  .dsent = 411,
	  .dreceived = 412,
	  .subject = "subject 30",
	  .from = "from 30",
	  .to = "to 30",
	  .cc = "cc 30",
	  .mlist = "mlist 30",
	  .part = (gchar *) "part 30",
	  .labels = (gchar *) "labels 30",
	  .usertags = (gchar *) "usertags 30",
	  .cinfo = (gchar *) "cinfo 30",
	  .bdata = (gchar *) "bdata 30",
	  .userheaders = (gchar *) "userheaders 30",
	  .preview = (gchar *) "preview 30" },
	{ .uid = "21",
	  .folder_id = 0,
	  .flags = 501,
	  .msg_type = 502,
	  .dirty = 503,
	  .size = 510,
	  .dsent = 511,
	  .dreceived = 512,
	  .subject = "subject 21",
	  .from = "from 21",
	  .to = "to 21",
	  .cc = "cc 21",
	  .mlist = "mlist 21",
	  .part = (gchar *) "part 21",
	  .labels = (gchar *) "labels 21",
	  .usertags = (gchar *) "usertags 21",
	  .cinfo = (gchar *) "cinfo 21",
	  .bdata = (gchar *) "bdata 21",
	  .userheaders = (gchar *) "userheaders 21",
	  .preview = (gchar *) "preview 21" },
	{ .uid = "10",
	  .folder_id = 0,
	  .flags = 601,
	  .msg_type = 602,
	  .dirty = 603,
	  .size = 610,
	  .dsent = 611,
	  .dreceived = 612,
	  .subject = "subject 3:10",
	  .from = "from 3:10",
	  .to = "to 3:10",
	  .cc = "cc 3:10",
	  .mlist = NULL,
	  .part = NULL,
	  .labels = (gchar *) "labels 3:10",
	  .usertags = (gchar *) "usertags 3:10",
	  .cinfo = (gchar *) "cinfo 3:10",
	  .bdata = (gchar *) "bdata 3:10",
	  .userheaders = (gchar *) "userheaders 3:10",
	  .preview = (gchar *) "preview 3:10" },
	{ .uid = NULL }
};

static void
test_create_old_data (const gchar *filename,
		      gint version)
{
	CamelStoreDBFolderRecord fir0 = fir0_base;
	CamelStoreDBFolderRecord fir1 = fir1_base;
	CamelStoreDBFolderRecord fir2 = fir2_base;
	CamelStoreDBFolderRecord fir3 = fir3_base;
	CamelDB *cdb;
	GError *error = NULL;

	cdb = camel_db_new (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cdb);

	fir0.version = version;
	fir1.version = version;
	fir2.version = version;
	fir3.version = version;

	test_create_old_folder_data (cdb, &fir0, NULL);
	test_create_old_folder_data (cdb, &fir1, mirs1);
	test_create_old_folder_data (cdb, &fir2, mirs2);
	if (version > 0)
		test_create_old_folder_data (cdb, &fir3, mirs3);

	g_object_unref (cdb);

	/* verify saved content */

	cdb = camel_db_new (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cdb);

	if (version == 0)
		g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	else
		g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 4);

	if (version == 0)
		g_assert_cmpint (test_count_tables (cdb), ==, 7);
	else if (version == 1)
		g_assert_cmpint (test_count_tables (cdb), ==, 9);
	else if (version == 2 || version == 3)
		g_assert_cmpint (test_count_tables (cdb), ==, 17);
	else
		g_assert_not_reached ();

	test_verify_old_folder_data (cdb, &fir0, NULL);
	test_verify_old_folder_data (cdb, &fir1, mirs1);
	test_verify_old_folder_data (cdb, &fir2, mirs2);
	if (version > 0)
		test_verify_old_folder_data (cdb, &fir3, mirs3);

	g_object_unref (cdb);
}

static gint
test_camel_db_collate_columna (gpointer enc,
			       gint length1,
			       gconstpointer data1,
			       gint length2,
			       gconstpointer data2)
{
	const gchar *order = "CAB";
	const gchar *str1 = data1;
	const gchar *str2 = data2;
	gint idx1, idx2;

	g_assert_nonnull (str1);
	g_assert_nonnull (str2);

	for (idx1 = 0; order[idx1]; idx1++) {
		if (order[idx1] == str1[0])
			break;
	}

	for (idx2 = 0; order[idx2]; idx2++) {
		if (order[idx2] == str2[0])
			break;
	}

	return idx1 - idx2;
}

typedef struct BasicReadData {
	guint32 n_read;
	GSList *expected;
} BasicReadData;

static gboolean
test_camel_db_basic_read_cb (gpointer user_data,
			     gint ncol,
			     gchar **colvalues,
			     gchar **colnames)
{
	BasicReadData *brd = user_data;

	brd->n_read++;

	if (brd->expected) {
		const gchar *expected_value = brd->expected->data;

		if (g_strcmp0 (expected_value, colvalues[0]) == 0)
			brd->expected = g_slist_remove (brd->expected, expected_value);
	}

	return TRUE;
}

static void
test_camel_db_basic (void)
{
	CamelDB *cdb;
	GError *error = NULL;
	gchar *filename;
	BasicReadData brd = { 0, };
	gboolean success;

	filename = test_create_tmp_file ();

	cdb = camel_db_new (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cdb);

	g_assert_cmpstr (camel_db_get_filename (cdb), ==, filename);
	g_assert_false (camel_db_has_table (cdb, "table1"));
	g_assert_false (camel_db_has_table_with_column (cdb, "table1", "columnA"));

	success = camel_db_exec_statement (cdb, "CREATE TABLE table1 (column1 INTEGER)", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_true (camel_db_has_table (cdb, "table1"));
	g_assert_false (camel_db_has_table_with_column (cdb, "table1", "columnA"));

	success = camel_db_exec_statement (cdb, "ALTER TABLE table1 ADD COLUMN columnA TEXT", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_true (camel_db_has_table (cdb, "table1"));
	g_assert_true (camel_db_has_table_with_column (cdb, "table1", "columnA"));

	brd.n_read = 0;
	brd.expected = NULL;
	success = camel_db_exec_select (cdb, "SELECT * FROM table1", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 0);

	success = camel_db_exec_statement (cdb, "INSERT INTO table1 (column1, columnA) VALUES (1, 'A')", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_db_exec_statement (cdb, "INSERT INTO table1 (column1, columnA) VALUES (2, 'B')", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_db_exec_statement (cdb, "INSERT INTO table1 (column1, columnA) VALUES (3, 'C')", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	success = camel_db_exec_select (cdb, "SELECT * FROM table1", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);

	success = camel_db_maybe_run_maintenance (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "C");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");

	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);
	g_assert_null (brd.expected);

	success = camel_db_set_collate (cdb, "columnA", "collateColumnA", test_camel_db_collate_columna);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "C");

	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA COLLATE collateColumnA", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);
	g_assert_null (brd.expected);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "C");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");

	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA COLLATE collateColumnA DESC", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);
	g_assert_null (brd.expected);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "C");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");

	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);
	g_assert_null (brd.expected);

	success = camel_db_begin_transaction (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = NULL;
	success = camel_db_exec_select (cdb, "SELECT * FROM table1", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);

	success = camel_db_exec_statement (cdb, "DELETE FROM table1 WHERE columnA='B'", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "C");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");
	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 2);
	g_assert_null (brd.expected);

	/* revert changes in transaction */
	success = camel_db_abort_transaction (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "C");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");

	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);
	g_assert_null (brd.expected);

	success = camel_db_begin_transaction (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = NULL;
	success = camel_db_exec_select (cdb, "SELECT * FROM table1", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 3);

	success = camel_db_exec_statement (cdb, "DELETE FROM table1 WHERE columnA='C'", &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");
	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 2);
	g_assert_null (brd.expected);

	/* commit changes in transaction */
	success = camel_db_end_transaction (cdb, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	brd.n_read = 0;
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "A");
	brd.expected = g_slist_prepend (brd.expected, (gpointer) "B");
	success = camel_db_exec_select (cdb, "SELECT columnA FROM table1 ORDER BY columnA DESC", test_camel_db_basic_read_cb, &brd, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (brd.n_read, ==, 2);
	g_assert_null (brd.expected);

	camel_db_release_cache_memory ();

	g_object_unref (cdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_free (filename);
}

static void
test_camel_store_db_empty (void)
{
	CamelStoreDB *sdb;
	CamelDB *cdb;
	GError *error = NULL;
	gchar *filename;

	filename = test_create_tmp_file ();

	sdb = camel_store_db_new (filename, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);

	cdb = CAMEL_DB (sdb);

	g_assert_true (camel_db_has_table (cdb, "folders"));
	g_assert_true (camel_db_has_table (cdb, "keys"));

	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "keys"), ==, 2);

	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::folders_version\"");
	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::messages_version\"");

	g_object_unref (sdb);

	/* data survive object life time */
	sdb = camel_store_db_new (filename, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);

	cdb = CAMEL_DB (sdb);

	g_assert_true (camel_db_has_table (cdb, "folders"));
	g_assert_true (camel_db_has_table (cdb, "keys"));

	g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 0);
	g_assert_cmpint (test_count_table_rows (cdb, "keys"), ==, 2);

	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::folders_version\"");
	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::messages_version\"");

	g_object_unref (sdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_free (filename);
}

static void
test_camel_store_db_keys_internal (gboolean in_transaction)
{
	CamelStoreDB *sdb;
	CamelDB *cdb;
	GError *error = NULL;
	gchar *filename, *tmp;
	gboolean success;

	filename = test_create_tmp_file ();

	sdb = camel_store_db_new (filename, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);

	cdb = CAMEL_DB (sdb);

	if (in_transaction) {
		success = camel_db_begin_transaction (cdb, &error);
		g_assert_no_error (error);
		g_assert_true (success);
	}

	g_assert_cmpint (test_count_table_rows (cdb, "keys"), ==, 2);

	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::folders_version\"");
	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::messages_version\"");

	/* cannot read nor override CamelDB internal keys with this API */
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "folders_version", 999), ==, 999);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "csdb::folders_version", 999), ==, 999);

	success = camel_store_db_set_int_key (sdb, "folders_version", 123, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "folders_version", 999), ==, 123);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "csdb::folders_version", 999), ==, 999);

	success = camel_store_db_set_int_key (sdb, "csdb::folders_version", 567, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "folders_version", 999), ==, 123);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "csdb::folders_version", 999), ==, 567);

	g_assert_cmpint (camel_store_db_get_int_key (sdb, "int-key", 999), ==, 999);
	success = camel_store_db_set_int_key (sdb, "int-key", 480, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_assert_cmpint (camel_store_db_get_int_key (sdb, "folders_version", 999), ==, 123);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "csdb::folders_version", 999), ==, 567);
	g_assert_cmpint (camel_store_db_get_int_key (sdb, "int-key", 999), ==, 480);

	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::folders_version\"");
	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::messages_version\"");

	tmp = camel_store_db_dup_string_key (sdb, "unknown-key");
	g_assert_cmpstr (tmp, ==, NULL);

	tmp = camel_store_db_dup_string_key (sdb, "folders_version");
	g_assert_cmpstr (tmp, ==, "123");
	g_free (tmp);
	tmp = camel_store_db_dup_string_key (sdb, "csdb::folders_version");
	g_assert_cmpstr (tmp, ==, "567");
	g_free (tmp);

	success = camel_store_db_set_string_key (sdb, "folders_version", "abc", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	tmp = camel_store_db_dup_string_key (sdb, "folders_version");
	g_assert_cmpstr (tmp, ==, "abc");
	g_free (tmp);

	success = camel_store_db_set_string_key (sdb, "csdb::folders_version", "def", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	tmp = camel_store_db_dup_string_key (sdb, "csdb::folders_version");
	g_assert_cmpstr (tmp, ==, "def");
	g_free (tmp);

	success = camel_store_db_set_string_key (sdb, "str-key", "xyz", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	tmp = camel_store_db_dup_string_key (sdb, "str-key");
	g_assert_cmpstr (tmp, ==, "xyz");
	g_free (tmp);

	g_assert_cmpstr (camel_store_db_dup_string_key (sdb, "obscure-'\"\\?!#%&-key"), ==, NULL);
	success = camel_store_db_set_string_key (sdb, "obscure-'\"\\?!#%&-key", "obscure &*(^%$#@!)' \\\"'''", &error);
	g_assert_no_error (error);
	g_assert_true (success);
	tmp = camel_store_db_dup_string_key (sdb, "obscure-'\"\\?!#%&-key");
	g_assert_cmpstr (tmp, ==, "obscure &*(^%$#@!)' \\\"'''");
	g_free (tmp);

	if (in_transaction) {
		success = camel_db_end_transaction (cdb, &error);
		g_assert_no_error (error);
		g_assert_true (success);
	}

	g_object_unref (sdb);

	/* changes survive object life time */
	sdb = camel_store_db_new (filename, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);

	cdb = CAMEL_DB (sdb);

	g_assert_cmpint (test_count_table_rows (cdb, "keys"), ==, 7);

	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::folders_version\"");
	test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::messages_version\"");

	tmp = camel_store_db_dup_string_key (sdb, "folders_version");
	g_assert_cmpstr (tmp, ==, "abc");
	g_free (tmp);

	tmp = camel_store_db_dup_string_key (sdb, "csdb::folders_version");
	g_assert_cmpstr (tmp, ==, "def");
	g_free (tmp);

	g_assert_cmpint (camel_store_db_get_int_key (sdb, "int-key", 999), ==, 480);

	tmp = camel_store_db_dup_string_key (sdb, "str-key");
	g_assert_cmpstr (tmp, ==, "xyz");
	g_free (tmp);

	tmp = camel_store_db_dup_string_key (sdb, "obscure-'\"\\?!#%&-key");
	g_assert_cmpstr (tmp, ==, "obscure &*(^%$#@!)' \\\"'''");
	g_free (tmp);

	if (in_transaction) {
		success = camel_db_begin_transaction (cdb, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		success = camel_store_db_set_int_key (sdb, "int-key", 256, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_cmpint (camel_store_db_get_int_key (sdb, "int-key", 999), ==, 256);

		success = camel_store_db_set_string_key (sdb, "str-key", "tuv", &error);
		g_assert_no_error (error);
		g_assert_true (success);
		tmp = camel_store_db_dup_string_key (sdb, "str-key");
		g_assert_cmpstr (tmp, ==, "tuv");
		g_free (tmp);

		success = camel_store_db_set_int_key (sdb, "int-key2", 111, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_cmpint (camel_store_db_get_int_key (sdb, "int-key2", 999), ==, 111);

		/* changes are reverted */
		success = camel_db_abort_transaction (cdb, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		g_assert_cmpint (test_count_table_rows (cdb, "keys"), ==, 7);

		test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::folders_version\"");
		test_has_table_with_column_value (cdb, "keys", "value", "1", "key=\"csdb::messages_version\"");

		tmp = camel_store_db_dup_string_key (sdb, "folders_version");
		g_assert_cmpstr (tmp, ==, "abc");
		g_free (tmp);

		tmp = camel_store_db_dup_string_key (sdb, "csdb::folders_version");
		g_assert_cmpstr (tmp, ==, "def");
		g_free (tmp);

		g_assert_cmpint (camel_store_db_get_int_key (sdb, "int-key", 999), ==, 480);

		tmp = camel_store_db_dup_string_key (sdb, "str-key");
		g_assert_cmpstr (tmp, ==, "xyz");
		g_free (tmp);

		tmp = camel_store_db_dup_string_key (sdb, "obscure-'\"\\?!#%&-key");
		g_assert_cmpstr (tmp, ==, "obscure &*(^%$#@!)' \\\"'''");
		g_free (tmp);
	}

	g_object_unref (sdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_free (filename);
}

static void
test_camel_store_db_keys (void)
{
	test_camel_store_db_keys_internal (FALSE);
	test_camel_store_db_keys_internal (TRUE);
}

static void
test_camel_store_db_migrate_push_message_cb (GCancellable *cancellable,
					     const gchar *message,
					     gpointer user_data)
{
	gint *p_n_pushed = user_data;
	*p_n_pushed = (*p_n_pushed) + 1;

	printf ("   %s: '%s'\n", G_STRFUNC, message);
}

static void
test_camel_store_db_migrate_pop_message_cb (GCancellable *cancellable,
					    gpointer user_data)
{
	gint *p_n_pushed = user_data;
	*p_n_pushed = (*p_n_pushed) - 1;

	printf ("   %s:\n", G_STRFUNC);
}

static void
test_camel_store_db_migrate_progress_cb (GCancellable *cancellable,
					 gint percent,
					 gpointer user_data)
{
	gint *p_n_percent = user_data;
	*p_n_percent = percent;

	printf ("   %s: %d%%\n", G_STRFUNC, percent);
}

static void
test_camel_store_db_migrate (gconstpointer user_data)
{
	gint version = GPOINTER_TO_INT (user_data);
	gint n_pushed = 0, n_percent = 0;
	CamelStoreDBFolderRecord fir0 = fir0_base;
	CamelStoreDBFolderRecord fir1 = fir1_base;
	CamelStoreDBFolderRecord fir2 = fir2_base;
	CamelStoreDBFolderRecord fir3 = fir3_base;
	CamelStoreDB *sdb;
	CamelDB *cdb;
	GCancellable *cancellable = NULL;
	GError *error = NULL;
	gchar *filename;
	gchar *stmt;
	gboolean success;

	fir0.version = version;
	fir1.version = version;
	fir2.version = version;
	if (version != 0)
		fir3.version = version;

	filename = test_create_tmp_file ();

	test_create_old_data (filename, version);

	if (version == 0) {
		cancellable = camel_operation_new ();
		/* use the internal signals, there's no "in idle" here, need to know immediately */
		g_signal_connect (cancellable, "push-message", G_CALLBACK (test_camel_store_db_migrate_push_message_cb), &n_pushed);
		g_signal_connect (cancellable, "pop-message", G_CALLBACK (test_camel_store_db_migrate_pop_message_cb), &n_pushed);
		g_signal_connect (cancellable, "progress", G_CALLBACK (test_camel_store_db_migrate_progress_cb), &n_percent);
	}

	sdb = camel_store_db_new (filename, cancellable, &error);
	g_assert_no_error (error);
	g_assert_nonnull (sdb);
	g_assert_cmpint (n_pushed, ==, 0);
	g_assert_cmpint (n_percent, ==, 0);

	cdb = CAMEL_DB (sdb);

	if (version == 0)
		g_assert_cmpint (test_count_tables (cdb), ==, 5);
	else
		g_assert_cmpint (test_count_tables (cdb), ==, 6);
	g_assert_true (camel_db_has_table (cdb, "folders"));
	g_assert_true (camel_db_has_table (cdb, "keys"));
	g_assert_true (camel_db_has_table (cdb, "messages_1"));
	g_assert_true (camel_db_has_table (cdb, "messages_2"));
	g_assert_true (camel_db_has_table (cdb, "messages_3"));
	if (version != 0)
		g_assert_true (camel_db_has_table (cdb, "messages_4"));
	if (version == 0)
		g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 3);
	else
		g_assert_cmpint (test_count_table_rows (cdb, "folders"), ==, 4);

	#define read_folder_id(_fir) { \
		stmt = sqlite3_mprintf ("SELECT folder_id FROM folders WHERE folder_name=%Q", _fir.folder_name); \
		success = camel_db_exec_select (cdb, stmt, test_read_integer_cb, &_fir.folder_id, &error); \
		g_assert_no_error (error); \
		g_assert_true (success); \
		g_assert_cmpint (_fir.folder_id, !=, 0); \
		sqlite3_free (stmt); }

	read_folder_id (fir0);
	read_folder_id (fir1);
	read_folder_id (fir2);
	if (version != 0) {
		read_folder_id (fir3);
	}

	#undef read_folder_id

	test_verify_new_folder_data (cdb, &fir0, NULL);
	test_verify_new_folder_data (cdb, &fir1, mirs1);
	test_verify_new_folder_data (cdb, &fir2, mirs2);
	if (version != 0)
		test_verify_new_folder_data (cdb, &fir3, mirs3);

	g_clear_object (&cancellable);
	g_object_unref (sdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_free (filename);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/CamelDB/Basic", test_camel_db_basic);
	g_test_add_func ("/CamelStoreDB/Empty", test_camel_store_db_empty);
	g_test_add_func ("/CamelStoreDB/Keys", test_camel_store_db_keys);
	g_test_add_func ("/CamelStoreDB/FolderOps", test_camel_store_db_folder_ops);
	g_test_add_func ("/CamelStoreDB/MessageOps", test_camel_store_db_message_ops);
	g_test_add_data_func ("/CamelStoreDB/MigrateVer0", GINT_TO_POINTER (0), test_camel_store_db_migrate);
	g_test_add_data_func ("/CamelStoreDB/MigrateVer1", GINT_TO_POINTER (1), test_camel_store_db_migrate);
	g_test_add_data_func ("/CamelStoreDB/MigrateVer2", GINT_TO_POINTER (2), test_camel_store_db_migrate);
	g_test_add_data_func ("/CamelStoreDB/MigrateVer3", GINT_TO_POINTER (3), test_camel_store_db_migrate);

	return g_test_run ();
}
