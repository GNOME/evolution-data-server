/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * A simple, extensible s-exp evaluation engine.
 *
 * Author :
 *  Michael Zucchi <notzed@ximian.com>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 *   The following built-in s-exp's are supported:
 *
 *   list = (and list*)
 *      perform an intersection of a number of lists, and return that.
 *
 *   bool = (and bool*)
 *      perform a boolean AND of boolean values.
 *
 *   list = (or list*)
 *      perform a union of a number of lists, returning the new list.
 *
 *   bool = (or bool*)
 *      perform a boolean OR of boolean values.
 *
 *   gint = (+ int*)
 *      Add integers.
 *
 *   string = (+ string*)
 *      Concat strings.
 *
 *   time_t = (+ time_t*)
 *      Add time_t values.
 *
 *   gint = (- gint int*)
 *      Subtract integers from the first.
 *
 *   time_t = (- time_t*)
 *      Subtract time_t values from the first.
 *
 *   gint = (cast-int string|int|bool)
 *         Cast to an integer value.
 *
 *   string = (cast-string string|int|bool)
 *         Cast to an string value.
 *
 *   Comparison operators:
 *
 *   bool = (< gint gint)
 *   bool = (> gint gint)
 *   bool = (= gint gint)
 *
 *   bool = (< string string)
 *   bool = (> string string)
 *   bool = (= string string)
 *
 *   bool = (< time_t time_t)
 *   bool = (> time_t time_t)
 *   bool = (= time_t time_t)
 *      Perform a comparision of 2 integers, 2 string values, or 2 time values.
 *
 *   Function flow:
 *
 *   type = (if bool function)
 *   type = (if bool function function)
 *      Choose a flow path based on a boolean value
 *
 *   type = (begin  func func func)
 *         Execute a sequence.  The last function return is the return type.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "e-sexp.h"
#include "e-memory.h"

#define p(x)			/* parse debug */
#define r(x)			/* run debug */
#define d(x)			/* general debug */

#ifdef E_SEXP_IS_G_OBJECT
G_DEFINE_TYPE (ESExp, e_sexp, G_TYPE_OBJECT)
#endif

static struct _ESExpTerm *
			parse_list		(ESExp *f,
						 gint gotbrace);
static struct _ESExpTerm *
			parse_value		(ESExp *f);

#ifdef TESTER
static void		parse_dump_term		(struct _ESExpTerm *t,
						 gint depth);
#endif

typedef gboolean	(ESGeneratorFunc)	(gint argc,
						 struct _ESExpResult **argv,
						 struct _ESExpResult *r);
typedef gboolean	(ESOperatorFunc)	(gint argc,
						 struct _ESExpResult **argv,
						 struct _ESExpResult *r);

/* FIXME: constant _TIME_MAX used in different files, move it somewhere */
#define _TIME_MAX	((time_t) INT_MAX)	/* Max valid time_t	*/

static const GScannerConfig scanner_config =
{
	( (gchar *) " \t\r\n")		/* cset_skip_characters */,
	( (gchar *) G_CSET_a_2_z
	  "_+-<=>?"
	  G_CSET_A_2_Z)			/* cset_identifier_first */,
	( (gchar *) G_CSET_a_2_z
	  "_0123456789-<>?"
	  G_CSET_A_2_Z
	  G_CSET_LATINS
	  G_CSET_LATINC	)		/* cset_identifier_nth */,
	( (gchar *) ";\n" )		/* cpair_comment_single */,

	FALSE				/* case_sensitive */,

	TRUE				/* skip_comment_multi */,
	TRUE				/* skip_comment_single */,
	TRUE				/* scan_comment_multi */,
	TRUE				/* scan_identifier */,
	TRUE				/* scan_identifier_1char */,
	FALSE				/* scan_identifier_NULL */,
	TRUE				/* scan_symbols */,
	FALSE				/* scan_binary */,
	TRUE				/* scan_octal */,
	TRUE				/* scan_float */,
	TRUE				/* scan_hex */,
	FALSE				/* scan_hex_dollar */,
	TRUE				/* scan_string_sq */,
	TRUE				/* scan_string_dq */,
	TRUE				/* numbers_2_int */,
	FALSE				/* int_2_float */,
	FALSE				/* identifier_2_string */,
	TRUE				/* char_2_token */,
	FALSE				/* symbol_2_token */,
	FALSE				/* scope_0_fallback */,
};

/* jumps back to the caller of f->failenv, only to be called from inside a callback */
void
e_sexp_fatal_error (struct _ESExp *f,
                    const gchar *why,
                    ...)
{
	va_list args;

	if (f->error)
		g_free (f->error);

	va_start (args, why);
	f->error = g_strdup_vprintf (why, args);
	va_end (args);

	longjmp (f->failenv, 1);
}

const gchar *
e_sexp_error (struct _ESExp *f)
{
	return f->error;
}

struct _ESExpResult *
e_sexp_result_new (struct _ESExp *f,
                   gint type)
{
	struct _ESExpResult *r = e_memchunk_alloc0 (f->result_chunks);
	r->type = type;
	r->occuring_start = 0;
	r->occuring_end = _TIME_MAX;
	r->time_generator = FALSE;
	return r;
}

void
e_sexp_result_free (struct _ESExp *f,
                    struct _ESExpResult *t)
{
	if (t == NULL)
		return;

	switch (t->type) {
	case ESEXP_RES_ARRAY_PTR:
		if (t->value.ptrarray)
			g_ptr_array_free (t->value.ptrarray, TRUE);
		break;
	case ESEXP_RES_BOOL:
	case ESEXP_RES_INT:
	case ESEXP_RES_TIME:
		break;
	case ESEXP_RES_STRING:
		g_free (t->value.string);
		break;
	case ESEXP_RES_UNDEFINED:
		break;
	default:
		g_assert_not_reached ();
	}
	e_memchunk_free (f->result_chunks, t);
}

/* used in normal functions if they have to abort, and free their arguments */
void
e_sexp_resultv_free (struct _ESExp *f,
                     gint argc,
                     struct _ESExpResult **argv)
{
	gint i;

	for (i = 0; i < argc; i++) {
		e_sexp_result_free (f, argv[i]);
	}
}

/* implementations for the builtin functions */

/* we can only itereate a hashtable from a called function */
struct IterData {
	gint count;
	GPtrArray *uids;
};

