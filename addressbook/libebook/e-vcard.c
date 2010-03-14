/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-vcard.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 * Author: Chris Toshok (toshok@ximian.com)
 */

/* This file implements the decoding of the v-card format
 * http://www.imc.org/pdi/vcard-21.txt
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "e-vcard.h"

#define d(x)

#define CRLF "\r\n"

G_DEFINE_TYPE (EVCard, e_vcard, G_TYPE_OBJECT)

/** Encoding used in v-card
 *  Note: v-card spec defines additional 7BIT 8BIT and X- encoding
 */
typedef enum {
	EVC_ENCODING_RAW,    /* no encoding */
	EVC_ENCODING_BASE64, /* base64 */
	EVC_ENCODING_QP      /* quoted-printable */
} EVCardEncoding;

struct _EVCardPrivate {
	GList *attributes;
};

struct _EVCardAttribute {
	gchar  *group;
	gchar  *name;
	GList *params; /* EVCardParam */
	GList *values;
	GList *decoded_values;
	EVCardEncoding encoding;
	gboolean encoding_set;
};

struct _EVCardAttributeParam {
	gchar     *name;
	GList    *values;  /* GList of gchar *'s*/
};

static GObjectClass *parent_class;

static void
e_vcard_dispose (GObject *object)
{
	EVCard *evc = E_VCARD (object);

	if (evc->priv) {

		g_list_foreach (evc->priv->attributes, (GFunc)e_vcard_attribute_free, NULL);
		g_list_free (evc->priv->attributes);

		g_free (evc->priv);
		evc->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_vcard_class_init (EVCardClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_ref (G_TYPE_OBJECT);

	object_class->dispose = e_vcard_dispose;
}

static void
e_vcard_init (EVCard *evc)
{
	evc->priv = g_new0 (EVCardPrivate, 1);
}

/*  Skip newline characters and return the next character.
 *  This function takes care of folding lines, skipping
 *  newline characters if found, taking care of equal characters
 *  and other strange things.
 */
static gchar *
skip_newline (gchar *str, gboolean quoted_printable)
{
	gchar *p;
	gchar *next;
	gchar *next2;
	p = str;

	/* -- swallow equal signs at end of line for quoted printable */
	/* note: a quoted_printable linefolding is an equal sign followed by
	   one or more newline characters and optional a whitespace */
	if (quoted_printable && *p == '=' ) {
		next = g_utf8_next_char (p);
		if (*next == '\r' || *next == '\n') {
			p = g_utf8_next_char (next); /* swallow equal and newline */

			if ((*p == '\r' || *p == '\n') && *p != *next ) {
				p = g_utf8_next_char (p); /* swallow second newline */

				if (*p == ' ' || *p == '\t') {
					p = g_utf8_next_char (p); /* swallow whitespace */
				}
			}

		}

	/* -- swallow newline and (if present) following whitespaces */
	} else if (*p == '\r' || *p == '\n') {

		next = g_utf8_next_char (p);
		if ((*next == '\n' || *next == '\r') && *p != *next) {

			next2 = g_utf8_next_char (next);
			if (*next2 == ' ' || *next2 == '\t') {
				p = g_utf8_next_char (next2); /* we found a line folding */
			}

		} else if (*next == ' ' || *next == '\t') {
			p = g_utf8_next_char (next);  /* we found a line folding */
		}
	}

	return p;
}

/* skip forward until we hit the CRLF, or \0 */
static void
skip_to_next_line (gchar **p)
{
	gchar *lp;
	lp = *p;

	while (*lp != '\n' && *lp != '\r' && *lp != '\0')
		lp = g_utf8_next_char (lp);

	/* -- skip over the endline */
	while (*lp == '\r' || *lp == '\n') {
		lp = g_utf8_next_char (lp);
	}

	*p = lp;
}

/* skip forward until we hit a character in @s, CRLF, or \0.  leave *p
   pointing at the character that causes us to stop */
static void
skip_until (gchar **p, const gchar *s)
{
	gchar *lp;

	lp = *p;

	while (*lp != '\r' && *lp != '\0') {
		gboolean s_matches = FALSE;
		const gchar *ls;
		for (ls = s; *ls; ls = g_utf8_next_char (ls)) {
			if (g_utf8_get_char (ls) == g_utf8_get_char (lp)) {
				s_matches = TRUE;
				break;
			}
		}

		if (s_matches)
			break;
		lp = g_utf8_next_char (lp);
	}

	*p = lp;
}

static void
read_attribute_value (EVCardAttribute *attr, gchar **p, gboolean quoted_printable, const gchar *charset)
{
	gchar *lp = *p;
	GString *str;

	/* read in the value */
	str = g_string_new ("");
	for ( lp =  skip_newline( *p, quoted_printable );
	     *lp != '\n' && *lp != '\r' && *lp != '\0';
	     lp = skip_newline( lp, quoted_printable ) ) {

		if (*lp == '=' && quoted_printable) {
			gchar a, b;
			if ((a = *(++lp)) == '\0') break;
			if ((b = *(++lp)) == '\0') break;
			if (isxdigit(a) && isxdigit (b)) {
				gchar c;

				a = tolower (a);
				b = tolower (b);

				c = (((a>='a'?a-'a'+10:a-'0')&0x0f) << 4)
					| ((b>='a'?b-'a'+10:b-'0')&0x0f);

				g_string_append_c (str, c); /* add decoded byte (this is not a unicode yet) */
			}
			else
				{
					g_string_append_c (str, a);
					g_string_append_c (str, b);
				}

			lp++;

		} else if (*lp == '\\') {
			/* convert back to the non-escaped version of
			   the characters */
			lp = g_utf8_next_char(lp);
			if (*lp == '\0') {
				g_string_append_c (str, '\\');
				break;
			}

			/* beware, there might be a line break due to folding,
			 * need next real character
			 */
			lp = skip_newline (lp, quoted_printable);

			switch (*lp) {
			case 'n': g_string_append_c (str, '\n'); break;
			case 'N': g_string_append_c (str, '\n'); break;
			case 'r': g_string_append_c (str, '\r'); break;
			case 'R': g_string_append_c (str, '\r'); break;
			case ';': g_string_append_c (str, ';'); break;
			case ',': g_string_append_c (str, ','); break;
			case '\\': g_string_append_c (str, '\\'); break;
			default:
				g_warning ("invalid escape, passing it through");
				g_string_append_c (str, '\\');
				g_string_append_unichar (str, g_utf8_get_char(lp));
				break;
			}
			lp = g_utf8_next_char(lp);
		}
		else if ((*lp == ';') ||
			 (*lp == ',' && !g_ascii_strcasecmp (attr->name, "CATEGORIES"))) {
			if (charset) {
				gchar *tmp;

				tmp = g_convert (str->str, str->len, "UTF-8", charset, NULL, NULL, NULL);
				if (tmp) {
					g_string_assign (str, tmp);
					g_free (tmp);
				}
			}

			e_vcard_attribute_add_value (attr, str->str);
			g_string_assign (str, "");
			lp = g_utf8_next_char(lp);
		}
		else {
			g_string_append_unichar (str, g_utf8_get_char (lp));
			lp = g_utf8_next_char(lp);
		}
	}
	if (str) {
		if (charset) {
			gchar *tmp;

			tmp = g_convert (str->str, str->len, "UTF-8", charset, NULL, NULL, NULL);
			if (tmp) {
				g_string_assign (str, tmp);
				g_free (tmp);
			}
		}

		e_vcard_attribute_add_value (attr, str->str);
		g_string_free (str, TRUE);
	}

	skip_to_next_line( &lp );

	*p = lp;
}

static void
read_attribute_params (EVCardAttribute *attr, gchar **p, gboolean *quoted_printable, gchar **charset)
{
	gchar *lp;
	GString *str;
	EVCardAttributeParam *param = NULL;
	gboolean in_quote = FALSE;

	str = g_string_new ("");
	for ( lp =  skip_newline( *p, *quoted_printable );
	     *lp != '\n' && *lp != '\r' && *lp != '\0';
	     lp = skip_newline( lp, *quoted_printable ) ) {

		if (*lp == '"') {
			in_quote = !in_quote;
			lp = g_utf8_next_char (lp);
		}
		else if (in_quote || g_unichar_isalnum (g_utf8_get_char (lp)) || *lp == '-' || *lp == '_') {
			g_string_append_unichar (str, g_utf8_get_char (lp));
			lp = g_utf8_next_char (lp);
		}
		/* accumulate until we hit the '=' or ';'.  If we hit
		 * a '=' the string contains the parameter name.  if
		 * we hit a ';' the string contains the parameter
		 * value and the name is either ENCODING (if value ==
		 * QUOTED-PRINTABLE) or TYPE (in any other case.)
		 */
		else if (*lp == '=') {
			if (str->len > 0) {
				param = e_vcard_attribute_param_new (str->str);
				g_string_assign (str, "");
				lp = g_utf8_next_char (lp);
			}
			else {
				skip_until (&lp, ":;");
				if (*lp == ';') {
					lp = g_utf8_next_char (lp);

				} else if (*lp == ':') {
					/* do nothing */

				} else {
					skip_to_next_line( &lp );
					break;
				}
			}
		}
		else if (*lp == ';' || *lp == ':' || *lp == ',') {
			gboolean colon = (*lp == ':');
			gboolean comma = (*lp == ',');

			if (param) {
				if (str->len > 0) {
					e_vcard_attribute_param_add_value (param, str->str);
					g_string_assign (str, "");
					if (!colon)
						lp = g_utf8_next_char (lp);
				}
				else {
					/* we've got a parameter of the form:
					 * PARAM=(.*,)?[:;]
					 * so what we do depends on if there are already values
					 * for the parameter.  If there are, we just finish
					 * this parameter and skip past the offending character
					 * (unless it's the ':'). If there aren't values, we free
					 * the parameter then skip past the character.
					 */
					if (!param->values) {
						e_vcard_attribute_param_free (param);
						param = NULL;
						if (!colon)
							lp = g_utf8_next_char (lp);
					}
				}

				if (param
				    && !g_ascii_strcasecmp (param->name, "encoding")
				    && !g_ascii_strcasecmp (param->values->data, "quoted-printable")) {
					*quoted_printable = TRUE;
					e_vcard_attribute_param_free (param);
					param = NULL;
				} else if (param
				    && !g_ascii_strcasecmp (param->name, "charset")
				    && g_ascii_strcasecmp (param->values->data, "utf-8") != 0) {
					g_free (*charset);
					*charset = g_strdup (param->values->data);
					e_vcard_attribute_param_free (param);
					param = NULL;
				}
			}
			else {
				if (str->len > 0) {
					const gchar *param_name;
					if (!g_ascii_strcasecmp (str->str,
								 "quoted-printable")) {
						param_name = "ENCODING";
						*quoted_printable = TRUE;
					}
					/* apple's broken addressbook app outputs naked BASE64
					   parameters, which aren't even vcard 3.0 compliant. */
					else if (!g_ascii_strcasecmp (str->str,
								      "base64")) {
						param_name = "ENCODING";
						g_string_assign (str, "b");
					}
					else {
						param_name = "TYPE";
					}

					if (param_name) {
						param = e_vcard_attribute_param_new (param_name);
						e_vcard_attribute_param_add_value (param, str->str);
					}
					g_string_assign (str, "");
					if (!colon)
						lp = g_utf8_next_char (lp);
				}
				else {
					/* we've got an attribute with a truly empty
					   attribute parameter.  So it's of the form:

					   ATTR;[PARAM=value;]*;[PARAM=value;]*:

					   (note the extra ';')

					   the only thing to do here is, well.. nothing.
					   we skip over the character if it's not a colon,
					   and the rest is handled for us: We'll either
					   continue through the loop again if we hit a ';',
					   or we'll break out correct below if it was a ':' */
					if (!colon)
						lp = g_utf8_next_char (lp);
				}
			}
			if (param && !comma) {
				e_vcard_attribute_add_param (attr, param);
				param = NULL;
			}
			if (colon)
				break;
		}
		else {
			g_warning ("invalid character found in parameter spec");
			g_string_assign (str, "");
			/*			skip_until (&lp, ":;"); */

			skip_to_next_line( &lp );
		}
	}

	if (str)
		g_string_free (str, TRUE);

	*p = lp;
}

/* reads an entire attribute from the input buffer, leaving p pointing
   at the start of the next line (past the \r\n) */
static EVCardAttribute*
read_attribute (gchar **p)
{
	gchar *attr_group = NULL;
	gchar *attr_name = NULL;
	EVCardAttribute *attr = NULL;
	GString *str;
	gchar *lp;
	gboolean is_qp = FALSE;
	gchar *charset = NULL;

	/* first read in the group/name */
	str = g_string_new ("");
	for ( lp =  skip_newline( *p, is_qp );
	     *lp != '\n' && *lp != '\r' && *lp != '\0';
	     lp = skip_newline( lp, is_qp ) ) {

		if (*lp == ':' || *lp == ';') {
			if (str->len != 0) {
				/* we've got a name, break out to the value/attribute parsing */
				attr_name = g_string_free (str, FALSE);
				break;
			}
			else {
				/* a line of the form:
				 * (group.)?[:;]
				 *
				 * since we don't have an attribute
				 * name, skip to the end of the line
				 * and try again.
				 */
				g_string_free (str, TRUE);
				*p = lp;
				skip_to_next_line(p);
				goto lose;
			}
		}
		else if (*lp == '.') {
			if (attr_group) {
				g_warning ("extra `.' in attribute specification.  ignoring extra group `%s'",
					   str->str);
				g_string_free (str, TRUE);
				str = g_string_new ("");
			}
			if (str->len != 0) {
				attr_group = g_string_free (str, FALSE);
				str = g_string_new ("");
			}
		}
		else if (g_unichar_isalnum (g_utf8_get_char (lp)) || *lp == '-' || *lp == '_') {
			g_string_append_unichar (str, g_utf8_get_char (lp));
		}
		else {
			g_warning ("invalid character found in attribute group/name");
			g_string_free (str, TRUE);
			*p = lp;
			skip_to_next_line(p);
			goto lose;
		}

		lp = g_utf8_next_char(lp);
	}

	if (!attr_name) {
		skip_to_next_line (p);
		goto lose;
	}

	attr = e_vcard_attribute_new (attr_group, attr_name);
	g_free (attr_group);
	g_free (attr_name);

	if (*lp == ';') {
		/* skip past the ';' */
		lp = g_utf8_next_char(lp);
		read_attribute_params (attr, &lp, &is_qp, &charset);
		if (is_qp)
			attr->encoding = EVC_ENCODING_RAW;
	}
	if (*lp == ':') {
		/* skip past the ':' */
		lp = g_utf8_next_char(lp);
		read_attribute_value (attr, &lp, is_qp, charset);
	}

	g_free (charset);

	*p = lp;

	if (!attr->values)
		goto lose;

	return attr;
 lose:
	if (attr)
		e_vcard_attribute_free (attr);
	return NULL;
}

/* Stolen from glib/glib/gconvert.c */
static gchar *
make_valid_utf8 (const gchar *name)
{
	GString *string;
	const gchar *remainder, *invalid;
	gint remaining_bytes, valid_bytes;

	string = NULL;
	remainder = name;
	remaining_bytes = strlen (name);

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid))
			break;
	      valid_bytes = invalid - remainder;

		if (string == NULL)
			string = g_string_sized_new (remaining_bytes);

		g_string_append_len (string, remainder, valid_bytes);
		/* append U+FFFD REPLACEMENT CHARACTER */
		g_string_append (string, "\357\277\275");

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL)
		return g_strdup (name);

	g_string_append (string, remainder);

        g_assert (g_utf8_validate (string->str, -1, NULL));

        return g_string_free (string, FALSE);
}

