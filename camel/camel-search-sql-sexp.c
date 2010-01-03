/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2008 Novell, Inc. (www.novell.com)
 *
 *  Author: Srinivasa Ragavan  <sragavan@novell.com>
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include "camel-search-sql-sexp.h"
#define d(x) /* x;printf("\n"); */

#ifdef TEST_MAIN
#include <sqlite3.h>
typedef enum {
	CAMEL_SEARCH_MATCH_EXACT,
	CAMEL_SEARCH_MATCH_CONTAINS,
	CAMEL_SEARCH_MATCH_STARTS,
	CAMEL_SEARCH_MATCH_ENDS,
	CAMEL_SEARCH_MATCH_SOUNDEX
} camel_search_match_t;
gchar * camel_db_get_column_name (const gchar *raw_name);

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

#else
#include "camel-db.h"
#include "camel-folder-search.h"
#include "camel-search-private.h"
#endif

static gchar *
get_db_safe_string (const gchar *str)
{
	gchar *tmp = camel_db_sqlize_string (str);
	gchar *ret;

	ret = g_strdup(tmp);
	camel_db_free_sqlized_string (tmp);

	return ret;
}

/* Configuration of your sexp expression */

static ESExpResult *
func_and(ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	ESExpResult *r, *r1;
	GString *string;
	gint i;

	d(printf("executing and: %d", argc));

	string = g_string_new("( ");
	for (i = 0; i < argc; i ++) {
		r1 = e_sexp_term_eval(f, argv[i]);

		if (r1->type != ESEXP_RES_STRING) {
			e_sexp_result_free (f, r1);
			continue;
		}
		if (r1->value.string && *r1->value.string)
			g_string_append_printf(string, "%s%s", r1->value.string, ((argc>1) && (i != argc-1)) ?  " AND ":"");
		e_sexp_result_free (f, r1);
	}
	g_string_append(string, " )");
	r = e_sexp_result_new(f, ESEXP_RES_STRING);

	if (strlen(string->str) == 4)
		r->value.string = g_strdup("");
	else
		r->value.string = string->str;
	g_string_free(string, FALSE);

	return r;
}

static ESExpResult *
func_or(ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	ESExpResult *r, *r1;
	GString *string;
	gint i;

	d(printf("executing or: %d", argc));

	string = g_string_new("( ");
	for (i = 0; i < argc; i ++) {
		r1 = e_sexp_term_eval(f, argv[i]);

		if (r1->type != ESEXP_RES_STRING) {
			e_sexp_result_free (f, r1);
			continue;
		}
		g_string_append_printf(string, "%s%s", r1->value.string, ((argc>1) && (i != argc-1)) ?  " OR ":"");
		e_sexp_result_free (f, r1);
	}
	g_string_append(string, " )");

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = string->str;
	g_string_free(string, FALSE);
	return r;
}

static ESExpResult *
func_not(ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	ESExpResult *r=NULL, *r1;

	d(printf("executing not: %d", argc));
	r1 = e_sexp_term_eval(f, argv[0]);

	if (r1->type == ESEXP_RES_STRING) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		/* HACK: Fix and handle completed-on better. */
		if (g_strcmp0 (r1->value.string, "( (usertags LIKE '%completed-on 0%' AND usertags LIKE '%completed-on%') )") == 0)
			r->value.string = g_strdup ("( (not (usertags LIKE '%completed-on 0%')) AND usertags LIKE '%completed-on%' )");
		else
			r->value.string = g_strdup_printf ("(NOT (%s))",
							   r1->value.string);
	}
	e_sexp_result_free (f, r1);

	return r;
}