/* ok, store any values that are in all sets */
static void
htand (gchar *key,
       gint value,
       struct IterData *iter_data)
{
	if (value == iter_data->count) {
		g_ptr_array_add (iter_data->uids, key);
	}
}

/* or, store all unique values */
static void
htor (gchar *key,
      gint value,
      struct IterData *iter_data)
{
	g_ptr_array_add (iter_data->uids, key);
}

static ESExpResult *
term_eval_and (struct _ESExp *f,
               gint argc,
               struct _ESExpTerm **argv,
               gpointer data)
{
	struct _ESExpResult *r, *r1;
	GHashTable *ht = g_hash_table_new (g_str_hash, g_str_equal);
	struct IterData lambdafoo;
	gint type=-1;
	gint bool = TRUE;
	gint i;
	const gchar *oper;

	r (printf ("( and\n"));

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	oper = "AND";
	f->operators = g_slist_prepend (f->operators, (gpointer) oper);

	for (i = 0; bool && i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);
		if (type == -1)
			type = r1->type;
		if (type != r1->type) {
			e_sexp_result_free (f, r);
			e_sexp_result_free (f, r1);
			g_hash_table_destroy (ht);
			e_sexp_fatal_error (f, "Invalid types in AND");
		} else if (r1->type == ESEXP_RES_ARRAY_PTR) {
			gchar **a1;
			gint l1, j;

			a1 = (gchar **) r1->value.ptrarray->pdata;
			l1 = r1->value.ptrarray->len;
			for (j = 0; j < l1; j++) {
				gpointer ptr;
				gint n;
				ptr = g_hash_table_lookup (ht, a1[j]);
				n = GPOINTER_TO_INT (ptr);
				g_hash_table_insert (ht, a1[j], GINT_TO_POINTER (n + 1));
			}
		} else if (r1->type == ESEXP_RES_BOOL) {
			bool = bool && r1->value.boolean;
		}
		e_sexp_result_free (f, r1);
	}

	if (type == ESEXP_RES_ARRAY_PTR) {
		lambdafoo.count = argc;
		lambdafoo.uids = g_ptr_array_new ();
		g_hash_table_foreach (ht, (GHFunc) htand, &lambdafoo);
		r->type = ESEXP_RES_ARRAY_PTR;
		r->value.ptrarray = lambdafoo.uids;
	} else if (type == ESEXP_RES_BOOL) {
		r->type = ESEXP_RES_BOOL;
		r->value.boolean = bool;
	}

	g_hash_table_destroy (ht);
	f->operators = g_slist_remove (f->operators, oper);

	return r;
}

static ESExpResult *
term_eval_or (struct _ESExp *f,
              gint argc,
              struct _ESExpTerm **argv,
              gpointer data)
{
	struct _ESExpResult *r, *r1;
	GHashTable *ht = g_hash_table_new (g_str_hash, g_str_equal);
	struct IterData lambdafoo;
	gint type = -1;
	gint bool = FALSE;
	gint i;
	const gchar *oper;

	r (printf ("(or \n"));

	oper = "OR";
	f->operators = g_slist_prepend (f->operators, (gpointer) oper);

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	for (i = 0; !bool && i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);
		if (type == -1)
			type = r1->type;
		if (r1->type != type) {
			e_sexp_result_free (f, r);
			e_sexp_result_free (f, r1);
			g_hash_table_destroy (ht);
			e_sexp_fatal_error (f, "Invalid types in OR");
		} else if (r1->type == ESEXP_RES_ARRAY_PTR) {
			gchar **a1;
			gint l1, j;

			a1 = (gchar **) r1->value.ptrarray->pdata;
			l1 = r1->value.ptrarray->len;
			for (j = 0; j < l1; j++) {
				g_hash_table_insert (ht, a1[j], (gpointer) 1);
			}
		} else if (r1->type == ESEXP_RES_BOOL) {
			bool |= r1->value.boolean;
		}
		e_sexp_result_free (f, r1);
	}

	if (type == ESEXP_RES_ARRAY_PTR) {
		lambdafoo.count = argc;
		lambdafoo.uids = g_ptr_array_new ();
		g_hash_table_foreach (ht, (GHFunc) htor, &lambdafoo);
		r->type = ESEXP_RES_ARRAY_PTR;
		r->value.ptrarray = lambdafoo.uids;
	} else if (type == ESEXP_RES_BOOL) {
		r->type = ESEXP_RES_BOOL;
		r->value.boolean = bool;
	}
	g_hash_table_destroy (ht);

	f->operators = g_slist_remove (f->operators, oper);
	return r;
}

static ESExpResult *
term_eval_not (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	gint res = TRUE;
	ESExpResult *r;

	if (argc > 0) {
		if (argv[0]->type == ESEXP_RES_BOOL
		    && argv[0]->value.boolean)
			res = FALSE;
	}
	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = res;
	return r;
}

/* this should support all arguments ...? */
static ESExpResult *
term_eval_lt (struct _ESExp *f,
              gint argc,
              struct _ESExpTerm **argv,
              gpointer data)
{
	struct _ESExpResult *r, *r1, *r2;

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	if (argc == 2) {
		r1 = e_sexp_term_eval (f, argv[0]);
		r2 = e_sexp_term_eval (f, argv[1]);
		if (r1->type != r2->type) {
			e_sexp_result_free (f, r1);
			e_sexp_result_free (f, r2);
			e_sexp_result_free (f, r);
			e_sexp_fatal_error (f, "Incompatible types in compare <");
		} else if (r1->type == ESEXP_RES_INT) {
			r->type = ESEXP_RES_BOOL;
			r->value.boolean = r1->value.number < r2->value.number;
		} else if (r1->type == ESEXP_RES_TIME) {
			r->type = ESEXP_RES_BOOL;
			r->value.boolean = r1->value.time < r2->value.time;
		} else if (r1->type == ESEXP_RES_STRING) {
			r->type = ESEXP_RES_BOOL;
			r->value.boolean = strcmp (r1->value.string, r2->value.string) < 0;
		}
		e_sexp_result_free (f, r1);
		e_sexp_result_free (f, r2);
	}
	return r;
}

