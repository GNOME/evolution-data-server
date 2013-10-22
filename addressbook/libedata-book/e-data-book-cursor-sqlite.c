/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

/**
 * SECTION: e-data-book-cursor-sqlite
 * @include: libedata-book/libedata-book.h
 * @short_description: The SQLite cursor implementation
 *
 * This cursor implementation can be used with any backend which
 * stores contacts using #EBookBackendSqliteDB.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "e-data-book-cursor-sqlite.h"

/* GObjectClass */
static void e_data_book_cursor_sqlite_dispose      (GObject *object);
static void e_data_book_cursor_sqlite_finalize     (GObject *object);
static void e_data_book_cursor_sqlite_set_property (GObject *object,
						    guint property_id,
						    const GValue *value,
						    GParamSpec *pspec);

/* EDataBookCursorClass */
static gboolean e_data_book_cursor_sqlite_set_sexp             (EDataBookCursor     *cursor,
								const gchar         *sexp,
								GError             **error);
static gint     e_data_book_cursor_sqlite_step                 (EDataBookCursor     *cursor,
								const gchar         *revision_guard,
								EBookCursorStepFlags flags,
								EBookCursorOrigin    origin,
								gint                 count,
								GSList             **results,
								GError             **error);
static gboolean e_data_book_cursor_sqlite_set_alphabetic_index (EDataBookCursor     *cursor,
								gint                 index,
								const gchar         *locale,
								GError             **error);
static gboolean e_data_book_cursor_sqlite_get_position         (EDataBookCursor     *cursor,
								gint                *total,
								gint                *position,
								GError             **error);
static gint     e_data_book_cursor_sqlite_compare_contact      (EDataBookCursor     *cursor,
								EContact            *contact,
								gboolean            *matches_sexp);
static gboolean e_data_book_cursor_sqlite_load_locale          (EDataBookCursor     *cursor,
								gchar              **locale,
								GError             **error);

struct _EDataBookCursorSqlitePrivate {
	EBookBackendSqliteDB *ebsdb;
	EbSdbCursor          *cursor;
	gchar                *folder_id;
};

enum {
	PROP_0,
	PROP_EBSDB,
	PROP_FOLDER_ID,
	PROP_CURSOR,
};

G_DEFINE_TYPE (EDataBookCursorSqlite, e_data_book_cursor_sqlite, E_TYPE_DATA_BOOK_CURSOR);

/************************************************
 *                  GObjectClass                *
 ************************************************/