/* we try to be as forgiving as we possibly can here - this isn't a
 * validator.  Almost nothing is considered a fatal error.  We always
 * try to return *something*.
 */
static void
parse (EVCard *evc, const gchar *str)
{
	gchar *buf;
	gchar *p;
	EVCardAttribute *attr;

	buf = make_valid_utf8 (str);

	d(printf ("BEFORE FOLDING:\n"));
	d(printf (str));
	d(printf ("\n\nAFTER FOLDING:\n"));
	d(printf (buf));

	p = buf;

	attr = read_attribute (&p);
	if (!attr || attr->group || g_ascii_strcasecmp (attr->name, "begin")) {
		g_warning ("vcard began without a BEGIN:VCARD\n");
	}
	if (attr && !g_ascii_strcasecmp (attr->name, "begin")) {
		e_vcard_attribute_free (attr);
		attr = NULL;
	} else if (attr)
		e_vcard_add_attribute (evc, attr);

	while (*p) {
		EVCardAttribute *next_attr = read_attribute (&p);

		if (next_attr) {
			attr = next_attr;

			if (g_ascii_strcasecmp (next_attr->name, "end"))
				e_vcard_add_attribute (evc, next_attr);
			else
				break;
		}
	}

	if (!attr || attr->group || g_ascii_strcasecmp (attr->name, "end")) {
		g_warning ("vcard ended without END:VCARD\n");
	}

	if (attr && !g_ascii_strcasecmp (attr->name, "end"))
		e_vcard_attribute_free (attr);

	g_free (buf);

	evc->priv->attributes = g_list_reverse (evc->priv->attributes);
}

