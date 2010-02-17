/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>

#include "e-book-query.h"
#include "libedataserver/e-sexp.h"

#include <stdarg.h>
#include <string.h>

typedef enum {
	E_BOOK_QUERY_TYPE_AND,
	E_BOOK_QUERY_TYPE_OR,
	E_BOOK_QUERY_TYPE_NOT,
	E_BOOK_QUERY_TYPE_FIELD_EXISTS,
	E_BOOK_QUERY_TYPE_FIELD_TEST,
	E_BOOK_QUERY_TYPE_ANY_FIELD_CONTAINS
} EBookQueryType;

struct EBookQuery {
	EBookQueryType type;
	gint ref_count;

	union {
		struct {
			guint          nqs;
			EBookQuery   **qs;
		} andor;

		struct {
			EBookQuery    *q;
		} not;

		struct {
			EBookQueryTest test;
			gchar          *field_name;
			gchar          *value;
		} field_test;

		struct {
			EContactField  field;
			gchar          *vcard_field;
		} exist;

		struct {
			gchar          *value;
		} any_field_contains;
	} query;
};

static EBookQuery *
conjoin (EBookQueryType type, gint nqs, EBookQuery **qs, gboolean unref)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);
	gint i;

	ret->type = type;
	ret->query.andor.nqs = nqs;
	ret->query.andor.qs = g_new (EBookQuery *, nqs);
	for (i = 0; i < nqs; i++) {
		ret->query.andor.qs[i] = qs[i];
		if (!unref)
			e_book_query_ref (qs[i]);
	}

	return ret;
}

/**
 * e_book_query_and:
 * @nqs: the number of queries to AND
 * @qs: pointer to an array of #EBookQuery items
 * @unref: if %TRUE, the new query takes ownership of the existing queries
 *
 * Create a new #EBookQuery which is the logical AND of the queries in #qs.
 *
 * Return value: A new #EBookQuery
 **/
EBookQuery *
e_book_query_and (gint nqs, EBookQuery **qs, gboolean unref)
{
	return conjoin (E_BOOK_QUERY_TYPE_AND, nqs, qs, unref);
}

/**
 * e_book_query_or:
 * @nqs: the number of queries to OR
 * @qs: pointer to an array of #EBookQuery items
 * @unref: if #TRUE, the new query takes ownership of the existing queries
 *
 * Creates a new #EBookQuery which is the logical OR of the queries in #qs.
 *
 * Return value: A new #EBookQuery
 **/
EBookQuery *
e_book_query_or (gint nqs, EBookQuery **qs, gboolean unref)
{
	return conjoin (E_BOOK_QUERY_TYPE_OR, nqs, qs, unref);
}

static EBookQuery *
conjoinv (EBookQueryType type, EBookQuery *q, va_list ap)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);
	GPtrArray *qs;

	qs = g_ptr_array_new ();
	while (q) {
		g_ptr_array_add (qs, q);
		q = va_arg (ap, EBookQuery *);
	}
	va_end (ap);

	ret->type = type;
	ret->query.andor.nqs = qs->len;
	ret->query.andor.qs = (EBookQuery **)qs->pdata;
	g_ptr_array_free (qs, FALSE);

	return ret;
}

/**
 * e_book_query_andv:
 * @q: first #EBookQuery
 * @Varargs: #NULL terminated list of #EBookQuery pointers
 *
 * Creates a new #EBookQuery which is the logical AND of the queries specified.
 *
 * Return value: A new #EBookQuery
 **/
EBookQuery *
e_book_query_andv (EBookQuery *q, ...)
{
	va_list ap;

	va_start (ap, q);
	return conjoinv (E_BOOK_QUERY_TYPE_AND, q, ap);
}

/**
 * e_book_query_orv:
 * @q: first #EBookQuery
 * @Varargs: #NULL terminated list of #EBookQuery pointers
 *
 * Creates a new #EBookQuery which is the logical OR of the queries specified.
 *
 * Return value: A new #EBookQuery
 **/
EBookQuery *
e_book_query_orv (EBookQuery *q, ...)
{
	va_list ap;

	va_start (ap, q);
	return conjoinv (E_BOOK_QUERY_TYPE_OR, q, ap);
}