/* this should support all arguments ...? */
static ESExpResult *
term_eval_gt (struct _ESExp *f,
              gint argc,
              struct _ESExpTerm **argv,
              gpointer data)
{
	struct _ESExpResult *r, *r1, *r2;

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	if (argc == 2) {
		r1 = e_sexp_term_eval (f, argv[0]);
		r2 = e_sexp_term_eval (f, argv[1]);
		if (r1->type != r2->type) {
			e_sexp_result_free (f, r1);
			e_sexp_result_free (f, r2);
			e_sexp_result_free (f, r);
			e_sexp_fatal_error (f, "Incompatible types in compare >");
		} else if (r1->type == ESEXP_RES_INT) {
			r->type = ESEXP_RES_BOOL;
			r->value.boolean = r1->value.number > r2->value.number;
		} else if (r1->type == ESEXP_RES_TIME) {
			r->type = ESEXP_RES_BOOL;
			r->value.boolean = r1->value.time > r2->value.time;
		} else if (r1->type == ESEXP_RES_STRING) {
			r->type = ESEXP_RES_BOOL;
			r->value.boolean = strcmp (r1->value.string, r2->value.string) > 0;
		}
		e_sexp_result_free (f, r1);
		e_sexp_result_free (f, r2);
	}
	return r;
}

/* this should support all arguments ...? */
static ESExpResult *
term_eval_eq (struct _ESExp *f,
              gint argc,
              struct _ESExpTerm **argv,
              gpointer data)
{
	struct _ESExpResult *r, *r1, *r2;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);

	if (argc == 2) {
		r1 = e_sexp_term_eval (f, argv[0]);
		r2 = e_sexp_term_eval (f, argv[1]);
		if (r1->type != r2->type) {
			r->value.boolean = FALSE;
		} else if (r1->type == ESEXP_RES_INT) {
			r->value.boolean = r1->value.number == r2->value.number;
		} else if (r1->type == ESEXP_RES_BOOL) {
			r->value.boolean = r1->value.boolean == r2->value.boolean;
		} else if (r1->type == ESEXP_RES_TIME) {
			r->value.boolean = r1->value.time == r2->value.time;
		} else if (r1->type == ESEXP_RES_STRING) {
			r->value.boolean = strcmp (r1->value.string, r2->value.string) == 0;
		}
		e_sexp_result_free (f, r1);
		e_sexp_result_free (f, r2);
	}
	return r;
}

static ESExpResult *
term_eval_plus (struct _ESExp *f,
                gint argc,
                struct _ESExpResult **argv,
                gpointer data)
{
	struct _ESExpResult *r = NULL;
	gint type;
	gint i;

	if (argc > 0) {
		type = argv[0]->type;
		switch (type) {
		case ESEXP_RES_INT: {
			gint total = argv[0]->value.number;
			for (i = 1; i < argc && argv[i]->type == ESEXP_RES_INT; i++) {
				total += argv[i]->value.number;
			}
			if (i < argc) {
				e_sexp_resultv_free (f, argc, argv);
				e_sexp_fatal_error (f, "Invalid types in (+ ints)");
			}
			r = e_sexp_result_new (f, ESEXP_RES_INT);
			r->value.number = total;
			break; }
		case ESEXP_RES_STRING: {
			GString *s = g_string_new (argv[0]->value.string);
			for (i = 1; i < argc && argv[i]->type == ESEXP_RES_STRING; i++) {
				g_string_append (s, argv[i]->value.string);
			}
			if (i < argc) {
				e_sexp_resultv_free (f, argc, argv);
				e_sexp_fatal_error (f, "Invalid types in (+ strings)");
			}
			r = e_sexp_result_new (f, ESEXP_RES_STRING);
			r->value.string = s->str;
			g_string_free (s, FALSE);
			break; }
		case ESEXP_RES_TIME: {
			time_t total;

			total = argv[0]->value.time;

			for (i = 1; i < argc && argv[i]->type == ESEXP_RES_TIME; i++)
				total += argv[i]->value.time;

			if (i < argc) {
				e_sexp_resultv_free (f, argc, argv);
				e_sexp_fatal_error (f, "Invalid types in (+ time_t)");
			}

			r = e_sexp_result_new (f, ESEXP_RES_TIME);
			r->value.time = total;
			break; }
		}
	}

	if (!r) {
		r = e_sexp_result_new (f, ESEXP_RES_INT);
		r->value.number = 0;
	}
	return r;
}

static ESExpResult *
term_eval_sub (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	struct _ESExpResult *r = NULL;
	gint type;
	gint i;

	if (argc > 0) {
		type = argv[0]->type;
		switch (type) {
		case ESEXP_RES_INT: {
			gint total = argv[0]->value.number;
			for (i = 1; i < argc && argv[i]->type == ESEXP_RES_INT; i++) {
				total -= argv[i]->value.number;
			}
			if (i < argc) {
				e_sexp_resultv_free (f, argc, argv);
				e_sexp_fatal_error (f, "Invalid types in -");
			}
			r = e_sexp_result_new (f, ESEXP_RES_INT);
			r->value.number = total;
			break; }
		case ESEXP_RES_TIME: {
			time_t total;

			total = argv[0]->value.time;

			for (i = 1; i < argc && argv[i]->type == ESEXP_RES_TIME; i++)
				total -= argv[i]->value.time;

			if (i < argc) {
				e_sexp_resultv_free (f, argc, argv);
				e_sexp_fatal_error (f, "Invalid types in (- time_t)");
			}

			r = e_sexp_result_new (f, ESEXP_RES_TIME);
			r->value.time = total;
			break; }
		}
	}

	if (!r) {
		r = e_sexp_result_new (f, ESEXP_RES_INT);
		r->value.number = 0;
	}
	return r;
}

/* cast to gint */
static ESExpResult *
term_eval_castint (struct _ESExp *f,
                   gint argc,
                   struct _ESExpResult **argv,
                   gpointer data)
{
	struct _ESExpResult *r;

	if (argc != 1)
		e_sexp_fatal_error (f, "Incorrect argument count to (gint )");

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	switch (argv[0]->type) {
	case ESEXP_RES_INT:
		r->value.number = argv[0]->value.number;
		break;
	case ESEXP_RES_BOOL:
		r->value.number = argv[0]->value.boolean != 0;
		break;
	case ESEXP_RES_STRING:
		r->value.number = strtoul (argv[0]->value.string, NULL, 10);
		break;
	default:
		e_sexp_result_free (f, r);
		e_sexp_fatal_error (f, "Invalid type in (cast-int )");
	}

	return r;
}

