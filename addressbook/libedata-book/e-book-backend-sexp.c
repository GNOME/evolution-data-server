/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-backend-card-sexp.c
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <string.h>
#include "libedataserver/e-sexp.h"
#include "libedataserver/e-data-server-util.h"
#include "e-book-backend-sexp.h"

static GObjectClass *parent_class;

typedef struct _SearchContext SearchContext;

struct _EBookBackendSExpPrivate {
	ESExp *search_sexp;
	SearchContext *search_context;
};

struct _SearchContext {
	EContact *contact;
};

static gboolean
compare_im (EContact *contact, const char *str,
	    char *(*compare)(const char*, const char*),
	    EContactField im_field)
{
	GList    *aims, *l;
	gboolean  found_it = FALSE;

	aims = e_contact_get (contact, im_field);

	for (l = aims; l != NULL; l = l->next) {
		char *im = (char *) l->data;

		if (im && compare (im, str)) {
			found_it = TRUE;
			break;
		}
	}

	g_list_foreach (aims, (GFunc)g_free, NULL);
	g_list_free (aims);

	return found_it;
}

static gboolean
compare_im_aim (EContact *contact, const char *str,
		char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_AIM);
}

static gboolean
compare_im_msn (EContact *contact, const char *str,
		char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_MSN);
}

static gboolean
compare_im_icq (EContact *contact, const char *str,
		char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_ICQ);
}

static gboolean
compare_im_yahoo (EContact *contact, const char *str,
		  char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_YAHOO);
}

static gboolean
compare_im_gadugadu (EContact *contact, const char *str,
		  char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_GADUGADU);
}


static gboolean
compare_im_jabber (EContact *contact, const char *str,
		   char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_JABBER);
}

static gboolean
compare_im_groupwise (EContact *contact, const char *str,
		      char *(*compare)(const char*, const char*))
{
	return compare_im (contact, str, compare, E_CONTACT_IM_GROUPWISE);
}

static gboolean
compare_email (EContact *contact, const char *str,
	       char *(*compare)(const char*, const char*))
{
	int i;

	for (i = E_CONTACT_EMAIL_1; i <= E_CONTACT_EMAIL_4; i ++) {
		const char *email = e_contact_get_const (contact, i);

		if (email && compare(email, str))
			return TRUE;
	}

	return FALSE;
}

static gboolean
compare_phone (EContact *contact, const char *str,
	       char *(*compare)(const char*, const char*))
{
	int i;
	gboolean rv = FALSE;

	for (i = E_CONTACT_FIRST_PHONE_ID; i <= E_CONTACT_LAST_PHONE_ID; i ++) {
		char *phone = e_contact_get (contact, i);

		rv = phone && compare(phone, str);
		g_free (phone);

		if (rv)
			break;
	}

	return rv;
}

static gboolean
compare_name (EContact *contact, const char *str,
	      char *(*compare)(const char*, const char*))
{
	const char *name;

	name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
	if (name && compare (name, str))
		return TRUE;

	name = e_contact_get_const (contact, E_CONTACT_FAMILY_NAME);
	if (name && compare (name, str))
		return TRUE;

	name = e_contact_get_const (contact, E_CONTACT_GIVEN_NAME);
	if (name && compare (name, str))
		return TRUE;

	name = e_contact_get_const (contact, E_CONTACT_NICKNAME);
	if (name && compare (name, str))
		return TRUE;

	return FALSE;
}

static gboolean
compare_address (EContact *contact, const char *str,
		 char *(*compare)(const char*, const char*))
{

	int i;
	gboolean rv = FALSE;

	for (i = E_CONTACT_FIRST_ADDRESS_ID; i <= E_CONTACT_LAST_ADDRESS_ID; i ++) {
		EContactAddress *address = e_contact_get (contact, i);
		if (address) {
			rv =  (address->po && compare(address->po, str)) ||
				(address->street && compare(address->street, str)) ||
				(address->ext && compare(address->ext, str)) ||
				(address->locality && compare(address->locality, str)) ||
				(address->region && compare(address->region, str)) ||
				(address->code && compare(address->code, str)) ||
				(address->country && compare(address->country, str));

			e_contact_address_free (address);

			if (rv)
				break;
		}
	}

	return rv;

}