/**
 * e_vcard_escape_string:
 * @s: the string to escape
 *
 * Escapes a string according to RFC2426, section 5.
 *
 * Returns: A newly allocated, escaped string.
 **/
gchar *
e_vcard_escape_string (const gchar *s)
{
	GString *str;
	const gchar *p;

	str = g_string_new ("");

	/* Escape a string as described in RFC2426, section 5 */
	for (p = s; p && *p; p++) {
		switch (*p) {
		case '\n':
			g_string_append (str, "\\n");
			break;
		case '\r':
			if (*(p+1) == '\n')
				p++;
			g_string_append (str, "\\n");
			break;
		case ';':
			g_string_append (str, "\\;");
			break;
		case ',':
			g_string_append (str, "\\,");
			break;
		case '\\':
			g_string_append (str, "\\\\");
			break;
		default:
			g_string_append_c (str, *p);
			break;
		}
	}

	return g_string_free (str, FALSE);
}

/**
 * e_vcard_unescape_string:
 * @s: the string to unescape
 *
 * Unescapes a string according to RFC2426, section 5.
 *
 * Returns: A newly allocated, unescaped string.
 **/
gchar *
e_vcard_unescape_string (const gchar *s)
{
	GString *str;
	const gchar *p;

	g_return_val_if_fail (s != NULL, NULL);

	str = g_string_new ("");

	/* Unescape a string as described in RFC2426, section 5 */
	for (p = s; *p; p++) {
		if (*p == '\\') {
			p++;
			if (*p == '\0') {
				g_string_append_c (str, '\\');
				break;
			}
			switch (*p) {
			case 'n':  g_string_append_c (str, '\n'); break;
			case 'r':  g_string_append_c (str, '\r'); break;
			case ';':  g_string_append_c (str, ';'); break;
			case ',':  g_string_append_c (str, ','); break;
			case '\\': g_string_append_c (str, '\\'); break;
			default:
				g_warning ("invalid escape, passing it through");
				g_string_append_c (str, '\\');
				g_string_append_unichar (str, g_utf8_get_char(p));
				break;
			}
		}
	}

	return g_string_free (str, FALSE);
}

