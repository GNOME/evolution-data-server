/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* This is a helper class for folders to implement the search function.
   It implements enough to do basic searches on folders that can provide
   an in-memory summary and a body index. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* POSIX requires <sys/types.h> be included before <regex.h> */
#include <sys/types.h>

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-exception.h"
#include "camel-folder-search.h"
#include "camel-folder-thread.h"
#include "camel-iconv.h"
#include "camel-medium.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-search-private.h"
#include "camel-stream-mem.h"
#include "camel-db.h"
#include "camel-debug.h"
#include "camel-store.h"
#include "camel-vee-folder.h"
#include "camel-string-utils.h"
#include "camel-search-sql.h"
#include "camel-search-sql-sexp.h"

#define d(x)
#define r(x)
#define dd(x) if (camel_debug("search")) x

struct _CamelFolderSearchPrivate {
	CamelException *ex;

	CamelFolderThread *threads;
	GHashTable *threads_hash;
};

#define _PRIVATE(o) (((CamelFolderSearch *)(o))->priv)

static ESExpResult *search_not(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);

static ESExpResult *search_header_contains(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_matches(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_starts_with(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_ends_with(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_exists(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_soundex(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_regex(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_header_full_regex(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_match_all(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, CamelFolderSearch *search);
static ESExpResult *search_match_threads(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, CamelFolderSearch *s);
static ESExpResult *search_body_contains(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_body_regex(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);
static ESExpResult *search_user_flag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_user_tag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_system_flag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_get_sent_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_get_received_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_get_current_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_get_size(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_uid(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);
static ESExpResult *search_message_location(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s);

static ESExpResult *search_dummy(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search);

static void camel_folder_search_class_init (CamelFolderSearchClass *klass);
static void camel_folder_search_init       (CamelFolderSearch *obj);
static void camel_folder_search_finalize   (CamelObject *obj);

static gint read_uid_callback (gpointer  ref, gint ncol, gchar ** cols, gchar **name);

static CamelObjectClass *camel_folder_search_parent;

static void
camel_folder_search_class_init (CamelFolderSearchClass *klass)
{
	camel_folder_search_parent = camel_type_get_global_classfuncs (camel_object_get_type ());

	klass->not = search_not;

	klass->match_all = search_match_all;
	klass->match_threads = search_match_threads;
	klass->body_contains = search_body_contains;
	klass->body_regex = search_body_regex;
	klass->header_contains = search_header_contains;
	klass->header_matches = search_header_matches;
	klass->header_starts_with = search_header_starts_with;
	klass->header_ends_with = search_header_ends_with;
	klass->header_exists = search_header_exists;
	klass->header_soundex = search_header_soundex;
	klass->header_regex = search_header_regex;
	klass->header_full_regex = search_header_full_regex;
	klass->user_tag = search_user_tag;
	klass->user_flag = search_user_flag;
	klass->system_flag = search_system_flag;
	klass->get_sent_date = search_get_sent_date;
	klass->get_received_date = search_get_received_date;
	klass->get_current_date = search_get_current_date;
	klass->get_size = search_get_size;
	klass->uid = search_uid;
	klass->message_location = search_message_location;
}

static void
camel_folder_search_init (CamelFolderSearch *obj)
{
	struct _CamelFolderSearchPrivate *p;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	obj->sexp = e_sexp_new();
}

static void
camel_folder_search_finalize (CamelObject *obj)
{
	CamelFolderSearch *search = (CamelFolderSearch *)obj;
	struct _CamelFolderSearchPrivate *p = _PRIVATE(obj);

	if (search->sexp)
		e_sexp_unref(search->sexp);

	g_free(search->last_search);
	g_free(p);
}

CamelType
camel_folder_search_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_object_get_type (), "CamelFolderSearch",
					    sizeof (CamelFolderSearch),
					    sizeof (CamelFolderSearchClass),
					    (CamelObjectClassInitFunc) camel_folder_search_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_folder_search_init,
					    (CamelObjectFinalizeFunc) camel_folder_search_finalize);
	}

	return type;
}

#ifdef offsetof
#define CAMEL_STRUCT_OFFSET(type, field)        ((gint) offsetof (type, field))
#else
#define CAMEL_STRUCT_OFFSET(type, field)        ((gint) ((gchar *) &((type *) 0)->field))
#endif

static struct {
	const gchar *name;
	gint offset;
	gint flags;		/* 0x02 = immediate, 0x01 = always enter */
} builtins[] = {
	/* these have default implementations in e-sexp */
	{ "and", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, and), 2 },
	{ "or", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, or), 2 },
	/* we need to override this one though to implement an 'array not' */
	{ "not", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, not), 0 },
	{ "<", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, lt), 2 },
	{ ">", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, gt), 2 },
	{ "=", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, eq), 2 },

	/* these we have to use our own default if there is none */
	/* they should all be defined in the language? so it parses, or should they not?? */
	{ "match-all", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, match_all), 3 },
	{ "match-threads", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, match_threads), 3 },
	{ "body-contains", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, body_contains), 1 },
	{ "body-regex",  CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, body_regex), 1  },
	{ "header-contains", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_contains), 1 },
	{ "header-matches", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_matches), 1 },
	{ "header-starts-with", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_starts_with), 1 },
	{ "header-ends-with", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_ends_with), 1 },
	{ "header-exists", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_exists), 1 },
	{ "header-soundex", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_soundex), 1 },
	{ "header-regex", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_regex), 1 },
	{ "header-full-regex", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, header_full_regex), 1 },
	{ "user-tag", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, user_tag), 1 },
	{ "user-flag", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, user_flag), 1 },
	{ "system-flag", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, system_flag), 1 },
	{ "get-sent-date", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_sent_date), 1 },
	{ "get-received-date", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_received_date), 1 },
	{ "get-current-date", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_current_date), 1 },
	{ "get-size", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_size), 1 },
	{ "uid", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, uid), 1 },
	{ "message-location", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, message_location), 1 },
};

void
camel_folder_search_construct (CamelFolderSearch *search)
{
	gint i;
	CamelFolderSearchClass *klass = (CamelFolderSearchClass *)CAMEL_OBJECT_GET_CLASS(search);

	for (i = 0; i < G_N_ELEMENTS (builtins); i++) {
		gpointer func;
		/* c is sure messy sometimes */
		func = *((gpointer *)(((gchar *)klass)+builtins[i].offset));
		if (func == NULL && builtins[i].flags&1) {
			g_warning("Search class doesn't implement '%s' method: %s", builtins[i].name, camel_type_to_name(CAMEL_OBJECT_GET_CLASS(search)));
			func = (gpointer)search_dummy;
		}
		if (func != NULL) {
			if (builtins[i].flags&2) {
				e_sexp_add_ifunction(search->sexp, 0, builtins[i].name, (ESExpIFunc *)func, search);
			} else {
				e_sexp_add_function(search->sexp, 0, builtins[i].name, (ESExpFunc *)func, search);
			}
		}
	}
}