/* cast to string */
static ESExpResult *
term_eval_caststring (struct _ESExp *f,
                      gint argc,
                      struct _ESExpResult **argv,
                      gpointer data)
{
	struct _ESExpResult *r;

	if (argc != 1)
		e_sexp_fatal_error (f, "Incorrect argument count to (cast-string )");

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	switch (argv[0]->type) {
	case ESEXP_RES_INT:
		r->value.string = g_strdup_printf ("%d", argv[0]->value.number);
		break;
	case ESEXP_RES_BOOL:
		r->value.string = g_strdup_printf ("%d", argv[0]->value.boolean != 0);
		break;
	case ESEXP_RES_STRING:
		r->value.string = g_strdup (argv[0]->value.string);
		break;
	default:
		e_sexp_result_free (f, r);
		e_sexp_fatal_error (f, "Invalid type in (gint )");
	}

	return r;
}

/* implements 'if' function */
static ESExpResult *
term_eval_if (struct _ESExp *f,
              gint argc,
              struct _ESExpTerm **argv,
              gpointer data)
{
	struct _ESExpResult *r;
	gint doit;

	if (argc >=2 && argc <= 3) {
		r = e_sexp_term_eval (f, argv[0]);
		doit = (r->type == ESEXP_RES_BOOL && r->value.boolean);
		e_sexp_result_free (f, r);
		if (doit) {
			return e_sexp_term_eval (f, argv[1]);
		} else if (argc > 2) {
			return e_sexp_term_eval (f, argv[2]);
		}
	}
	return e_sexp_result_new (f, ESEXP_RES_UNDEFINED);
}

/* implements 'begin' statement */
static ESExpResult *
term_eval_begin (struct _ESExp *f,
                 gint argc,
                 struct _ESExpTerm **argv,
                 gpointer data)
{
	struct _ESExpResult *r = NULL;
	gint i;

	for (i = 0; i < argc; i++) {
		if (r)
			e_sexp_result_free (f, r);
		r = e_sexp_term_eval (f, argv[i]);
	}
	if (r)
		return r;
	else
		return e_sexp_result_new (f, ESEXP_RES_UNDEFINED);
}

/* this must only be called from inside term evaluation callbacks! */
struct _ESExpResult *
e_sexp_term_eval (struct _ESExp *f,
                  struct _ESExpTerm *t)
{
	struct _ESExpResult *r = NULL;
	gint i;
	struct _ESExpResult **argv;

	g_return_val_if_fail (t != NULL, NULL);

	r (printf ("eval term :\n"));
	r (parse_dump_term (t, 0));

	switch (t->type) {
	case ESEXP_TERM_STRING:
		r (printf (" (string \"%s\")\n", t->value.string));
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		/* erk, this shoul;dn't need to strdup this ... */
		r->value.string = g_strdup (t->value.string);
		break;
	case ESEXP_TERM_INT:
		r (printf (" (gint %d)\n", t->value.number));
		r = e_sexp_result_new (f, ESEXP_RES_INT);
		r->value.number = t->value.number;
		break;
	case ESEXP_TERM_BOOL:
		r (printf (" (gint %d)\n", t->value.number));
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = t->value.boolean;
		break;
	case ESEXP_TERM_TIME:
		r (printf (" (time_t %ld)\n", t->value.time));
		r = e_sexp_result_new (f, ESEXP_RES_TIME);
		r->value.time = t->value.time;
		break;
	case ESEXP_TERM_IFUNC:
		if (t->value.func.sym && t->value.func.sym->f.ifunc)
			r = t->value.func.sym->f.ifunc (
				f, t->value.func.termcount,
				t->value.func.terms, t->value.func.sym->data);
		break;
	case ESEXP_TERM_FUNC:
		/* first evaluate all arguments to result types */
		argv = alloca (sizeof (argv[0]) * t->value.func.termcount);
		for (i = 0; i < t->value.func.termcount; i++) {
			argv[i] = e_sexp_term_eval (f, t->value.func.terms[i]);
		}
		/* call the function */
		if (t->value.func.sym->f.func)
			r = t->value.func.sym->f.func (
				f, t->value.func.termcount,
				argv, t->value.func.sym->data);

		e_sexp_resultv_free (f, t->value.func.termcount, argv);
		break;
	default:
		e_sexp_fatal_error (f, "Unknown type in parse tree: %d", t->type);
	}

	if (r == NULL)
		r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

#ifdef TESTER
static void
eval_dump_result (ESExpResult *r,
                  gint depth)
{
	gint i;

	if (r == NULL) {
		printf ("null result???\n");
		return;
	}

	for (i = 0; i < depth; i++)
		printf ("   ");

	switch (r->type) {
	case ESEXP_RES_ARRAY_PTR:
		printf ("array pointers\n");
		break;
	case ESEXP_RES_INT:
		printf ("int: %d\n", r->value.number);
		break;
	case ESEXP_RES_STRING:
		printf ("string: '%s'\n", r->value.string);
		break;
	case ESEXP_RES_BOOL:
		printf ("bool: %c\n", r->value.boolean ? 't':'f');
		break;
	case ESEXP_RES_TIME:
		printf ("time_t: %ld\n", (glong) r->value.time);
		break;
	case ESEXP_RES_UNDEFINED:
		printf (" <undefined>\n");
		break;
	}
	printf ("\n");
}
#endif

#ifdef TESTER
static void
parse_dump_term (struct _ESExpTerm *t,
                 gint depth)
{
	gint i;

	if (t == NULL) {
		printf ("null term??\n");
		return;
	}

	for (i = 0; i < depth; i++)
		printf ("   ");

	switch (t->type) {
	case ESEXP_TERM_STRING:
		printf (" \"%s\"", t->value.string);
		break;
	case ESEXP_TERM_INT:
		printf (" %d", t->value.number);
		break;
	case ESEXP_TERM_BOOL:
		printf (" #%c", t->value.boolean ? 't':'f');
		break;
	case ESEXP_TERM_TIME:
		printf (" %ld", (glong) t->value.time);
		break;
	case ESEXP_TERM_IFUNC:
	case ESEXP_TERM_FUNC:
		printf (" (function %s\n", t->value.func.sym->name);
		/*printf(" [%d] ", t->value.func.termcount);*/
		for (i = 0; i < t->value.func.termcount; i++) {
			parse_dump_term (t->value.func.terms[i], depth + 1);
		}
		for (i = 0; i < depth; i++)
			printf ("   ");
		printf (" )");
		break;
	case ESEXP_TERM_VAR:
		printf (" (variable %s )\n", t->value.var->name);
		break;
	default:
		printf ("unknown type: %d\n", t->type);
	}

	printf ("\n");
}
#endif

const gchar *time_functions[] = {
	"time-now",
	"make-time",
	"time-add-day",
	"time-day-begin",
	"time-day-end"
};

static gboolean
occur_in_time_range_generator (gint argc,
                  struct _ESExpResult **argv,
                  struct _ESExpResult *r)
{
	g_return_val_if_fail (r != NULL, FALSE);
	g_return_val_if_fail (argc == 2 || argc == 3, FALSE);