static gboolean
compare_category (EContact *contact, const char *str,
		  char *(*compare)(const char*, const char*))
{
	GList *categories;
	GList *iterator;
	gboolean ret_val = FALSE;

	categories = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);

	for (iterator = categories; iterator; iterator = iterator->next) {
		const char *category = iterator->data;

		if (compare(category, str)) {
			ret_val = TRUE;
			break;
		}
	}

	g_list_foreach (categories, (GFunc)g_free, NULL);
	g_list_free (categories);

	return ret_val;
}

static struct prop_info {
	EContactField field_id;
	const char *query_prop;
#define PROP_TYPE_NORMAL   0x01
#define PROP_TYPE_LIST     0x02
	int prop_type;
	gboolean (*list_compare)(EContact *contact, const char *str,
				 char *(*compare)(const char*, const char*));

} prop_info_table[] = {
#define NORMAL_PROP(f,q) {f, q, PROP_TYPE_NORMAL, NULL}
#define LIST_PROP(q,c) {0, q, PROP_TYPE_LIST, c}

	/* query prop,   type,              list compare function */
	NORMAL_PROP ( E_CONTACT_FILE_AS, "file_as" ),
	NORMAL_PROP ( E_CONTACT_UID, "id" ),
	LIST_PROP ( "full_name", compare_name), /* not really a list, but we need to compare both full and surname */
	NORMAL_PROP ( E_CONTACT_GIVEN_NAME, "given_name"),
	NORMAL_PROP ( E_CONTACT_FAMILY_NAME, "family_name"),
	NORMAL_PROP ( E_CONTACT_HOMEPAGE_URL, "url"),
	NORMAL_PROP ( E_CONTACT_BLOG_URL, "blog_url"),
	NORMAL_PROP ( E_CONTACT_CALENDAR_URI, "calurl"),
	NORMAL_PROP ( E_CONTACT_FREEBUSY_URL, "fburl"),
	NORMAL_PROP ( E_CONTACT_ICS_CALENDAR, "icscalendar"),
	NORMAL_PROP ( E_CONTACT_VIDEO_URL, "video_url"),

	NORMAL_PROP ( E_CONTACT_MAILER, "mailer"),
	NORMAL_PROP ( E_CONTACT_ORG, "org"),
	NORMAL_PROP ( E_CONTACT_ORG_UNIT, "org_unit"),
	NORMAL_PROP ( E_CONTACT_OFFICE, "office"),
	NORMAL_PROP ( E_CONTACT_TITLE, "title"),
	NORMAL_PROP ( E_CONTACT_ROLE, "role"),
	NORMAL_PROP ( E_CONTACT_MANAGER, "manager"),
	NORMAL_PROP ( E_CONTACT_ASSISTANT, "assistant"),
	NORMAL_PROP ( E_CONTACT_NICKNAME, "nickname"),
	NORMAL_PROP ( E_CONTACT_SPOUSE, "spouse" ),
	NORMAL_PROP ( E_CONTACT_NOTE, "note"),
	LIST_PROP ( "im_aim",    compare_im_aim ),
	LIST_PROP ( "im_msn",    compare_im_msn ),
	LIST_PROP ( "im_icq",    compare_im_icq ),
	LIST_PROP ( "im_jabber", compare_im_jabber ),
	LIST_PROP ( "im_yahoo",  compare_im_yahoo ),
	LIST_PROP ( "im_gadugadu",  compare_im_gadugadu ),
	LIST_PROP ( "im_groupwise", compare_im_groupwise ),
	LIST_PROP ( "email",     compare_email ),
	LIST_PROP ( "phone",     compare_phone ),
	LIST_PROP ( "address",   compare_address ),
	LIST_PROP ( "category_list",  compare_category ),
};