/**
 * camel_folder_search_new:
 *
 * Create a new CamelFolderSearch object.
 *
 * A CamelFolderSearch is a subclassable, extensible s-exp
 * evaluator which enforces a particular set of s-expressions.
 * Particular methods may be overriden by an implementation to
 * implement a search for any sort of backend.
 *
 * Return value: A new CamelFolderSearch widget.
 **/
CamelFolderSearch *
camel_folder_search_new (void)
{
	CamelFolderSearch *new = CAMEL_FOLDER_SEARCH (camel_object_new (camel_folder_search_get_type ()));

	camel_folder_search_construct(new);
	return new;
}

/**
 * camel_folder_search_set_folder:
 * @search:
 * @folder: A folder.
 *
 * Set the folder attribute of the search.  This is currently unused, but
 * could be used to perform a slow-search when indexes and so forth are not
 * available.  Or for use by subclasses.
 **/
void
camel_folder_search_set_folder(CamelFolderSearch *search, CamelFolder *folder)
{
	search->folder = folder;
}

/**
 * camel_folder_search_set_summary:
 * @search:
 * @summary: An array of CamelMessageInfo pointers.
 *
 * Set the array of summary objects representing the span of the search.
 *
 * If this is not set, then a subclass must provide the functions
 * for searching headers and for the match-all operator.
 **/
void
camel_folder_search_set_summary(CamelFolderSearch *search, GPtrArray *summary)
{
	search->summary = summary;
}

/**
 * camel_folder_search_set_body_index:
 * @search:
 * @index:
 *
 * Set the index representing the contents of all messages
 * in this folder.  If this is not set, then the folder implementation
 * should sub-class the CamelFolderSearch and provide its own
 * body-contains function.
 **/
void
camel_folder_search_set_body_index(CamelFolderSearch *search, CamelIndex *index)
{
	if (search->body_index)
		camel_object_unref((CamelObject *)search->body_index);
	search->body_index = index;
	if (index)
		camel_object_ref((CamelObject *)index);
}

/**
 * camel_folder_search_execute_expression:
 * @search:
 * @expr:
 * @ex:
 *
 * Execute the search expression @expr, returning an array of
 * all matches as a GPtrArray of uid's of matching messages.
 *
 * Note that any settings such as set_body_index(), set_folder(),
 * and so on are reset to #NULL once the search has completed.
 *
 * TODO: The interface should probably return summary items instead
 * (since they are much more useful to any client).
 *
 * Return value: A GPtrArray of strings of all matching messages.
 * This must only be freed by camel_folder_search_free_result.
 **/
GPtrArray *
camel_folder_search_execute_expression(CamelFolderSearch *search, const gchar *expr, CamelException *ex)
{
	ESExpResult *r;
	GPtrArray *matches;
	gint i;
	GHashTable *results;
	struct _CamelFolderSearchPrivate *p = _PRIVATE(search);

	p->ex = ex;

	/* only re-parse if the search has changed */
	if (search->last_search == NULL
	    || strcmp(search->last_search, expr)) {
		e_sexp_input_text(search->sexp, expr, strlen(expr));
		if (e_sexp_parse(search->sexp) == -1) {
			camel_exception_setv(ex, 1, _("Cannot parse search expression: %s:\n%s"), e_sexp_error(search->sexp), expr);
			return NULL;
		}

		g_free(search->last_search);
		search->last_search = g_strdup(expr);
	}
	r = e_sexp_eval(search->sexp);
	if (r == NULL) {
		if (!camel_exception_is_set(ex))
			camel_exception_setv(ex, 1, _("Error executing search expression: %s:\n%s"), e_sexp_error(search->sexp), expr);
		return NULL;
	}

	matches = g_ptr_array_new();

	/* now create a folder summary to return?? */
	if (r->type == ESEXP_RES_ARRAY_PTR) {
		d(printf("got result ...\n"));
		if (search->summary) {
			/* reorder result in summary order */
			results = g_hash_table_new(g_str_hash, g_str_equal);
			for (i=0;i<r->value.ptrarray->len;i++) {
				d(printf("adding match: %s\n", (gchar *)g_ptr_array_index(r->value.ptrarray, i)));
				g_hash_table_insert(results, g_ptr_array_index(r->value.ptrarray, i), GINT_TO_POINTER (1));
			}
			for (i=0;i<search->summary->len;i++) {
				gchar *uid = g_ptr_array_index(search->summary, i);
				if (g_hash_table_lookup(results, uid)) {
					g_ptr_array_add(matches, (gpointer) camel_pstring_strdup(uid));
				}
			}
			g_hash_table_destroy(results);
		} else {
			for (i=0;i<r->value.ptrarray->len;i++) {
				d(printf("adding match: %s\n", (gchar *)g_ptr_array_index(r->value.ptrarray, i)));
				g_ptr_array_add(matches, (gpointer) camel_pstring_strdup(g_ptr_array_index(r->value.ptrarray, i)));
			}
		}
	} else {
		g_warning("Search returned an invalid result type");
	}

	e_sexp_result_free(search->sexp, r);

	if (p->threads)
		camel_folder_thread_messages_unref(p->threads);
	if (p->threads_hash)
		g_hash_table_destroy(p->threads_hash);

	p->threads = NULL;
	p->threads_hash = NULL;
	search->folder = NULL;
	search->summary = NULL;
	search->current = NULL;
	search->body_index = NULL;

	return matches;
}

/**
 * camel_folder_search_count:
 * @search:
 * @expr:
 * @uids: to search against, NULL for all uid's.
 * @ex:
 *
 * Run a search.  Search must have had Folder already set on it, and
 * it must implement summaries.
 *
 * Return value: Number of messages that match the query.
 **/