	if ((argv[0]->type != ESEXP_RES_TIME) || (argv[1]->type != ESEXP_RES_TIME))
		return FALSE;

	r->occuring_start = argv[0]->value.time;
	r->occuring_end = argv[1]->value.time;

	return TRUE;
}

static gboolean
binary_generator (gint argc,
                  struct _ESExpResult **argv,
                  struct _ESExpResult *r)
{
	g_return_val_if_fail (r != NULL, FALSE);
	g_return_val_if_fail (argc == 2, FALSE);

	if ((argv[0]->type != ESEXP_RES_TIME) || (argv[1]->type != ESEXP_RES_TIME))
		return FALSE;

	r->occuring_start = argv[0]->value.time;
	r->occuring_end = argv[1]->value.time;

	return TRUE;
}

static gboolean
unary_generator (gint argc,
                 struct _ESExpResult **argv,
                 struct _ESExpResult *r)
{
	/* unary generator with end time */
	g_return_val_if_fail (r != NULL, FALSE);
	g_return_val_if_fail (argc == 1, FALSE);

	if (argv[0]->type != ESEXP_RES_TIME)
		return FALSE;

	r->occuring_start = 0;
	r->occuring_end = argv[0]->value.time;

	return TRUE;
}

static const struct {
	const gchar *name;
	ESGeneratorFunc *func;
} generators[] = {
	{"occur-in-time-range?", occur_in_time_range_generator},
	{"due-in-time-range?", binary_generator},
	{"has-alarms-in-range?", binary_generator},
	{"completed-before?", unary_generator},
};

const gint generators_count = sizeof (generators) / sizeof (generators[0]);

static gboolean
or_operator (gint argc,
             struct _ESExpResult **argv,
             struct _ESExpResult *r)
{
	gint ii;

	/*
	 * A          B           A or B
	 * ----       ----        ------
	 * norm (0)   norm (0)    norm (0)
	 * gen (1)    norm (0)    norm (0)
	 * norm (0)   gen (1)     norm (0)
	 * gen (1)    gen (1)     gen*(1)
	 */

	g_return_val_if_fail (r != NULL, FALSE);
	g_return_val_if_fail (argc > 0, FALSE);

	r->time_generator = TRUE;
	for (ii = 0; ii < argc && r->time_generator; ii++) {
		r->time_generator = argv[ii]->time_generator;
	}

	if (r->time_generator) {
		r->occuring_start = argv[0]->occuring_start;
		r->occuring_end = argv[0]->occuring_end;

		for (ii = 1; ii < argc; ii++) {
			r->occuring_start = MIN (r->occuring_start, argv[ii]->occuring_start);
			r->occuring_end = MAX (r->occuring_end, argv[ii]->occuring_end);
		}
	}

	return TRUE;
}

static gboolean
and_operator (gint argc,
              struct _ESExpResult **argv,
              struct _ESExpResult *r)
{
	gint ii;

	/*
	 * A           B          A and B
	 * ----        ----       ------- -
	 * norm (0)     norm (0)    norm (0)
	 * gen (1)      norm (0)    gen (1)
	 * norm (0)     gen (1)     gen (1)
	 * gen (1)      gen (1)     gen (1)
	 * */

	g_return_val_if_fail (r != NULL, FALSE);
	g_return_val_if_fail (argc > 0, FALSE);

	r->time_generator = FALSE;
	for (ii = 0; ii < argc && !r->time_generator; ii++) {
		r->time_generator = argv[ii]->time_generator;
	}

	if (r->time_generator) {
		r->occuring_start = argv[0]->occuring_start;
		r->occuring_end = argv[0]->occuring_end;

		for (ii = 1; ii < argc; ii++) {
			r->occuring_start = MAX (r->occuring_start, argv[ii]->occuring_start);
			r->occuring_end = MIN (r->occuring_end, argv[ii]->occuring_end);
		}
	}

	return TRUE;
}

static const struct {
	const gchar *name;
	ESOperatorFunc *func;
} operators[] = {
	{"or", or_operator},
	{"and", and_operator}
};

const gint operators_count = sizeof (operators) / sizeof (operators[0]);

static ESOperatorFunc *
get_operator_function (const gchar *fname)
{
	gint i;

	g_return_val_if_fail (fname != NULL, NULL);

	for (i = 0; i < sizeof (operators) / sizeof (operators[0]); i++)
		if (strcmp (operators[i].name, fname) == 0)
			return operators[i].func;

	return NULL;
}

static inline gboolean
is_time_function (const gchar *fname)
{
	gint i;

	g_return_val_if_fail (fname != NULL, FALSE);

	for (i = 0; i < sizeof (time_functions) / sizeof (time_functions[0]); i++)
		if (strcmp (time_functions[i], fname) == 0)
			return TRUE;

	return FALSE;
}

static ESGeneratorFunc *
get_generator_function (const gchar *fname)
{
	gint i;

	g_return_val_if_fail (fname != NULL, NULL);

	for (i = 0; i < sizeof (generators) / sizeof (generators[0]); i++)
		if (strcmp (generators[i].name, fname) == 0)
			return generators[i].func;

	return NULL;
}

/* this must only be called from inside term evaluation callbacks! */
static struct _ESExpResult *
e_sexp_term_evaluate_occur_times (struct _ESExp *f,
                                  struct _ESExpTerm *t,
                                  time_t *start,
                                  time_t *end)
{
	struct _ESExpResult *r = NULL;
	gint i, argc;
	struct _ESExpResult **argv;
	gboolean ok = TRUE;

	g_return_val_if_fail (t != NULL, NULL);
	g_return_val_if_fail (start != NULL, NULL);
	g_return_val_if_fail (end != NULL, NULL);

	/*
	printf ("eval term :\n");
	parse_dump_term (t, 0);
	*/

	switch (t->type) {
	case ESEXP_TERM_STRING:
		r (printf (" (string \"%s\")\n", t->value.string));
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		r->value.string = g_strdup (t->value.string);
		break;
	case ESEXP_TERM_IFUNC:
	case ESEXP_TERM_FUNC:
	{
		ESGeneratorFunc *generator = NULL;
		ESOperatorFunc *operator = NULL;

		r (printf (" (function \"%s\"\n", t->value.func.sym->name));

		r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);
		argc = t->value.func.termcount;
		argv = alloca (sizeof (argv[0]) * argc);

		for (i = 0; i < t->value.func.termcount; i++) {
			argv[i] = e_sexp_term_evaluate_occur_times (
				f, t->value.func.terms[i], start, end);
		}

		if (is_time_function (t->value.func.sym->name)) {
			/* evaluate time */
			if (t->value.func.sym->f.func)
				r = t->value.func.sym->f.func (f, t->value.func.termcount,
					      argv, t->value.func.sym->data);
		} else if ((generator = get_generator_function (t->value.func.sym->name)) != NULL) {
			/* evaluate generator function */
			r->time_generator = TRUE;
			ok = generator (argc, argv, r);
		} else if ((operator = get_operator_function (t->value.func.sym->name)) != NULL)
			/* evaluate operator function */
			ok = operator (argc, argv, r);
		else {
			/* normal function: we need to scan all objects */
			r->time_generator = FALSE;
		}

		e_sexp_resultv_free (f, t->value.func.termcount, argv);
		break;
	}
	case ESEXP_TERM_INT:
	case ESEXP_TERM_BOOL:
	case ESEXP_TERM_TIME:
		break;
	default:
		ok = FALSE;
		break;
	}