/**
 * e_book_query_not:
 * @q: an #EBookQuery
 * @unref: if #TRUE, the new query takes ownership of the existing queries
 *
 * Creates a new #EBookQuery which is the opposite of #q.
 *
 * Return value: the new #EBookQuery
 **/
EBookQuery *
e_book_query_not (EBookQuery *q, gboolean unref)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_NOT;
	ret->query.not.q = q;
	if (!unref)
		e_book_query_ref (q);

	return ret;
}

/**
 * e_book_query_field_test:
 * @field: an #EContactField to test
 * @test: the test to apply
 * @value: the value to test for
 *
 * Creates a new #EBookQuery which tests @field for @value using the test @test.
 *
 * Return value: the new #EBookQuery
 **/
EBookQuery *
e_book_query_field_test (EContactField field,
			 EBookQueryTest test,
			 const gchar *value)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_FIELD_TEST;
	ret->query.field_test.field_name = g_strdup (e_contact_field_name (field));
	ret->query.field_test.test = test;
	ret->query.field_test.value = g_strdup (value);

	return ret;
}

/**
 * e_book_query_vcard_field_test:
 * @field: a EVCard field name to test
 * @test: the test to apply
 * @value: the value to test for
 *
 * Creates a new #EBookQuery which tests @field for @value using the test @test.
 *
 * Return value: the new #EBookQuery
 **/
EBookQuery *
e_book_query_vcard_field_test (const gchar     *field,
			       EBookQueryTest  test,
			       const gchar     *value)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_FIELD_TEST;
	ret->query.field_test.field_name = g_strdup (field);
	ret->query.field_test.test = test;
	ret->query.field_test.value = g_strdup (value);

	return ret;
}

/**
 * e_book_query_field_exists:
 * @field: a #EContactField
 *
 * Creates a new #EBookQuery which tests if the field @field exists.
 * Return value: the new #EBookQuery
 **/
EBookQuery *
e_book_query_field_exists (EContactField field)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_FIELD_EXISTS;
	ret->query.exist.field = field;
	ret->query.exist.vcard_field = NULL;

	return ret;
}

/**
 * e_book_query_vcard_field_exists:
 * @field: a field name
 *
 * Creates a new #EBookQuery which tests if the field @field exists. @field
 * should be a vCard field name, such as #FN or #X-MSN.
 * Return value: the new #EBookQuery
 **/
EBookQuery *
e_book_query_vcard_field_exists (const gchar *field)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_FIELD_EXISTS;
	ret->query.exist.field = 0;
	ret->query.exist.vcard_field = g_strdup (field);

	return ret;
}

/**
 * e_book_query_any_field_contains:
 * @value: a value
 *
 * Creates a new #EBookQuery which tests if any field contains @value.
 *
 * Return value: the new #EBookQuery
 **/
EBookQuery *
e_book_query_any_field_contains (const gchar *value)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_ANY_FIELD_CONTAINS;
	ret->query.any_field_contains.value = g_strdup (value);

	return ret;
}

/**
 * e_book_query_unref:
 * @q: an #EBookQuery
 *
 * Decrement the reference count on @q. When the reference count reaches 0, @q
 * will be freed and any child queries will have e_book_query_unref() called.
 **/
void
e_book_query_unref (EBookQuery *q)
{
	gint i;

	if (q->ref_count--)
		return;

	switch (q->type) {
	case E_BOOK_QUERY_TYPE_AND:
	case E_BOOK_QUERY_TYPE_OR:
		for (i = 0; i < q->query.andor.nqs; i++)
			e_book_query_unref (q->query.andor.qs[i]);
		g_free (q->query.andor.qs);
		break;

	case E_BOOK_QUERY_TYPE_NOT:
		e_book_query_unref (q->query.not.q);
		break;

	case E_BOOK_QUERY_TYPE_FIELD_TEST:
		g_free (q->query.field_test.field_name);
		g_free (q->query.field_test.value);
		break;

	case E_BOOK_QUERY_TYPE_FIELD_EXISTS:
		g_free (q->query.exist.vcard_field);
		break;

	case E_BOOK_QUERY_TYPE_ANY_FIELD_CONTAINS:
		g_free (q->query.any_field_contains.value);
		break;

	default:
		break;
	}

	g_free (q);
}