void
e_vcard_construct (EVCard *evc, const gchar *str)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (str != NULL);

	if (*str)
		parse (evc, str);
}

/**
 * e_vcard_new:
 *
 * Creates a new, blank #EVCard.
 *
 * Returns: A new, blank #EVCard.
 **/
EVCard *
e_vcard_new (void)
{
	return e_vcard_new_from_string ("");
}

/**
 * e_vcard_new_from_string:
 * @str: a string representation of the vcard to create
 *
 * Creates a new #EVCard from the passed-in string
 * representation.
 *
 * Returns: A new #EVCard.
 **/
EVCard *
e_vcard_new_from_string (const gchar *str)
{
	EVCard *evc;

	g_return_val_if_fail (str != NULL, NULL);

	evc = g_object_new (E_TYPE_VCARD, NULL);

	e_vcard_construct (evc, str);

	return evc;
}

static gchar *
e_vcard_to_string_vcard_21  (EVCard *evc)
{
	g_warning ("need to implement e_vcard_to_string_vcard_21");
	return g_strdup ("");
}

static gchar *
e_vcard_to_string_vcard_30 (EVCard *evc)
{
	GList *l;
	GList *v;

	GString *str = g_string_new ("");

	g_string_append (str, "BEGIN:VCARD" CRLF);

	/* we hardcode the version (since we're outputting to a
	   specific version) and ignore any version attributes the
	   vcard might contain */
	g_string_append (str, "VERSION:3.0" CRLF);

	for (l = evc->priv->attributes; l; l = l->next) {
		GList *list;
		EVCardAttribute *attr = l->data;
		GString *attr_str;
		glong len;

		if (!g_ascii_strcasecmp (attr->name, "VERSION"))
			continue;

		attr_str = g_string_new ("");

		/* From rfc2425, 5.8.2
		 *
		 * contentline  = [group "."] name *(";" param) ":" value CRLF
		 */

		if (attr->group) {
			g_string_append (attr_str, attr->group);
			g_string_append_c (attr_str, '.');
		}
		g_string_append (attr_str, attr->name);

		/* handle the parameters */
		for (list = attr->params; list; list = list->next) {
			EVCardAttributeParam *param = list->data;
			/* 5.8.2:
			 * param        = param-name "=" param-value *("," param-value)
			 */
			g_string_append_c (attr_str, ';');
			g_string_append (attr_str, param->name);
			if (param->values) {
				g_string_append_c (attr_str, '=');
				for (v = param->values; v; v = v->next) {
					gchar *value = v->data;
					gchar *pval = value;
					gboolean quotes = FALSE;
					while (*pval) {
						if (!g_unichar_isalnum (g_utf8_get_char (pval))) {
							quotes = TRUE;
							break;
						}
						pval = g_utf8_next_char (pval);
					}

					if (quotes) {
						gint i;

						g_string_append_c (attr_str, '"');

						for (i = 0; value [i]; i++) {
							/* skip quotes in quoted string; it is not allowed */
							if (value [i] == '\"')
								continue;

							g_string_append_c (attr_str, value [i]);
						}

						g_string_append_c (attr_str, '"');
					} else
						g_string_append (attr_str, value);

					if (v->next)
						g_string_append_c (attr_str, ',');
				}
			}
		}

		g_string_append_c (attr_str, ':');

		for (v = attr->values; v; v = v->next) {
			gchar *value = v->data;
			gchar *escaped_value = NULL;

			escaped_value = e_vcard_escape_string (value);

			g_string_append (attr_str, escaped_value);
			if (v->next) {
				/* XXX toshok - i hate you, rfc 2426.
				   why doesn't CATEGORIES use a; like
				   a normal list attribute? */
				if (!g_ascii_strcasecmp (attr->name, "CATEGORIES"))
					g_string_append_c (attr_str, ',');
				else
					g_string_append_c (attr_str, ';');
			}

			g_free (escaped_value);
		}

		/* 5.8.2:
		 * When generating a content line, lines longer than 75
		 * characters SHOULD be folded
		 */
		len = g_utf8_strlen (attr_str->str, -1);
		if (len > 75) {
			GString *fold_str = g_string_sized_new (attr_str->len + len/74*3);
			gchar *pos1 = attr_str->str;
			gchar *pos2 = pos1;
			pos2 = g_utf8_offset_to_pointer (pos2, 75);
			len -= 75;

			while (1) {
				g_string_append_len (fold_str, pos1, pos2 - pos1);
				g_string_append (fold_str, CRLF " ");
				pos1 = pos2;
				if (len <= 74)
					break;
				pos2 = g_utf8_offset_to_pointer (pos2, 74);
				len -= 74;
			}
			g_string_append (fold_str, pos1);
			g_string_free (attr_str, TRUE);
			attr_str = fold_str;
		}
		g_string_append (attr_str, CRLF);

		g_string_append (str, attr_str->str);
		g_string_free (attr_str, TRUE);
	}

	g_string_append (str, "END:VCARD");

	return g_string_free (str, FALSE);
}

/**
 * e_vcard_to_string:
 * @evc: the #EVCard to export
 * @format: the format to export to
 *
 * Exports @evc to a string representation, specified
 * by the @format argument.
 *
 * Returns: A newly allocated string representing the vcard.
 **/
gchar *
e_vcard_to_string (EVCard *evc, EVCardFormat format)
{
	g_return_val_if_fail (E_IS_VCARD (evc), NULL);

	switch (format) {
	case EVC_FORMAT_VCARD_21:
		return e_vcard_to_string_vcard_21 (evc);
	case EVC_FORMAT_VCARD_30:
		return e_vcard_to_string_vcard_30 (evc);
	default:
		g_warning ("invalid format specifier passed to e_vcard_to_string");
		return g_strdup ("");
	}
}

/**
 * e_vcard_dump_structure:
 * @evc: the #EVCard to dump
 *
 * Prints a dump of @evc's structure to stdout. Used for
 * debugging.
 **/
void
e_vcard_dump_structure (EVCard *evc)
{
	GList *a;
	GList *v;
	gint i;

	g_return_if_fail (E_IS_VCARD (evc));

	printf ("vCard\n");
	for (a = evc->priv->attributes; a; a = a->next) {
		GList *p;
		EVCardAttribute *attr = a->data;
		printf ("+-- %s\n", attr->name);
		if (attr->params) {
			printf ("    +- params=\n");

			for (p = attr->params, i = 0; p; p = p->next, i++) {
				EVCardAttributeParam *param = p->data;
				printf ("    |   [%d] = %s", i,param->name);
				printf ("(");
				for (v = param->values; v; v = v->next) {
					gchar *value = e_vcard_escape_string ((gchar *)v->data);
					printf ("%s", value);
					if (v->next)
						printf (",");
					g_free (value);
				}

				printf (")\n");
			}
		}
		printf ("    +- values=\n");
		for (v = attr->values, i = 0; v; v = v->next, i++) {
			printf ("        [%d] = `%s'\n", i, (gchar *)v->data);
		}
	}
}