	if (!ok)
		e_sexp_fatal_error (f, "Error in parse tree");

	if (r == NULL)
		r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

/*
  PARSER
*/

static struct _ESExpTerm *
parse_term_new (struct _ESExp *f,
                gint type)
{
	struct _ESExpTerm *s = e_memchunk_alloc0 (f->term_chunks);
	s->type = type;
	return s;
}

static void
parse_term_free (struct _ESExp *f,
                 struct _ESExpTerm *t)
{
	gint i;

	if (t == NULL) {
		return;
	}

	switch (t->type) {
	case ESEXP_TERM_INT:
	case ESEXP_TERM_BOOL:
	case ESEXP_TERM_TIME:
	case ESEXP_TERM_VAR:
		break;

	case ESEXP_TERM_STRING:
		g_free (t->value.string);
		break;

	case ESEXP_TERM_FUNC:
	case ESEXP_TERM_IFUNC:
		for (i = 0; i < t->value.func.termcount; i++) {
			parse_term_free (f, t->value.func.terms[i]);
		}
		g_free (t->value.func.terms);
		break;

	default:
		printf ("parse_term_free: unknown type: %d\n", t->type);
	}
	e_memchunk_free (f->term_chunks, t);
}

static struct _ESExpTerm **
parse_values (ESExp *f,
              gint *len)
{
	gint token;
	struct _ESExpTerm **terms;
	gint i, size = 0;
	GScanner *gs = f->scanner;
	GSList *list = NULL, *l;

	p (printf ("parsing values\n"));

	while ( (token = g_scanner_peek_next_token (gs)) != G_TOKEN_EOF
		&& token != ')') {
		list = g_slist_prepend (list, parse_value (f));
		size++;
	}

	/* go over the list, and put them backwards into the term array */
	terms = g_malloc (size * sizeof (*terms));
	l = list;
	for (i = size - 1; i >= 0; i--) {
		g_assert (l);
		g_assert (l->data);
		terms[i] = l->data;
		l = g_slist_next (l);
	}
	g_slist_free (list);

	p (printf ("found %d subterms\n", size));
	*len = size;

	p (printf ("done parsing values\n"));
	return terms;
}

/**
 * e_sexp_parse_value:
 *
 * Since: 2.28
 **/
ESExpTerm *
e_sexp_parse_value (ESExp *f)
{
	return parse_value (f);
}

static struct _ESExpTerm *
parse_value (ESExp *f)
{
	gint token, negative = FALSE;
	struct _ESExpTerm *t = NULL;
	GScanner *gs = f->scanner;
	struct _ESExpSymbol *s;

	p (printf ("parsing value\n"));

	token = g_scanner_get_next_token (gs);
	switch (token) {
	case G_TOKEN_EOF:
		break;
	case G_TOKEN_LEFT_PAREN:
		p (printf ("got brace, its a list!\n"));
		return parse_list (f, TRUE);
	case G_TOKEN_STRING:
		p (printf (
			"got string '%s'\n",
			g_scanner_cur_value (gs).v_string));
		t = parse_term_new (f, ESEXP_TERM_STRING);
		t->value.string = g_strdup (g_scanner_cur_value (gs).v_string);
		break;
	case '-':
		p (printf ("got negative int?\n"));
		token = g_scanner_get_next_token (gs);
		if (token != G_TOKEN_INT) {
			e_sexp_fatal_error (
				f, "Invalid format for a integer value");
			return NULL;
		}

		negative = TRUE;
		/* fall through... */
	case G_TOKEN_INT:
		t = parse_term_new (f, ESEXP_TERM_INT);
		t->value.number = g_scanner_cur_value (gs).v_int;
		if (negative)
			t->value.number = -t->value.number;
		p (printf ("got gint %d\n", t->value.number));
		break;
	case '#': {
		gchar *str;

		p (printf ("got bool?\n"));
		token = g_scanner_get_next_token (gs);
		if (token != G_TOKEN_IDENTIFIER) {
			e_sexp_fatal_error (
				f, "Invalid format for a boolean value");
			return NULL;
		}

		str = g_scanner_cur_value (gs).v_identifier;

		g_assert (str != NULL);
		if (!(strlen (str) == 1 && (str[0] == 't' || str[0] == 'f'))) {
			e_sexp_fatal_error (
				f, "Invalid format for a boolean value");
			return NULL;
		}

		t = parse_term_new (f, ESEXP_TERM_BOOL);
		t->value.boolean = (str[0] == 't');
		break; }
	case G_TOKEN_SYMBOL:
		s = g_scanner_cur_value (gs).v_symbol;
		p (printf ("got symbol '%s'\n", s->name));
		switch (s->type) {
		case ESEXP_TERM_FUNC:
		case ESEXP_TERM_IFUNC:
			/* this is basically invalid, since we can't use
			 * function pointers, but let the runtime catch it */
			t = parse_term_new (f, s->type);
			t->value.func.sym = s;
			t->value.func.terms = parse_values (
				f, &t->value.func.termcount);
			break;
		case ESEXP_TERM_VAR:
			t = parse_term_new (f, s->type);
			t->value.var = s;
			break;
		default:
			e_sexp_fatal_error (
				f, "Invalid symbol type: %s: %d",
				s->name, s->type);
		}
		break;
	case G_TOKEN_IDENTIFIER:
		p (printf (
			"got unknown identifider '%s'\n",
			g_scanner_cur_value (gs).v_identifier));
		e_sexp_fatal_error (
			f, "Unknown identifier: %s",
			g_scanner_cur_value (gs).v_identifier);
		break;
	default:
		e_sexp_fatal_error (
			f, "Unexpected token encountered: %d", token);
	}
	p (printf ("done parsing value\n"));
	return t;
}

/* FIXME: this needs some robustification */
static struct _ESExpTerm *
parse_list (ESExp *f,
            gint gotbrace)
{
	gint token;
	struct _ESExpTerm *t = NULL;
	GScanner *gs = f->scanner;

	p (printf ("parsing list\n"));
	if (gotbrace)
		token = '(';
	else
		token = g_scanner_get_next_token (gs);
	if (token =='(') {
		token = g_scanner_get_next_token (gs);
		switch (token) {
		case G_TOKEN_SYMBOL: {
			struct _ESExpSymbol *s;

			s = g_scanner_cur_value (gs).v_symbol;
			p (printf ("got funciton: %s\n", s->name));
			t = parse_term_new (f, s->type);
			p (printf ("created new list %p\n", t));
			/* if we have a variable, find out its base type */
			while (s->type == ESEXP_TERM_VAR) {
				s = ((ESExpTerm *)(s->data))->value.var;
			}
			if (s->type == ESEXP_TERM_FUNC
			    || s->type == ESEXP_TERM_IFUNC) {
				t->value.func.sym = s;
				t->value.func.terms = parse_values (
					f, &t->value.func.termcount);
			} else {
				parse_term_free (f, t);
				e_sexp_fatal_error (
					f, "Trying to call variable "
					"as function: %s", s->name);
			}
			break; }
		case G_TOKEN_IDENTIFIER:
			e_sexp_fatal_error (
				f, "Unknown identifier: %s",
				g_scanner_cur_value (gs).v_identifier);
			break;
		case G_TOKEN_LEFT_PAREN:
			return parse_list (f, TRUE);
		default:
			e_sexp_fatal_error (f, "Unexpected token encountered: %d", token);
		}
		token = g_scanner_get_next_token (gs);
		if (token != ')') {
			e_sexp_fatal_error (f, "Missing ')'");
		}
	} else {
		e_sexp_fatal_error (f, "Missing '('");
	}

	p (printf ("returning list %p\n", t));
	return t;
}

static void e_sexp_finalise (gpointer);

#ifdef E_SEXP_IS_G_OBJECT
static void
e_sexp_class_init (ESExpClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = e_sexp_finalise;
}
#endif

/* 'builtin' functions */
static const struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "and", (ESExpFunc *) term_eval_and, 1 },
	{ "or", (ESExpFunc *) term_eval_or, 1 },
	{ "not", (ESExpFunc *) term_eval_not, 0 },
	{ "<", (ESExpFunc *) term_eval_lt, 1 },
	{ ">", (ESExpFunc *) term_eval_gt, 1 },
	{ "=", (ESExpFunc *) term_eval_eq, 1 },
	{ "+", (ESExpFunc *) term_eval_plus, 0 },
	{ "-", (ESExpFunc *) term_eval_sub, 0 },
	{ "cast-int", (ESExpFunc *) term_eval_castint, 0 },
	{ "cast-string", (ESExpFunc *) term_eval_caststring, 0 },
	{ "if", (ESExpFunc *) term_eval_if, 1 },
	{ "begin", (ESExpFunc *) term_eval_begin, 1 },
};