static ESExpResult *
entry_compare(SearchContext *ctx, struct _ESExp *f,
	      int argc, struct _ESExpResult **argv,
	      char *(*compare)(const char*, const char*))
{
	ESExpResult *r;
	int truth = FALSE;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname;
		struct prop_info *info = NULL;
		int i;
		gboolean any_field;
		gboolean saw_any = FALSE;

		propname = argv[0]->value.string;

		any_field = !strcmp(propname, "x-evolution-any-field");
		for (i = 0; i < G_N_ELEMENTS (prop_info_table); i ++) {
			if (any_field
			    || !strcmp (prop_info_table[i].query_prop, propname)) {
				saw_any = TRUE;
				info = &prop_info_table[i];

				if (any_field && info->field_id == E_CONTACT_UID) {
					/* We need to skip UID from any field contains search
					 * any-field search should be supported for the
					 * visible fields only.
					 */
					truth = FALSE;
				}
				else if (info->prop_type == PROP_TYPE_NORMAL) {
					const char *prop = NULL;
					/* straight string property matches */

					prop = e_contact_get_const (ctx->contact, info->field_id);

					if (prop && compare(prop, argv[1]->value.string)) {
						truth = TRUE;
					}
					if ((!prop) && compare("", argv[1]->value.string)) {
						truth = TRUE;
					}
				}
				else if (info->prop_type == PROP_TYPE_LIST) {
					/* the special searches that match any of the list elements */
					truth = info->list_compare (ctx->contact, argv[1]->value.string, compare);
				}

				/* if we're looking at all fields and find a match,
				   or if we're just looking at this one field,
				   break. */
				if ((any_field && truth)
				    || !any_field)
					break;
			}
		}

		if (!saw_any) {
			/* propname didn't match to any of our known "special" properties,
			   so try to find if it isn't a real field and if so, then compare
			   against value in this field only */
			EContactField fid = e_contact_field_id (propname);

			if (fid >= E_CONTACT_FIELD_FIRST && fid < E_CONTACT_FIELD_LAST) {
				const char *prop = e_contact_get_const (ctx->contact, fid);

				if (prop && compare (prop, argv[1]->value.string)) {
					truth = TRUE;
				}

				if ((!prop) && compare ("", argv[1]->value.string)) {
					truth = TRUE;
				}
			} else {
				/* it is not direct EContact known field, so try to find
				   it in EVCard attributes */
				EVCardAttribute *attr = e_vcard_get_attribute (E_VCARD (ctx->contact), propname);
				GList *l, *values = attr ? e_vcard_attribute_get_values (attr) : NULL;

				for (l = values; l && !truth; l = l->next) {
					const char *value = l->data;

					if (value && compare (value, argv[1]->value.string)) {
						truth = TRUE;
					} else if ((!value) && compare ("", argv[1]->value.string)) {
						truth = TRUE;
					}
				}
			}
		}
	}
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = truth;

	return r;
}

static void
contains_helper_free_word (gpointer data, gpointer user_data)
{
	if (data) {
		g_string_free ((GString *)data, TRUE);
	}
}