guint32
camel_folder_search_count(CamelFolderSearch *search, const gchar *expr, CamelException *ex)
{
	ESExpResult *r;
	GPtrArray *summary_set;
	gint i;
	CamelDB *cdb;
	gchar *sql_query, *tmp, *tmp1;
	GHashTable *results;
	guint32 count = 0;

	struct _CamelFolderSearchPrivate *p = _PRIVATE(search);

	g_assert(search->folder);

	p->ex = ex;

	/* We route body-contains search and thread based search through memory and not via db. */
	if (strstr((const gchar *) expr, "body-contains") || strstr((const gchar *) expr, "match-threads")) {
		/* setup our search list only contains those we're interested in */
		search->summary = camel_folder_get_summary(search->folder);

		summary_set = search->summary;

		/* only re-parse if the search has changed */
		if (search->last_search == NULL
		    || strcmp(search->last_search, expr)) {
			e_sexp_input_text(search->sexp, expr, strlen(expr));
			if (e_sexp_parse(search->sexp) == -1) {
				camel_exception_setv(ex, 1, _("Cannot parse search expression: %s:\n%s"), e_sexp_error(search->sexp), expr);
				goto fail;
			}

			g_free(search->last_search);
			search->last_search = g_strdup(expr);
		}
		r = e_sexp_eval(search->sexp);
		if (r == NULL) {
			if (!camel_exception_is_set(ex))
				camel_exception_setv(ex, 1, _("Error executing search expression: %s:\n%s"), e_sexp_error(search->sexp), expr);
			goto fail;
		}

		/* now create a folder summary to return?? */
		if (r->type == ESEXP_RES_ARRAY_PTR) {
			d(printf("got result\n"));

			/* reorder result in summary order */
			results = g_hash_table_new(g_str_hash, g_str_equal);
			for (i=0;i<r->value.ptrarray->len;i++) {
				d(printf("adding match: %s\n", (gchar *)g_ptr_array_index(r->value.ptrarray, i)));
				g_hash_table_insert(results, g_ptr_array_index(r->value.ptrarray, i), GINT_TO_POINTER (1));
			}

			for (i=0;i<summary_set->len;i++) {
				gchar *uid = g_ptr_array_index(summary_set, i);
				if (g_hash_table_lookup(results, uid))
					count++;
			}
			g_hash_table_destroy(results);
		}

		e_sexp_result_free(search->sexp, r);

	} else {
		/* Sync the db, so that we search the db for changes */
		camel_folder_summary_save_to_db (search->folder->summary, ex);

		dd(printf ("sexp is : [%s]\n", expr));
		if (g_getenv("SQL_SEARCH_OLD"))
			sql_query = camel_sexp_to_sql (expr);
		else
			sql_query = camel_sexp_to_sql_sexp (expr);
		tmp1 = camel_db_sqlize_string(search->folder->full_name);
		tmp = g_strdup_printf ("SELECT COUNT (*) FROM %s %s %s", tmp1, sql_query ? "WHERE":"", sql_query?sql_query:"");
		camel_db_free_sqlized_string (tmp1);
		g_free (sql_query);
		dd(printf("Equivalent sql %s\n", tmp));

		cdb = (CamelDB *) (search->folder->parent_store->cdb_r);
		camel_db_count_message_info  (cdb, tmp, &count, ex);
		if (ex && camel_exception_is_set(ex)) {
			const gchar *exception = camel_exception_get_description (ex);
			if (strncmp(exception, "no such table", 13) == 0) {
				d(g_warning ("Error during searching %s: %s\n", tmp, exception));
				camel_exception_clear (ex); /* Suppress no such table */
			}
		}
		g_free (tmp);

	}

fail:
	/* these might be allocated by match-threads */
	if (p->threads)
		camel_folder_thread_messages_unref(p->threads);
	if (p->threads_hash)
		g_hash_table_destroy(p->threads_hash);
	if (search->summary_set)
		g_ptr_array_free(search->summary_set, TRUE);
	if (search->summary)
		camel_folder_free_summary(search->folder, search->summary);

	p->threads = NULL;
	p->threads_hash = NULL;
	search->folder = NULL;
	search->summary = NULL;
	search->summary_set = NULL;
	search->current = NULL;
	search->body_index = NULL;

	return count;
}

static gboolean
do_search_in_memory (const gchar *expr)
{
	/* if the expression contains any of these tokens, then perform a memory search, instead of the SQL one */
	const gchar *in_memory_tokens[] = { "body-contains", "body-regex", "match-threads", "message-location", "header-soundex", "header-regex", "header-full-regex", "header-contains", NULL };
	gint i;

	if (!expr)
		return FALSE;

	for (i = 0; in_memory_tokens [i]; i++) {
		if (strstr (expr, in_memory_tokens [i]))
			return TRUE;
	}

	return FALSE;
}

/**
 * camel_folder_search_search:
 * @search:
 * @expr:
 * @uids: to search against, NULL for all uid's.
 * @ex:
 *
 * Run a search.  Search must have had Folder already set on it, and
 * it must implement summaries.
 *
 * Return value:
 **/
GPtrArray *
camel_folder_search_search(CamelFolderSearch *search, const gchar *expr, GPtrArray *uids, CamelException *ex)
{
	ESExpResult *r;
	GPtrArray *matches = NULL, *summary_set;
	gint i;
	CamelDB *cdb;
	gchar *sql_query, *tmp, *tmp1;
	GHashTable *results;

	struct _CamelFolderSearchPrivate *p = _PRIVATE(search);

	g_assert(search->folder);

	p->ex = ex;

	/* We route body-contains / thread based search and uid search through memory and not via db. */
	if (uids || do_search_in_memory (expr)) {
		/* setup our search list only contains those we're interested in */
		search->summary = camel_folder_get_summary(search->folder);

		if (uids) {
			GHashTable *uids_hash = g_hash_table_new(g_str_hash, g_str_equal);

			summary_set = search->summary_set = g_ptr_array_new();
			for (i=0;i<uids->len;i++)
				g_hash_table_insert(uids_hash, uids->pdata[i], uids->pdata[i]);
			for (i=0;i<search->summary->len;i++)
				if (g_hash_table_lookup(uids_hash, search->summary->pdata[i]))
					g_ptr_array_add(search->summary_set, search->summary->pdata[i]);
			g_hash_table_destroy(uids_hash);
		} else {
			summary_set = search->summary;
		}

		/* only re-parse if the search has changed */
		if (search->last_search == NULL
		    || strcmp(search->last_search, expr)) {
			e_sexp_input_text(search->sexp, expr, strlen(expr));
			if (e_sexp_parse(search->sexp) == -1) {
				camel_exception_setv(ex, 1, _("Cannot parse search expression: %s:\n%s"), e_sexp_error(search->sexp), expr);
				goto fail;
			}

			g_free(search->last_search);
			search->last_search = g_strdup(expr);
		}
		r = e_sexp_eval(search->sexp);
		if (r == NULL) {
			if (!camel_exception_is_set(ex))
				camel_exception_setv(ex, 1, _("Error executing search expression: %s:\n%s"), e_sexp_error(search->sexp), expr);
			goto fail;
		}

		matches = g_ptr_array_new();

		/* now create a folder summary to return?? */
		if (r->type == ESEXP_RES_ARRAY_PTR) {
			d(printf("got result\n"));

			/* reorder result in summary order */
			results = g_hash_table_new(g_str_hash, g_str_equal);
			for (i=0;i<r->value.ptrarray->len;i++) {
				d(printf("adding match: %s\n", (gchar *)g_ptr_array_index(r->value.ptrarray, i)));
				g_hash_table_insert(results, g_ptr_array_index(r->value.ptrarray, i), GINT_TO_POINTER (1));
			}

			for (i=0;i<summary_set->len;i++) {
				gchar *uid = g_ptr_array_index(summary_set, i);
				if (g_hash_table_lookup(results, uid))
					g_ptr_array_add(matches, (gpointer) camel_pstring_strdup(uid));
			}
			g_hash_table_destroy(results);
		}

		e_sexp_result_free(search->sexp, r);

	} else {
		/* Sync the db, so that we search the db for changes */
		camel_folder_summary_save_to_db (search->folder->summary, ex);

		dd(printf ("sexp is : [%s]\n", expr));
		if (g_getenv("SQL_SEARCH_OLD"))
			sql_query = camel_sexp_to_sql (expr);
		else
			sql_query = camel_sexp_to_sql_sexp (expr);
		tmp1 = camel_db_sqlize_string(search->folder->full_name);
		tmp = g_strdup_printf ("SELECT uid FROM %s %s %s", tmp1, sql_query ? "WHERE":"", sql_query?sql_query:"");
		camel_db_free_sqlized_string (tmp1);
		g_free (sql_query);
		dd(printf("Equivalent sql %s\n", tmp));

		matches = g_ptr_array_new();
		cdb = (CamelDB *) (search->folder->parent_store->cdb_r);
		camel_db_select (cdb, tmp, (CamelDBSelectCB) read_uid_callback, matches, ex);
		if (ex && camel_exception_is_set(ex)) {
			const gchar *exception = camel_exception_get_description (ex);
			if (strncmp(exception, "no such table", 13) == 0) {
				d(g_warning ("Error during searching %s: %s\n", tmp, exception));
				camel_exception_clear (ex); /* Suppress no such table */
			}
		}
		g_free (tmp);

	}

fail:
	/* these might be allocated by match-threads */
	if (p->threads)
		camel_folder_thread_messages_unref(p->threads);
	if (p->threads_hash)
		g_hash_table_destroy(p->threads_hash);
	if (search->summary_set)
		g_ptr_array_free(search->summary_set, TRUE);
	if (search->summary)
		camel_folder_free_summary(search->folder, search->summary);

	p->threads = NULL;
	p->threads_hash = NULL;
	search->folder = NULL;
	search->summary = NULL;
	search->summary_set = NULL;
	search->current = NULL;
	search->body_index = NULL;

	return matches;
}