/* this should support all arguments ...? */
static ESExpResult *
eval_eq(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	struct _ESExpResult *r, *r1, *r2;

	r = e_sexp_result_new(f, ESEXP_RES_STRING);

	if (argc == 2) {
		GString *str = g_string_new("( ");
		r1 = e_sexp_term_eval(f, argv[0]);
		r2 = e_sexp_term_eval(f, argv[1]);

		if (r1->type == ESEXP_RES_INT)
			g_string_append_printf(str, "%d", r1->value.number);
		else if (r1->type == ESEXP_RES_TIME)
			g_string_append_printf(str, "%ld", r1->value.time);
		else if (r1->type == ESEXP_RES_STRING)
			g_string_append_printf(str, "%s", r1->value.string);

		if (!strstr(str->str, "completed-on") && !strstr(str->str, "follow-up")) {
			gboolean ut = FALSE;

			if (strstr(str->str, "usertags"))
				ut = TRUE;
			if (ut)
				g_string_append_printf(str, " LIKE ");
			else
				g_string_append_printf(str, " = ");
			if (r2->type == ESEXP_RES_INT)
				g_string_append_printf(str, "%d", r2->value.number);
			if (r2->type == ESEXP_RES_BOOL)
				g_string_append_printf(str, "%d", r2->value.bool);
			else if (r2->type == ESEXP_RES_TIME)
				g_string_append_printf(str, "%ld", r2->value.time);
			else if (r2->type == ESEXP_RES_STRING) {
				gchar *tmp = g_strdup_printf("%c%s%c", ut ? '%':' ', r2->value.string, ut?'%':' ');
				gchar *safe = get_db_safe_string(tmp);
				g_string_append_printf(str, "%s", safe);
				g_free(safe);
				g_free(tmp);
			}
		}
		e_sexp_result_free(f, r1);
		e_sexp_result_free(f, r2);
		g_string_append (str, " )");
		r->value.string = str->str;
		g_string_free(str, FALSE);
	} else {
		r->value.string = g_strdup ("(0)");
	}
	return r;
}

static ESExpResult *
eval_lt(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	struct _ESExpResult *r, *r1, *r2;

	r = e_sexp_result_new(f, ESEXP_RES_STRING);

	if (argc == 2) {
		GString *str = g_string_new("( ");
		r1 = e_sexp_term_eval(f, argv[0]);
		r2 = e_sexp_term_eval(f, argv[1]);

		if (r1->type == ESEXP_RES_INT)
			g_string_append_printf(str, "%d", r1->value.number);
		else if (r1->type == ESEXP_RES_TIME)
			g_string_append_printf(str, "%ld", r1->value.time);
		else if (r1->type == ESEXP_RES_STRING)
			g_string_append_printf(str, "%s", r1->value.string);

		g_string_append_printf(str, " < ");
		if (r2->type == ESEXP_RES_INT)
			g_string_append_printf(str, "%d", r2->value.number);
		if (r2->type == ESEXP_RES_BOOL)
			g_string_append_printf(str, "%d", r2->value.bool);
		else if (r2->type == ESEXP_RES_TIME)
			g_string_append_printf(str, "%ld", r2->value.time);
		else if (r2->type == ESEXP_RES_STRING)
			g_string_append_printf(str, "%s", r2->value.string);
		e_sexp_result_free(f, r1);
		e_sexp_result_free(f, r2);
		g_string_append (str, " )");

		r->value.string = str->str;
		g_string_free(str, FALSE);
	}
	return r;
}

/* this should support all arguments ...? */
static ESExpResult *
eval_gt(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	struct _ESExpResult *r, *r1, *r2;

	r = e_sexp_result_new(f, ESEXP_RES_STRING);

	if (argc == 2) {
		GString *str = g_string_new("( ");
		r1 = e_sexp_term_eval(f, argv[0]);
		r2 = e_sexp_term_eval(f, argv[1]);

		if (r1->type == ESEXP_RES_INT)
			g_string_append_printf(str, "%d", r1->value.number);
		else if (r1->type == ESEXP_RES_TIME)
			g_string_append_printf(str, "%ld", r1->value.time);
		else if (r1->type == ESEXP_RES_STRING)
			g_string_append_printf(str, "%s", r1->value.string);

		g_string_append_printf(str, " > ");
		if (r2->type == ESEXP_RES_INT)
			g_string_append_printf(str, "%d", r2->value.number);
		if (r2->type == ESEXP_RES_BOOL)
			g_string_append_printf(str, "%d", r2->value.bool);
		else if (r2->type == ESEXP_RES_TIME)
			g_string_append_printf(str, "%ld", r2->value.time);
		else if (r2->type == ESEXP_RES_STRING)
			g_string_append_printf(str, "%s", r2->value.string);
		e_sexp_result_free(f, r1);
		e_sexp_result_free(f, r2);
		g_string_append (str, " )");

		r->value.string = str->str;
		g_string_free(str, FALSE);
	}
	return r;
}