/**
 * e_vcard_attribute_new:
 * @attr_group: a group name
 * @attr_name: an attribute name
 *
 * Creates a new #EVCardAttribute with the specified group and
 * attribute names.
 *
 * Returns: A new #EVCardAttribute.
 **/
EVCardAttribute*
e_vcard_attribute_new (const gchar *attr_group, const gchar *attr_name)
{
	EVCardAttribute *attr;

	attr = g_slice_new0 (EVCardAttribute);

	attr->group = g_strdup (attr_group);
	attr->name = g_strdup (attr_name);

	return attr;
}

/**
 * e_vcard_attribute_free:
 * @attr: attribute to free
 *
 * Frees an attribute, its values and its parameters.
 **/
void
e_vcard_attribute_free (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	g_free (attr->group);
	g_free (attr->name);

	e_vcard_attribute_remove_values (attr);

	e_vcard_attribute_remove_params (attr);

	g_slice_free (EVCardAttribute, attr);
}

/**
 * e_vcard_attribute_copy:
 * @attr: attribute to copy
 *
 * Makes a copy of @attr.
 *
 * Returns: A new #EVCardAttribute identical to @attr.
 **/
EVCardAttribute*
e_vcard_attribute_copy (EVCardAttribute *attr)
{
	EVCardAttribute *a;
	GList *p;

	g_return_val_if_fail (attr != NULL, NULL);

	a = e_vcard_attribute_new (attr->group, attr->name);

	for (p = attr->values; p; p = p->next)
		e_vcard_attribute_add_value (a, p->data);

	for (p = attr->params; p; p = p->next)
		e_vcard_attribute_add_param (a, e_vcard_attribute_param_copy (p->data));

	return a;
}

/**
 * e_vcard_remove_attributes:
 * @evc: vcard object
 * @attr_group: group name of attributes to be removed
 * @attr_name: name of the arributes to be removed
 *
 * Removes all the attributes with group name and attribute name equal to
 * passed in values. If @attr_group is %NULL or an empty string,
 * it removes all the attributes with passed in name irrespective of
 * their group names.
 **/
void
e_vcard_remove_attributes (EVCard *evc, const gchar *attr_group, const gchar *attr_name)
{
	GList *attr;

	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr_name != NULL);

	attr = evc->priv->attributes;
	while (attr) {
		GList *next_attr;
		EVCardAttribute *a = attr->data;

		next_attr = attr->next;

		if (((!attr_group || *attr_group == '\0') ||
		     (attr_group && !g_ascii_strcasecmp (attr_group, a->group))) &&
		    ((!attr_name && !a->name) || !g_ascii_strcasecmp (attr_name, a->name))) {

			/* matches, remove/delete the attribute */
			evc->priv->attributes = g_list_delete_link (evc->priv->attributes, attr);

			e_vcard_attribute_free (a);
		}

		attr = next_attr;
	}
}

/**
 * e_vcard_remove_attribute:
 * @evc: an #EVCard
 * @attr: an #EVCardAttribute to remove
 *
 * Removes @attr from @evc and frees it.
 **/
void
e_vcard_remove_attribute (EVCard *evc, EVCardAttribute *attr)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr != NULL);

	evc->priv->attributes = g_list_remove (evc->priv->attributes, attr);
	e_vcard_attribute_free (attr);
}

/**
 * e_vcard_add_attribute:
 * @evc: an #EVCard
 * @attr: an #EVCardAttribute to add
 *
 * Adds @attr to @evc.
 **/
void
e_vcard_add_attribute (EVCard *evc, EVCardAttribute *attr)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr != NULL);

	evc->priv->attributes = g_list_prepend (evc->priv->attributes, attr);
}

/**
 * e_vcard_add_attribute_with_value:
 * @evcard: an #EVCard
 * @attr: an #EVCardAttribute to add
 * @value: a value to assign to the attribute
 *
 * Adds @attr to @evcard, setting it to @value.
 **/
void
e_vcard_add_attribute_with_value (EVCard *evcard,
				  EVCardAttribute *attr, const gchar *value)
{
	g_return_if_fail (E_IS_VCARD (evcard));
	g_return_if_fail (attr != NULL);

	e_vcard_attribute_add_value (attr, value);

	e_vcard_add_attribute (evcard, attr);
}

/**
 * e_vcard_add_attribute_with_values:
 * @evcard: an @EVCard
 * @attr: an #EVCardAttribute to add
 * @Varargs: a %NULL-terminated list of values to assign to the attribute
 *
 * Adds @attr to @evcard, assigning the list of values to it.
 **/
void
e_vcard_add_attribute_with_values (EVCard *evcard, EVCardAttribute *attr, ...)
{
	va_list ap;
	gchar *v;

	g_return_if_fail (E_IS_VCARD (evcard));
	g_return_if_fail (attr != NULL);

	va_start (ap, attr);

	while ((v = va_arg (ap, gchar *))) {
		e_vcard_attribute_add_value (attr, v);
	}

	va_end (ap);

	e_vcard_add_attribute (evcard, attr);
}

/**
 * e_vcard_attribute_add_value:
 * @attr: an #EVCardAttribute
 * @value: a string value
 *
 * Adds @value to @attr's list of values.
 **/
void
e_vcard_attribute_add_value (EVCardAttribute *attr, const gchar *value)
{
	g_return_if_fail (attr != NULL);

	attr->values = g_list_append (attr->values, g_strdup (value));
}

/**
 * e_vcard_attribute_add_value_decoded:
 * @attr: an #EVCardAttribute
 * @value: an encoded value
 * @len: the length of the encoded value, in bytes
 *
 * Decodes @value according to the encoding used for @attr, and
 * adds it to @attr's list of values.
 **/
void
e_vcard_attribute_add_value_decoded (EVCardAttribute *attr, const gchar *value, gint len)
{
	g_return_if_fail (attr != NULL);

	switch (attr->encoding) {
	case EVC_ENCODING_RAW:
		g_warning ("can't add_value_decoded with an attribute using RAW encoding.  you must set the ENCODING parameter first");
		break;
	case EVC_ENCODING_BASE64: {
		gchar *b64_data = g_base64_encode ((guchar *) value, len);
		GString *decoded = g_string_new_len (value, len);

		/* make sure the decoded list is up to date */
		e_vcard_attribute_get_values_decoded (attr);

		d(printf ("base64 encoded value: %s\n", b64_data));
		d(printf ("original length: %d\n", len));

		attr->values = g_list_append (attr->values, b64_data);
		attr->decoded_values = g_list_append (attr->decoded_values, decoded);
		break;
	}
	case EVC_ENCODING_QP:
		g_warning ("need to implement quoted printable decoding");
		break;
	}
}