static void
free_symbol (gpointer key,
             gpointer value,
             gpointer data)
{
	struct _ESExpSymbol *s = value;

	g_free (s->name);
	g_free (s);
}

static void
e_sexp_finalise (gpointer o)
{
	ESExp *s = (ESExp *) o;

	if (s->tree) {
		parse_term_free (s, s->tree);
		s->tree = NULL;
	}

	g_free (s->error);

	e_memchunk_destroy (s->term_chunks);
	e_memchunk_destroy (s->result_chunks);

	g_scanner_scope_foreach_symbol (s->scanner, 0, free_symbol, NULL);
	g_scanner_destroy (s->scanner);

#ifdef E_SEXP_IS_G_OBJECT
	G_OBJECT_CLASS (e_sexp_parent_class)->finalize (o);
#endif
}

static void
e_sexp_init (ESExp *s)
{
	gint i;

	s->scanner = g_scanner_new (&scanner_config);
	s->term_chunks = e_memchunk_new (16, sizeof (struct _ESExpTerm));
	s->result_chunks = e_memchunk_new (16, sizeof (struct _ESExpResult));

	/* load in builtin symbols? */
	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction (
				s, 0,
				symbols[i].name,
				(ESExpIFunc *) symbols[i].func,
				(gpointer) &symbols[i]);
		} else {
			e_sexp_add_function (
				s, 0,
				symbols[i].name,
				symbols[i].func,
				(gpointer) &symbols[i]);
		}
	}

#ifndef E_SEXP_IS_G_OBJECT
	s->refcount = 1;
#endif
}

ESExp *
e_sexp_new (void)
{
#ifdef E_SEXP_IS_G_OBJECT
	ESExp *f = (ESexp *) g_object_new (E_TYPE_SEXP, NULL);
#else
	ESExp *f = g_malloc0 (sizeof (ESExp));
	e_sexp_init (f);
#endif

	return f;
}

#ifndef E_SEXP_IS_G_OBJECT
void
e_sexp_ref (ESExp *f)
{
	f->refcount++;
}

void
e_sexp_unref (ESExp *f)
{
	f->refcount--;
	if (f->refcount == 0) {
		e_sexp_finalise (f);
		g_free (f);
	}
}
#endif

void
e_sexp_add_function (ESExp *f,
                     gint scope,
                     const gchar *name,
                     ESExpFunc *func,
                     gpointer data)
{
	struct _ESExpSymbol *s;

	g_return_if_fail (IS_E_SEXP (f));
	g_return_if_fail (name != NULL);

	e_sexp_remove_symbol (f, scope, name);

	s = g_malloc0 (sizeof (*s));
	s->name = g_strdup (name);
	s->f.func = func;
	s->type = ESEXP_TERM_FUNC;
	s->data = data;
	g_scanner_scope_add_symbol (f->scanner, scope, s->name, s);
}