static void
e_data_book_cursor_sqlite_class_init (EDataBookCursorSqliteClass *class)
{
	GObjectClass *object_class;
	EDataBookCursorClass *cursor_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_data_book_cursor_sqlite_dispose;
	object_class->finalize = e_data_book_cursor_sqlite_finalize;
	object_class->set_property = e_data_book_cursor_sqlite_set_property;

	cursor_class = E_DATA_BOOK_CURSOR_CLASS (class);
	cursor_class->set_sexp = e_data_book_cursor_sqlite_set_sexp;
	cursor_class->step = e_data_book_cursor_sqlite_step;
	cursor_class->set_alphabetic_index = e_data_book_cursor_sqlite_set_alphabetic_index;
	cursor_class->get_position = e_data_book_cursor_sqlite_get_position;
	cursor_class->compare_contact = e_data_book_cursor_sqlite_compare_contact;
	cursor_class->load_locale = e_data_book_cursor_sqlite_load_locale;

	g_object_class_install_property (
		object_class,
		PROP_EBSDB,
		g_param_spec_object (
			"ebsdb", "EBookBackendSqliteDB",
			"The EBookBackendSqliteDB to use for queries",
			E_TYPE_BOOK_BACKEND_SQLITEDB,
			G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_FOLDER_ID,
		g_param_spec_string (
			"folder-id", "Folder ID",
			"The folder identifier to use with the EBookBackendSqliteDB object",
			NULL,
			G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CURSOR,
		g_param_spec_pointer (
			"cursor", "Cursor",
			"The EbSdbCursor pointer",
			G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (class, sizeof (EDataBookCursorSqlitePrivate));
}

static void
e_data_book_cursor_sqlite_init (EDataBookCursorSqlite *cursor)
{
	cursor->priv = 
	  G_TYPE_INSTANCE_GET_PRIVATE (cursor,
				       E_TYPE_DATA_BOOK_CURSOR_SQLITE,
				       EDataBookCursorSqlitePrivate);
}

static void
e_data_book_cursor_sqlite_dispose (GObject *object)
{
	EDataBookCursorSqlite        *cursor = E_DATA_BOOK_CURSOR_SQLITE (object);
	EDataBookCursorSqlitePrivate *priv = cursor->priv;

	if (priv->ebsdb) {

		if (priv->cursor)
			e_book_backend_sqlitedb_cursor_free (priv->ebsdb,
							     priv->cursor);

		g_object_unref (priv->ebsdb);
		priv->ebsdb = NULL;
		priv->cursor = NULL;
	}

	G_OBJECT_CLASS (e_data_book_cursor_sqlite_parent_class)->dispose (object);
}

static void
e_data_book_cursor_sqlite_finalize (GObject *object)
{
	EDataBookCursorSqlite        *cursor = E_DATA_BOOK_CURSOR_SQLITE (object);
	EDataBookCursorSqlitePrivate *priv = cursor->priv;

	g_free (priv->folder_id);

	G_OBJECT_CLASS (e_data_book_cursor_sqlite_parent_class)->finalize (object);
}

static void
e_data_book_cursor_sqlite_set_property (GObject *object,
					guint property_id,
					const GValue *value,
					GParamSpec *pspec)
{
	EDataBookCursorSqlite        *cursor = E_DATA_BOOK_CURSOR_SQLITE (object);
	EDataBookCursorSqlitePrivate *priv = cursor->priv;

	switch (property_id) {
	case PROP_EBSDB:
		/* Construct-only, can only be set once */
		priv->ebsdb = g_value_dup_object (value);
		break;
	case PROP_FOLDER_ID:
		/* Construct-only, can only be set once */
		priv->folder_id = g_value_dup_string (value);
		break;
	case PROP_CURSOR:
		/* Construct-only, can only be set once */
		priv->cursor = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/************************************************
 *            EDataBookCursorClass              *
 ************************************************/
static gboolean
e_data_book_cursor_sqlite_set_sexp (EDataBookCursor     *cursor,
				    const gchar         *sexp,
				    GError             **error)
{
	EDataBookCursorSqlite *cursor_sqlite;
	EDataBookCursorSqlitePrivate *priv;
	GError *local_error = NULL;
	gboolean success;

	cursor_sqlite = E_DATA_BOOK_CURSOR_SQLITE (cursor);
	priv = cursor_sqlite->priv;

	success = e_book_backend_sqlitedb_cursor_set_sexp (priv->ebsdb,
							   priv->cursor,
							   sexp,
							   &local_error);

	if (!success) {
		if (g_error_matches (local_error,
				     E_BOOK_SDB_ERROR,
				     E_BOOK_SDB_ERROR_INVALID_QUERY)) {
			g_set_error_literal (error,
					     E_CLIENT_ERROR,
					     E_CLIENT_ERROR_INVALID_QUERY,
					     local_error->message);
			g_clear_error (&local_error);
		} else {
			g_propagate_error (error, local_error);
		}
	}

	return success;
}

static gboolean
convert_origin (EBookCursorOrigin    src_origin,
		EbSdbCursorOrigin   *dest_origin,
		GError             **error)
{
	gboolean success = TRUE;

	switch (src_origin) {
	case E_BOOK_CURSOR_ORIGIN_CURRENT:
		*dest_origin = EBSDB_CURSOR_ORIGIN_CURRENT;
		break;
	case E_BOOK_CURSOR_ORIGIN_BEGIN:
		*dest_origin = EBSDB_CURSOR_ORIGIN_BEGIN;
		break;
	case E_BOOK_CURSOR_ORIGIN_END:
		*dest_origin = EBSDB_CURSOR_ORIGIN_END;
		break;
	default:
		success = FALSE;
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_INVALID_ARG,
				     _("Unrecognized cursor origin"));
		break;
	}

	return success;
}

static void
convert_flags (EBookCursorStepFlags    src_flags,
	       EbSdbCursorStepFlags   *dest_flags)
{
	if (src_flags & E_BOOK_CURSOR_STEP_MOVE)
		*dest_flags |= EBSDB_CURSOR_STEP_MOVE;

	if (src_flags & E_BOOK_CURSOR_STEP_FETCH)
		*dest_flags |= EBSDB_CURSOR_STEP_FETCH;
}

static gint
e_data_book_cursor_sqlite_step (EDataBookCursor     *cursor,
				const gchar         *revision_guard,
				EBookCursorStepFlags flags,
				EBookCursorOrigin    origin,
				gint                 count,
				GSList             **results,
				GError             **error)
{
	EDataBookCursorSqlite *cursor_sqlite;
	EDataBookCursorSqlitePrivate *priv;
	GSList *local_results = NULL, *local_converted_results = NULL, *l;
	EbSdbCursorOrigin sqlitedb_origin = EBSDB_CURSOR_ORIGIN_CURRENT;
	EbSdbCursorStepFlags sqlitedb_flags = 0;
	gchar *revision = NULL;
	gboolean success = FALSE;
	gint n_results = -1;

	cursor_sqlite = E_DATA_BOOK_CURSOR_SQLITE (cursor);
	priv = cursor_sqlite->priv;

	if (!convert_origin (origin, &sqlitedb_origin, error))
		return FALSE;

	convert_flags (flags, &sqlitedb_flags);

	/* Here we check the EBookBackendSqliteDB revision
	 * against the revision_guard with an atomic transaction
	 * with the sqlite.
	 *
	 * The addressbook modifications and revision changes
	 * are also atomically committed to the SQLite.
	 */
	success = e_book_backend_sqlitedb_lock_updates (priv->ebsdb, error);

	if (success && revision_guard)
		success = e_book_backend_sqlitedb_get_revision (priv->ebsdb,
								priv->folder_id,
								&revision,
								error);

	if (success && revision_guard &&
	    g_strcmp0 (revision, revision_guard) != 0) {

		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC,
				     _("Out of sync revision while moving cursor"));
		success = FALSE;
	}

	if (success) {
		GError *local_error = NULL;

		n_results = e_book_backend_sqlitedb_cursor_step (priv->ebsdb,
								 priv->cursor,
								 sqlitedb_flags,
								 sqlitedb_origin,
								 count,
								 &local_results,
								 &local_error);

		if (n_results < 0) {

			/* Convert the SQLite backend error to an EClient error */
			if (g_error_matches (local_error,
					     E_BOOK_SDB_ERROR,
					     E_BOOK_SDB_ERROR_END_OF_LIST)) {
				g_set_error_literal (error, E_CLIENT_ERROR,
						     E_CLIENT_ERROR_QUERY_REFUSED,
						     local_error->message);
				g_clear_error (&local_error);
			} else
				g_propagate_error (error, local_error);

			success = FALSE;
		}
	}

	if (success) {
		success = e_book_backend_sqlitedb_unlock_updates (priv->ebsdb, TRUE, error);

	} else {
		GError *local_error = NULL;

		/* Rollback transaction */
		if (!e_book_backend_sqlitedb_unlock_updates (priv->ebsdb, FALSE, &local_error)) {
			g_warning ("Failed to rollback transaction after failing move cursor: %s",
				   local_error->message);
			g_clear_error (&local_error);
		}
	}

	for (l = local_results; l; l = l->next) {
		EbSdbSearchData *data = l->data;

		local_converted_results =
			g_slist_prepend (local_converted_results, data->vcard);
		data->vcard = NULL;
	}

	g_slist_free_full (local_results, (GDestroyNotify)e_book_backend_sqlitedb_search_data_free);

	if (results)
		*results = g_slist_reverse (local_converted_results);
	else
		g_slist_free_full (local_converted_results, (GDestroyNotify)g_free);

	g_free (revision);

	if (success)
		return n_results;

	return -1;
}

static gboolean
e_data_book_cursor_sqlite_set_alphabetic_index (EDataBookCursor     *cursor,
						gint                 index,
						const gchar         *locale,
						GError             **error)
{
	EDataBookCursorSqlite *cursor_sqlite;
	EDataBookCursorSqlitePrivate *priv;
	gchar *current_locale = NULL;

	cursor_sqlite = E_DATA_BOOK_CURSOR_SQLITE (cursor);
	priv = cursor_sqlite->priv;

	if (!e_book_backend_sqlitedb_get_locale (priv->ebsdb, priv->folder_id,
						 &current_locale, error))
		return FALSE;

	/* Locale mismatch, need to report error */
	if (g_strcmp0 (current_locale, locale) != 0) {
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC,
				     _("Alphabetic index was set for incorrect locale"));
		g_free (current_locale);
		return FALSE;
	}

	e_book_backend_sqlitedb_cursor_set_target_alphabetic_index (priv->ebsdb,
								    priv->cursor,
								    index);
	g_free (current_locale);
	return TRUE;
}

static gboolean
e_data_book_cursor_sqlite_get_position (EDataBookCursor     *cursor,
					gint                *total,
					gint                *position,
					GError             **error)
{
	EDataBookCursorSqlite *cursor_sqlite;
	EDataBookCursorSqlitePrivate *priv;

	cursor_sqlite = E_DATA_BOOK_CURSOR_SQLITE (cursor);
	priv = cursor_sqlite->priv;

	return e_book_backend_sqlitedb_cursor_calculate (priv->ebsdb,
							 priv->cursor,
							 total, position,
							 error);
}

static gint
e_data_book_cursor_sqlite_compare_contact (EDataBookCursor     *cursor,
					   EContact            *contact,
					   gboolean            *matches_sexp)
{
	EDataBookCursorSqlite *cursor_sqlite;
	EDataBookCursorSqlitePrivate *priv;

	cursor_sqlite = E_DATA_BOOK_CURSOR_SQLITE (cursor);
	priv = cursor_sqlite->priv;

	return e_book_backend_sqlitedb_cursor_compare_contact (priv->ebsdb,
							       priv->cursor,
							       contact,
							       matches_sexp);
}

static gboolean
e_data_book_cursor_sqlite_load_locale (EDataBookCursor     *cursor,
				       gchar              **locale,
				       GError             **error)
{
	EDataBookCursorSqlite        *cursor_sqlite;
	EDataBookCursorSqlitePrivate *priv;

	cursor_sqlite = E_DATA_BOOK_CURSOR_SQLITE (cursor);
	priv = cursor_sqlite->priv;

	return e_book_backend_sqlitedb_get_locale (priv->ebsdb,
						   priv->folder_id,
						   locale,
						   error);
}

/************************************************
 *                       API                    *
 ************************************************/
/**
 * e_data_book_cursor_sqlite_new:
 * @backend: the #EBookBackend creating this cursor
 * @ebsdb: the #EBookBackendSqliteDB object to base this cursor on
 * @folder_id: the folder identifier to be used in EBookBackendSqliteDB API calls
 * @sort_fields: (array length=n_fields): an array of #EContactFields as sort keys in order of priority
 * @sort_types: (array length=n_fields): an array of #EBookCursorSortTypes, one for each field in @sort_fields
 * @n_fields: the number of fields to sort results by.
 * @error: a return location to story any error that might be reported.
 *
 * Creates an #EDataBookCursor and implements all of the cursor methods
 * using the delegate @ebsdb object.
 *
 * This is a suitable cursor type for any backend which stores its contacts
 * using the #EBookBackendSqliteDB object.
 *
 * Returns: (transfer full): A newly created #EDataBookCursor, or %NULL if cursor creation failed.
 *
 * Since: 3.12
 */
EDataBookCursor *
e_data_book_cursor_sqlite_new (EBookBackend         *backend,
			       EBookBackendSqliteDB *ebsdb,
			       const gchar          *folder_id,
			       EContactField        *sort_fields,
			       EBookCursorSortType  *sort_types,
			       guint                 n_fields,
			       GError              **error)
{
	EDataBookCursor *cursor = NULL;
	EbSdbCursor     *ebsdb_cursor;
	GError          *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SQLITEDB (ebsdb), NULL);
	g_return_val_if_fail (folder_id && folder_id[0], NULL);

	ebsdb_cursor = e_book_backend_sqlitedb_cursor_new (ebsdb, folder_id, NULL,
							   sort_fields,
							   sort_types,
							   n_fields,
							   &local_error);

	if (ebsdb_cursor) {
		cursor = g_object_new (E_TYPE_DATA_BOOK_CURSOR_SQLITE,
				       "backend", backend,
				       "ebsdb", ebsdb,
				       "folder-id", folder_id,
				       "cursor", ebsdb_cursor,
				       NULL);

		/* Initially created cursors should have a position & total */
		if (!e_data_book_cursor_load_locale (E_DATA_BOOK_CURSOR (cursor),
						     NULL, error))
			g_clear_object (&cursor);

	} else if (g_error_matches (local_error,
				  E_BOOK_SDB_ERROR,
				  E_BOOK_SDB_ERROR_INVALID_QUERY)) {
		g_set_error_literal (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_INVALID_QUERY,
				     local_error->message);
		g_clear_error (&local_error);
	} else {
		g_propagate_error (error, local_error);
	}

	return cursor;
}