static char *
try_contains_word (const gchar *s1, GSList *word)
{
	const gchar *o, *p;
	gunichar unival, first_w_char;
	GString *w;

	if (s1 == NULL)
		return NULL;
	if (word == NULL)
		return (char*)s1; /* previous was last word */
	if (word->data == NULL)
		return NULL; /* illegal structure */

	w = word->data;
	first_w_char = g_utf8_get_char (w->str);

	o  = s1;
	for (p = e_util_unicode_get_utf8 (o, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
		if (unival == first_w_char) {
			gunichar unival2;
			const gchar *q = p;
			const gchar *r = e_util_unicode_get_utf8 (w->str, &unival2);
			while (q && r && unival && unival2) {
				q = e_util_unicode_get_utf8 (q, &unival);
				if (!q)
					break;
				r = e_util_unicode_get_utf8 (r, &unival2);
				if (!r)
					break;
				if (unival != unival2)
					break;
			}
			if (!unival2 && r && q) {
				/* we read whole word and no illegal character has been found */
				if (word->next == NULL ||
				    try_contains_word (e_util_unicode_get_utf8 (o, &unival), word->next)) {
					return (char*)o;
				}
			}
		}
		o = p;
	}

	return NULL;
}

/* converts str into utf8 GString in lowercase;
   returns NULL if str is invalid utf8 string otherwise
   returns newly allocated GString
*/
static GString *
chars_to_unistring_lowercase (const char *str)
{
	GString *res;
	gunichar unich;
	gchar *p;

	if (str == NULL)
		return NULL;

	res = g_string_new ("");

	for (p = e_util_unicode_get_utf8 (str, &unich); p && unich; p = e_util_unicode_get_utf8 (p, &unich)) {
		g_string_append_unichar (res, g_unichar_tolower (unich));
	}

	/* it was invalid unichar string */
	if (p == NULL) {
		g_string_free (res, TRUE);
		return NULL;
	}

	return res;
}

/* first space between words is treated as wildcard character;
   we are looking for s2 in s1, so s2 will be breaked into words
*/
static char *
contains_helper (const char *s1, const char *s2)
{
	GString *s1uni;
	GString *s2uni;
	GSList *words;
	char *next;
	gboolean have_nonspace;
	gboolean have_space;
	GString *last_word, *w;
	char *res = NULL;
	gunichar unich;
	glong len1, len2;

	if (!s2)
		return NULL;

	/* the initial word contains an empty string for sure */
	if (!*s2)
		return (char *)s1;

	s1uni = chars_to_unistring_lowercase (s1);
	if (s1uni == NULL)
		return NULL;

	s2uni = chars_to_unistring_lowercase (s2);
	if (s2uni == NULL) {
		g_string_free (s1uni, TRUE);
		return NULL;
	}

	len1 = g_utf8_strlen (s1uni->str, -1);
	len2 = g_utf8_strlen (s2uni->str, -1);
	if (len1 == 0 || len2 == 0) {
		g_string_free (s1uni, TRUE);
		g_string_free (s2uni, TRUE);

		/* both are empty strings */
		if (len1 == len2)
			return (char *)s1;

		return NULL;
	}

	/* breaking s2 into words */
        words = NULL;
	have_nonspace = FALSE;
	have_space = FALSE;
	last_word = NULL;
	w = g_string_new ("");
	for (next = e_util_unicode_get_utf8 (s2uni->str, &unich); next && unich; next = e_util_unicode_get_utf8 (next, &unich)) {
		if (unich == ' ') {
			if (have_nonspace && !have_space) {
				/* treat only first space after nonspace character as wildcard,
				   so we will start new word here
				*/
				have_space = TRUE;
				words = g_slist_append (words, w);
				last_word = w;
				w = g_string_new ("");
			} else {
				g_string_append_unichar (w, unich);
			}
		} else {
			have_nonspace = TRUE;
			have_space = FALSE;
			g_string_append_unichar (w, unich);
		}
	}

	if (have_space) {
		/* there was one or more spaces at the end of string,
		   concat actual word with that last one
		*/
		g_string_append_len (last_word, w->str, w->len);
		g_string_free (w, TRUE);
	} else {
		/* append actual word into words list */
		words = g_slist_append (words, w);
	}

	res = try_contains_word (s1uni->str, words);

	g_string_free (s1uni, TRUE);
	g_string_free (s2uni, TRUE);
	g_slist_foreach (words, contains_helper_free_word, NULL);
	g_slist_free (words);

	return res;
}

static ESExpResult *
func_contains(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	SearchContext *ctx = data;

	return entry_compare (ctx, f, argc, argv, contains_helper);
}

static char *
is_helper (const char *s1, const char *s2)
{
	if (!e_util_utf8_strcasecmp (s1, s2))
		return (char*)s1;
	else
		return NULL;
}

static ESExpResult *
func_is(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	SearchContext *ctx = data;

	return entry_compare (ctx, f, argc, argv, is_helper);
}

static char *
endswith_helper (const char *s1, const char *s2)
{
	glong s1len = g_utf8_strlen (s1, -1);
	glong s2len = g_utf8_strlen (s2, -1);

	if (s1len < s2len)
		return NULL;

	return (char *)e_util_utf8_strstrcase (g_utf8_offset_to_pointer (s1, s1len - s2len), s2);
}

static ESExpResult *
func_endswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	SearchContext *ctx = data;

	return entry_compare (ctx, f, argc, argv, endswith_helper);
}