/**
 * e_vcard_attribute_add_values:
 * @attr: an #EVCardAttribute
 * @Varargs: a %NULL-terminated list of strings
 *
 * Adds a list of values to @attr.
 **/
void
e_vcard_attribute_add_values (EVCardAttribute *attr,
			      ...)
{
	va_list ap;
	gchar *v;

	g_return_if_fail (attr != NULL);

	va_start (ap, attr);

	while ((v = va_arg (ap, gchar *))) {
		e_vcard_attribute_add_value (attr, v);
	}

	va_end (ap);
}

static void
free_gstring (GString *str)
{
	g_string_free (str, TRUE);
}

/**
 * e_vcard_attribute_remove_values:
 * @attr: an #EVCardAttribute
 *
 * Removes all values from @attr.
 **/
void
e_vcard_attribute_remove_values (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	g_list_foreach (attr->values, (GFunc)g_free, NULL);
	g_list_free (attr->values);
	attr->values = NULL;

	g_list_foreach (attr->decoded_values, (GFunc)free_gstring, NULL);
	g_list_free (attr->decoded_values);
	attr->decoded_values = NULL;
}

/**
 * e_vcard_attribute_remove_value:
 * @attr: an #EVCardAttribute
 * @s: an value to remove
 *
 * Removes from the value list in @attr the value @s.
 **/
void
e_vcard_attribute_remove_value (EVCardAttribute *attr, const gchar *s)
{
	GList *l;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (s != NULL);

	l = g_list_find_custom (attr->values, s, (GCompareFunc)strcmp);
	if (l == NULL) {
		return;
	}

	attr->values = g_list_delete_link (attr->values, l);
}

/**
 * e_vcard_attribute_remove_param:
 * @attr: an #EVCardAttribute
 * @param_name: a parameter name
 *
 * Removes the parameter @param_name from the attribute @attr.
 *
 * Since: 1.12
 */
void
e_vcard_attribute_remove_param (EVCardAttribute *attr, const gchar *param_name)
{
	GList *l;
	EVCardAttributeParam *param;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (param_name != NULL);

	for (l = attr->params; l; l = l->next) {
		param = l->data;
		if (g_ascii_strcasecmp (e_vcard_attribute_param_get_name (param),
					param_name) == 0) {
			attr->params = g_list_delete_link (attr->params, l);
			e_vcard_attribute_param_free(param);
			break;
		}
	}
}

/**
 * e_vcard_attribute_remove_params:
 * @attr: an #EVCardAttribute
 *
 * Removes all parameters from @attr.
 **/
void
e_vcard_attribute_remove_params (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	g_list_foreach (attr->params, (GFunc)e_vcard_attribute_param_free, NULL);
	g_list_free (attr->params);
	attr->params = NULL;

	/* also remove the cached encoding on this attribute */
	attr->encoding_set = FALSE;
	attr->encoding = EVC_ENCODING_RAW;
}

/**
 * e_vcard_attribute_param_new:
 * @name: the name of the new parameter
 *
 * Creates a new parameter named @name.
 *
 * Returns: A new #EVCardAttributeParam.
 **/
EVCardAttributeParam*
e_vcard_attribute_param_new (const gchar *name)
{
	EVCardAttributeParam *param = g_slice_new (EVCardAttributeParam);
	param->values = NULL;
	param->name = g_strdup (name);

	return param;
}

/**
 * e_vcard_attribute_param_free:
 * @param: an #EVCardAttributeParam
 *
 * Frees @param and its values.
 **/
void
e_vcard_attribute_param_free (EVCardAttributeParam *param)
{
	g_return_if_fail (param != NULL);

	g_free (param->name);

	e_vcard_attribute_param_remove_values (param);

	g_slice_free (EVCardAttributeParam, param);
}

/**
 * e_vcard_attribute_param_copy:
 * @param: an #EVCardAttributeParam
 *
 * Makes a copy of @param.
 *
 * Returns: a new #EVCardAttributeParam identical to @param.
 **/
EVCardAttributeParam*
e_vcard_attribute_param_copy (EVCardAttributeParam *param)
{
	EVCardAttributeParam *p;
	GList *l;

	g_return_val_if_fail (param != NULL, NULL);

	p = e_vcard_attribute_param_new (e_vcard_attribute_param_get_name (param));

	for (l = param->values; l; l = l->next) {
		e_vcard_attribute_param_add_value (p, l->data);
	}

	return p;
}

/**
 * e_vcard_attribute_add_param:
 * @attr: an #EVCardAttribute
 * @param: an #EVCardAttributeParam to add
 *
 * Adds @param to @attr's list of parameters.
 * It tests for duplicities, only new parameters are added,
 * when a new parameter already exists in attr, then those
 * values are merged, also without creating duplicities.
 * When we will not add whole param, then it's freed here.
 **/
void
e_vcard_attribute_add_param (EVCardAttribute *attr,
			     EVCardAttributeParam *param)
{
	gboolean contains;
	GList *params, *p;
	const gchar *par_name;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (param != NULL);

	contains = FALSE;
	params = attr->params;
	par_name = param->name;

	for (p = params; p; p = p->next) {
		EVCardAttributeParam *param2 = p->data;
		if (g_ascii_strcasecmp (param2->name, par_name) == 0) {
			/* param2 has same name as our new param;
			   better merge them than have more parameters
			   with same name within one attribute.
			*/
			GList *vals,*v;

			vals = param->values;

			for (v = vals; v; v = v->next) {
				const gchar *my_value;
				GList *vals2,*v2;

				my_value = (const gchar *)v->data;
				vals2 = param2->values;

				for (v2 = vals2; v2; v2 = v2->next) {
					if (g_ascii_strcasecmp ((const gchar *)v2->data, my_value) == 0) {
						break;
					}
				}

				if (!v2) {
					/* we did loop through all values and none of them was my_value */
					e_vcard_attribute_param_add_value (param2, my_value);
				}
			}

			contains = TRUE;
			break;
		}
	}

	if (!contains) {
		attr->params = g_list_prepend (attr->params, param);
	}

	/* we handle our special encoding stuff here */

	if (!g_ascii_strcasecmp (param->name, EVC_ENCODING)) {
		if (attr->encoding_set) {
			g_warning ("ENCODING specified twice");
			if (contains) {
				e_vcard_attribute_param_free (param);
			}
			return;
		}

		if (param->values && param->values->data) {
			if (!g_ascii_strcasecmp ((gchar *)param->values->data, "b") ||
			    !g_ascii_strcasecmp ((gchar *)param->values->data, "BASE64"))
				attr->encoding = EVC_ENCODING_BASE64;
			else if (!g_ascii_strcasecmp ((gchar *)param->values->data, EVC_QUOTEDPRINTABLE))
				attr->encoding = EVC_ENCODING_QP;
			else {
				g_warning ("Unknown value `%s' for ENCODING parameter.  values will be treated as raw",
					   (gchar *)param->values->data);
			}

			attr->encoding_set = TRUE;
		}
		else {
			g_warning ("ENCODING parameter added with no value");
		}
	}

	if (contains) {
		e_vcard_attribute_param_free (param);
	}
}