void camel_folder_search_free_result(CamelFolderSearch *search, GPtrArray *result)
{
	g_ptr_array_foreach (result, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free(result, TRUE);
}

/* dummy function, returns false always, or an empty match array */
static ESExpResult *
search_dummy(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;

	if (search->current == NULL) {
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();
	}

	return r;
}

/* impelemnt an 'array not', i.e. everything in the summary, not in the supplied array */
static ESExpResult *
search_not(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	gint i;

	if (argc>0) {
		if (argv[0]->type == ESEXP_RES_ARRAY_PTR) {
			GPtrArray *v = argv[0]->value.ptrarray;
			const gchar *uid;

			r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
			r->value.ptrarray = g_ptr_array_new();

			/* not against a single message?*/
			if (search->current) {
				gint found = FALSE;

				uid = camel_message_info_uid(search->current);
				for (i=0;!found && i<v->len;i++) {
					if (strcmp(uid, v->pdata[i]) == 0)
						found = TRUE;
				}

				if (!found)
					g_ptr_array_add(r->value.ptrarray, (gchar *)uid);
			} else if (search->summary == NULL) {
				g_warning("No summary set, 'not' against an array requires a summary");
			} else {
				/* 'not' against the whole summary */
				GHashTable *have = g_hash_table_new(g_str_hash, g_str_equal);
				gchar **s;
				gchar **m;

				s = (gchar **)v->pdata;
				for (i=0;i<v->len;i++)
					g_hash_table_insert(have, s[i], s[i]);

				v = search->summary_set?search->summary_set:search->summary;
				m = (gchar **)v->pdata;
				for (i=0;i<v->len;i++) {
					gchar *uid = m[i];

					if (g_hash_table_lookup(have, uid) == NULL)
						g_ptr_array_add(r->value.ptrarray, uid);
				}
				g_hash_table_destroy(have);
			}
		} else {
			gint res = TRUE;

			if (argv[0]->type == ESEXP_RES_BOOL)
				res = !argv[0]->value.bool;

			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = res;
		}
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = TRUE;
	}

	return r;
}

static ESExpResult *
search_match_all(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, CamelFolderSearch *search)
{
	gint i;
	ESExpResult *r, *r1;
	gchar *error_msg;
	GPtrArray *v;

	if (argc>1) {
		g_warning("match-all only takes a single argument, other arguments ignored");
	}

	/* we are only matching a single message?  or already inside a match-all? */
	if (search->current) {
		d(printf("matching against 1 message: %s\n", camel_message_info_subject(search->current)));

		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;

		if (argc>0) {
			r1 = e_sexp_term_eval(f, argv[0]);
			if (r1->type == ESEXP_RES_BOOL) {
				r->value.bool = r1->value.bool;
			} else {
				g_warning("invalid syntax, matches require a single bool result");
				error_msg = g_strdup_printf(_("(%s) requires a single bool result"), "match-all");
				e_sexp_fatal_error(f, error_msg);
				g_free(error_msg);
			}
			e_sexp_result_free(f, r1);
		} else {
			r->value.bool = TRUE;
		}
		return r;
	}

	r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
	r->value.ptrarray = g_ptr_array_new();

	if (search->summary == NULL) {
		/* TODO: make it work - e.g. use the folder and so forth for a slower search */
		g_warning("No summary supplied, match-all doesn't work with no summary");
		g_assert(0);
		return r;
	}

	v = search->summary_set?search->summary_set:search->summary;

	if (v->len > g_hash_table_size (search->folder->summary->loaded_infos) && !CAMEL_IS_VEE_FOLDER (search->folder)) {
		camel_folder_summary_reload_from_db (search->folder->summary, search->priv->ex);
	}

	for (i=0;i<v->len;i++) {
		const gchar *uid;

		search->current = camel_folder_summary_uid (search->folder->summary, v->pdata[i]);
		if (!search->current)
			continue;
		uid = camel_message_info_uid(search->current);

		if (argc>0) {
			r1 = e_sexp_term_eval(f, argv[0]);
			if (r1->type == ESEXP_RES_BOOL) {
				if (r1->value.bool)
					g_ptr_array_add(r->value.ptrarray, (gchar *)uid);
			} else {
				g_warning("invalid syntax, matches require a single bool result");
				error_msg = g_strdup_printf(_("(%s) requires a single bool result"), "match-all");
				e_sexp_fatal_error(f, error_msg);
				g_free(error_msg);
			}
			e_sexp_result_free(f, r1);
		} else {
			g_ptr_array_add(r->value.ptrarray, (gchar *)uid);
		}
		camel_message_info_free (search->current);
	}
	search->current = NULL;
	return r;
}

static void
fill_thread_table(struct _CamelFolderThreadNode *root, GHashTable *id_hash)
{
	while (root) {
		g_hash_table_insert(id_hash, (gchar *)camel_message_info_uid(root->message), root);
		if (root->child)
			fill_thread_table(root->child, id_hash);
		root = root->next;
	}
}

static void
add_thread_results(struct _CamelFolderThreadNode *root, GHashTable *result_hash)
{
	while (root) {
		g_hash_table_insert(result_hash, (gchar *)camel_message_info_uid(root->message), GINT_TO_POINTER (1));
		if (root->child)
			add_thread_results(root->child, result_hash);
		root = root->next;
	}
}

static void
add_results(gchar *uid, gpointer dummy, GPtrArray *result)
{
	g_ptr_array_add(result, uid);
}

static ESExpResult *
search_match_threads(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	struct _CamelFolderSearchPrivate *p = search->priv;
	gint i, type;
	GHashTable *results;
	gchar *error_msg;

	/* not supported in match-all */
	if (search->current) {
		error_msg = g_strdup_printf(_("(%s) not allowed inside %s"), "match-threads", "match-all");
		e_sexp_fatal_error(f, error_msg);
		g_free(error_msg);
	}

	if (argc == 0) {
		error_msg = g_strdup_printf(_("(%s) requires a match type string"), "match-threads");
		e_sexp_fatal_error(f, error_msg);
		g_free(error_msg);
	}

	r = e_sexp_term_eval(f, argv[0]);
	if (r->type != ESEXP_RES_STRING) {
		error_msg = g_strdup_printf(_("(%s) requires a match type string"), "match-threads");
		e_sexp_fatal_error(f, error_msg);
		g_free(error_msg);
	}

	type = 0;
	if (!strcmp(r->value.string, "none"))
		type = 0;
	else if (!strcmp(r->value.string, "all"))
		type = 1;
	else if (!strcmp(r->value.string, "replies"))
		type = 2;
	else if (!strcmp(r->value.string, "replies_parents"))
		type = 3;
	else if (!strcmp(r->value.string, "single"))
		type = 4;
	e_sexp_result_free(f, r);

	/* behave as (begin does */
	r = NULL;
	for (i=1;i<argc;i++) {
		if (r)
			e_sexp_result_free(f, r);
		r = e_sexp_term_eval(f, argv[i]);
	}

	if (r == NULL || r->type != ESEXP_RES_ARRAY_PTR) {
		error_msg = g_strdup_printf(_("(%s) expects an array result"), "match-threads");
		e_sexp_fatal_error(f, error_msg);
		g_free(error_msg);
	}

	if (type == 0)
		return r;

	if (search->folder == NULL) {
		error_msg = g_strdup_printf(_("(%s) requires the folder set"), "match-threads");
		e_sexp_fatal_error(f, error_msg);
		g_free(error_msg);
	}

	/* cache this, so we only have to re-calculate once per search at most */
	if (p->threads == NULL) {
		p->threads = camel_folder_thread_messages_new(search->folder, NULL, TRUE);
		p->threads_hash = g_hash_table_new(g_str_hash, g_str_equal);

		fill_thread_table(p->threads->tree, p->threads_hash);
	}

	results = g_hash_table_new(g_str_hash, g_str_equal);
	for (i=0;i<r->value.ptrarray->len;i++) {
		struct _CamelFolderThreadNode *node, *scan;

		if (type != 4)
			g_hash_table_insert(results, g_ptr_array_index(r->value.ptrarray, i), GINT_TO_POINTER(1));

		node = g_hash_table_lookup(p->threads_hash, (gchar *)g_ptr_array_index(r->value.ptrarray, i));
		if (node == NULL) /* this shouldn't happen but why cry over spilt milk */
			continue;

		/* select messages in thread according to search criteria */
		if (type == 4) {
			if (node->child == NULL && node->parent == NULL)
				g_hash_table_insert(results, (gchar *)camel_message_info_uid(node->message), GINT_TO_POINTER(1));
		} else {
			if (type == 3) {
				scan = node;
				while (scan && scan->parent) {
					scan = scan->parent;
					g_hash_table_insert(results, (gchar *)camel_message_info_uid(scan->message), GINT_TO_POINTER(1));
				}
			} else if (type == 1) {
				while (node && node->parent)
					node = node->parent;
			}
			g_hash_table_insert(results, (gchar *)camel_message_info_uid(node->message), GINT_TO_POINTER(1));
			if (node->child)
				add_thread_results(node->child, results);
		}
	}
	e_sexp_result_free(f, r);

	r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
	r->value.ptrarray = g_ptr_array_new();

	g_hash_table_foreach(results, (GHFunc)add_results, r->value.ptrarray);
	g_hash_table_destroy(results);

	return r;
}

static CamelMimeMessage *
get_current_message (CamelFolderSearch *search)
{
	CamelException x = CAMEL_EXCEPTION_INITIALISER;
	CamelMimeMessage *res;

	if (!search || !search->folder || !search->current)
		return NULL;

	res = camel_folder_get_message (search->folder, search->current->uid, &x);

	if (!res)
		camel_exception_clear (&x);

	return res;
}

static ESExpResult *
check_header (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search, camel_search_match_t how)
{
	ESExpResult *r;
	gint truth = FALSE;

	r(printf("executing check-header %d\n", how));

	/* are we inside a match-all? */
	if (search->current && argc>1
	    && argv[0]->type == ESEXP_RES_STRING) {
		gchar *headername;
		const gchar *header = NULL, *charset = NULL;
		gchar strbuf[32];
		gint i, j;
		camel_search_t type = CAMEL_SEARCH_TYPE_ASIS;
		struct _camel_search_words *words;
		CamelMimeMessage *message = NULL;
		struct _camel_header_raw *raw_header;

		/* only a subset of headers are supported .. */
		headername = argv[0]->value.string;
		if (!g_ascii_strcasecmp(headername, "subject")) {
			header = camel_message_info_subject(search->current);
		} else if (!g_ascii_strcasecmp(headername, "date")) {
			/* FIXME: not a very useful form of the date */
			sprintf(strbuf, "%d", (gint)camel_message_info_date_sent(search->current));
			header = strbuf;
		} else if (!g_ascii_strcasecmp(headername, "from")) {
			header = camel_message_info_from(search->current);
			type = CAMEL_SEARCH_TYPE_ADDRESS;
		} else if (!g_ascii_strcasecmp(headername, "to")) {
			header = camel_message_info_to(search->current);
			type = CAMEL_SEARCH_TYPE_ADDRESS;
		} else if (!g_ascii_strcasecmp(headername, "cc")) {
			header = camel_message_info_cc(search->current);
			type = CAMEL_SEARCH_TYPE_ADDRESS;
		} else if (!g_ascii_strcasecmp(headername, "x-camel-mlist")) {
			header = camel_message_info_mlist(search->current);
			type = CAMEL_SEARCH_TYPE_MLIST;
		} else {
			message = get_current_message (search);
			if (message) {
				CamelContentType *ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));

				if (ct) {
					charset = camel_content_type_param (ct, "charset");
					charset = camel_iconv_charset_name (charset);
				}
			}
		}

		if (header == NULL)
			header = "";

		/* performs an OR of all words */
		for (i=1;i<argc && !truth;i++) {
			if (argv[i]->type == ESEXP_RES_STRING) {
				if (argv[i]->value.string[0] == 0) {
					truth = TRUE;
				} else if (how == CAMEL_SEARCH_MATCH_CONTAINS) {
					/* doesn't make sense to split words on anything but contains i.e. we can't have an ending match different words */
					words = camel_search_words_split((const guchar *) argv[i]->value.string);
					truth = TRUE;
					for (j=0;j<words->len && truth;j++) {
						if (message) {
							for (raw_header = ((CamelMimePart *)message)->headers; raw_header; raw_header = raw_header->next) {
								if (!g_ascii_strcasecmp (raw_header->name, headername)) {
									if (camel_search_header_match (raw_header->value, words->words[j]->word, how, type, charset))
										break;;
								}
							}

							truth = raw_header != NULL;
						} else
							truth = camel_search_header_match(header, words->words[j]->word, how, type, charset);
					}
					camel_search_words_free(words);
				} else {
					if (message) {
						for (raw_header = ((CamelMimePart *)message)->headers; raw_header && !truth; raw_header = raw_header->next) {
							if (!g_ascii_strcasecmp (raw_header->name, headername)) {
								truth = camel_search_header_match(raw_header->value, argv[i]->value.string, how, type, charset);
							}
						}
					} else
						truth = camel_search_header_match(header, argv[i]->value.string, how, type, charset);
				}
			}
		}

		if (message)
			camel_object_unref (message);
	}
	/* TODO: else, find all matches */

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = truth;

	return r;
}