static ESExpResult *
match_all(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	ESExpResult *r;

	d(printf("executing match-all: %d", argc));
	if (argv[0]->type != ESEXP_TERM_BOOL)
		r = e_sexp_term_eval(f, argv[0]);
	else {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup(argv[0]->value.bool ? "1" : "0");
	}

	return r;

}

static ESExpResult *
match_threads(struct _ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	ESExpResult *r;
	gint i;
	GString *str = g_string_new ("( ");

	d(printf("executing match-threads: %d", argc));

	for (i=1;i<argc;i++) {
		r = e_sexp_term_eval(f, argv[i]);
		g_string_append_printf(str, "%s%s", r->value.string, ((argc>1) && (i != argc-1)) ?  " AND ":"");
		e_sexp_result_free(f, r);
	}

	g_string_append (str, " )");
	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = str->str;
	g_string_free(str, FALSE);

	return r;
}

static ESExpResult *
check_header (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data, camel_search_match_t how)
{
	ESExpResult *r;
	gchar *str=NULL;

	d(printf("executing check-header %d\n", how));

	/* are we inside a match-all? */
	if (argc>1 && argv[0]->type == ESEXP_RES_STRING) {
		gchar *headername;
		gint i;

		/* only a subset of headers are supported .. */
		headername = camel_db_get_column_name (argv[0]->value.string);

		/* performs an OR of all words */
		for (i=1;i<argc;i++) {
			if (argv[i]->type == ESEXP_RES_STRING) {
				gchar *value=NULL, *tstr=NULL;
				if (argv[i]->value.string[0] == 0)
					continue;
				if (how == CAMEL_SEARCH_MATCH_CONTAINS) {
					tstr = g_strdup_printf ("%c%s%c", '%', argv[i]->value.string, '%');
					value = get_db_safe_string (tstr);
					g_free (tstr);
				} else if (how == CAMEL_SEARCH_MATCH_ENDS) {
					tstr = g_strdup_printf ("%c%s", '%', argv[i]->value.string);
					value = get_db_safe_string (tstr);
					g_free (tstr);
				} else if (how == CAMEL_SEARCH_MATCH_STARTS) {
					tstr = g_strdup_printf ("%s%c", argv[i]->value.string, '%');
					value = get_db_safe_string (tstr);
					g_free (tstr);
				} else if (how == CAMEL_SEARCH_MATCH_EXACT) {
					tstr = g_strdup_printf ("%c%s%c", '%', argv[i]->value.string, '%');
					value = get_db_safe_string (tstr);
					g_free (tstr);
				}
				str = g_strdup_printf("(%s IS NOT NULL AND %s LIKE %s)", headername, headername, value);
				g_free(value);
			}
		}
		g_free (headername);
	}
	/* TODO: else, find all matches */

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = str;

	return r;
}

static ESExpResult *
header_contains(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	d(printf("executing header-contains: %d", argc));

	return check_header(f, argc, argv, data, CAMEL_SEARCH_MATCH_CONTAINS);
}

static ESExpResult *
header_matches(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	d(printf("executing header-matches: %d", argc));

	return check_header(f, argc, argv, data, CAMEL_SEARCH_MATCH_EXACT);
}