static char *
beginswith_helper (const char *s1, const char *s2)
{
	char *p;
	if ((p = (char*) e_util_utf8_strstrcase(s1, s2))
	    && (p == s1))
		return p;
	else
		return NULL;
}

static ESExpResult *
func_beginswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	SearchContext *ctx = data;

	return entry_compare (ctx, f, argc, argv, beginswith_helper);
}

static ESExpResult *
func_exists(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	SearchContext *ctx = data;
	ESExpResult *r;
	int truth = FALSE;

	if (argc == 1
	    && argv[0]->type == ESEXP_RES_STRING) {
		char *propname;
		struct prop_info *info = NULL;
		int i;
		gboolean saw_any = FALSE;

		propname = argv[0]->value.string;

		for (i = 0; i < G_N_ELEMENTS (prop_info_table); i ++) {
			if (!strcmp (prop_info_table[i].query_prop, propname)) {
				saw_any = TRUE;
				info = &prop_info_table[i];

				if (info->prop_type == PROP_TYPE_NORMAL) {
					const char *prop = NULL;
					/* searches where the query's property
					   maps directly to an ecard property */

					prop = e_contact_get_const (ctx->contact, info->field_id);

					if (prop && *prop)
						truth = TRUE;
				}
				else if (info->prop_type == PROP_TYPE_LIST) {
				/* the special searches that match any of the list elements */
					truth = info->list_compare (ctx->contact, "", (char *(*)(const char*, const char*)) e_util_utf8_strstrcase);
				}

				break;
			}
		}

		if (!saw_any) {
			/* propname didn't match to any of our known "special" properties,
			   so try to find if it isn't a real field and if so, then check
			   against value in this field only */
			EContactField fid = e_contact_field_id (propname);

			if (fid >= E_CONTACT_FIELD_FIRST && fid < E_CONTACT_FIELD_LAST) {
				const char *prop = e_contact_get_const (ctx->contact, fid);

				if (prop && *prop)
					truth = TRUE;
			} else {
				/* is is not a known EContact field, try with EVCard attributes */
				EVCardAttribute *attr = e_vcard_get_attribute (E_VCARD (ctx->contact), propname);
				GList *l, *values = attr ? e_vcard_attribute_get_values (attr) : NULL;

				for (l = values; l && !truth; l = l->next) {
					const char *value = l->data;

					if (value && *value)
						truth = TRUE;
				}
			}
		}
	}
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = truth;

	return r;
}