void
e_sexp_add_ifunction (ESExp *f,
                      gint scope,
                      const gchar *name,
                      ESExpIFunc *ifunc,
                      gpointer data)
{
	struct _ESExpSymbol *s;

	g_return_if_fail (IS_E_SEXP (f));
	g_return_if_fail (name != NULL);

	e_sexp_remove_symbol (f, scope, name);

	s = g_malloc0 (sizeof (*s));
	s->name = g_strdup (name);
	s->f.ifunc = ifunc;
	s->type = ESEXP_TERM_IFUNC;
	s->data = data;
	g_scanner_scope_add_symbol (f->scanner, scope, s->name, s);
}

void
e_sexp_add_variable (ESExp *f,
                     gint scope,
                     gchar *name,
                     ESExpTerm *value)
{
	struct _ESExpSymbol *s;

	g_return_if_fail (IS_E_SEXP (f));
	g_return_if_fail (name != NULL);

	s = g_malloc0 (sizeof (*s));
	s->name = g_strdup (name);
	s->type = ESEXP_TERM_VAR;
	s->data = value;
	g_scanner_scope_add_symbol (f->scanner, scope, s->name, s);
}

void
e_sexp_remove_symbol (ESExp *f,
                      gint scope,
                      const gchar *name)
{
	gint oldscope;
	struct _ESExpSymbol *s;

	g_return_if_fail (IS_E_SEXP (f));
	g_return_if_fail (name != NULL);

	oldscope = g_scanner_set_scope (f->scanner, scope);
	s = g_scanner_lookup_symbol (f->scanner, name);
	g_scanner_scope_remove_symbol (f->scanner, scope, name);
	g_scanner_set_scope (f->scanner, oldscope);
	if (s) {
		g_free (s->name);
		g_free (s);
	}
}

gint
e_sexp_set_scope (ESExp *f,
                  gint scope)
{
	g_return_val_if_fail (IS_E_SEXP (f), 0);

	return g_scanner_set_scope (f->scanner, scope);
}

void
e_sexp_input_text (ESExp *f,
                   const gchar *text,
                   gint len)
{
	g_return_if_fail (IS_E_SEXP (f));
	g_return_if_fail (text != NULL);

	g_scanner_input_text (f->scanner, text, len);
}

void
e_sexp_input_file (ESExp *f,
                   gint fd)
{
	g_return_if_fail (IS_E_SEXP (f));

	g_scanner_input_file (f->scanner, fd);
}

/* returns -1 on error */
gint
e_sexp_parse (ESExp *f)
{
	g_return_val_if_fail (IS_E_SEXP (f), -1);

	if (setjmp (f->failenv))
		return -1;

	if (f->tree)
		parse_term_free (f, f->tree);

	f->tree = parse_value (f);

	return 0;
}

/* returns NULL on error */
struct _ESExpResult *
e_sexp_eval (ESExp *f)
{
	g_return_val_if_fail (IS_E_SEXP (f), NULL);
	g_return_val_if_fail (f->tree != NULL, NULL);

	if (setjmp (f->failenv)) {
		g_warning ("Error in execution: %s", f->error);
		return NULL;
	}

	return e_sexp_term_eval (f, f->tree);
}

/**
 * e_cal_backend_sexp_evaluate_occur_times:
 * @f: An #ESExp object.
 * @start: Start of the time window will be stored here.
 * @end: End of the time window will be stored here.
 *
 * Determines biggest time window given by expressions "occur-in-range" in sexp.
 *
 * Since: 2.32
 */
gboolean
e_sexp_evaluate_occur_times (ESExp *f,
                             time_t *start,
                             time_t *end)
{
	struct _ESExpResult *r;
	gboolean generator;
	g_return_val_if_fail (IS_E_SEXP (f), FALSE);
	g_return_val_if_fail (f->tree != NULL, FALSE);
	g_return_val_if_fail (start != NULL, FALSE);
	g_return_val_if_fail (end != NULL, FALSE);

	*start = *end = -1;

	if (setjmp (f->failenv)) {
		g_warning ("Error in execution: %s", f->error);
		return FALSE;
	}

	r = e_sexp_term_evaluate_occur_times (f, f->tree, start, end);
	generator = r->time_generator;

	if (generator) {
		*start = r->occuring_start;
		*end = r->occuring_end;
	}

	e_sexp_result_free (f, r);

	return generator;
}

/**
 * e_sexp_encode_bool:
 * @s: A #GString to append to
 * @state: The boolean value
 *
 * Encode a bool into an s-expression @s.  Bools are
 * encoded using #t #f syntax.
 **/
void
e_sexp_encode_bool (GString *s,
                    gboolean state)
{
	if (state)
		g_string_append (s, " #t");
	else
		g_string_append (s, " #f");
}

/**
 * e_sexp_encode_string:
 * @s: Destination string.
 * @string: String expression.
 *
 * Add a c string @string to the s-expression stored in
 * the gstring @s.  Quotes are added, and special characters
 * are escaped appropriately.
 **/
void
e_sexp_encode_string (GString *s,
                      const gchar *string)
{
	gchar c;
	const gchar *p;

	if (string == NULL)
		p = "";
	else
		p = string;
	g_string_append (s, " \"");
	while ((c = *p++)) {
		if (c == '\\' || c == '\"' || c == '\'')
			g_string_append_c (s, '\\');
		g_string_append_c (s, c);
	}
	g_string_append (s, "\"");
}

#ifdef TESTER
gint main (gint argc, gchar **argv)
{
	ESExp *f;
	gchar *t = "(+ \"foo\" \"\\\"\" \"bar\" \"\\\\ blah \\x \")";
	ESExpResult *r;

	f = e_sexp_new ();

	e_sexp_add_variable (f, 0, "test", NULL);

	if (argc < 2 || !argv[1])
		return;

	e_sexp_input_text (f, t, t);
	e_sexp_parse (f);

	if (f->tree) {
		parse_dump_term (f->tree, 0);
	}

	r = e_sexp_eval (f);
	if (r) {
		eval_dump_result (r, 0);
	} else {
		printf ("no result?|\n");
	}

	return 0;
}
#endif