static ESExpResult *
header_starts_with (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	d(printf("executing header-starts-with: %d", argc));

	return check_header(f, argc, argv, data, CAMEL_SEARCH_MATCH_STARTS);
}

static ESExpResult *
header_ends_with (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	d(printf("executing header-ends-with: %d", argc));

	return check_header(f, argc, argv, data, CAMEL_SEARCH_MATCH_ENDS);
}

static ESExpResult *
header_exists (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	gchar *headername;

	d(printf("executing header-exists: %d", argc));

	headername = camel_db_get_column_name (argv[0]->value.string);
	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = g_strdup_printf ("(%s NOTNULL)", headername);
	g_free (headername);
	return r;
}

static ESExpResult *
user_tag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	d(printf("executing user-tag: %d", argc));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	/* Hacks no otherway to fix these really :( */
	if (g_strcmp0 (argv[0]->value.string, "completed-on") == 0)
		r->value.string = g_strdup_printf("(usertags LIKE '%ccompleted-on 0%c' AND usertags LIKE '%ccompleted-on%c')", '%', '%', '%', '%');
	else if (g_strcmp0 (argv[0]->value.string, "follow-up") == 0)
		r->value.string = g_strdup_printf("usertags NOT LIKE '%cfollow-up%c'", '%', '%');
	else
		r->value.string = g_strdup("usertags");

	return r;
}

static ESExpResult *
user_flag(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	gchar *tstr, *qstr;

	d(printf("executing user-flag: %d", argc));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);

	if (argc != 1) {
		r->value.string = g_strdup ("(0)");
	} else {
		tstr = g_strdup_printf("%%%s%%", argv[0]->value.string);
		qstr = get_db_safe_string(tstr);
		g_free(tstr);
		r->value.string = g_strdup_printf("(labels LIKE %s)", qstr);
		g_free(qstr);
	}

	return r;
}

static ESExpResult *
system_flag (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	gchar *tstr;

	d(printf("executing system-flag: %d", argc));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);

	if (argc != 1) {
		r->value.string = g_strdup ("(0)");
	} else {
		tstr = camel_db_get_column_name(argv[0]->value.string);
		r->value.string = g_strdup_printf("(%s = 1)", tstr);
		g_free(tstr);
	}

	return r;
}

static ESExpResult *
get_sent_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	d(printf("executing get-sent-date\n"));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = g_strdup("dsent");

	return r;
}

static ESExpResult *
get_received_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	d(printf("executing get-received-date\n"));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = g_strdup("dreceived");

	return r;
}

static ESExpResult *
get_current_date(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	d(printf("executing get-current-date\n"));

	r = e_sexp_result_new(f, ESEXP_RES_INT);
	r->value.number = time (NULL);
	return r;
}

static ESExpResult *
get_size (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	d(printf("executing get-size\n"));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = g_strdup("size");

	return r;
}

static ESExpResult *
sql_exp (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	gint i;
	GString *str = g_string_new (NULL);

	d(printf("executing sql-exp\n"));

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	for (i=0;i<argc;i++) {
		if (argv[i]->type == ESEXP_RES_STRING && argv[i]->value.string)
			g_string_append (str, argv[i]->value.string);
	}
	r->value.string = str->str;
	g_string_free (str, FALSE);

	return r;
}

/* 'builtin' functions */
static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} symbols[] = {
	{ "and", (ESExpFunc *) func_and, 1 },
	{ "or", (ESExpFunc *) func_or, 1},
	{ "not", (ESExpFunc *) func_not, 1},
	{ "=", (ESExpFunc *)eval_eq, 1},
	{ ">", (ESExpFunc *)eval_gt, 1},
	{ "<", (ESExpFunc *)eval_lt, 1},

	{ "match-all", (ESExpFunc *)match_all, 1 },
	{ "match-threads", (ESExpFunc *)match_threads, 1 },
/*	{ "body-contains", body_contains}, */ /* We don't store body on the db. */
	{ "header-contains", header_contains, 0},
	{ "header-matches", header_matches, 0},
	{ "header-starts-with", header_starts_with, 0},
	{ "header-ends-with", header_ends_with, 0},
	{ "header-exists", header_exists, 0},
	{ "user-tag", user_tag, 0},
	{ "user-flag", user_flag, 0},
	{ "system-flag", system_flag, 0},
	{ "get-sent-date", get_sent_date, 0},
	{ "get-received-date", get_received_date, 0},
	{ "get-current-date", get_current_date, 0},
	{ "get-size", get_size, 0},
	{ "sql-exp", sql_exp, 0},

/*	{ "uid", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, uid), 1 },	*/
};