/**
 * e_book_query_ref:
 * @q: a #EBookQuery
 *
 * Increment the reference count on @q.
 * Return value: @q
 **/
EBookQuery *
e_book_query_ref (EBookQuery *q)
{
	q->ref_count++;
	return q;
}

static ESExpResult *
func_and(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;
	EBookQuery **qs;

	if (argc > 0) {
		gint i;

		qs = g_new0(EBookQuery*, argc);

		for (i = 0; i < argc; i ++) {
			GList *list_head = *list;
			if (!list_head)
				break;
			qs[i] = list_head->data;
			*list = g_list_delete_link(*list, list_head);
		}

		*list = g_list_prepend(*list,
				       e_book_query_and (argc, qs, TRUE));

		g_free (qs);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_or(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;
	EBookQuery **qs;

	if (argc > 0) {
		gint i;

		qs = g_new0(EBookQuery*, argc);

		for (i = 0; i < argc; i ++) {
			GList *list_head = *list;
			if (!list_head)
				break;
			qs[i] = list_head->data;
			*list = g_list_delete_link(*list, list_head);
		}

		*list = g_list_prepend(*list,
				       e_book_query_or (argc, qs, TRUE));

		g_free (qs);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_not(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;

	/* just replace the head of the list with the NOT of it. */
	if (argc > 0) {
		EBookQuery *term = (*list)->data;
		(*list)->data = e_book_query_not (term, TRUE);
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_contains(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;

		if (!strcmp (propname, "x-evolution-any-field")) {
			*list = g_list_prepend (*list, e_book_query_any_field_contains (str));
		}
		else {
			EContactField field = e_contact_field_id (propname);

			if (field)
				*list = g_list_prepend (*list, e_book_query_field_test (field,
											E_BOOK_QUERY_CONTAINS,
											str));
			else
				*list = g_list_prepend (*list, e_book_query_vcard_field_test (propname,
											E_BOOK_QUERY_CONTAINS,
											str));
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;
		EContactField field = e_contact_field_id (propname);

		if (field)
			*list = g_list_prepend (*list, e_book_query_field_test (field,
										E_BOOK_QUERY_IS,
										str));
		else
			*list = g_list_prepend (*list, e_book_query_vcard_field_test (propname,
										E_BOOK_QUERY_IS,
										str));
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_beginswith(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;
		EContactField field = e_contact_field_id (propname);

		if (field)
			*list = g_list_prepend (*list, e_book_query_field_test (field,
										E_BOOK_QUERY_BEGINS_WITH,
										str));
		else
			*list = g_list_prepend (*list, e_book_query_vcard_field_test (propname,
										E_BOOK_QUERY_BEGINS_WITH,
										str));
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_endswith(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;
		EContactField field = e_contact_field_id (propname);

		if (field)
			*list = g_list_prepend (*list, e_book_query_field_test (field,
										E_BOOK_QUERY_ENDS_WITH,
										str));
		else
			*list = g_list_prepend (*list, e_book_query_vcard_field_test (propname,
										E_BOOK_QUERY_ENDS_WITH,
										str));
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_exists(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	GList **list = data;
	ESExpResult *r;

	if (argc == 1
	    && argv[0]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		EContactField field = e_contact_field_id (propname);

		if (field)
			*list = g_list_prepend (*list, e_book_query_field_exists (field));
		else
			*list = g_list_prepend (*list, e_book_query_vcard_field_exists (propname));
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

/* 'builtin' functions */
static const struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "and", func_and, 0 },
	{ "or", func_or, 0 },
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
	{ "exists", func_exists, 0 },
};

/**
 * e_book_query_from_string:
 * @query_string: the query
 *
 * Parse @query_string and return a new #EBookQuery representing it.
 *
 * Return value: the new #EBookQuery.
 **/
EBookQuery*
e_book_query_from_string  (const gchar *query_string)
{
	ESExp *sexp;
	ESExpResult *r;
	EBookQuery *retval;
	GList *list = NULL;
	gint i;

	sexp = e_sexp_new();

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction(sexp, 0, symbols[i].name,
					     (ESExpIFunc *)symbols[i].func, &list);
		} else {
			e_sexp_add_function(sexp, 0, symbols[i].name,
					    symbols[i].func, &list);
		}
	}

	e_sexp_input_text(sexp, query_string, strlen(query_string));
	e_sexp_parse(sexp);

	r = e_sexp_eval(sexp);

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	if (list) {
		if (list->next) {
			g_warning ("conversion to EBookQuery");
			retval = NULL;
			g_list_foreach (list, (GFunc)e_book_query_unref, NULL);
		}
		else {
			retval = list->data;
		}
	}
	else {
		g_warning ("conversion to EBookQuery failed");
		retval = NULL;
	}

	g_list_free (list);
	return retval;
}

/**
 * e_book_query_to_string:
 * @q: an #EBookQuery
 *
 * Return the string representation of @q.
 *
 * Return value: The string form of the query. This string should be freed when
 * finished with.
 **/
gchar *
e_book_query_to_string    (EBookQuery *q)
{
	GString *str = g_string_new ("(");
	GString *encoded = g_string_new ("");
	gint i;
	gchar *s = NULL;
	const gchar *cs;

	switch (q->type) {
	case E_BOOK_QUERY_TYPE_AND:
		g_string_append (str, "and ");
		for (i = 0; i < q->query.andor.nqs; i ++) {
			s = e_book_query_to_string (q->query.andor.qs[i]);
			g_string_append (str, s);
			g_free (s);
			g_string_append_c (str, ' ');
		}
		break;
	case E_BOOK_QUERY_TYPE_OR:
		g_string_append (str, "or ");
		for (i = 0; i < q->query.andor.nqs; i ++) {
			s = e_book_query_to_string (q->query.andor.qs[i]);
			g_string_append (str, s);
			g_free (s);
			g_string_append_c (str, ' ');
		}
		break;
	case E_BOOK_QUERY_TYPE_NOT:
		s = e_book_query_to_string (q->query.not.q);
		g_string_append_printf (str, "not %s", s);
		g_free (s);
		break;
	case E_BOOK_QUERY_TYPE_FIELD_EXISTS:
		if (q->query.exist.vcard_field) {
			g_string_append_printf (str, "exists_vcard \"%s\"", q->query.exist.vcard_field);
		} else {
			g_string_append_printf (str, "exists \"%s\"", e_contact_field_name (q->query.exist.field));
		}
		break;
	case E_BOOK_QUERY_TYPE_FIELD_TEST:
		switch (q->query.field_test.test) {
		case E_BOOK_QUERY_IS: cs = "is"; break;
		case E_BOOK_QUERY_CONTAINS: cs = "contains"; break;
		case E_BOOK_QUERY_BEGINS_WITH: cs = "beginswith"; break;
		case E_BOOK_QUERY_ENDS_WITH: cs = "endswith"; break;
		default:
			g_assert_not_reached();
			break;
		}

		e_sexp_encode_string (encoded, q->query.field_test.value);

		g_string_append_printf (str, "%s \"%s\" %s",
					cs,
					q->query.field_test.field_name,
					encoded->str);
		break;
	case E_BOOK_QUERY_TYPE_ANY_FIELD_CONTAINS:
		g_string_append_printf (str, "contains \"x-evolution-any-field\"");
		e_sexp_encode_string (str, q->query.any_field_contains.value);
		break;
	}

	g_string_append (str, ")");

	g_string_free (encoded, TRUE);

	return g_string_free (str, FALSE);
}

GType
e_book_query_get_type (void)
{
	static volatile gsize type_id__volatile = 0;

	if (g_once_init_enter (&type_id__volatile)) {
		GType type_id;

		type_id = g_boxed_type_register_static ("EBookQuery",
							(GBoxedCopyFunc) e_book_query_copy,
							(GBoxedFreeFunc) e_book_query_unref);

		g_once_init_leave (&type_id__volatile, type_id);
	}

	return type_id__volatile;
}

/**
 * e_book_query_oopy:
 * @q: an #EBookQuery
 *
 * Creates a copy of @q.
 *
 * Return value: A new #EBookQuery identical to @q.
 **/
EBookQuery*
e_book_query_copy (EBookQuery *q)
{
	gchar *str = e_book_query_to_string (q);
	EBookQuery *nq = e_book_query_from_string (str);

	g_free (str);
	return nq;
}