/*
static void
l_printf(gchar *node)
{
printf("%s\t", node);
}
*/

static ESExpResult *
search_header_contains(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	return check_header(f, argc, argv, search, CAMEL_SEARCH_MATCH_CONTAINS);
}

static ESExpResult *
search_header_matches(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	return check_header(f, argc, argv, search, CAMEL_SEARCH_MATCH_EXACT);
}

static ESExpResult *
search_header_starts_with (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	return check_header(f, argc, argv, search, CAMEL_SEARCH_MATCH_STARTS);
}

static ESExpResult *
search_header_ends_with (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	return check_header(f, argc, argv, search, CAMEL_SEARCH_MATCH_ENDS);
}

static ESExpResult *
search_header_exists (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;

	r(printf ("executing header-exists\n"));

	if (search->current) {
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		if (argc == 1 && argv[0]->type == ESEXP_RES_STRING)
			r->value.bool = camel_medium_get_header(CAMEL_MEDIUM(search->current), argv[0]->value.string) != NULL;

	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();
	}

	return r;
}

static ESExpResult *
search_header_soundex (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	return check_header (f, argc, argv, search, CAMEL_SEARCH_MATCH_SOUNDEX);
}

static ESExpResult *
search_header_regex (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	CamelMimeMessage *msg;

	msg = get_current_message (search);

	if (msg) {
		regex_t pattern;
		const gchar *contents;

		r = e_sexp_result_new (f, ESEXP_RES_BOOL);

		if (argc > 1 && argv[0]->type == ESEXP_RES_STRING
		    && (contents = camel_medium_get_header (CAMEL_MEDIUM (msg), argv[0]->value.string))
		    && camel_search_build_match_regex (&pattern, CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_ICASE, argc-1, argv+1, search->priv->ex) == 0) {
			r->value.bool = regexec (&pattern, contents, 0, NULL, 0) == 0;
			regfree (&pattern);
		} else
			r->value.bool = FALSE;

		camel_object_unref (msg);
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();
	}

	return r;
}