gchar *
camel_sexp_to_sql_sexp (const gchar *sql)
{
	ESExp *sexp;
	ESExpResult *r;
	gint i;
	gchar *res;

	sexp = e_sexp_new();

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].immediate)
			e_sexp_add_ifunction(sexp, 0, symbols[i].name,
					     (ESExpIFunc *) symbols[i].func, NULL);
		else
			e_sexp_add_function(sexp, 0, symbols[i].name,
					    symbols[i].func, NULL);
	}

	e_sexp_input_text (sexp, sql, strlen (sql));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (!r)
		return NULL;
	if (r->type == ESEXP_RES_STRING) {
		res = g_strdup (r->value.string);
	} else
		g_assert(0);

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);
	return res;
}

#ifdef TEST_MAIN
/*

(and (match-all (and (not (system-flag "deleted")) (not (system-flag "junk"))))
 (and   (or

     (match-all (not (system-flag "Attachments")))

  )
 ))

"
replied INTEGER ,                (match-all (system-flag  "Answered"))
size INTEGER ,                   (match-all (< (get-size) 100))
dsent NUMERIC ,                  (match-all (< (get-sent-date) (- (get-current-date) 10)))
dreceived NUMERIC ,               (match-all (< (get-received-date) (- (get-current-date) 10)))
//mlist TEXT ,                      x-camel-mlist   (match-all (header-matches "x-camel-mlist"  "gnome.org"))
//attachment,                      system-flag "Attachments"   (match-all (system-flag "Attachments"))
//followup_flag TEXT ,             (match-all (not (= (user-tag "follow-up") "")))
//followup_completed_on TEXT ,      (match-all (not (= (user-tag "completed-on") "")))
//followup_due_by TEXT ," //NOTREQD
*/

gchar * camel_db_get_column_name (const gchar *raw_name)
{
	/* d(g_print ("\n\aRAW name is : [%s] \n\a", raw_name)); */
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
	else {
		/* Let it crash for all unknown columns for now.
		We need to load the messages into memory and search etc.
		We should extend this for camel-folder-search system flags search as well
		otherwise, search-for-signed-messages will not work etc.*/

		return g_strdup (raw_name);
	}

}