/**
 * e_vcard_attribute_param_add_value:
 * @param: an #EVCardAttributeParam
 * @value: a string value to add
 *
 * Adds @value to @param's list of values.
 **/
void
e_vcard_attribute_param_add_value (EVCardAttributeParam *param,
				   const gchar *value)
{
	g_return_if_fail (param != NULL);

	param->values = g_list_append (param->values, g_strdup (value));
}

/**
 * e_vcard_attribute_param_add_values:
 * @param: an #EVCardAttributeParam
 * @Varargs: a %NULL-terminated list of strings
 *
 * Adds a list of values to @param.
 **/
void
e_vcard_attribute_param_add_values (EVCardAttributeParam *param,
				    ...)
{
	va_list ap;
	gchar *v;

	g_return_if_fail (param != NULL);

	va_start (ap, param);

	while ((v = va_arg (ap, gchar *))) {
		e_vcard_attribute_param_add_value (param, v);
	}

	va_end (ap);
}

/**
 * e_vcard_attribute_add_param_with_value:
 * @attr: an #EVCardAttribute
 * @param: an #EVCardAttributeParam
 * @value: a string value
 *
 * Adds @value to @param, then adds @param to @attr.
 **/
void
e_vcard_attribute_add_param_with_value (EVCardAttribute *attr,
					EVCardAttributeParam *param, const gchar *value)
{
	g_return_if_fail (attr != NULL);
	g_return_if_fail (param != NULL);

	e_vcard_attribute_param_add_value (param, value);

	e_vcard_attribute_add_param (attr, param);
}

/**
 * e_vcard_attribute_add_param_with_values:
 * @attr: an #EVCardAttribute
 * @param: an #EVCardAttributeParam
 * @Varargs: a %NULL-terminated list of strings
 *
 * Adds the list of values to @param, then adds @param
 * to @attr.
 **/
void
e_vcard_attribute_add_param_with_values (EVCardAttribute *attr,
					 EVCardAttributeParam *param, ...)
{
	va_list ap;
	gchar *v;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (param != NULL);

	va_start (ap, param);

	while ((v = va_arg (ap, gchar *))) {
		e_vcard_attribute_param_add_value (param, v);
	}

	va_end (ap);

	e_vcard_attribute_add_param (attr, param);
}

/**
 * e_vcard_attribute_param_remove_values:
 * @param: an #EVCardAttributeParam
 *
 * Removes and frees all values from @param.
 **/
void
e_vcard_attribute_param_remove_values (EVCardAttributeParam *param)
{
	g_return_if_fail (param != NULL);

	g_list_foreach (param->values, (GFunc)g_free, NULL);
	g_list_free (param->values);
	param->values = NULL;
}

/**
 * e_vcard_attribute_remove_param_value:
 * @attr: an #EVCardAttribute
 * @param_name: a parameter name
 * @s: a value
 *
 * Removes the value @s from the parameter @param_name on the attribute @attr.
 **/
void
e_vcard_attribute_remove_param_value (EVCardAttribute *attr, const gchar *param_name, const gchar *s)
{
	GList *l, *params;
	EVCardAttributeParam *param;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (param_name != NULL);
	g_return_if_fail (s != NULL);

	params = e_vcard_attribute_get_params (attr);

	for (l = params; l; l = l->next) {
		param = l->data;
		if (g_ascii_strcasecmp (e_vcard_attribute_param_get_name (param), param_name) == 0) {
			l = g_list_find_custom (param->values, s, (GCompareFunc)strcmp);
			if (l == NULL) {
				return;
			}

			param->values = g_list_delete_link (param->values, l);
			if (param->values == NULL) {
				e_vcard_attribute_param_free (param);
				attr->params = g_list_remove (attr->params, param);
			}
			break;
		}
	}
	return;
}

/**
 * e_vcard_get_attributes:
 * @evcard: an #EVCard
 *
 * Gets the list of attributes from @evcard. The list and its
 * contents are owned by @evcard, and must not be freed.
 *
 * Returns: A list of attributes of type #EVCardAttribute.
 **/
GList*
e_vcard_get_attributes (EVCard *evcard)
{
	g_return_val_if_fail (E_IS_VCARD (evcard), NULL);

	return evcard->priv->attributes;
}

/**
 * e_vcard_get_attribute:
 * @evc: an #EVCard
 * @name: the name of the attribute to get
 *
 * Get the attribute @name from @evc.  The #EVCardAttribute is owned by
 * @evcard and should not be freed. If the attribute does not exist, #NULL is
 * returned.
 *
 * Returns: An #EVCardAttribute if found, or #NULL.
 **/
EVCardAttribute *
e_vcard_get_attribute (EVCard     *evc,
		       const gchar *name)
{
        GList *attrs, *l;

        g_return_val_if_fail (E_IS_VCARD (evc), NULL);
        g_return_val_if_fail (name != NULL, NULL);

        attrs = e_vcard_get_attributes (evc);
        for (l = attrs; l; l = l->next) {
                EVCardAttribute *attr;

                attr = (EVCardAttribute *) l->data;
                if (g_ascii_strcasecmp (attr->name, name) == 0)
                        return attr;
        }

        return NULL;
}
/**
 * e_vcard_attribute_get_group:
 * @attr: an #EVCardAttribute
 *
 * Gets the group name of @attr.
 *
 * Returns: The attribute's group name.
 **/
const gchar *
e_vcard_attribute_get_group (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	return attr->group;
}

/**
 * e_vcard_attribute_get_name:
 * @attr: an #EVCardAttribute
 *
 * Gets the name of @attr.
 *
 * Returns: The attribute's name.
 **/