static gchar *
get_full_header (CamelMimeMessage *message)
{
	CamelMimePart *mp = CAMEL_MIME_PART (message);
	GString *str = g_string_new ("");
	struct _camel_header_raw *h;

	for (h = mp->headers; h; h = h->next) {
		if (h->value != NULL) {
			g_string_append (str, h->name);
			if (isspace (h->value[0]))
				g_string_append (str, ":");
			else
				g_string_append (str, ": ");
			g_string_append (str, h->value);
			g_string_append_c (str, '\n');
		}
	}

	return g_string_free (str, FALSE);
}

static ESExpResult *
search_header_full_regex (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	CamelMimeMessage *msg;

	msg = get_current_message (search);

	if (msg) {
		regex_t pattern;

		r = e_sexp_result_new (f, ESEXP_RES_BOOL);

		if (camel_search_build_match_regex (&pattern, CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_ICASE|CAMEL_SEARCH_MATCH_NEWLINE, argc, argv, search->priv->ex) == 0) {
			gchar *contents;

			contents = get_full_header (msg);
			r->value.bool = regexec (&pattern, contents, 0, NULL, 0) == 0;

			g_free (contents);
			regfree (&pattern);
		} else
			r->value.bool = FALSE;

		camel_object_unref (msg);
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();
	}

	return r;
}

/* this is just to OR results together */
struct IterData {
	gint count;
	GPtrArray *uids;
};

/* or, store all unique values */
static void
htor(gchar *key, gint value, struct IterData *iter_data)
{
	g_ptr_array_add(iter_data->uids, key);
}

/* and, only store duplicates */
static void
htand(gchar *key, gint value, struct IterData *iter_data)
{
	if (value == iter_data->count)
		g_ptr_array_add(iter_data->uids, key);
}

static gint
match_message_index(CamelIndex *idx, const gchar *uid, const gchar *match, CamelException *ex)
{
	CamelIndexCursor *wc, *nc;
	const gchar *word, *name;
	gint truth = FALSE;

	wc = camel_index_words(idx);
	if (wc) {
		while (!truth && (word = camel_index_cursor_next(wc))) {
			if (camel_ustrstrcase(word,match) != NULL) {
				/* perf: could have the wc cursor return the name cursor */
				nc = camel_index_find(idx, word);
				if (nc) {
					while (!truth && (name = camel_index_cursor_next(nc)))
						truth = strcmp(name, uid) == 0;
					camel_object_unref((CamelObject *)nc);
				}
			}
		}
		camel_object_unref((CamelObject *)wc);
	}

	return truth;
}

/*
 "one two" "three" "four five"

  one and two
or
  three
or
  four and five
*/