gint main ()
{

	gint i=0;
	gchar *txt[] = {
#if 0
	"(match-all (header-contains \"From\"  \"org\"))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (match-all (or (= (user-tag \"label\")  \"work\")  (user-flag  \"work\"))) )))",

	"(and  (and   (match-all (header-contains \"From\"  \"org\"))   )  (match-all (not (system-flag \"junk\"))))",

	"(and  (and (match-all (header-contains \"From\"  \"org\"))) (and (match-all (not (system-flag \"junk\"))) (and   (or (match-all (header-contains \"Subject\"  \"test\")) (match-all (header-contains \"From\"  \"test\"))))))",
	"(and  (and   (match-all (header-exists \"From\"))   )  (match-all (not (system-flag \"junk\"))))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (header-contains \"Subject\"  \"org\")) (match-all (header-contains \"From\"  \"org\")) (match-all (system-flag  \"Flagged\")) (match-all (system-flag  \"Seen\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (= (user-tag \"label\")  \"work\") )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (user-flag  \"work\") )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (user-flag  (+ \"$Label\"  \"work\")) )))"
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (not (= (user-tag \"follow-up\") \"\"))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (= (user-tag \"follow-up\") \"\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (not (= (user-tag \"completed-on\") \"\"))) )))",
	"(match-all (and  (match-all #t) (system-flag \"deleted\")))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (= (user-tag \"label\")  \"important\") (user-flag (+ \"$Label\"  \"important\")) (user-flag  \"important\"))) )))",
	"(or (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")) (not (system-flag \"Attachments\")) (not (system-flag \"Answered\")))) (and   (or (match-all (= (user-tag \"completed-on\") \"\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (= (user-tag \"completed-on\") \"\")) (match-all (= (user-tag \"follow-up\") \"\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (> (get-sent-date) (- (get-current-date) 100))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (< (get-sent-date) (+ (get-current-date) 100))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (not (= (get-sent-date) 1216146600))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (= (get-sent-date) 1216146600)) )))"	,
	"(match-threads \"all\"  (or (match-all (header-contains \"From\"  \"@edesvcs.com\")) (match-all (or (header-contains \"To\"  \"@edesvcs.com\") (header-contains \"Cc\"  \"@edesvcs.com\"))) ))",
	"(match-all (not (system-flag \"deleted\")))",
	"(match-all (system-flag \"seen\"))",
	"(match-all (and  (match-all #t) (system-flag \"deleted\")))",
	"(match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\"))))",

	"(and (or (match-all (header-contains \"Subject\"  \"lin\")) ) (and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (header-contains \"Subject\"  \"case\")) (match-all (header-contains \"From\"  \"case\"))))))",
	"(and ( match-all(or (match-all (header-contains \"Subject\"  \"lin\")) (match-all (header-contains \"From\"  \"in\")))) (and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (header-contains \"Subject\"  \"proc\")) (match-all (header-contains \"From\"  \"proc\"))))))",
	"(and  (or (match-all (header-contains \"Subject\"  \"[LDTP-NOSIP]\")) ) (and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (header-contains \"Subject\"  \"vamsi\")) (match-all (header-contains \"From\"  \"vamsi\"))))))",
	/* Last one doesn't work so well and fails on one case. But I doubt, you can create a query like that in Evo. */
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (match-all (or (= (user-tag \"label\") \"_office\") (user-flag \"$Label_office\") (user-flag \"_office\"))))",
	"(and  (and (match-all #t))(and(match-all #t)))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (and (match-all (header-contains \"Subject\"  \"mysubject\")) (match-all (not (header-matches \"From\"  \"mysender\"))) (match-all (= (get-sent-date) (+ (get-current-date) 1))) (match-all (= (get-received-date) (- (get-current-date) 604800))) (match-all (or (= (user-tag \"label\")  \"important\") (user-flag (+ \"$Label\"  \"important\")) (match-all (< (get-size) 7000)) (match-all (not (= (get-sent-date) 1216146600)))  (match-all (> (cast-int (user-tag \"score\")) 3))  (user-flag  \"important\"))) (match-all (system-flag  \"Deleted\")) (match-all (not (= (user-tag \"follow-up\") \"\"))) (match-all (= (user-tag \"completed-on\") \"\")) (match-all (system-flag \"Attachments\")) (match-all (header-contains \"x-camel-mlist\"  \"evo-hackers\")) )))",
	"(and (or  (match-all (or (= (user-tag \"label\") \"important\") (user-flag (+ \"$Label\" \"important\")) (user-flag \"important\")))    (match-all (or (= (user-tag \"label\") \"work\") (user-flag (+ \"$Label\" \"work\")) (user-flag \"work\")))    (match-all (or (= (user-tag \"label\") \"personal\") (user-flag (+ \"$Label\" \"personal\")) (user-flag \"personal\")))    (match-all (or (= (user-tag \"label\") \"todo\") (user-flag (+ \"$Label\" \"todo\")) (user-flag \"todo\")))    (match-all (or (= (user-tag \"label\") \"later\") (user-flag (+ \"$Label\" \"later\")) (user-flag \"later\")))  )  (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))))",
	"(or (header-matches \"to\" \"maw@ximian.com\") (header-matches \"to\" \"mw@ximian.com\")   (header-matches \"to\" \"maw@novell.com\")   (header-matches \"to\" \"maw.AMERICAS3.AMERICAS@novell.com\") (header-matches \"cc\" \"maw@ximian.com\") (header-matches \"cc\" \"mw@ximian.com\")     (header-matches \"cc\" \"maw@novell.com\")   (header-matches \"cc\" \"maw.AMERICAS3.AMERICAS@novell.com\"))",
	"(not (or (header-matches \"from\" \"bugzilla-daemon@bugzilla.ximian.com\") (header-matches \"from\" \"bugzilla-daemon@bugzilla.gnome.org\") (header-matches \"from\" \"bugzilla_noreply@novell.com\") (header-matches \"from\" \"bugzilla-daemon@mozilla.org\") (header-matches \"from\" \"root@dist.suse.de\") (header-matches \"from\" \"root@hilbert3.suse.de\") (header-matches \"from\" \"root@hilbert4.suse.de\") (header-matches \"from\" \"root@hilbert5.suse.de\") (header-matches \"from\" \"root@hilbert6.suse.de\") (header-matches \"from\" \"root@suse.de\") (header-matches \"from\" \"swamp_noreply@suse.de\") (and (header-matches \"from\" \"hermes@opensuse.org\") (header-starts-with \"subject\" \"submit-Request\"))))",
	"(and (match-threads \"replies_parents\" (and (match-all (or (header-matches \"to\" \"maw@ximian.com\") (header-matches \"to\" \"mw@ximian.com\")   (header-matches \"to\" \"maw@novell.com\")   (header-matches \"to\" \"maw.AMERICAS3.AMERICAS@novell.com\") (header-matches \"cc\" \"maw@ximian.com\") (header-matches \"cc\" \"mw@ximian.com\")     (header-matches \"cc\" \"maw@novell.com\")   (header-matches \"cc\" \"maw.AMERICAS3.AMERICAS@novell.com\"))) (match-all (not (or (header-matches \"from\" \"bugzilla-daemon@bugzilla.ximian.com\") (header-matches \"from\" \"bugzilla-daemon@bugzilla.gnome.org\") (header-matches \"from\" \"bugzilla_noreply@novell.com\") (header-matches \"from\" \"bugzilla-daemon@mozilla.org\") (header-matches \"from\" \"root@dist.suse.de\") (header-matches \"from\" \"root@hilbert3.suse.de\") (header-matches \"from\" \"root@hilbert4.suse.de\") (header-matches \"from\" \"root@hilbert5.suse.de\") (header-matches \"from\" \"root@hilbert6.suse.de\") (header-matches \"from\" \"root@suse.de\") (header-matches \"from\" \"swamp_noreply@suse.de\") (and (header-matches \"from\" \"hermes@opensuse.org\") (header-starts-with \"subject\" \"submit-Request\"))))) (match-all (> (get-sent-date) (- (get-current-date) 1209600))) )) (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))))",
	"and ((match-all (system-flag \"Deleted\")) (match-all (system-flag  \"junk\")))",
	"(and (match-threads \"replies_parents\" (and (match-all (or (header-matches \"to\" \"maw@ximian.com\")))))))",
	"(and (sql-exp \"folder_key = 'ASDGASd' AND folder_key = 'DSFWEA'\") (match-threads \"replies_parents\" (and (match-all (or (header-matches \"to\" \"maw@ximian.com\")))))))"
#endif
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or  (match-all list-post.*zypp-devel)  ) ))"
	};

	for (i=0; i < G_N_ELEMENTS(txt); i++) {
		gchar *sql = NULL;
		printf("Q: %s\n\"%c\"\n", txt[i], 40);
		sql = camel_sexp_to_sql_sexp (txt[i]);
		printf("A: %s\n\n\n", sql);
		g_free (sql);
	}

}
#endif