static ESExpResult *
func_exists_vcard(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	SearchContext *ctx = data;
	ESExpResult *r;
	int truth = FALSE;

	if (argc == 1 && argv[0]->type == ESEXP_RES_STRING) {
		const char *attr_name;
		EVCardAttribute *attr;
		GList *values;
		char *s;

		attr_name = argv[0]->value.string;
		attr = e_vcard_get_attribute (E_VCARD (ctx->contact), attr_name);
		if (attr) {
			values = e_vcard_attribute_get_values (attr);
			if (g_list_length (values) > 0) {
				s = values->data;
				if (s[0] != '\0') {
					truth = TRUE;
				}
			}
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = truth;

	return r;
}

/* 'builtin' functions */
static struct {
	char *name;
	ESExpFunc *func;
	int type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
	{ "exists", func_exists, 0 },
	{ "exists_vcard", func_exists_vcard, 0 },
};

/**
 * e_book_backend_sexp_match_contact:
 * @sexp: an #EBookBackendSExp
 * @contact: an #EContact
 *
 * Checks if @contact matches @sexp.
 *
 * Return value: %TRUE if the contact matches, %FALSE otherwise.
 **/
gboolean
e_book_backend_sexp_match_contact (EBookBackendSExp *sexp, EContact *contact)
{
	ESExpResult *r;
	gboolean retval;

	if (!contact) {
		g_warning ("null EContact passed to e_book_backend_sexp_match_contact");
		return FALSE;
	}

	sexp->priv->search_context->contact = g_object_ref (contact);

	r = e_sexp_eval(sexp->priv->search_sexp);

	retval = (r && r->type == ESEXP_RES_BOOL && r->value.bool);

	g_object_unref(sexp->priv->search_context->contact);

	e_sexp_result_free(sexp->priv->search_sexp, r);

	return retval;
}

/**
 * e_book_backend_sexp_match_vcard:
 * @sexp: an #EBookBackendSExp
 * @vcard: a VCard string
 *
 * Checks if @vcard matches @sexp.
 *
 * Return value: %TRUE if the VCard matches, %FALSE otherwise.
 **/
gboolean
e_book_backend_sexp_match_vcard (EBookBackendSExp *sexp, const char *vcard)
{
	EContact *contact;
	gboolean retval;

	contact = e_contact_new_from_vcard (vcard);

	retval = e_book_backend_sexp_match_contact (sexp, contact);

	g_object_unref(contact);

	return retval;
}



/**
 * e_book_backend_sexp_new:
 * @text: an s-expression to parse
 *
 * Creates a new #EBookBackendSExp from @text.
 *
 * Return value: A new #EBookBackendSExp.
 **/
EBookBackendSExp *
e_book_backend_sexp_new (const char *text)
{
	EBookBackendSExp *sexp = g_object_new (E_TYPE_BACKEND_SEXP, NULL);
	int esexp_error;
	int i;

	sexp->priv->search_sexp = e_sexp_new();

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction(sexp->priv->search_sexp, 0, symbols[i].name,
					     (ESExpIFunc *)symbols[i].func, sexp->priv->search_context);
		}
		else {
			e_sexp_add_function(sexp->priv->search_sexp, 0, symbols[i].name,
					    symbols[i].func, sexp->priv->search_context);
		}
	}

	e_sexp_input_text(sexp->priv->search_sexp, text, strlen(text));
	esexp_error = e_sexp_parse(sexp->priv->search_sexp);

	if (esexp_error == -1) {
		g_object_unref (sexp);
		sexp = NULL;
	}

	return sexp;
}

static void
e_book_backend_sexp_dispose (GObject *object)
{
	EBookBackendSExp *sexp = E_BOOK_BACKEND_SEXP (object);

	if (sexp->priv) {
		e_sexp_unref(sexp->priv->search_sexp);

		g_free (sexp->priv->search_context);
		g_free (sexp->priv);
		sexp->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_book_backend_sexp_class_init (EBookBackendSExpClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	/* Set the virtual methods. */

	object_class->dispose = e_book_backend_sexp_dispose;
}

static void
e_book_backend_sexp_init (EBookBackendSExp *sexp)
{
	EBookBackendSExpPrivate *priv;

	priv             = g_new0 (EBookBackendSExpPrivate, 1);

	sexp->priv = priv;
	priv->search_context = g_new (SearchContext, 1);
}

/**
 * e_book_backend_sexp_get_type:
 */
GType
e_book_backend_sexp_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendSExpClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_sexp_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendSExp),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_sexp_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "EBookBackendSExp", &info, 0);
	}

	return type;
}