const gchar *
e_vcard_attribute_get_name (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	return attr->name;
}

/**
 * e_vcard_attribute_get_values:
 * @attr: an #EVCardAttribute
 *
 * Gets the list of values from @attr. The list and its
 * contents are owned by @attr, and must not be freed.
 *
 * Returns: A list of string values.
 **/
GList*
e_vcard_attribute_get_values (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	return attr->values;
}

/**
 * e_vcard_attribute_get_values_decoded:
 * @attr: an #EVCardAttribute
 *
 * Gets the list of values from @attr, decoding them if
 * necessary. The list and its contents are owned by @attr,
 * and must not be freed.
 *
 * Returns: A list of values of type #GString.
 **/
GList*
e_vcard_attribute_get_values_decoded (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	if (!attr->decoded_values) {
		GList *l;
		switch (attr->encoding) {
		case EVC_ENCODING_RAW:
			for (l = attr->values; l; l = l->next)
				attr->decoded_values = g_list_prepend (attr->decoded_values, g_string_new ((gchar *)l->data));
			attr->decoded_values = g_list_reverse (attr->decoded_values);
			break;
		case EVC_ENCODING_BASE64:
			for (l = attr->values; l; l = l->next) {
				guchar *decoded;
				gsize len = 0;

				decoded = g_base64_decode (l->data, &len);
				attr->decoded_values = g_list_prepend (attr->decoded_values, g_string_new_len ((gchar *) decoded, len));
				g_free (decoded);
			}
			attr->decoded_values = g_list_reverse (attr->decoded_values);
			break;
		case EVC_ENCODING_QP:
			g_warning ("need to implement quoted printable decoding");
			break;
		}
	}

	return attr->decoded_values;
}

/**
 * e_vcard_attribute_is_single_valued:
 * @attr: an #EVCardAttribute
 *
 * Checks if @attr has a single value.
 *
 * Returns: %TRUE if the attribute has exactly one value, %FALSE otherwise.
 **/
gboolean
e_vcard_attribute_is_single_valued (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, FALSE);

	if (attr->values == NULL
	    || attr->values->next != NULL)
		return FALSE;

	return TRUE;
}

/**
 * e_vcard_attribute_get_value:
 * @attr: an #EVCardAttribute
 *
 * Gets the value of a single-valued #EVCardAttribute, @attr.
 *
 * Returns: A newly allocated string representing the value.
 **/
gchar *
e_vcard_attribute_get_value (EVCardAttribute *attr)
{
	GList *values;

	g_return_val_if_fail (attr != NULL, NULL);

	values = e_vcard_attribute_get_values (attr);

	if (!e_vcard_attribute_is_single_valued (attr))
		g_warning ("e_vcard_attribute_get_value called on multivalued attribute");

	return values ? g_strdup ((gchar *)values->data) : NULL;
}

/**
 * e_vcard_attribute_get_value_decoded:
 * @attr: an #EVCardAttribute
 *
 * Gets the value of a single-valued #EVCardAttribute, @attr, decoding
 * it if necessary.
 *
 * Note: this function seems currently to be unused. Could be removed.
 *
 * Returns: A newly allocated #GString representing the value.
 **/
GString*
e_vcard_attribute_get_value_decoded (EVCardAttribute *attr)
{
	GList *values;
	GString *str = NULL;

	g_return_val_if_fail (attr != NULL, NULL);

	values = e_vcard_attribute_get_values_decoded (attr);

	if (!e_vcard_attribute_is_single_valued (attr))
		g_warning ("e_vcard_attribute_get_value_decoded called on multivalued attribute");

	if (values)
		str = values->data;

	return str ? g_string_new_len (str->str, str->len) : NULL;
}

/**
 * e_vcard_attribute_has_type:
 * @attr: an #EVCardAttribute
 * @typestr: a string representing the type
 *
 * Checks if @attr has an #EVCardAttributeParam of the specified type.
 *
 * Returns: %TRUE if such a parameter exists, %FALSE otherwise.
 **/
gboolean
e_vcard_attribute_has_type (EVCardAttribute *attr, const gchar *typestr)
{
	GList *params;
	GList *p;

	g_return_val_if_fail (attr != NULL, FALSE);
	g_return_val_if_fail (typestr != NULL, FALSE);

	params = e_vcard_attribute_get_params (attr);

	for (p = params; p; p = p->next) {
		EVCardAttributeParam *param = p->data;

		if (!g_ascii_strcasecmp (e_vcard_attribute_param_get_name (param), EVC_TYPE)) {
			GList *values = e_vcard_attribute_param_get_values (param);
			GList *v;

			for (v = values; v; v = v->next) {
				if (!g_ascii_strcasecmp ((gchar *)v->data, typestr))
					return TRUE;
			}
		}
	}

	return FALSE;
}

/**
 * e_vcard_attribute_get_params:
 * @attr: an #EVCardAttribute
 *
 * Gets the list of parameters from @attr. The list and its
 * contents are owned by @attr, and must not be freed.
 *
 * Returns: A list of elements of type #EVCardAttributeParam.
 **/
GList*
e_vcard_attribute_get_params (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	return attr->params;
}

/**
 * e_vcard_attribute_get_param:
 * @attr: an #EVCardAttribute
 * @name: a parameter name
 *
 * Gets the list of values for the paramater @name from @attr. The list and its
 * contents are owned by @attr, and must not be freed.
 *
 * Returns: A list of string elements representing the parameter's values.
 **/
GList *
e_vcard_attribute_get_param (EVCardAttribute *attr, const gchar *name)
{
	GList *params, *p;

	g_return_val_if_fail (attr != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	params = e_vcard_attribute_get_params (attr);

	for (p = params; p; p = p->next) {
		EVCardAttributeParam *param = p->data;
		if (g_ascii_strcasecmp (e_vcard_attribute_param_get_name (param), name) == 0) {
			return e_vcard_attribute_param_get_values (param);
		}
	}

	return NULL;
}

/**
 * e_vcard_attribute_param_get_name:
 * @param: an #EVCardAttributeParam
 *
 * Gets the name of @param.
 *
 * Returns: The name of the parameter.
 **/
const gchar *
e_vcard_attribute_param_get_name (EVCardAttributeParam *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return param->name;
}

/**
 * e_vcard_attribute_param_get_values:
 * @param: an #EVCardAttributeParam
 *
 * Gets the list of values from @param. The list and its
 * contents are owned by @param, and must not be freed.
 *
 * Returns: A list of string elements representing the parameter's values.
 **/
GList*
e_vcard_attribute_param_get_values (EVCardAttributeParam *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return param->values;
}