/* returns messages which contain all words listed in words */
static GPtrArray *
match_words_index(CamelFolderSearch *search, struct _camel_search_words *words, CamelException *ex)
{
	GPtrArray *result = g_ptr_array_new();
	GHashTable *ht = g_hash_table_new(g_str_hash, g_str_equal);
	struct IterData lambdafoo;
	CamelIndexCursor *wc, *nc;
	const gchar *word, *name;
	gint i;

	/* we can have a maximum of 32 words, as we use it as the AND mask */

	wc = camel_index_words(search->body_index);
	if (wc) {
		while ((word = camel_index_cursor_next(wc))) {
			for (i=0;i<words->len;i++) {
				if (camel_ustrstrcase(word, words->words[i]->word) != NULL) {
					/* perf: could have the wc cursor return the name cursor */
					nc = camel_index_find(search->body_index, word);
					if (nc) {
						while ((name = camel_index_cursor_next(nc))) {
								gint mask;

								mask = (GPOINTER_TO_INT(g_hash_table_lookup(ht, name))) | (1<<i);
								g_hash_table_insert(ht, (gchar *) camel_pstring_peek(name), GINT_TO_POINTER(mask));
						}
						camel_object_unref((CamelObject *)nc);
					}
				}
			}
		}
		camel_object_unref((CamelObject *)wc);

		lambdafoo.uids = result;
		lambdafoo.count = (1<<words->len) - 1;
		g_hash_table_foreach(ht, (GHFunc)htand, &lambdafoo);
		g_hash_table_destroy(ht);
	}

	return result;
}

static gboolean
match_words_1message (CamelDataWrapper *object, struct _camel_search_words *words, guint32 *mask)
{
	CamelDataWrapper *containee;
	gint truth = FALSE;
	gint parts, i;

	containee = camel_medium_get_content_object (CAMEL_MEDIUM (object));

	if (containee == NULL)
		return FALSE;

	/* using the object types is more accurate than using the mime/types */
	if (CAMEL_IS_MULTIPART (containee)) {
		parts = camel_multipart_get_number (CAMEL_MULTIPART (containee));
		for (i = 0; i < parts && truth == FALSE; i++) {
			CamelDataWrapper *part = (CamelDataWrapper *)camel_multipart_get_part (CAMEL_MULTIPART (containee), i);
			if (part)
				truth = match_words_1message(part, words, mask);
		}
	} else if (CAMEL_IS_MIME_MESSAGE (containee)) {
		/* for messages we only look at its contents */
		truth = match_words_1message((CamelDataWrapper *)containee, words, mask);
	} else if (camel_content_type_is(CAMEL_DATA_WRAPPER (containee)->mime_type, "text", "*")) {
		/* for all other text parts, we look inside, otherwise we dont care */
		CamelStreamMem *mem = (CamelStreamMem *)camel_stream_mem_new ();

		/* FIXME: The match should be part of a stream op */
		camel_data_wrapper_decode_to_stream (containee, CAMEL_STREAM (mem));
		camel_stream_write (CAMEL_STREAM (mem), "", 1);
		for (i=0;i<words->len;i++) {
			/* FIXME: This is horridly slow, and should use a real search algorithm */
			if (camel_ustrstrcase((const gchar *) mem->buffer->data, words->words[i]->word) != NULL) {
				*mask |= (1<<i);
				/* shortcut a match */
				if (*mask == (1<<(words->len))-1)
					return TRUE;
			}
		}

		camel_object_unref (mem);
	}

	return truth;
}

static gboolean
match_words_message(CamelFolder *folder, const gchar *uid, struct _camel_search_words *words, CamelException *ex)
{
	guint32 mask;
	CamelMimeMessage *msg;
	CamelException x = CAMEL_EXCEPTION_INITIALISER;
	gint truth;

	msg = camel_folder_get_message(folder, uid, &x);
	if (msg) {
		mask = 0;
		truth = match_words_1message((CamelDataWrapper *)msg, words, &mask);
		camel_object_unref((CamelObject *)msg);
	} else {
		camel_exception_clear(&x);
		truth = FALSE;
	}

	return truth;
}

static GPtrArray *
match_words_messages(CamelFolderSearch *search, struct _camel_search_words *words, CamelException *ex)
{
	gint i;
	GPtrArray *matches = g_ptr_array_new();

	if (search->body_index) {
		GPtrArray *indexed;
		struct _camel_search_words *simple;

		simple = camel_search_words_simple(words);
		indexed = match_words_index(search, simple, ex);
		camel_search_words_free(simple);

		for (i=0;i<indexed->len;i++) {
			const gchar *uid = g_ptr_array_index(indexed, i);

			if (match_words_message(search->folder, uid, words, ex))
				g_ptr_array_add(matches, (gchar *)uid);
		}

		g_ptr_array_free(indexed, TRUE);
	} else {
		GPtrArray *v = search->summary_set?search->summary_set:search->summary;

		for (i=0;i<v->len;i++) {
			gchar *uid  = g_ptr_array_index(v, i);

			if (match_words_message(search->folder, uid, words, ex))
				g_ptr_array_add(matches, (gchar *)uid);
		}
	}

	return matches;
}

static ESExpResult *
search_body_contains(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	gint i, j;
	CamelException *ex = search->priv->ex;
	struct _camel_search_words *words;
	ESExpResult *r;
	struct IterData lambdafoo;

	if (search->current) {
		gint truth = FALSE;

		if (argc == 1 && argv[0]->value.string[0] == 0) {
			truth = TRUE;
		} else {
			for (i=0;i<argc && !truth;i++) {
				if (argv[i]->type == ESEXP_RES_STRING) {
					words = camel_search_words_split((const guchar *) argv[i]->value.string);
					truth = TRUE;
					if ((words->type & CAMEL_SEARCH_WORD_COMPLEX) == 0 && search->body_index) {
						for (j=0;j<words->len && truth;j++)
							truth = match_message_index(search->body_index, camel_message_info_uid(search->current), words->words[j]->word, ex);
					} else {
						/* TODO: cache current message incase of multiple body search terms */
						truth = match_words_message(search->folder, camel_message_info_uid(search->current), words, ex);
					}
					camel_search_words_free(words);
				}
			}
		}
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = truth;
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();

		if (argc == 1 && argv[0]->value.string[0] == 0) {
			GPtrArray *v = search->summary_set?search->summary_set:search->summary;

			for (i=0;i<v->len;i++) {
				gchar *uid = g_ptr_array_index(v, i);

				g_ptr_array_add(r->value.ptrarray, uid);
			}
		} else {
			GHashTable *ht = g_hash_table_new(g_str_hash, g_str_equal);
			GPtrArray *matches;

			for (i=0;i<argc;i++) {
				if (argv[i]->type == ESEXP_RES_STRING) {
					words = camel_search_words_split((const guchar *) argv[i]->value.string);
					if ((words->type & CAMEL_SEARCH_WORD_COMPLEX) == 0 && search->body_index) {
						matches = match_words_index(search, words, ex);
					} else {
						matches = match_words_messages(search, words, ex);
					}
					for (j=0;j<matches->len;j++) {
						g_hash_table_insert(ht, matches->pdata[j], matches->pdata[j]);
					}
					g_ptr_array_free(matches, TRUE);
					camel_search_words_free(words);
				}
			}
			lambdafoo.uids = r->value.ptrarray;
			g_hash_table_foreach(ht, (GHFunc)htor, &lambdafoo);
			g_hash_table_destroy(ht);
		}
	}

	return r;
}

static ESExpResult *
search_body_regex (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	CamelMimeMessage *msg = get_current_message (search);

	if (msg) {
		regex_t pattern;

		r = e_sexp_result_new (f, ESEXP_RES_BOOL);

		if (camel_search_build_match_regex (&pattern, CAMEL_SEARCH_MATCH_ICASE|CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_NEWLINE, argc, argv, search->priv->ex) == 0) {
			r->value.bool = camel_search_message_body_contains ((CamelDataWrapper *) msg, &pattern);
			regfree (&pattern);
		} else
			r->value.bool = FALSE;

		camel_object_unref (msg);
	} else {
		regex_t pattern;

		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();

		if (camel_search_build_match_regex (&pattern, CAMEL_SEARCH_MATCH_ICASE|CAMEL_SEARCH_MATCH_REGEX|CAMEL_SEARCH_MATCH_NEWLINE, argc, argv, search->priv->ex) == 0) {
			gint i;
			GPtrArray *v = search->summary_set?search->summary_set:search->summary;
			CamelException x = CAMEL_EXCEPTION_INITIALISER;
			CamelMimeMessage *message;

			for (i = 0; i < v->len; i++) {
				gchar *uid = g_ptr_array_index(v, i);

				message = camel_folder_get_message (search->folder, uid, &x);
				if (message) {
					if (camel_search_message_body_contains ((CamelDataWrapper *) message, &pattern)) {
						g_ptr_array_add (r->value.ptrarray, uid);
					}

					camel_object_unref ((CamelObject *)message);
				} else {
					camel_exception_clear (&x);
				}
			}

			regfree (&pattern);
		}
	}

	return r;
}

static ESExpResult *
search_user_flag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	gint i;

	r(printf("executing user-flag\n"));

	/* are we inside a match-all? */
	if (search->current) {
		gint truth = FALSE;
		/* performs an OR of all words */
		for (i=0;i<argc && !truth;i++) {
			if (argv[i]->type == ESEXP_RES_STRING
			    && camel_message_info_user_flag(search->current, argv[i]->value.string)) {
				truth = TRUE;
				break;
			}
		}
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = truth;
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();
	}

	return r;
}

static ESExpResult *
search_system_flag (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;

	r(printf ("executing system-flag\n"));

	if (search->current) {
		gboolean truth = FALSE;

		if (argc == 1)
			truth = camel_system_flag_get (camel_message_info_flags(search->current), argv[0]->value.string);

		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = truth;
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();
	}

	return r;
}

static ESExpResult *
search_user_tag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	const gchar *value = NULL;
	ESExpResult *r;

	r(printf("executing user-tag\n"));

	if (search->current && argc == 1)
		value = camel_message_info_user_tag(search->current, argv[0]->value.string);

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = g_strdup (value ? value : "");

	return r;
}

static ESExpResult *
search_get_sent_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s)
{
	ESExpResult *r;

	r(printf("executing get-sent-date\n"));

	/* are we inside a match-all? */
	if (s->current) {
		r = e_sexp_result_new(f, ESEXP_RES_INT);

		r->value.number = camel_message_info_date_sent(s->current);
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();
	}

	return r;
}

static ESExpResult *
search_get_received_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s)
{
	ESExpResult *r;

	r(printf("executing get-received-date\n"));

	/* are we inside a match-all? */
	if (s->current) {
		r = e_sexp_result_new(f, ESEXP_RES_INT);

		r->value.number = camel_message_info_date_received(s->current);
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();
	}

	return r;
}

static ESExpResult *
search_get_current_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s)
{
	ESExpResult *r;

	r(printf("executing get-current-date\n"));

	r = e_sexp_result_new(f, ESEXP_RES_INT);
	r->value.number = time (NULL);
	return r;
}

static ESExpResult *
search_get_size (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *s)
{
	ESExpResult *r;

	r(printf("executing get-size\n"));

	/* are we inside a match-all? */
	if (s->current) {
		r = e_sexp_result_new (f, ESEXP_RES_INT);
		r->value.number = camel_message_info_size(s->current) / 1024;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();
	}

	return r;
}

static ESExpResult *
search_uid(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	gint i;

	r(printf("executing uid\n"));

	/* are we inside a match-all? */
	if (search->current) {
		gint truth = FALSE;
		const gchar *uid = camel_message_info_uid(search->current);

		/* performs an OR of all words */
		for (i=0;i<argc && !truth;i++) {
			if (argv[i]->type == ESEXP_RES_STRING
			    && !strcmp(uid, argv[i]->value.string)) {
				truth = TRUE;
				break;
			}
		}
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = truth;
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new();
		for (i=0;i<argc;i++) {
			if (argv[i]->type == ESEXP_RES_STRING)
				g_ptr_array_add(r->value.ptrarray, argv[i]->value.string);
		}
	}

	return r;
}

static gint
read_uid_callback (gpointer  ref, gint ncol, gchar ** cols, gchar **name)
{
	GPtrArray *matches;

	matches = (GPtrArray *) ref;

	g_ptr_array_add (matches, (gpointer) camel_pstring_strdup (cols [0]));
	return 0;
}

static ESExpResult *
search_message_location (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFolderSearch *search)
{
	ESExpResult *r;
	gboolean same = FALSE;

	if (argc == 1 && argv[0]->type == ESEXP_RES_STRING) {
		if (argv[0]->value.string && search->folder && search->folder->parent_store && camel_folder_get_full_name (search->folder)) {
			CamelFolderInfo *fi = camel_store_get_folder_info (search->folder->parent_store, camel_folder_get_full_name (search->folder), 0, NULL);
			if (fi) {
				same = g_str_equal (fi->uri ? fi->uri : "", argv[0]->value.string);

				camel_store_free_folder_info (search->folder->parent_store, fi);
			}
		}
	}

	if (search->current) {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.bool = same ? TRUE : FALSE;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();

		if (same) {
			/* all matches */
			gint i;
			GPtrArray *v = search->summary_set ? search->summary_set : search->summary;

			for (i = 0; i < v->len; i++) {
				gchar *uid = g_ptr_array_index (v, i);

				g_ptr_array_add (r->value.ptrarray, uid);
			}
		}
	}

	return r;
}
