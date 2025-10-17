/* e-vcard.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2013 Collabora Ltd.
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
 * Authors: Chris Toshok (toshok@ximian.com)
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 */

/**
 * SECTION:e-vcard
 * @short_description: vCard parsing and interpretation
 * @stability: Stable
 * @include: libebook-contacts/libebook-contacts.h
 *
 * #EVCard is a low-level representation of a vCard, as specified in RFCs
 * 2425 and 2426 (for vCard version 3.0) and RFC 6350 (for vCard version 4.0)
 * with some of its extensions; this class only supports versions 2.1,
 * 3.0 and 4.0 of the vCard standard.
 *
 * A vCard is an unordered set of attributes (otherwise known as properties),
 * each with one or more values. The number of values an attribute has is
 * determined by its type, and is given in the specification. Each attribute may
 * also have zero or more named parameters, which provide metadata about its
 * value.
 *
 * For example, the following line from a vCard:
 * |[
 * ADR;TYPE=WORK:;;100 Waters Edge;Baytown;LA;30314;United States of America
 * ]|
 * is an <code>ADR</code> attribute with 6 values giving the different
 * components of the postal address. It has one parameter, <code>TYPE</code>,
 * which specifies that it’s a work address rather than a home address.
 *
 * Using the #EVCard API, this data is accessible as follows:
 * <example>
 * <title>Accessing a Multi-Valued Attribute</title>
 * <programlisting>
 * EVCard *vcard;
 * EVCardAttribute *attr;
 * GList *param_values, *values;
 *
 * vcard = e_vcard_new_from_string (
 *    "BEGIN:VCARD\n"
 *    "VERSION:3.0\n"
 *    "ADR;TYPE=WORK:;;100 Waters Edge;Baytown;LA;30314;United States of America\n"
 *    "END:VCARD\n");
 * attr = e_vcard_get_attribute (vcard, "ADR");
 *
 * g_assert_cmpstr (e_vcard_attribute_get_name (attr), ==, "ADR");
 * g_assert_true (e_vcard_attribute_is_single_valued (attr) == FALSE);
 *
 * param_values = e_vcard_attribute_get_param (attr, "TYPE");
 * g_assert_cmpuint (g_list_length (param_values), ==, 1);
 * g_assert_cmpstr (param_values->data, ==, "WORK");
 *
 * values = e_vcard_attribute_get_values (attr);
 * g_assert_cmpuint (g_list_length (values), ==, 6);
 * g_assert_cmpstr (values->data, ==, "");
 * g_assert_cmpstr (values->next->data, ==, "100 Waters Edge");
 * g_assert_cmpstr (values->next->next->data, ==, "Baytown");
 * /<!-- -->* etc. *<!-- -->/
 *
 * g_object_unref (vcard);
 * </programlisting>
 * </example>
 *
 * If a second <code>ADR</code> attribute was present in the vCard, the above
 * example would only ever return the first attribute. To access the values of
 * other attributes of the same type, the entire attribute list must be iterated
 * over using e_vcard_get_attributes(), then matching on
 * e_vcard_attribute_get_name().
 *
 * vCard attribute values may be encoded in the vCard source, using base-64 or
 * quoted-printable encoding. Such encoded values are automatically decoded when
 * parsing the vCard, so the values returned by e_vcard_attribute_get_value()
 * do not need further decoding. The ‘decoded’ functions,
 * e_vcard_attribute_get_value_decoded() and
 * e_vcard_attribute_get_values_decoded() are only relevant when adding
 * attributes which use pre-encoded values and have their <code>ENCODING</code>
 * parameter set.
 *
 * String comparisons in #EVCard are almost universally case-insensitive.
 * Attribute names, parameter names and parameter values are all compared
 * case-insensitively. Only attribute values are case sensitive.
 *
 * #EVCard implements lazy parsing of its vCard data, so the first time its
 * attributes are accessed may take slightly longer than normal to allow for the
 * vCard to be parsed. This can be tested by calling e_vcard_is_parsed().
 */

/* This file implements the decoding of the v-card format
 * http://www.imc.org/pdi/vcard-21.txt
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "camel/camel.h"
#include "libedataserver/libedataserver.h"

#include "e-vcard.h"

#define d(x)

#define CRLF "\r\n"

#define E_VCARD_21_VALID_PROPERTIES \
	"ADR,ORG,N,AGENT,LOGO,PHOTO,LABEL,FN,TITLE,SOUND,VERSION,TEL," \
	"EMAIL,TZ,GEO,NOTE,URL,BDAY,ROLE,REV,UID,KEY,MAILER"

#define E_VCARD_21_VALID_PARAMETERS \
	"TYPE,VALUE,ENCODING,CHARSET,LANGUAGE,DOM,INTL,POSTAL,PARCEL," \
	"HOME,WORK,PREF,VOICE,FAX,MSG,CELL,PAGER,BBS,MODEM,CAR,ISDN,VIDEO," \
	"AOL,APPLELINK,ATTMAIL,CIS,EWORLD,INTERNET,IBMMAIL,MCIMAIL," \
	"POWERSHARE,PRODIGY,TLX,X400,GIF,CGM,WMF,BMP,MET,PMB,DIB,PICT,TIFF," \
	"PDF,PS,JPEG,QTIME,MPEG,MPEG2,AVI,WAVE,AIFF,PCM,X509,PGP"

/* RFC 2426 */

#define E_VCARD_30_VALID_PROPERTIES \
	"NAME,PROFILE,SOURCE,FN,N,NICKNAME,PHOTO,BDAY,ADR,LABEL,TEL,EMAIL,MAILER,TZ,GEO," \
	"TITLE,ROLE,LOGO,AGENT,ORG,CATEGORIES,NOTE,PRODID,REV,SORT-STRING," \
	"SOUND,UID,URL,VERSION,CLASS,KEY"

#define E_VCARD_30_VALID_PARAMETERS \
	"VALUE,CONTEXT,TYPE,PREF,ENCODING,LANGUAGE," \
	"HOME,WORK,PREF,VOICE,FAX,MSG,CELL,PAGER,BBS,MODEM,CAR,ISDN,VIDEO,PCS," /* tel-type */ \
	"INTERNET,X400," /* email-type */ \
	"X509,PGP," /* keytype */ \
	"DOM,INTL,POSTAL,PARCEL,HOME,WORK,PREF" /* addr-type */

/* RFC 6350 */

#define E_VCARD_40_VALID_PROPERTIES \
	"SOURCE,KIND,XML,FN,N,NICKNAME,PHOTO,BDAY,ANNIVERSARY,GENDER,ADR,TEL,EMAIL,IMPP," \
	"LANG,TZ,GEO,TITLE,ROLE,LOGO,ORG,MEMBER,RELATED,CATEGORIES,NOTE,PRODID,REV," \
	"SOUND,UID,CLIENTPIDMAP,URL,VERSION,KEY,FBURL,CALADRURI,CALURI," \
	/* RFC 6474 */ \
	"BIRTHPLACE,DEATHPLACE,DEATHDATE," \
	/* RFC 6715 */ \
	"EXPERTISE,HOBBY,INTEREST,ORG-DIRECTORY," \
	/* RFC 8605 */ \
	"CONTACT-URI," \
	/* RFC 9554 */ \
	"CREATED,LANGUAGE,GRAMGENDER,PRONOUNS,SOCIALPROFILE," \
	/* RFC 9555 */ \
	"JSPROP"

#define E_VCARD_40_VALID_PARAMETERS \
	"LANGUAGE,VALUE,PREF,ALTID,PID,TYPE,MEDIATYPE,CALSCALE,SORT-AS,GEO,TZ," \
	"WORK,HOME,TEXT,VOICE,FAX,CELL,VIDEO,PAGER,TEXTPHONE,GREGORIAN,CONTACT," \
	"ACQUAINTANCE,FRIEND,MET,CO-WORKER,COLLEAGUE,CO-RESIDENT,NEIGHBOR,CHILD," \
	"PARENT,SIBLING,SPOUSE,KIN,MUSE,CRUSH,DATE,SWEETHEART,ME,AGENT,EMERGENCY," \
	/* RFC 6715 */ \
	"INDEX,LEVEL,BEGINNER,AVERAGE,EXPERT,HIGH,MEDIUM,LOW," \
	/* RFC 9554 */ \
	"AUTHOR,AUTHOR-NAME,CREATED,DERIVED,LABEL,PHONETIC,PROP-ID,SCRIPT,SERVICE-TYPE,USERNAME," \
	"ANIMATE,COMMON,FEMININE,INANIMATE,MASCULINE,NEUTER," \
	"IPA,PINY,JYUT,SCRIPT,BILLING,DELIVERY"

static EVCardAttribute *e_vcard_attribute_ref (EVCardAttribute *attr);
static void e_vcard_attribute_unref (EVCardAttribute *attr);
static EVCardAttributeParam *e_vcard_attribute_param_ref (EVCardAttributeParam *param);
static void e_vcard_attribute_param_unref (EVCardAttributeParam *param);

/* Encoding used in v-card
 * Note: v-card spec defines additional 7BIT 8BIT and X- encoding
 */
typedef enum {
	EVC_ENCODING_RAW,    /* no encoding */
	EVC_ENCODING_BASE64, /* base64 */
	EVC_ENCODING_QP      /* quoted-printable */
} EVCardEncoding;

struct _EVCardPrivate {
	GList *attributes;
	gchar *vcard;
	EVCardVersion version;
};

struct _EVCardAttribute {
	gint ref_count;
	gchar  *group;
	gchar  *name;
	GList *params; /* EVCardParam */
	GList *values;
	GList *decoded_values;
	EVCardEncoding encoding;
	gboolean encoding_set;
};

struct _EVCardAttributeParam {
	gint ref_count;
	gchar     *name;
	GList    *values;  /* GList of gchar *'s */
};

G_DEFINE_TYPE_WITH_PRIVATE (EVCard, e_vcard, G_TYPE_OBJECT)

G_DEFINE_BOXED_TYPE (EVCardAttribute, e_vcard_attribute, e_vcard_attribute_ref, e_vcard_attribute_unref)

G_DEFINE_BOXED_TYPE (EVCardAttributeParam, e_vcard_attribute_param, e_vcard_attribute_param_ref, e_vcard_attribute_param_unref)

static void
vcard_finalize (GObject *object)
{
	EVCardPrivate *priv;

	priv = E_VCARD (object)->priv;

	/* Directly access priv->attributes and don't call
	 * e_vcard_ensure_attributes(), since it is pointless
	 * to start vCard parsing that late. */
	g_list_free_full (
		priv->attributes, (GDestroyNotify) e_vcard_attribute_free);

	g_free (priv->vcard);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_vcard_parent_class)->finalize (object);
}

static void
e_vcard_class_init (EVCardClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = vcard_finalize;
}

static void
e_vcard_init (EVCard *evc)
{
	evc->priv = e_vcard_get_instance_private (evc);
	evc->priv->version = E_VCARD_VERSION_UNKNOWN;
}

static EVCardAttribute *
e_vcard_attribute_new_take (gchar *attr_group,
			    gchar *attr_name)
{
	EVCardAttribute *attr;

	attr = g_slice_new0 (EVCardAttribute);

	if (attr_group && !*attr_group) {
		g_free (attr_group);
		attr_group = NULL;
	}

	attr->ref_count = 1;
	attr->group = attr_group;
	attr->name = attr_name;

	return attr;
}

static gboolean
e_vcard_attribute_is_singlevalue_type (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, FALSE);

	return g_ascii_strcasecmp (attr->name, EVC_KEY) == 0 ||
		g_ascii_strcasecmp (attr->name, EVC_LOGO) == 0 ||
		g_ascii_strcasecmp (attr->name, EVC_PHOTO) == 0 ||
		g_ascii_strcasecmp (attr->name, "SOUND") == 0 ||
		g_ascii_strcasecmp (attr->name, "TZ") == 0;
}

/* Stolen from glib/glib/gconvert.c */
static GString *
make_utf8_valid (GString **inout_str)
{
	GString *string;
	const gchar *remainder, *invalid;
	gint remaining_bytes, valid_bytes;

	string = NULL;
	remainder = (*inout_str)->str;
	remaining_bytes = (*inout_str)->len;

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid))
			break;
		valid_bytes = invalid - remainder;

		if (string == NULL)
			string = g_string_sized_new (remaining_bytes + 3);

		g_string_append_len (string, remainder, valid_bytes);
		/* append U+FFFD REPLACEMENT CHARACTER */
		g_string_append (string, "\357\277\275");

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL)
		return *inout_str;

	g_string_append (string, remainder);

	g_warn_if_fail (g_utf8_validate (string->str, -1, NULL));

	g_string_free (*inout_str, TRUE);
	*inout_str = string;

	return *inout_str;
}

/*  Skip newline characters and return the next character.
 *  This function takes care of folding lines, skipping
 *  newline characters if found, taking care of equal characters
 *  and other strange things.
 */
static const gchar *
skip_newline (const gchar *str,
              gboolean quoted_printable)
{
	const gchar *p, *next, *next2;

	p = str;

	/* -- swallow equal signs at end of line for quoted printable */
	/* note: a quoted_printable linefolding is an equal sign followed by
	 * one or more newline characters and optional a whitespace */
	if (quoted_printable && *p == '=' ) {
		next = p + 1;
		if (*next == '\r' || *next == '\n') {
			p = next + 1; /* swallow equal and newline */

			if ((*p == '\r' || *p == '\n') && *p != *next ) {
				p = p + 1; /* swallow second newline */

				if (*p == ' ' || *p == '\t') {
					p = p + 1; /* swallow whitespace */
				}
			}

		}

	/* -- swallow newline and (if present) following whitespaces */
	} else if (*p == '\r' || *p == '\n') {

		next = p + 1;
		if ((*next == '\n' || *next == '\r') && *p != *next) {

			next2 = next + 1;
			if (*next2 == ' ' || *next2 == '\t') {
				p = next2 + 1; /* we found a line folding */
			}

		} else if (*next == ' ' || *next == '\t') {
			p = next + 1;  /* we found a line folding */
		}
	}

	return p;
}

/* skip forward until we hit the CRLF, or \0 */
static void
skip_to_next_line (const gchar **p)
{
	const gchar *lp = *p;

	while (*lp != '\n' && *lp != '\r' && *lp != '\0')
		lp++;

	/* -- skip over the endline */
	while (*lp == '\r' || *lp == '\n') {
		lp++;
	}

	*p = lp;
}

/* skip forward until we hit a character in @s, CRLF, or \0.  leave *p
 * pointing at the character that causes us to stop */
static void
skip_until (const gchar **p,
            const gchar *s)
{
	const gchar *lp;

	lp = *p;

	while (*lp != '\r' && *lp != '\0') {
		gboolean s_matches = FALSE;
		const gchar *ls;
		for (ls = s; *ls; ls++) {
			if (*ls == *lp) {
				s_matches = TRUE;
				break;
			}
		}

		if (s_matches)
			break;
		lp++;
	}

	*p = lp;
}

static void
read_attribute_value (EVCardAttribute *attr,
                      const gchar **p,
                      gboolean quoted_printable,
                      const gchar *charset)
{
	const gchar *lp = *p;
	const gchar *chunk_start = NULL;
	gboolean is_singlevalue_type;
	gboolean is_categories;
	GString *str;

	#define WRITE_CHUNK() G_STMT_START { \
		if (chunk_start) { \
			g_string_append_len (str, chunk_start, lp - chunk_start); \
			chunk_start = NULL; \
		} } G_STMT_END

	is_singlevalue_type = e_vcard_attribute_is_singlevalue_type (attr);
	is_categories = !g_ascii_strcasecmp (attr->name, EVC_CATEGORIES);

	/* read in the value */
	str = g_string_sized_new (16);
	for (lp = skip_newline ( *p, quoted_printable);
	     *lp != '\n' && *lp != '\r' && *lp != '\0';
	     lp = skip_newline ( lp, quoted_printable ) ) {

		if (*lp == '=' && quoted_printable) {
			gchar a, b;

			WRITE_CHUNK ();

			/* it's for the '=' */
			lp++;
			lp = skip_newline (lp, quoted_printable);

			a = *lp;
			lp++;
			if (a == '\0')
				break;

			lp = skip_newline (lp, quoted_printable);

			b = *lp;
			if (b == '\0')
				break;
			lp++;

			if (g_ascii_isxdigit (a) && g_ascii_isxdigit (b)) {
				gchar c;

				gint a_bin = g_ascii_xdigit_value (a);
				gint b_bin = g_ascii_xdigit_value (b);

				c = (a_bin << 4) | b_bin;

				g_string_append_c (str, c); /* add decoded byte (this is not a unicode yet) */
			} else {
				g_string_append_c (str, '=');
				g_string_append_c (str, a);
				g_string_append_c (str, b);
			}
		} else if (*lp == '\\') {
			WRITE_CHUNK ();

			/* convert back to the non-escaped version of
			 * the characters */
			lp++;
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
				d (g_warning ("invalid escape, passing it through"));
				g_string_append_c (str, '\\');
				chunk_start = lp;
				break;
			}
			lp++;
		}
		else if ((*lp == ';' && !is_singlevalue_type) ||
			 (*lp == ',' && is_categories)) {
			WRITE_CHUNK ();
			if (charset) {
				gchar *tmp;

				tmp = g_convert (str->str, str->len, "UTF-8", charset, NULL, NULL, NULL);
				if (tmp) {
					g_string_assign (str, tmp);
					g_free (tmp);
				}
			}

			e_vcard_attribute_add_value (attr, make_utf8_valid (&str)->str);
			g_string_set_size (str, 0);
			lp++;
		}
		else {
			if (!chunk_start)
				chunk_start = lp;

			lp++;
		}

		if (*lp == '\n' || *lp == '\r')
			WRITE_CHUNK ();
	}

	WRITE_CHUNK ();

	#undef WRITE_CHUNK

	if (str) {
		if (charset) {
			gchar *tmp;

			tmp = g_convert (str->str, str->len, "UTF-8", charset, NULL, NULL, NULL);
			if (tmp) {
				g_string_assign (str, tmp);
				g_free (tmp);
			}
		}

		e_vcard_attribute_add_value_take (attr, g_string_free (make_utf8_valid (&str), FALSE));
	}

	skip_to_next_line ( &lp );

	*p = lp;
}

static void
read_attribute_params (EVCardAttribute *attr,
                       const gchar **p,
                       gboolean *quoted_printable,
                       gchar **charset)
{
	const gchar *lp;
	const gchar *chunk_start = NULL;
	GString *str;
	EVCardAttributeParam *param = NULL;
	gboolean in_quote = FALSE;

	#define WRITE_CHUNK() G_STMT_START { \
		if (chunk_start) { \
			g_string_append_len (str, chunk_start, lp - chunk_start); \
			chunk_start = NULL; \
		} } G_STMT_END

	str = g_string_sized_new (16);
	for (lp = skip_newline ( *p, *quoted_printable);
	     *lp != '\n' && *lp != '\r' && *lp != '\0';
	     lp = skip_newline ( lp, *quoted_printable ) ) {
		if (*lp == '"') {
			WRITE_CHUNK ();

			in_quote = !in_quote;
			lp++;
		} else  if (in_quote || *lp == '-' || *lp == '_' || g_ascii_isalnum (*lp)) {
			WRITE_CHUNK ();

			if (in_quote && *lp == '\\') {
				lp = skip_newline (lp + 1, *quoted_printable);

				switch (*lp) {
				case 'n':
				case 'N':
					g_string_append_c (str, '\n');
					break;
				case 'r':
				case 'R':
					g_string_append_c (str, '\r');
					break;
				case ';':
					g_string_append_c (str, ';');
					break;
				case ',':
					g_string_append_c (str, ',');
					break;
				case '\\':
					g_string_append_c (str, '\\');
					break;
				default:
					g_string_append_c (str, '\\');
					g_string_append_c (str, *lp);
					break;
				}
			} else {
				g_string_append_c (str, *lp);
			}
			if (*lp != '\0')
				lp++;
		}
		/* accumulate until we hit the '=' or ';'.  If we hit
		 * a '=' the string contains the parameter name.  if
		 * we hit a ';' the string contains the parameter
		 * value and the name is either ENCODING (if value ==
		 * QUOTED-PRINTABLE) or TYPE (in any other case.)
		 */
		else if (*lp == '=') {
			WRITE_CHUNK ();

			if (str->len > 0) {
				param = e_vcard_attribute_param_new (make_utf8_valid (&str)->str);
				g_string_set_size (str, 0);
				lp++;
			}
			else {
				skip_until (&lp, ":;");
				if (*lp == ';') {
					lp++;

				} else if (*lp == ':') {
					/* do nothing */

				} else {
					skip_to_next_line ( &lp );
					break;
				}
			}
		}
		else if (*lp == ';' || *lp == ':' || *lp == ',') {
			gboolean colon = (*lp == ':');
			gboolean comma = (*lp == ',');

			WRITE_CHUNK ();

			if (param) {
				if (str->len > 0) {
					e_vcard_attribute_param_add_value (param, make_utf8_valid (&str)->str);
					g_string_set_size (str, 0);
					if (!colon)
						lp++;
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
							lp++;
					} else if (!colon) {
						lp++;
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
					 * parameters, which aren't even vcard 3.0 compliant. */
					else if (!g_ascii_strcasecmp (str->str,
								      "base64")) {
						param_name = "ENCODING";
						g_string_assign (str, "b");
					}
					else {
						param_name = "TYPE";
					}

					param = e_vcard_attribute_param_new (param_name);
					e_vcard_attribute_param_add_value (param, make_utf8_valid (&str)->str);
					g_string_set_size (str, 0);
					if (!colon)
						lp++;
				}
				else {
					/* we've got an attribute with a truly empty
					 * attribute parameter.  So it's of the form:
					 *
					 * ATTR;[PARAM=value;]*;[PARAM=value;]*:
					 *
					 * (note the extra ';')
					 *
					 * the only thing to do here is, well.. nothing.
					 * we skip over the character if it's not a colon,
					 * and the rest is handled for us: We'll either
					 * continue through the loop again if we hit a ';',
					 * or we'll break out correct below if it was a ':' */
					if (!colon)
						lp++;
				}
			}
			if (param && !comma) {
				e_vcard_attribute_add_param (attr, param);
				param = NULL;
			}
			if (colon)
				break;
		} else if (param) {
			/* reading param value, which is SAFE-CHAR, aka
			 * any character except CTLs, DQUOTE, ";", ":", "," */
			if (!chunk_start)
				chunk_start = lp;

			lp++;
		} else {
			d (g_warning ("invalid character (%c/0x%02x) found in parameter spec (%s)", *lp, *lp, lp));
			chunk_start = NULL;
			g_string_set_size (str, 0);
			skip_until (&lp, ":;");
		}

		if (*lp == '\n' || *lp == '\r')
			WRITE_CHUNK ();
	}

	#undef WRITE_CHUNK

	if (str)
		g_string_free (str, TRUE);

	*p = lp;
}

/* reads an entire attribute from the input buffer, leaving p pointing
 * at the start of the next line (past the \r\n) */
static EVCardAttribute *
read_attribute (const gchar **p)
{
	gchar *attr_group = NULL;
	gchar *attr_name = NULL;
	EVCardAttribute *attr = NULL;
	GString *str;
	const gchar *lp;
	gboolean is_qp = FALSE;
	gchar *charset = NULL;

	/* first read in the group/name */
	str = g_string_sized_new (16);
	for (lp = skip_newline ( *p, is_qp);
	     *lp != '\n' && *lp != '\r' && *lp != '\0';
	     lp = skip_newline ( lp, is_qp ) ) {
		if (*lp == ':' || *lp == ';') {
			if (str->len != 0) {
				/* we've got a name, break out to the value/attribute parsing */
				attr_name = g_string_free (make_utf8_valid (&str), FALSE);
				str = NULL;
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
				str = NULL;
				*p = lp;
				skip_to_next_line (p);
				goto lose;
			}
		}
		else if (*lp == '.') {
			if (attr_group) {
				d (g_warning ("extra `.' in attribute specification.  ignoring extra group `%s'", str->str));
				g_string_set_size (str, 0);
			}
			if (str->len != 0) {
				attr_group = g_string_free (make_utf8_valid (&str), FALSE);
				str = g_string_sized_new (16);
			}
		}
		else if (*lp == '-' || *lp == '_' || g_ascii_isalnum (*lp)) {
			g_string_append_c (str, *lp);
		}
		else {
			d (g_warning ("invalid character found in attribute group/name"));
			g_string_free (str, TRUE);
			str = NULL;
			*p = lp;
			skip_to_next_line (p);
			goto lose;
		}

		lp++;
	}

	if (str) {
		g_string_free (str, TRUE);
		str = NULL;
	}

	if (!attr_name) {
		skip_to_next_line (p);
		goto lose;
	}

	/* This consumes (takes) both strings */
	attr = e_vcard_attribute_new_take (attr_group, attr_name);
	attr_group = NULL;
	attr_name = NULL;

	if (*lp == ';') {
		/* skip past the ';' */
		lp++;
		read_attribute_params (attr, &lp, &is_qp, &charset);
		if (is_qp)
			attr->encoding = EVC_ENCODING_RAW;
	}
	if (*lp == ':') {
		/* skip past the ':' */
		lp++;
		read_attribute_value (attr, &lp, is_qp, charset);
	}

	g_free (charset);

	*p = lp;

	if (!attr->values)
		goto lose;

	return attr;
 lose:

	if (str)
		g_string_free (str, TRUE);

	if (attr)
		e_vcard_attribute_free (attr);

	g_free (attr_group);
	g_free (attr_name);

	return NULL;
}

/* we try to be as forgiving as we possibly can here - this isn't a
 * validator.  Almost nothing is considered a fatal error.  We always
 * try to return *something*.
 */
static void
parse (EVCard *evc,
       const gchar *str,
       gboolean ignore_uid)
{
	const gchar *p;
	EVCardAttribute *attr;

	d (printf ("vCard parse input:\n%s\n", str));

	evc->priv->version = E_VCARD_VERSION_UNKNOWN;

	p = str;

	attr = read_attribute (&p);
	if (!attr || attr->group || g_ascii_strcasecmp (attr->name, "begin")) {
		d (g_warning ("vcard began without a BEGIN:VCARD (%s)\n", str));
	}
	if (attr && !g_ascii_strcasecmp (attr->name, "begin")) {
		e_vcard_attribute_free (attr);
		attr = NULL;
	} else if (attr) {
		if ((!ignore_uid || g_ascii_strcasecmp (attr->name, EVC_UID) != 0) &&
		    g_ascii_strcasecmp (attr->name, "end") != 0)
			e_vcard_add_attribute (evc, attr);
	}
	while (*p) {
		EVCardAttribute *next_attr = read_attribute (&p);

		if (next_attr) {
			attr = next_attr;

			if (g_ascii_strcasecmp (next_attr->name, "end") == 0)
				break;

			if (ignore_uid && g_ascii_strcasecmp (attr->name, EVC_UID) == 0) {
				e_vcard_attribute_free (attr);
				attr = NULL;
				continue;
			}

			e_vcard_add_attribute (evc, next_attr);
		}
	}

	if (!attr || attr->group || g_ascii_strcasecmp (attr->name, "end")) {
		d (g_warning ("vcard ended without END:VCARD (%s)\n", str));
	}

	if (attr && !g_ascii_strcasecmp (attr->name, "end"))
		e_vcard_attribute_free (attr);

	evc->priv->attributes = g_list_reverse (evc->priv->attributes);
}

static GList *
e_vcard_ensure_attributes (EVCard *evc)
{
	if (evc->priv->vcard) {
		gboolean have_uid = (evc->priv->attributes != NULL);
		gchar *vcs = evc->priv->vcard;

		/* detach vCard to avoid loops */
		evc->priv->vcard = NULL;

		/* Parse the vCard */
		parse (evc, vcs, have_uid);
		g_free (vcs);
	}

	return evc->priv->attributes;
}

static gchar *
e_vcard_escape_semicolons (const gchar *s)
{
	GString *str;
	const gchar *p;

	if (s)
		str = g_string_sized_new (strlen (s));
	else
		str = g_string_new ("");

	for (p = s; p && *p; p++) {
		if (*p == ';')
			g_string_append_c (str, '\\');

		g_string_append_c (str, *p);
	}

	return g_string_free (str, FALSE);
}

static void
e_vcard_escape_string_internal (const gchar *s,
				GString *str,
				gboolean skip_double_quotes)
{
	const gchar *p;

	/* Escape a string as described in RFC2426, section 5 */
	for (p = s; p && *p; p++) {
		if (skip_double_quotes && *p == '\"')
			continue;

		switch (*p) {
		case '\n':
			g_string_append (str, "\\n");
			break;
		case '\r':
			if (*(p + 1) == '\n')
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
}

/**
 * e_vcard_escape_string:
 * @s: the string to escape
 *
 * Escapes a string according to RFC2426, section 5.
 *
 * Returns: (transfer full): A newly allocated, escaped string.
 **/
gchar *
e_vcard_escape_string (const gchar *s)
{
	GString *str;

	if (s)
		str = g_string_sized_new (strlen (s));
	else
		str = g_string_new ("");

	e_vcard_escape_string_internal (s, str, FALSE);

	return g_string_free (str, FALSE);
}

/**
 * e_vcard_unescape_string:
 * @s: the string to unescape
 *
 * Unescapes a string according to RFC2426, section 5.
 *
 * Returns: (transfer full): A newly allocated, unescaped string.
 **/
gchar *
e_vcard_unescape_string (const gchar *s)
{
	GString *str;
	const gchar *p;

	g_return_val_if_fail (s != NULL, NULL);

	str = g_string_sized_new (strlen (s));

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
				d (g_warning ("invalid escape, passing it through"));
				g_string_append_c (str, '\\');
				g_string_append_c (str, *p);
				break;
			}
		} else {
			g_string_append_c (str, *p);
		}
	}

	return g_string_free (str, FALSE);
}

/**
 * e_vcard_construct:
 * @evc: an existing #EVCard
 * @str: a vCard string
 *
 * Constructs the existing #EVCard, @evc, setting its vCard data to @str.
 *
 * This modifies @evc.
 */
void
e_vcard_construct (EVCard *evc,
                   const gchar *str)
{
	e_vcard_construct_with_uid (evc, str, NULL);
}

/**
 * e_vcard_construct_with_uid:
 * @evc: an existing #EVCard
 * @str: a vCard string
 * @uid: (nullable): a unique ID string
 *
 * Constructs the existing #EVCard, @evc, setting its vCard data to @str, and
 * adding a new UID attribute with the value given in @uid (if @uid is
 * non-%NULL).
 *
 * This modifies @evc.
 *
 * Since: 3.4
 **/
void
e_vcard_construct_with_uid (EVCard *evc,
                            const gchar *str,
                            const gchar *uid)
{
	e_vcard_construct_full (evc, str, -1, uid);
}

/**
 * e_vcard_construct_full:
 * @evc: an existing #EVCard
 * @str: a vCard string
 * @len: length of @str, or -1 if @str is %NULL terminated
 * @uid: (nullable): a unique ID string
 *
 * Similar to e_vcard_construct_with_uid(), but can also
 * be used with an @str that is not %NULL terminated.
 *
 * Since: 3.12
 **/
void
e_vcard_construct_full (EVCard *evc,
                        const gchar *str,
                        gssize len,
                        const gchar *uid)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (str != NULL);
	g_return_if_fail (evc->priv->vcard == NULL);
	g_return_if_fail (evc->priv->attributes == NULL);

	evc->priv->version = E_VCARD_VERSION_UNKNOWN;

	/* Lazy construction */
	if (*str) {

		if (len < 0)
			evc->priv->vcard = g_strdup (str);
		else
			evc->priv->vcard = g_strndup (str, len);
	}

	/* Add UID attribute */
	if (uid) {
		EVCardAttribute *attr;

		attr = e_vcard_attribute_new (NULL, EVC_UID);
		e_vcard_attribute_add_value (attr, uid);

		evc->priv->attributes = g_list_prepend (evc->priv->attributes, attr);
	}
}

/**
 * e_vcard_new:
 *
 * Creates a new, blank #EVCard.
 *
 * Returns: (transfer full): A new, blank #EVCard.
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
 * Returns: (transfer full): A new #EVCard.
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
e_vcard_qp_encode (const gchar *txt,
                   gboolean can_wrap)
{
	const gchar *p = txt;
	GString *escaped = g_string_sized_new (strlen (txt));
	gint count = 0;

	while (*p != '\0') {
		if ((*p >= 33 && *p <= 60) || (*p >= 62 && *p <= 126)) {
			if (can_wrap && count == 75) {
				g_string_append (escaped, "=" CRLF " ");
				count = 1; /* 1 for space needed for folding */
			}

			g_string_append_c (escaped, *p++);
			count++;

			continue;
		}

		if (count >= 73 && can_wrap) {
			g_string_append (escaped, "=" CRLF " ");
			count = 1; /* 1 for space needed for folding */
		}

		g_string_append_printf (escaped, "=%2.2X", (guchar) *p++);
		count += 3;
	}

	return g_string_free (escaped, FALSE);
}

static gchar *
e_vcard_qp_decode (const gchar *txt)
{
	const gchar *inptr;
	gchar *decoded, *outptr;

	if (!txt)
		return NULL;

	decoded = g_malloc (sizeof (gchar) * strlen (txt) + 1);

	outptr = decoded;

	for (inptr = txt; *inptr; inptr++) {
		gchar c = *inptr;

		if (c == '=' && (inptr[1] == '\r' || inptr[1] == '\n')) {
			/* soft line-break */
			if (inptr[2] == '\n')
				inptr++;
			inptr++;
			continue;
		}

		if (c == '=' && inptr[1] && inptr[2]) {
			guchar a = toupper (inptr[1]), b = toupper (inptr[2]);
			if (isxdigit (a) && isxdigit (b)) {
				*outptr++ = (((a >= 'A' ? a - 'A' + 10 : a - '0') & 0x0f) << 4)
					   | ((b >= 'A' ? b - 'A' + 10 : b - '0') & 0x0f);
			} else {
				*outptr++ = '=';
				*outptr++ = inptr[1];
				*outptr++ = inptr[2];
			}

			inptr += 2;
		} else {
			*outptr++ = *inptr;
		}
	}

	*outptr = '\0';

	return decoded;
}

static GHashTable *
generate_dict_validator (const gchar *words)
{
	GHashTable *dict;
	gchar **list;
	gint i;

	if (!words)
		return NULL;

	dict = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);
	list = g_strsplit (words, ",", 0);

	for (i = 0; list[i]; i++)
		g_hash_table_insert (dict, list[i], NULL);

	g_free (list);

	return dict;
}

static gchar *
e_vcard_to_string_vcard_21 (EVCard *evc)
{
	GList *l, *v;
	GString *str = g_string_new ("");
	GHashTable *evc_prop = generate_dict_validator (E_VCARD_21_VALID_PROPERTIES);
	GHashTable *evc_params = generate_dict_validator (E_VCARD_21_VALID_PARAMETERS);

	g_string_append (str, "BEGIN:VCARD" CRLF);
	g_string_append (str, "VERSION:2.1" CRLF);

	for (l = e_vcard_ensure_attributes (evc); l; l = l->next) {
		GList *list;
		EVCardAttribute *attr = l->data;
		GString *attr_str;
		gboolean empty, encode;

		if (g_ascii_strcasecmp (attr->name, "VERSION") == 0)
			continue;

		/* Checking whether current property (attribute) is valid for vCard 2.1 */
		if (g_ascii_strncasecmp (attr->name, "X-", 2) != 0 &&
		    !g_hash_table_contains (evc_prop, attr->name))
			continue;

		empty = TRUE; /* Empty fields should be omitted -- some headsets may choke on it */
		encode = FALSE; /* Generally only new line MUST be encoded (Quoted Printable) */

		for (v = attr->values; v; v = v->next) {
			gchar *value = v->data;

			if (value && *value)
				empty = FALSE;
			else
				continue;

			if (strstr (value, "\n") != NULL) {
				encode = TRUE;
				break;
			}
		}

		if (empty)
			continue;

		attr_str = g_string_sized_new (strlen (attr->name) + (attr->group ? strlen (attr->group) + 1 : 0));

		/* From vCard 2.1 spec page 27, 28
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

			/* Page 28
			 *
			 * param        = param-name "=" param-value
			 */

			/* Checking whether current parameter is valid for vCard 2.1 */
			if (g_ascii_strncasecmp (param->name, "X-", 2) != 0 &&
			    !g_hash_table_contains (evc_params, param->name))
				continue;

			g_string_append_c (attr_str, ';');
			g_string_append (attr_str, param->name);
			if (!param->values)
				continue;

			for (v = param->values; v; v = v->next) {
				gchar *value = v->data;
				gchar *escaped_value = e_vcard_escape_semicolons (value);

				g_string_append_printf (attr_str, "=%s", escaped_value);

				if (v->next)
					g_string_append_printf (attr_str, ";%s", param->name);

				g_free (escaped_value);
			}
		}

		if (encode)
			g_string_append (attr_str, ";ENCODING=QUOTED-PRINTABLE");

		g_string_append_c (attr_str, ':');

		for (v = attr->values; v; v = v->next) {
			gchar *value = v->data;

			if (encode) {
				gchar *escaped_value;

				escaped_value = e_vcard_qp_encode (value, TRUE);
				g_string_append (attr_str, escaped_value);

				g_free (escaped_value);
			} else
				g_string_append (attr_str, value);

			if (v->next)
				g_string_append_c (attr_str, ';');
		}

		g_string_append (attr_str, CRLF);

		g_string_append (str, attr_str->str);

		g_string_free (attr_str, TRUE);
	}

	g_string_append (str, "END:VCARD");

	g_hash_table_destroy (evc_params);
	g_hash_table_destroy (evc_prop);

	return g_string_free (str, FALSE);
}

static EVCardVersion
vcard_version_from_string_with_fallback (const gchar *str,
					 EVCardVersion fallback)
{
	EVCardVersion version;

	version = e_vcard_version_from_string (str);
	if (version == E_VCARD_VERSION_UNKNOWN)
		version = fallback;

	return version;
}

static const gchar *
vcard_version_to_string_with_fallback (EVCardVersion version,
				       const gchar *fallback)
{
	const gchar *value;

	value = e_vcard_version_to_string (version);
	if (!value)
		value = fallback;

	return value;
}

static gchar *
e_vcard_to_string_vcard (EVCard *evc,
			 EVCardVersion to_version,
			 const gchar *valid_properties,
			 const gchar *valid_parameters)
{
	GHashTable *evc_prop = generate_dict_validator (valid_properties);
	GHashTable *evc_params = generate_dict_validator (valid_parameters);
	GList *l;
	GList *v;
	GString *str = g_string_new ("");

	g_string_append (str, "BEGIN:VCARD" CRLF);
	g_string_append (str, "VERSION:");
	g_string_append (str, vcard_version_to_string_with_fallback (to_version, "???"));
	g_string_append (str, CRLF);

	for (l = e_vcard_ensure_attributes (evc); l; l = l->next) {
		GList *list;
		EVCardAttribute *attr = l->data;
		GString *attr_str;
		glong len;
		EVCardAttributeParam *quoted_printable_param = NULL;
		gboolean is_categories;

		if (!g_ascii_strcasecmp (attr->name, "VERSION"))
			continue;

		/* Checking whether current property (attribute) is valid */
		if (evc_prop && g_ascii_strncasecmp (attr->name, "X-", 2) != 0 &&
		    !g_hash_table_contains (evc_prop, attr->name)) {
			continue;
		}

		attr_str = g_string_sized_new (strlen (attr->name) + (attr->group ? strlen (attr->group) + 1 : 0));

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

			/* quoted-printable encoding was eliminated in 3.0,
			 * thus decode the value before saving and remove the param later */
			if (!quoted_printable_param &&
			    param->values && param->values->data && !param->values->next &&
			    g_ascii_strcasecmp (param->name, "ENCODING") == 0 &&
			    g_ascii_strcasecmp (param->values->data, "quoted-printable") == 0) {
				quoted_printable_param = param;
				/* do not store it */
				continue;
			}

			/* Checking whether current parameter is valid for vCard 3.0 */
			if (evc_params && g_ascii_strncasecmp (param->name, "X-", 2) != 0 &&
			    !g_hash_table_contains (evc_params, param->name)) {
				continue;
			}

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
					while (pval && *pval) {
						if (!g_unichar_isalnum (g_utf8_get_char (pval))) {
							quotes = TRUE;
							break;
						}
						pval = g_utf8_next_char (pval);
					}

					if (quotes) {
						g_string_append_c (attr_str, '"');
						e_vcard_escape_string_internal (value, attr_str, quotes);
						g_string_append_c (attr_str, '"');
					} else {
						g_string_append (attr_str, value);
					}

					if (v->next)
						g_string_append_c (attr_str, ',');
				}
			}
		}

		g_string_append_c (attr_str, ':');

		is_categories = !g_ascii_strcasecmp (attr->name, EVC_CATEGORIES);

		for (v = attr->values; v; v = v->next) {
			gchar *value = v->data;
			gchar *escaped_value = NULL;

			/* values are in quoted-printable encoding, but this cannot be used in vCard other than 2.1,
			 * thus it needs to be converted first */
			if (quoted_printable_param) {
				gchar *qp_decoded;

				qp_decoded = e_vcard_qp_decode (value);

				/* replace the actual value with the decoded */
				g_free (value);
				value = qp_decoded;
				v->data = value;
			}

			escaped_value = e_vcard_escape_string (value);

			g_string_append (attr_str, escaped_value);
			if (v->next) {
				/* XXX toshok - i hate you, rfc 2426.
				 * why doesn't CATEGORIES use a; like
				 * a normal list attribute? */
				if (is_categories)
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
			GString *fold_str = g_string_sized_new (attr_str->len + len / 74 * 3);
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

		/* remove the encoding parameter, to not decode multiple times */
		if (quoted_printable_param)
			e_vcard_attribute_remove_param (attr, quoted_printable_param->name);
	}

	g_string_append (str, "END:VCARD");

	g_clear_pointer (&evc_params, g_hash_table_unref);
	g_clear_pointer (&evc_prop, g_hash_table_unref);

	return g_string_free (str, FALSE);
}

static gchar *
e_vcard_to_string_vcard_30 (EVCard *evc)
{
	EVCard *converted;
	gchar *str;

	converted = e_vcard_convert (evc, E_VCARD_VERSION_30);

	str = e_vcard_to_string_vcard (converted ? converted : evc, E_VCARD_VERSION_30,
		E_VCARD_30_VALID_PROPERTIES, E_VCARD_30_VALID_PARAMETERS);

	g_clear_object (&converted);

	return str;
}

static gchar *
e_vcard_to_string_vcard_40 (EVCard *evc)
{
	EVCard *converted;
	gchar *str;

	converted = e_vcard_convert (evc, E_VCARD_VERSION_40);

	str = e_vcard_to_string_vcard (converted ? converted : evc, E_VCARD_VERSION_40,
		E_VCARD_40_VALID_PROPERTIES, E_VCARD_40_VALID_PARAMETERS);

	g_clear_object (&converted);

	return str;
}

/**
 * e_vcard_to_string:
 * @self: an #EVCard to export
 *
 * Exports @self to a string representation. To use a specific vCard version
 * use e_vcard_convert_to_string().
 *
 * Returns: (transfer full): A newly allocated string representing the vCard.
 *
 * Since: 3.60
 **/
gchar *
e_vcard_to_string (EVCard *self)
{
	EVCardVersion version;

	g_return_val_if_fail (E_IS_VCARD (self), NULL);

	if (self->priv->vcard && self->priv->attributes == NULL)
		return g_strdup (self->priv->vcard);

	version = e_vcard_get_version (self);
	if (version == E_VCARD_VERSION_UNKNOWN)
		version = E_VCARD_VERSION_40;

	return e_vcard_convert_to_string (self, version);
}

/**
 * e_vcard_get_version:
 * @self: an #EVCard
 *
 * Gets a vCard version of the @self. The %E_VCARD_VERSION_UNKNOWN
 * is returned only if there is a VERSION attribute, but with an unknown
 * value. Otherwise an %E_VCARD_VERSION_40 is used as a fallback version.
 *
 * Returns: a vCard version of the @self, as one of #EVCardVersion
 *
 * Since: 3.60
 **/
EVCardVersion
e_vcard_get_version (EVCard *self)
{
	GList *link;

	g_return_val_if_fail (E_IS_VCARD (self), E_VCARD_VERSION_UNKNOWN);

	if (self->priv->version != E_VCARD_VERSION_UNKNOWN)
		return self->priv->version;

	if (self->priv->vcard) {
		/* ordered from the newest */
		if (camel_strstrcase (self->priv->vcard, CRLF "VERSION:4.0" CRLF)) {
			self->priv->version = E_VCARD_VERSION_40;
			return self->priv->version;
		}
		if (camel_strstrcase (self->priv->vcard, CRLF "VERSION:3.0" CRLF)) {
			self->priv->version = E_VCARD_VERSION_30;
			return self->priv->version;
		}
		if (camel_strstrcase (self->priv->vcard, CRLF "VERSION:2.1" CRLF)) {
			self->priv->version = E_VCARD_VERSION_21;
			return self->priv->version;
		}
	}

	for (link = e_vcard_get_attributes (self); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;

		if (attr && attr->name && !attr->group && g_ascii_strcasecmp (attr->name, "VERSION") == 0) {
			GList *values;

			values = e_vcard_attribute_get_values (attr);
			if (values && values->data) {
				const gchar *value = values->data;

				self->priv->version = vcard_version_from_string_with_fallback (value, self->priv->version);
			}

			return self->priv->version;
		}
	}

	/* assume the newest version when none is set in the vCard data */
	self->priv->version = E_VCARD_VERSION_40;

	return self->priv->version;
}

static EVCard *
e_vcard_convert_to_generic (EVCard *self,
			    EVCardVersion to_version,
			    const gchar *valid_properties,
			    const gchar *valid_parameters,
			    GHashTable *attr_renames,
			    /* called with the original attr name, not with the renamed */
			    gboolean (* attr_handler_cb) (EVCard *new_vcard, EVCardAttribute *new_attr, const gchar **inout_new_attr_name, gpointer user_data),
			    gpointer user_data)
{
	EVCard *converted;
	EVCardAttribute *attr;
	GList *attr_link;
	GHashTable *evc_prop;
	GHashTable *evc_params;

	switch (to_version) {
	case E_VCARD_VERSION_21:
	case E_VCARD_VERSION_30:
	case E_VCARD_VERSION_40:
		break;
	default:
		g_warn_if_reached ();
		return NULL;
	}

	evc_prop = generate_dict_validator (valid_properties);
	evc_params = generate_dict_validator (valid_parameters);

	converted = e_vcard_new ();

	attr = e_vcard_attribute_new (NULL, EVC_VERSION);
	e_vcard_add_attribute_with_value (converted, attr, vcard_version_to_string_with_fallback (to_version, "???"));

	for (attr_link = e_vcard_ensure_attributes (self); attr_link; attr_link = g_list_next (attr_link)) {
		EVCardAttribute *new_attr;
		GList *link, *next = NULL;
		const gchar *attr_name;
		gboolean empty;

		attr = attr_link->data;
		attr_name = attr->name;

		if (g_ascii_strcasecmp (attr_name, EVC_VERSION) == 0)
			continue;

		if (attr_renames) {
			const gchar *rename = g_hash_table_lookup (attr_renames, attr_name);

			if (rename)
				attr_name = rename;
		}

		/* Checking whether current property (attribute) is valid */
		if (evc_prop && g_ascii_strncasecmp (attr_name, "X-", 2) != 0 &&
		    !g_hash_table_contains (evc_prop, attr_name))
			continue;

		empty = TRUE; /* Empty fields should be omitted -- some headsets may choke on it */

		for (link = attr->values; link; link = g_list_next (link)) {
			const gchar *value = link->data;

			if (value && *value) {
				empty = FALSE;
				break;
			}
		}

		if (empty)
			continue;

		new_attr = e_vcard_attribute_copy (attr);

		if (attr_handler_cb && !attr_handler_cb (converted, new_attr, &attr_name, user_data)) {
			e_vcard_attribute_free (new_attr);
			continue;
		}

		if (attr_name != attr->name) {
			g_free (new_attr->name);
			new_attr->name = g_strdup (attr_name);
		}

		/* handle the parameters */
		if (evc_params) {
			for (link = new_attr->params; link; link = next) {
				EVCardAttributeParam *param = link->data;

				next = g_list_next (link);

				/* Checking whether current parameter is valid */
				if (g_ascii_strncasecmp (param->name, "X-", 2) != 0 &&
				    !g_hash_table_contains (evc_params, param->name)) {
					new_attr->params = g_list_delete_link (new_attr->params, link);
					e_vcard_attribute_param_free (param);
				}
			}
		}

		e_vcard_add_attribute (converted, new_attr);
	}

	g_clear_pointer (&evc_params, g_hash_table_destroy);
	g_clear_pointer (&evc_prop, g_hash_table_destroy);

	converted->priv->attributes = g_list_reverse (converted->priv->attributes);

	return converted;
}

/* quoted-printable encoding was eliminated in 3.0,
 * thus decode the value before saving and remove the param later */
static gboolean
e_vcard_convert_qp_encoding_cb (EVCard *new_vcard,
				EVCardAttribute *new_attr,
				const gchar **inout_new_attr_name,
				gpointer user_data)
{
	GList *decoded_values = NULL, *link;

	if (new_attr->encoding != EVC_ENCODING_QP)
		return TRUE;

	for (link = new_attr->values; link; link = g_list_next (link)) {
		const gchar *value = link->data;

		if (value && *value)
			decoded_values = g_list_prepend (decoded_values, e_vcard_qp_decode (value));
		else
			decoded_values = g_list_prepend (decoded_values, g_strdup (value));
	}

	e_vcard_attribute_remove_param (new_attr, EVC_ENCODING);
	e_vcard_attribute_remove_values (new_attr);

	new_attr->values = g_list_reverse (decoded_values);
	/* it should be set already due to removed ENCODING parameter, but just in case */
	new_attr->encoding = EVC_ENCODING_RAW;

	return TRUE;
}

static gboolean
e_vcard_is_date_attr (EVCardAttribute *new_attr)
{
	return g_ascii_strcasecmp (new_attr->name, EVC_X_ANNIVERSARY) == 0 ||
		g_ascii_strcasecmp (new_attr->name, EVC_ANNIVERSARY) == 0 ||
		g_ascii_strcasecmp (new_attr->name, EVC_BDAY) == 0;
}

static void
e_vcard_convert_date_value (EVCardAttribute *new_attr,
			    EVCardVersion to_version)
{
	GList *values = e_vcard_attribute_get_values (new_attr);
	const gchar *value = values ? values->data : NULL;

	if (value && *value) {
		if (to_version == E_VCARD_VERSION_40) {
			if (strlen (value) == 10 && value[4] == '-' && value[7] == '-') {
				gchar new_value[9];

				new_value[0] = value[0];
				new_value[1] = value[1];
				new_value[2] = value[2];
				new_value[3] = value[3];
				new_value[4] = value[5];
				new_value[5] = value[6];
				new_value[6] = value[8];
				new_value[7] = value[9];
				new_value[8] = '\0';

				e_vcard_attribute_remove_values (new_attr);
				e_vcard_attribute_add_value (new_attr, new_value);
			}
		} else if (strlen (value) == 8) {
			gchar new_value[11];

			new_value[0] = value[0];
			new_value[1] = value[1];
			new_value[2] = value[2];
			new_value[3] = value[3];
			new_value[4] = '-';
			new_value[5] = value[4];
			new_value[6] = value[5];
			new_value[7] = '-';
			new_value[8] = value[6];
			new_value[9] = value[7];
			new_value[10] = '\0';

			e_vcard_attribute_remove_values (new_attr);
			e_vcard_attribute_add_value (new_attr, new_value);
		}
	}
}

static gboolean
e_vcard_convert_to_21_attrs_cb (EVCard *new_vcard,
				EVCardAttribute *new_attr,
				const gchar **inout_new_attr_name,
				gpointer user_data)
{
	if (e_vcard_is_date_attr (new_attr))
		e_vcard_convert_date_value (new_attr, E_VCARD_VERSION_21);

	return TRUE;
}

/* very easy, just drop everything unknown in 2.1 */
static EVCard *
e_vcard_convert_to_21 (EVCard *self)
{
	return e_vcard_convert_to_generic (self, E_VCARD_VERSION_21,
		E_VCARD_21_VALID_PROPERTIES, E_VCARD_21_VALID_PARAMETERS,
		NULL, e_vcard_convert_to_21_attrs_cb, NULL);
}

static EVCard *
e_vcard_convert_21_to_30 (EVCard *self)
{
	return e_vcard_convert_to_generic (self, E_VCARD_VERSION_30,
		E_VCARD_30_VALID_PROPERTIES, E_VCARD_30_VALID_PARAMETERS,
		NULL, e_vcard_convert_qp_encoding_cb, NULL);
}

/* holds both property and parameter names */
static GHashTable * /* const gchar * ~> const gchar * */
e_vcard_convert_get_30_40_rename_hash (EVCardVersion to_version)
{
	struct _renames {
		const gchar *in_30;
		const gchar *in_40;
	} renames[] = {
		#define item(_nm40) { EVC_X_EVOLUTION_ ## _nm40, EVC_ ## _nm40 }
		/* properties */
		{ EVC_X_ANNIVERSARY, EVC_ANNIVERSARY },
		{ EVC_X_SOCIALPROFILE, EVC_SOCIALPROFILE },
		item (SOURCE),
		item (KIND),
		item (XML),
		item (GENDER),
		item (IMPP),
		item (LANG),
		item (MEMBER),
		item (RELATED),
		item (CLIENTPIDMAP),
		item (CALADRURI),
		item (BIRTHPLACE),
		item (DEATHPLACE),
		item (DEATHDATE),
		item (EXPERTISE),
		item (HOBBY),
		item (INTEREST),
		item (ORG_DIRECTORY),
		item (CONTACT_URI),
		item (CREATED),
		item (GRAMGENDER),
		item (PRONOUNS),
		/* parameters */
		item (LANGUAGE),
		item (PREF),
		item (ALTID),
		item (PID),
		item (MEDIATYPE),
		item (CALSCALE),
		item (SORT_AS)
		#undef item
	};
	GHashTable *hash;
	guint ii;

	g_return_val_if_fail (to_version == E_VCARD_VERSION_30 || to_version == E_VCARD_VERSION_40, NULL);

	hash = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);

	for (ii = 0; ii < G_N_ELEMENTS (renames); ii++) {
		const gchar *lookup, *rename;

		if (to_version == E_VCARD_VERSION_30) {
			lookup = renames[ii].in_40;
			rename = renames[ii].in_30;
		} else {
			lookup = renames[ii].in_30;
			rename = renames[ii].in_40;
		}

		g_hash_table_insert (hash, (gpointer) lookup, (gpointer) rename);
	}

	return hash;
}

static GHashTable * /* const gchar * ~> const gchar * */
e_vcard_convert_get_30_40_impp_scheme_hash (EVCardVersion to_version)
{
	struct _schemes {
		const gchar *in_30; /* attr name */
		const gchar *in_40; /* scheme */
	} schemes[] = {
		{ EVC_X_AIM, "aim:" },
		{ EVC_X_GADUGADU, "gadugadu:" },
		{ EVC_X_GOOGLE_TALK, "googletalk:" },
		{ EVC_X_GROUPWISE, "groupwise:" },
		{ EVC_X_ICQ, "icq:" },
		{ EVC_X_JABBER, "jabber:" },
		{ EVC_X_MATRIX, "matrix:" },
		{ EVC_X_MSN, "msn:" },
		{ EVC_X_SKYPE, "skype:" },
		{ EVC_X_TWITTER, "twitter:" },
		{ EVC_X_YAHOO, "yahoo:" }
	};
	GHashTable *hash;
	guint ii;

	g_return_val_if_fail (to_version == E_VCARD_VERSION_30 || to_version == E_VCARD_VERSION_40, NULL);

	hash = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);

	for (ii = 0; ii < G_N_ELEMENTS (schemes); ii++) {
		const gchar *lookup, *result;

		if (to_version == E_VCARD_VERSION_30) {
			lookup = schemes[ii].in_40;
			result = schemes[ii].in_30;
		} else {
			lookup = schemes[ii].in_30;
			result = schemes[ii].in_40;
		}

		g_hash_table_insert (hash, (gpointer) lookup, (gpointer) result);
	}

	return hash;
}

typedef struct _ConvertData30 {
	EVCardVersion to_version;
	GHashTable *renames; /* const gchar * ~> const gchar * */
	GHashTable *impp_schemes; /* constgchar * ~> const gchar *; scheme to 3.0 attr name or vice versa */
	GHashTable *attrs_by_name; /* const gchar *name ~> GPtrArray { EVCardAttribute * }*/
} ConvertData30;

static void
convert_data_30_init (ConvertData30 *data,
		      EVCard *self,
		      EVCardVersion to_version)
{
	GList *link;

	data->to_version = to_version;
	data->renames = e_vcard_convert_get_30_40_rename_hash (to_version);
	data->impp_schemes = e_vcard_convert_get_30_40_impp_scheme_hash (to_version);
	data->attrs_by_name = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, NULL, (GDestroyNotify) g_ptr_array_unref);

	for (link = e_vcard_ensure_attributes (self); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;

		if (attr && attr->name) {
			GPtrArray *attrs;

			attrs = g_hash_table_lookup (data->attrs_by_name, attr->name);
			if (!attrs) {
				attrs = g_ptr_array_new ();
				g_hash_table_insert (data->attrs_by_name, (gpointer) attr->name, attrs);
			}

			g_ptr_array_add (attrs, attr);
		}
	}
}

static void
convert_data_30_clear (ConvertData30 *data)
{
	g_clear_pointer (&data->renames, g_hash_table_unref);
	g_clear_pointer (&data->impp_schemes, g_hash_table_unref);
	g_clear_pointer (&data->attrs_by_name, g_hash_table_unref);
}

static gboolean
e_vcard_convert_30_40_cb (EVCard *new_vcard,
			  EVCardAttribute *new_attr,
			  const gchar **inout_new_attr_name,
			  gpointer user_data)
{
	ConvertData30 *data = user_data;
	gboolean keep = TRUE;

	if (g_ascii_strcasecmp (new_attr->name, EVC_ADR) == 0) {
		/* LABEL of an ADR became a parameter in 4.0, while it was an attribute in 3.0 */
		if (data->to_version == E_VCARD_VERSION_30) {
			GList *values;

			values = e_vcard_attribute_get_param (new_attr, EVC_LABEL);
			if (values && !values->next && values->data) {
				EVCardAttribute *attr;

				attr = e_vcard_attribute_new (e_vcard_attribute_get_group (new_attr), EVC_LABEL);
				e_vcard_add_attribute_with_value (new_vcard, attr, values->data);

				values = e_vcard_attribute_get_param (new_attr, EVC_TYPE);
				if (values) {
					EVCardAttributeParam *param;
					GList *link;
					gboolean any_added = FALSE;

					param = e_vcard_attribute_param_new (EVC_TYPE);

					for (link = values; link; link = g_list_next (link)) {
						const gchar *value = link->data;

						if (value && *value) {
							e_vcard_attribute_param_add_value (param, value);
							any_added = TRUE;
						}
					}

					if (any_added)
						e_vcard_attribute_add_param (attr, param);
					else
						e_vcard_attribute_param_free (param);
				}
			}
		} else { /* data->to_version == E_VCARD_VERSION_40 */
			GPtrArray *labels;

			labels = g_hash_table_lookup (data->attrs_by_name, EVC_LABEL);

			if (labels && labels->len > 0) {
				GList *values, *link;
				GHashTable *expected_types;
				guint ii, n_expected_types;

				expected_types = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);
				values = e_vcard_attribute_get_param (new_attr, EVC_TYPE);

				for (link = values; link; link = g_list_next (link)) {
					const gchar *value = link->data;

					if (value && *value)
						g_hash_table_add (expected_types, (gpointer) value);
				}

				n_expected_types = g_hash_table_size (expected_types);

				for (ii = 0; ii < labels->len; ii++) {
					EVCardAttribute *label_attr = g_ptr_array_index (labels, ii);
					guint n_found = 0;

					values = e_vcard_attribute_get_param (label_attr, EVC_TYPE);

					for (link = values; link; link = g_list_next (link)) {
						const gchar *value = link->data;

						if (value && *value && g_hash_table_contains (expected_types, value)) {
							n_found++;
							if (n_found == n_expected_types)
								break;
						}
					}

					if (n_found == n_expected_types) {
						GString *value;

						value = e_vcard_attribute_get_value_decoded (label_attr);
						if (value) {
							if (value->len) {
								EVCardAttributeParam *param;

								/* this should not happen, but if a leftover exists... */
								e_vcard_attribute_remove_param (new_attr, EVC_LABEL);

								param = e_vcard_attribute_param_new (EVC_LABEL);
								e_vcard_attribute_add_param_with_value (new_attr, param, value->str);
							}

							g_string_free (value, TRUE);
						}

						/* to not use it again */
						g_ptr_array_remove_index_fast (labels, ii);
						break;
					}
				}

				g_hash_table_unref (expected_types);
			}
		}
	} else if (data->to_version == E_VCARD_VERSION_40 && g_ascii_strcasecmp (*inout_new_attr_name, EVC_KIND) == 0) {
		/* EVC_X_LIST has precedence, if it came first */
		keep = !e_vcard_get_attribute (new_vcard, EVC_KIND);
	} else if (data->to_version == E_VCARD_VERSION_40 && g_ascii_strcasecmp (*inout_new_attr_name, EVC_X_LIST) == 0) {
		EVCardAttribute *existing_attr = e_vcard_get_attribute (new_vcard, EVC_KIND);
		GList *values = e_vcard_attribute_get_values (new_attr);
		const gchar *value = values ? values->data : NULL;

		/* replace EVC_X_LIST with KIND:group if the kind is not set yet */
		if (!existing_attr && value && g_ascii_strcasecmp (value, "TRUE") == 0)
			e_vcard_add_attribute_with_value (new_vcard, e_vcard_attribute_new (NULL, EVC_KIND), "group");
		keep = FALSE;
	} else if (data->to_version == E_VCARD_VERSION_30 && g_ascii_strcasecmp (*inout_new_attr_name, EVC_X_EVOLUTION_KIND) == 0) {
		GList *values = e_vcard_attribute_get_values (new_attr);
		const gchar *value = values ? values->data : NULL;

		if (value && g_ascii_strcasecmp (value, "group") == 0)
			e_vcard_add_attribute_with_value (new_vcard, e_vcard_attribute_new (NULL, EVC_X_LIST), "TRUE");
	} else if (data->to_version == E_VCARD_VERSION_30 && g_ascii_strcasecmp (new_attr->name, EVC_IMPP) == 0) {
		keep = e_vcard_attribute_is_single_valued (new_attr);
		if (keep) {
			GString *value;

			value = e_vcard_attribute_get_value_decoded (new_attr);
			keep = value && value->len > 0;

			if (keep) {
				/* it's supposed to be a URI, aka 'xmpp:user@example.com' */
				gchar *scheme_split, next_char;

				scheme_split = strchr (value->str, ':');
				keep = scheme_split != NULL;

				if (keep) {
					const gchar *in_30;

					next_char = scheme_split[1];
					scheme_split[1] = '\0';
					/* value->str is cut after the scheme delimiter now */
					in_30 = g_hash_table_lookup (data->impp_schemes, value->str);
					scheme_split[1] = next_char;

					if (in_30) {
						*inout_new_attr_name = in_30;
						e_vcard_attribute_remove_values (new_attr);
						e_vcard_attribute_add_value (new_attr, scheme_split + 1);
					} else {
						*inout_new_attr_name = EVC_X_EVOLUTION_IMPP;
					}
				}
			}

			if (value)
				g_string_free (value, TRUE);
		}
	} else if (g_ascii_strcasecmp (new_attr->name, EVC_PHOTO) == 0 ||
		   g_ascii_strcasecmp (new_attr->name, EVC_LOGO) == 0) {
		GList *values;
		const gchar *value = NULL;

		values = e_vcard_attribute_get_param (new_attr, EVC_VALUE);
		if (values)
			value = values->data;

		if (data->to_version == E_VCARD_VERSION_40) {
			/* inline photo/logo is converted into "data:" uri */
			if (!value || g_ascii_strcasecmp (value, "uri") != 0) {
				values = e_vcard_attribute_get_param (new_attr, EVC_ENCODING);
				if (values && (g_ascii_strcasecmp (values->data, "b") == 0 ||
					       /* second for photo vCard 2.1 support */
					       g_ascii_strcasecmp (values->data, "base64") == 0)) {
					values = e_vcard_attribute_get_values (new_attr);
					if (values && values->data) {
						const gchar *content = values->data;

						if (content && *content) {
							GString *data_uri = g_string_new ("data:");

							values = e_vcard_attribute_get_param (new_attr, EVC_TYPE);
							if (values && values->data) {
								g_string_append_printf (data_uri, "image/%s", (const gchar *) values->data);
								g_string_append_c (data_uri, ';');
							}

							g_string_append (data_uri, "base64,");
							g_string_append (data_uri, content);

							e_vcard_attribute_remove_params (new_attr);
							e_vcard_attribute_remove_values (new_attr);
							e_vcard_attribute_add_value_take (new_attr, g_string_free (data_uri, FALSE));
						}
					}
				}
			}
		} else /* if (data->to_version == E_VCARD_VERSION_30) */ {
			values = e_vcard_attribute_get_values (new_attr);
			/* convert "data:" URI into inline photo */
			if (values && values->data && g_ascii_strncasecmp (values->data, "data:", 5) == 0) {
				const gchar *data_start = NULL;
				gchar *mime_type = NULL;
				gboolean is_base64 = FALSE;

				if (e_util_split_data_uri (values->data, &mime_type, NULL, &is_base64, &data_start) && is_base64) {
					e_vcard_attribute_remove_params (new_attr);
					e_vcard_attribute_remove_values (new_attr);
					e_vcard_attribute_add_value (new_attr, data_start);
					e_vcard_attribute_add_param_with_value (new_attr, e_vcard_attribute_param_new (EVC_ENCODING), "b");
					if (mime_type) {
						const gchar *dash = strchr (mime_type, '/');

						if (dash && dash[1])
							e_vcard_attribute_add_param_with_value (new_attr, e_vcard_attribute_param_new (EVC_TYPE), dash + 1);
					}
				}

				g_free (mime_type);
			} else if (values && values->data) {
				values = e_vcard_attribute_get_param (new_attr, EVC_VALUE);
				if (!values)
					e_vcard_attribute_add_param_with_value (new_attr, e_vcard_attribute_param_new (EVC_VALUE), "uri");
			}
		}
	/* convert date values between YYYY-MM-DD and YYYYMMDD */
	} else if (e_vcard_is_date_attr (new_attr)) {
		e_vcard_convert_date_value (new_attr, data->to_version);
	} else if (data->to_version == E_VCARD_VERSION_40) {
		const gchar *impp_scheme;

		impp_scheme = g_hash_table_lookup (data->impp_schemes, new_attr->name);
		if (impp_scheme) {
			*inout_new_attr_name = EVC_IMPP;

			keep = e_vcard_attribute_is_single_valued (new_attr);
			if (keep) {
				GString *value;

				value = e_vcard_attribute_get_value_decoded (new_attr);
				keep = value && value->len > 0;

				if (keep) {
					g_string_prepend (value, impp_scheme);
					e_vcard_attribute_remove_values (new_attr);
					e_vcard_attribute_add_value_take (new_attr, g_string_free (g_steal_pointer (&value), FALSE));
				}

				if (value)
					g_string_free (value, TRUE);
			}
		} else if (g_ascii_strcasecmp (new_attr->name, EVC_X_EVOLUTION_IMPP) == 0) {
			*inout_new_attr_name = EVC_IMPP;
		}
	}

	return keep;
}

static EVCard *
e_vcard_convert_30_to_40 (EVCard *self)
{
	EVCard *converted;
	ConvertData30 data;

	convert_data_30_init (&data, self, E_VCARD_VERSION_40);

	converted = e_vcard_convert_to_generic (self, E_VCARD_VERSION_40,
		E_VCARD_40_VALID_PROPERTIES, E_VCARD_40_VALID_PARAMETERS,
		data.renames, e_vcard_convert_30_40_cb, &data);

	convert_data_30_clear (&data);

	return converted;
}

static EVCard *
e_vcard_convert_40_to_30 (EVCard *self)
{
	EVCard *converted;
	ConvertData30 data;

	convert_data_30_init (&data, self, E_VCARD_VERSION_30);

	converted = e_vcard_convert_to_generic (self, E_VCARD_VERSION_30,
		E_VCARD_30_VALID_PROPERTIES, E_VCARD_30_VALID_PARAMETERS,
		data.renames, e_vcard_convert_30_40_cb, &data);

	convert_data_30_clear (&data);

	return converted;
}

/**
 * e_vcard_convert:
 * @self: an #EVCard
 * @to_version: the requested vCard version, one of #EVCardVersion
 *
 * Converts the @self into the vCard version @to_version and returns a converted
 * copy of the @self. When the @to_version matches the version of the @self,
 * then does nothing and returns %NULL.
 *
 * Returns: (transfer full) (nullable): the @self converted to @to_version,
 *    or %NULL, when it is in this version already
 *
 * Since: 3.60
 **/
EVCard *
e_vcard_convert (EVCard *self,
		 EVCardVersion to_version)
{
	EVCardVersion self_version;
	EVCard *converted = NULL, *tmp;

	g_return_val_if_fail (E_IS_VCARD (self), NULL);
	g_return_val_if_fail (to_version != E_VCARD_VERSION_UNKNOWN, NULL);

	self_version = e_vcard_get_version (self);

	if (self_version == to_version)
		return NULL;

	switch (to_version) {
	case E_VCARD_VERSION_21:
		converted = e_vcard_convert_to_21 (self);
		break;
	case E_VCARD_VERSION_30:
		switch (self_version) {
		case E_VCARD_VERSION_40:
			converted = e_vcard_convert_40_to_30 (self);
			break;
		case E_VCARD_VERSION_21:
			converted = e_vcard_convert_21_to_30 (self);
			break;
		default:
			break;
		}
		break;
	case E_VCARD_VERSION_40:
		switch (self_version) {
		case E_VCARD_VERSION_30:
			converted = e_vcard_convert_30_to_40 (self);
			break;
		case E_VCARD_VERSION_21:
			tmp = e_vcard_convert_21_to_30 (self);
			converted = e_vcard_convert_30_to_40 (tmp);
			g_clear_object (&tmp);
			break;
		default:
			break;
		}
		break;
	case E_VCARD_VERSION_UNKNOWN:
	default:
		break;
	}

	if (!converted) {
		GList *link;

		g_warning ("%s: Do not know how to convert vCard %s (%d) to %s (%d)\n", G_STRFUNC,
			vcard_version_to_string_with_fallback (self_version, "???"), self_version,
			vcard_version_to_string_with_fallback (to_version, "???"), to_version);

		converted = e_vcard_new ();

		g_clear_pointer (&converted->priv->vcard, g_free);
		g_list_free_full (converted->priv->attributes, (GDestroyNotify) e_vcard_attribute_free);
		converted->priv->attributes = NULL;

		for (link = e_vcard_get_attributes (self); link; link = g_list_next (link)) {
			EVCardAttribute *copy = e_vcard_attribute_copy (link->data);
			converted->priv->attributes = g_list_prepend (converted->priv->attributes, copy);
		}

		converted->priv->attributes = g_list_reverse (converted->priv->attributes);
		converted->priv->version = E_VCARD_VERSION_UNKNOWN;
	}

	return converted;
}

/**
 * e_vcard_convert_to_string:
 * @self: an #EVCard to export
 * @version: the vCard version to export to
 *
 * Exports @evc to a string representation conforming to vCard
 * version @version.
 *
 * Returns: (transfer full): A newly allocated string representing the vCard
 *    in version @version.
 *
 * Since: 3.60
 **/
gchar *
e_vcard_convert_to_string (EVCard *self,
			   EVCardVersion version)
{
	g_return_val_if_fail (E_IS_VCARD (self), NULL);
	g_return_val_if_fail (version != E_VCARD_VERSION_UNKNOWN, NULL);

	switch (version) {
	case E_VCARD_VERSION_21:
		if (self->priv->vcard && self->priv->attributes == NULL &&
		    camel_strstrcase (self->priv->vcard, CRLF "VERSION:2.1" CRLF))
			return g_strdup (self->priv->vcard);

		return e_vcard_to_string_vcard_21 (self);
	case E_VCARD_VERSION_30:
		if (self->priv->vcard && self->priv->attributes == NULL &&
		    camel_strstrcase (self->priv->vcard, CRLF "VERSION:3.0" CRLF))
			return g_strdup (self->priv->vcard);

		return e_vcard_to_string_vcard_30 (self);
	case E_VCARD_VERSION_40:
		if (self->priv->vcard && self->priv->attributes == NULL &&
		    camel_strstrcase (self->priv->vcard, CRLF "VERSION:4.0" CRLF))
			return g_strdup (self->priv->vcard);

		return e_vcard_to_string_vcard_40 (self);
	default:
		g_warning ("invalid format specifier passed to e_vcard_convert_to_string: %d", version);
		return NULL;
	}
}

/**
 * EVCardForeachFunc:
 * @vcard: an #EVCard
 * @attr: an #EVCardAttribute
 * @user_data: callback user data
 *
 * A callback prototype for e_vcard_foreach() and e_vcard_foreach_remove().
 *
 * Returns: the value depends on the function it is used with; see its
 *    documentation for more information
 *
 * Since: 3.60
 **/

/**
 * e_vcard_foreach:
 * @self: an #EVCard
 * @flags: a bit-or of #EVCardForeachFlags
 * @func: (scope call) (closure user_data): an #EVCardForeachFunc callback function
 * @user_data: user data passed to the @func
 *
 * Calls @func for each attribute in the @self. The @func returns %TRUE to
 * continue the walk-through, or %FALSE to stop.
 *
 * Since: 3.60
 **/
void
e_vcard_foreach (EVCard *self,
		 EVCardForeachFlags flags,
		 EVCardForeachFunc func,
		 gpointer user_data)
{
	GList *link;

	g_return_if_fail (E_IS_VCARD (self));
	g_return_if_fail (func != NULL);

	if ((flags & E_VCARD_FOREACH_FLAG_WILL_MODIFY) != 0) {
		GPtrArray *attrs;
		guint ii;

		attrs = g_ptr_array_new ();
		for (link = e_vcard_get_attributes (self); link; link = g_list_next (link)) {
			EVCardAttribute *attr = link->data;
			g_ptr_array_add (attrs, attr);
		}

		for (ii = 0; ii < attrs->len; ii++) {
			EVCardAttribute *attr = g_ptr_array_index (attrs, ii);

			if (!func (self, attr, user_data))
				break;
		}

		g_ptr_array_unref (attrs);
	} else {
		for (link = e_vcard_get_attributes (self); link; link = g_list_next (link)) {
			EVCardAttribute *attr = link->data;

			if (!func (self, attr, user_data))
				break;
		}
	}
}

/**
 * e_vcard_foreach_remove:
 * @self: an #EVCard
 * @func: (scope call) (closure user_data): an #EVCardForeachFunc callback function
 * @user_data: user data passed to the @func
 *
 * Removes all attributes the @func returns %TRUE for.
 *
 * Returns: how many attributes had been removed
 *
 * Since: 3.60
 **/
guint
e_vcard_foreach_remove (EVCard *self,
			EVCardForeachFunc func,
			gpointer user_data)
{
	GList *link;
	guint n_removed = 0;

	g_return_val_if_fail (E_IS_VCARD (self), 0);
	g_return_val_if_fail (func != NULL, 0);

	link = e_vcard_ensure_attributes (self);
	while (link) {
		GList *next;
		EVCardAttribute *attr = link->data;

		next = link->next;

		if (func (self, attr, user_data)) {
			if (self->priv->version != E_VCARD_VERSION_UNKNOWN &&
			    g_ascii_strcasecmp (attr->name, EVC_VERSION) == 0)
				self->priv->version = E_VCARD_VERSION_UNKNOWN;

			self->priv->attributes = g_list_delete_link (self->priv->attributes, link);

			e_vcard_attribute_free (attr);

			n_removed++;
		}

		link = next;
	}

	return n_removed;
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
	for (a = e_vcard_ensure_attributes (evc); a; a = a->next) {
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
					gchar *value = e_vcard_escape_string ((gchar *) v->data);
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
			printf ("        [%d] = `%s'\n", i, (gchar *) v->data);
		}
	}
}

/**
 * e_vcard_attribute_new:
 * @attr_group: (nullable): a group name
 * @attr_name: an attribute name
 *
 * Creates a new #EVCardAttribute with the specified group and
 * attribute names. The @attr_group may be %NULL or the empty string if no
 * group is needed.
 *
 * Returns: (transfer full): A new #EVCardAttribute.
 **/
EVCardAttribute *
e_vcard_attribute_new (const gchar *attr_group,
                       const gchar *attr_name)
{
	return e_vcard_attribute_new_take ((attr_group && *attr_group) ? g_strdup (attr_group) : NULL, g_strdup (attr_name));
}

/**
 * e_vcard_attribute_free:
 * @attr: (transfer full): attribute to free
 *
 * Frees an attribute, its values and its parameters.
 **/
void
e_vcard_attribute_free (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	e_vcard_attribute_unref (attr);
}

static EVCardAttribute *
e_vcard_attribute_ref (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	g_atomic_int_inc (&attr->ref_count);

	return attr;
}

static void
e_vcard_attribute_unref (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	if (g_atomic_int_dec_and_test (&attr->ref_count)) {
		g_free (attr->group);
		g_free (attr->name);

		e_vcard_attribute_remove_values (attr);

		e_vcard_attribute_remove_params (attr);

		g_slice_free (EVCardAttribute, attr);
	}
}

/**
 * e_vcard_attribute_copy:
 * @attr: attribute to copy
 *
 * Makes a copy of @attr.
 *
 * Returns: (transfer full): A new #EVCardAttribute identical to @attr.
 **/
EVCardAttribute *
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

typedef struct _RemoveAttrsData {
	const gchar *group;
	const gchar *name;
} RemoveAttrsData;

static gboolean
vcard_remove_attributes_cb (EVCard *vcard,
			    EVCardAttribute *attr,
			    gpointer user_data)
{
	RemoveAttrsData *data = user_data;

	return ((!data->group || *data->group == '\0') ||
		(attr->group && !g_ascii_strcasecmp (data->group, attr->group))) &&
		((!attr->name) || !g_ascii_strcasecmp (data->name, attr->name));
}

/**
 * e_vcard_remove_attributes:
 * @evc: vcard object
 * @attr_group: (nullable): group name of attributes to be removed
 * @attr_name: name of the arributes to be removed
 *
 * Removes all the attributes with group name and attribute name equal to the
 * passed in values. If @attr_group is %NULL or an empty string,
 * it removes all the attributes with passed in name irrespective of
 * their group names.
 **/
void
e_vcard_remove_attributes (EVCard *evc,
                           const gchar *attr_group,
                           const gchar *attr_name)
{
	RemoveAttrsData data;

	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr_name != NULL);

	data.group = attr_group;
	data.name = attr_name;

	if (g_ascii_strcasecmp (attr_name, EVC_VERSION) == 0)
		evc->priv->version = E_VCARD_VERSION_UNKNOWN;

	e_vcard_foreach_remove (evc, vcard_remove_attributes_cb, &data);
}

/**
 * e_vcard_remove_attribute:
 * @evc: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to remove
 *
 * Removes @attr from @evc and frees it. This takes ownership of @attr.
 **/
void
e_vcard_remove_attribute (EVCard *evc,
                          EVCardAttribute *attr)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr != NULL);

	if (attr->name && g_ascii_strcasecmp (attr->name, EVC_VERSION) == 0)
		evc->priv->version = E_VCARD_VERSION_UNKNOWN;

	/* No need to call e_vcard_ensure_attributes() here. It has
	 * already been called if this is a valid call and attr is among
	 * our attributes. */
	evc->priv->attributes = g_list_remove (evc->priv->attributes, attr);
	e_vcard_attribute_free (attr);
}

static void
cache_version_from_attr (EVCard *self,
			 EVCardAttribute *attr)
{
	if (attr && attr->name && attr->values && !attr->values->next && attr->values->data &&
	    g_ascii_strcasecmp (attr->name, EVC_VERSION) == 0) {
		const gchar *value = attr->values->data;

		self->priv->version = vcard_version_from_string_with_fallback (value, E_VCARD_VERSION_UNKNOWN);
	}
}

/**
 * e_vcard_append_attribute:
 * @evc: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to append
 *
 * Appends @attr to @evc to the end of a list of attributes. This takes
 * ownership of @attr.
 *
 * Since: 2.32
 **/
void
e_vcard_append_attribute (EVCard *evc,
                          EVCardAttribute *attr)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr != NULL);

	/* Handle UID special case:
	 * No need to parse the vcard to append an UID attribute */
	if (evc->priv->vcard != NULL && attr->name != NULL &&
	    g_ascii_strcasecmp (attr->name, EVC_UID) == 0) {
		evc->priv->attributes = g_list_append (evc->priv->attributes, attr);
	} else {
		evc->priv->attributes = g_list_append (e_vcard_ensure_attributes (evc), attr);
		cache_version_from_attr (evc, attr);
	}
}

/**
 * e_vcard_append_attribute_with_value:
 * @evcard: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to append
 * @value: a value to assign to the attribute
 *
 * Appends @attr to @evcard, setting it to @value. This takes ownership of
 * @attr.
 *
 * This is a convenience wrapper around e_vcard_attribute_add_value() and
 * e_vcard_append_attribute().
 *
 * See also e_vcard_append_attribute_with_value_take(), e_vcard_add_attribute_with_value()
 *
 * Since: 2.32
 **/
void
e_vcard_append_attribute_with_value (EVCard *evcard,
                                     EVCardAttribute *attr,
                                     const gchar *value)
{
	g_return_if_fail (E_IS_VCARD (evcard));
	g_return_if_fail (attr != NULL);

	e_vcard_attribute_add_value (attr, value);

	e_vcard_append_attribute (evcard, attr);
}

/**
 * e_vcard_append_attribute_with_value_take:
 * @evcard: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to append
 * @value: (transfer full): a value to assign to the attribute
 *
 * Appends @attr to @evcard, setting it to @value. This takes ownership of
 * @attr and the @value.
 *
 * This is a convenience wrapper around e_vcard_attribute_add_value_take() and
 * e_vcard_append_attribute().
 *
 * See also e_vcard_append_attribute_with_value(), e_vcard_add_attribute_with_value()
 *
 * Since: 3.60
 **/
void
e_vcard_append_attribute_with_value_take (EVCard *evcard,
					  EVCardAttribute *attr,
					  gchar *value)
{
	g_return_if_fail (E_IS_VCARD (evcard));
	g_return_if_fail (attr != NULL);

	e_vcard_attribute_add_value_take (attr, value);
	e_vcard_append_attribute (evcard, attr);
}

/**
 * e_vcard_append_attribute_with_values:
 * @evcard: an @EVCard
 * @attr: (transfer full): an #EVCardAttribute to append
 * @...: a %NULL-terminated list of values to assign to the attribute
 *
 * Appends @attr to @evcard, assigning the list of values to it. This takes
 * ownership of @attr.
 *
 * This is a convenience wrapper around e_vcard_attribute_add_value() and
 * e_vcard_append_attribute().
 *
 * Since: 2.32
 **/
void
e_vcard_append_attribute_with_values (EVCard *evcard,
                                      EVCardAttribute *attr,
                                      ...)
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

	e_vcard_append_attribute (evcard, attr);
}

/**
 * e_vcard_append_attributes:
 * @self: an #EVCard
 * @attrs: (element-type EVCardAttribute) (transfer none): a #GList of #EVCardAttribute
 *
 * Appends #EVCardAttribute structures from @attrs to @self. The respective
 * attributes are copied, thus the caller is responsible to take care
 * of the @attrs and its content.
 *
 * Since: 3.60
 **/
void
e_vcard_append_attributes (EVCard *self,
			   const GList *attrs)
{
	GList *link;
	GList *attrs_copy = NULL;

	g_return_if_fail (E_IS_VCARD (self));

	for (link = (GList *) attrs; link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;

		attrs_copy = g_list_prepend (attrs_copy, e_vcard_attribute_copy (attr));
	}

	attrs_copy = g_list_reverse (attrs_copy);

	e_vcard_append_attributes_take (self, attrs_copy);
}

/**
 * e_vcard_append_attributes_take:
 * @self: an #EVCard
 * @attrs: (element-type EVCardAttribute) (transfer full): a #GList of #EVCardAttribute
 *
 * Appends #EVCardAttribute structures from @attrs to @self. The @self
 * assumes ownership of both the @attrs and the respective attributes
 * stored in it.
 *
 * Since: 3.60
 **/
void
e_vcard_append_attributes_take (EVCard *self,
				GList *attrs)
{
	GList *link;

	g_return_if_fail (E_IS_VCARD (self));

	if (!attrs)
		return;

	link = e_vcard_ensure_attributes (self);
	self->priv->attributes = g_list_concat (link, attrs);
}

/**
 * e_vcard_add_attribute:
 * @evc: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to add
 *
 * Prepends @attr to @evc. This takes ownership of @attr.
 **/
void
e_vcard_add_attribute (EVCard *evc,
                       EVCardAttribute *attr)
{
	g_return_if_fail (E_IS_VCARD (evc));
	g_return_if_fail (attr != NULL);

	/* Handle UID special case:
	 * No need to parse the vcard to append an UID attribute */
	if (evc->priv->vcard != NULL && attr->name != NULL &&
	    g_ascii_strcasecmp (attr->name, EVC_UID) == 0) {
		evc->priv->attributes = g_list_prepend (evc->priv->attributes, attr);
	} else {
		evc->priv->attributes = g_list_prepend (e_vcard_ensure_attributes (evc), attr);
		cache_version_from_attr (evc, attr);
	}
}

/**
 * e_vcard_add_attribute_with_value:
 * @evcard: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to add
 * @value: a value to assign to the attribute
 *
 * Prepends @attr to @evcard, setting it to @value. This takes ownership of
 * @attr.
 *
 * This is a convenience wrapper around e_vcard_attribute_add_value() and
 * e_vcard_add_attribute().
 *
 * See also e_vcard_add_attribute_with_value_take(), e_vcard_append_attribute_with_value()
 **/
void
e_vcard_add_attribute_with_value (EVCard *evcard,
                                  EVCardAttribute *attr,
                                  const gchar *value)
{
	g_return_if_fail (E_IS_VCARD (evcard));
	g_return_if_fail (attr != NULL);

	e_vcard_attribute_add_value (attr, value);

	e_vcard_add_attribute (evcard, attr);
}

/**
 * e_vcard_add_attribute_with_value_take:
 * @evcard: an #EVCard
 * @attr: (transfer full): an #EVCardAttribute to add
 * @value: (transfer full): a value to assign to the attribute
 *
 * Prepends @attr to @evcard, setting it to @value. This takes ownership of
 * the @attr and the @value.
 *
 * This is a convenience wrapper around e_vcard_attribute_add_value_take() and
 * e_vcard_add_attribute().
 *
 * See also e_vcard_add_attribute_with_value(), e_vcard_append_attribute_with_value()
 *
 * Since: 3.60
 **/
void
e_vcard_add_attribute_with_value_take (EVCard *evcard,
				       EVCardAttribute *attr,
				       gchar *value)
{
	g_return_if_fail (E_IS_VCARD (evcard));
	g_return_if_fail (attr != NULL);

	e_vcard_attribute_add_value_take (attr, value);
	e_vcard_add_attribute (evcard, attr);
}

/**
 * e_vcard_add_attribute_with_values:
 * @evcard: an @EVCard
 * @attr: (transfer full): an #EVCardAttribute to add
 * @...: a %NULL-terminated list of values to assign to the attribute
 *
 * Prepends @attr to @evcard, assigning the list of values to it. This takes
 * ownership of @attr.
 *
 * This is a convenience wrapper around e_vcard_attribute_add_value() and
 * e_vcard_add_attribute().
 **/
void
e_vcard_add_attribute_with_values (EVCard *evcard,
                                   EVCardAttribute *attr,
                                   ...)
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
 * Appends @value to @attr's list of values.
 *
 * See also e_vcard_attribute_add_value_take().
 **/
void
e_vcard_attribute_add_value (EVCardAttribute *attr,
                             const gchar *value)
{
	g_return_if_fail (attr != NULL);

	attr->values = g_list_append (attr->values, g_strdup (value));
}

/**
 * e_vcard_attribute_add_value_take:
 * @attr: an #EVCardAttribute
 * @value: (transfer full): a value to add and take
 *
 * Appends @value to @attr's list of values, assuming ownership
 * of the @value.
 *
 * See also e_vcard_attribute_add_value().
 *
 * Since: 3.60
 **/
void
e_vcard_attribute_add_value_take (EVCardAttribute *attr,
				  gchar *value)
{
	g_return_if_fail (attr != NULL);

	attr->values = g_list_append (attr->values, value);
}

/**
 * e_vcard_attribute_add_value_decoded:
 * @attr: an #EVCardAttribute
 * @value: an encoded value
 * @len: the length of the encoded value, in bytes
 *
 * Encodes @value according to the encoding used for @attr, and appends it to
 * @attr's list of values.
 *
 * This should only be used if the #EVCardAttribute has a non-raw encoding (i.e.
 * if it’s encoded in base-64 or quoted-printable encoding).
 **/
void
e_vcard_attribute_add_value_decoded (EVCardAttribute *attr,
                                     const gchar *value,
                                     gint len)
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

		d (printf ("base64 encoded value: %s\n", b64_data));
		d (printf ("original length: %d\n", len));

		attr->values = g_list_append (attr->values, b64_data);
		attr->decoded_values = g_list_append (attr->decoded_values, decoded);
		break;
	}
	case EVC_ENCODING_QP: {
		GString *decoded = g_string_new_len (value, len);
		gchar *qp_data = e_vcard_qp_encode (decoded->str, FALSE);

		/* make sure the decoded list is up to date */
		e_vcard_attribute_get_values_decoded (attr);

		d (printf ("qp encoded value: %s\n", qp_data));
		d (printf ("original length: %d\n", len));

		attr->values = g_list_append (attr->values, qp_data);
		attr->decoded_values = g_list_append (attr->decoded_values, decoded);
		break;
	}
	}
}

/**
 * e_vcard_attribute_add_values:
 * @attr: an #EVCardAttribute
 * @...: a %NULL-terminated list of strings
 *
 * Appends a list of values to @attr.
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
 * Removes and frees all values from @attr.
 **/
void
e_vcard_attribute_remove_values (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	g_list_foreach (attr->values, (GFunc) g_free, NULL);
	g_list_free (attr->values);
	attr->values = NULL;

	g_list_foreach (attr->decoded_values, (GFunc) free_gstring, NULL);
	g_list_free (attr->decoded_values);
	attr->decoded_values = NULL;
}

/**
 * e_vcard_attribute_remove_value:
 * @attr: an #EVCardAttribute
 * @s: a value to remove
 *
 * Removes value @s from the value list in @attr. The value @s is not freed.
 **/
void
e_vcard_attribute_remove_value (EVCardAttribute *attr,
                                const gchar *s)
{
	GList *l;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (s != NULL);

	l = g_list_find_custom (attr->values, s, (GCompareFunc) strcmp);
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
 * Removes and frees parameter @param_name from the attribute @attr. Parameter
 * names are guaranteed to be unique, so @attr is guaranteed to have no
 * parameters named @param_name after this function returns.
 *
 * Since: 1.12
 */
void
e_vcard_attribute_remove_param (EVCardAttribute *attr,
                                const gchar *param_name)
{
	GList *l;
	EVCardAttributeParam *param;

	g_return_if_fail (attr != NULL);
	g_return_if_fail (param_name != NULL);

	for (l = attr->params; l; l = l->next) {
		param = l->data;
		if (g_ascii_strcasecmp (e_vcard_attribute_param_get_name (param),
					param_name) == 0) {
			if (g_ascii_strcasecmp (param_name, EVC_ENCODING) == 0) {
				attr->encoding_set = FALSE;
				attr->encoding = EVC_ENCODING_RAW;
			}

			attr->params = g_list_delete_link (attr->params, l);
			e_vcard_attribute_param_free (param);
			break;
		}
	}
}

/**
 * e_vcard_attribute_remove_params:
 * @attr: an #EVCardAttribute
 *
 * Removes and frees all parameters from @attr.
 *
 * This also resets the #EVCardAttribute's encoding back to raw.
 **/
void
e_vcard_attribute_remove_params (EVCardAttribute *attr)
{
	g_return_if_fail (attr != NULL);

	g_list_foreach (attr->params, (GFunc) e_vcard_attribute_param_free, NULL);
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
 * Returns: (transfer full): A new #EVCardAttributeParam.
 **/
EVCardAttributeParam *
e_vcard_attribute_param_new (const gchar *name)
{
	EVCardAttributeParam *param = g_slice_new (EVCardAttributeParam);

	param->ref_count = 1;
	param->values = NULL;
	param->name = g_strdup (name);

	return param;
}

/**
 * e_vcard_attribute_param_free:
 * @param: (transfer full): an #EVCardAttributeParam
 *
 * Frees @param and its values.
 **/
void
e_vcard_attribute_param_free (EVCardAttributeParam *param)
{
	g_return_if_fail (param != NULL);

	e_vcard_attribute_param_unref (param);
}

static EVCardAttributeParam *
e_vcard_attribute_param_ref (EVCardAttributeParam *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	g_atomic_int_inc (&param->ref_count);

	return param;
}

static void
e_vcard_attribute_param_unref (EVCardAttributeParam *param)
{
	g_return_if_fail (param != NULL);

	if (g_atomic_int_dec_and_test (&param->ref_count)) {
		g_free (param->name);

		e_vcard_attribute_param_remove_values (param);

		g_slice_free (EVCardAttributeParam, param);
	}
}

/**
 * e_vcard_attribute_param_copy:
 * @param: an #EVCardAttributeParam
 *
 * Makes a copy of @param and all its values.
 *
 * Returns: (transfer full): a new #EVCardAttributeParam identical to @param.
 **/
EVCardAttributeParam *
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
 * @param: (transfer full): an #EVCardAttributeParam to add
 *
 * Prepends @param to @attr's list of parameters. This takes ownership of
 * @param (and all its values).
 *
 * Duplicate parameters have their values merged, so that all parameter names
 * in @attr are unique. Values are also merged so that uniqueness is preserved.
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
			 * better merge them than have more parameters
			 * with same name within one attribute.
			*/
			GList *vals,*v;

			vals = param->values;

			for (v = vals; v; v = v->next) {
				const gchar *my_value;
				GList *vals2,*v2;

				my_value = (const gchar *) v->data;
				vals2 = param2->values;

				for (v2 = vals2; v2; v2 = v2->next) {
					if (g_ascii_strcasecmp ((const gchar *) v2->data, my_value) == 0) {
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
			if (!g_ascii_strcasecmp ((gchar *) param->values->data, "b") ||
			    !g_ascii_strcasecmp ((gchar *) param->values->data, "BASE64"))
				attr->encoding = EVC_ENCODING_BASE64;
			else if (!g_ascii_strcasecmp ((gchar *) param->values->data, EVC_QUOTEDPRINTABLE))
				attr->encoding = EVC_ENCODING_QP;
			else {
				g_warning (
					"Unknown value `%s' for ENCODING parameter.  values will be treated as raw",
					(gchar *) param->values->data);
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
 * Appends @value to @param's list of values.
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
 * @...: a %NULL-terminated list of strings
 *
 * Appends a list of values to @param.
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
 * @param: (transfer full): an #EVCardAttributeParam
 * @value: a string value
 *
 * Appends @value to @param, then prepends @param to @attr. This takes ownership
 * of @param, but not of @value.
 *
 * This is a convenience method for e_vcard_attribute_param_add_value() and
 * e_vcard_attribute_add_param().
 **/
void
e_vcard_attribute_add_param_with_value (EVCardAttribute *attr,
                                        EVCardAttributeParam *param,
                                        const gchar *value)
{
	g_return_if_fail (attr != NULL);
	g_return_if_fail (param != NULL);

	e_vcard_attribute_param_add_value (param, value);

	e_vcard_attribute_add_param (attr, param);
}

/**
 * e_vcard_attribute_add_param_with_values:
 * @attr: an #EVCardAttribute
 * @param: (transfer full): an #EVCardAttributeParam
 * @...: a %NULL-terminated list of strings
 *
 * Appends the list of values to @param, then prepends @param to @attr. This
 * takes ownership of @param, but not of the list of values.
 *
 * This is a convenience method for e_vcard_attribute_param_add_value() and
 * e_vcard_attribute_add_param().
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

	g_list_foreach (param->values, (GFunc) g_free, NULL);
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
 * If @s was the only value for parameter @param_name, that parameter is removed
 * entirely from @attr and freed.
 **/
void
e_vcard_attribute_remove_param_value (EVCardAttribute *attr,
                                      const gchar *param_name,
                                      const gchar *s)
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
			l = g_list_find_custom (param->values, s, (GCompareFunc) g_ascii_strcasecmp);
			if (l == NULL) {
				return;
			}

			g_free (l->data);
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
 * Gets the list of all attributes from @evcard. The list and its
 * contents are owned by @evcard, and must not be freed.
 *
 * Returns: (transfer none) (element-type EVCardAttribute): A list of attributes
 * of type #EVCardAttribute.
 **/
GList *
e_vcard_get_attributes (EVCard *evcard)
{
	g_return_val_if_fail (E_IS_VCARD (evcard), NULL);

	return e_vcard_ensure_attributes (evcard);
}

/**
 * e_vcard_get_attribute:
 * @evc: an #EVCard
 * @name: the name of the attribute to get
 *
 * Get the attribute @name from @evc.  The #EVCardAttribute is owned by
 * @evc and should not be freed. If the attribute does not exist, %NULL is
 * returned.
 *
 * <note><para>This will only return the <emphasis>first</emphasis> attribute
 * with the given @name. To get other attributes of that name (for example,
 * other <code>TEL</code> attributes if a contact has multiple telephone
 * numbers), use e_vcard_get_attributes() and iterate over the list searching
 * for matching attributes.</para>
 * <para>This method iterates over all attributes in the #EVCard, so should not
 * be called often. If extracting a large number of attributes from a vCard, it
 * is more efficient to iterate once over the list returned by
 * e_vcard_get_attributes().</para></note>
 *
 * Returns: (transfer none) (nullable): An #EVCardAttribute if found, or %NULL.
 **/
EVCardAttribute *
e_vcard_get_attribute (EVCard *evc,
                       const gchar *name)
{
	GList *attrs, *l;
	EVCardAttribute *attr;

	g_return_val_if_fail (E_IS_VCARD (evc), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	/* Handle UID special case */
	if (evc->priv->vcard != NULL && g_ascii_strcasecmp (name, EVC_UID) == 0) {
		for (l = evc->priv->attributes; l != NULL; l = l->next) {
			attr = (EVCardAttribute *) l->data;
			if (g_ascii_strcasecmp (attr->name, name) == 0)
				return attr;
		}
	}

	attrs = e_vcard_ensure_attributes (evc);
	for (l = attrs; l; l = l->next) {
		attr = (EVCardAttribute *) l->data;
		if (g_ascii_strcasecmp (attr->name, name) == 0)
			return attr;
	}

	return NULL;
}

typedef struct _AttrsByNameData {
	const gchar *name;
	GList *attrs;
} AttrsByNameData;

static gboolean
get_attributes_by_name_cb (EVCard *vcard,
			   EVCardAttribute *attr,
			   gpointer user_data)
{
	AttrsByNameData *data = user_data;

	if (g_ascii_strcasecmp (data->name, attr->name) == 0)
		data->attrs = g_list_prepend (data->attrs, attr);

	return TRUE;
}

/**
 * e_vcard_get_attributes_by_name:
 * @self: an #EVCard
 * @name: (not nullable): an attribute name
 *
 * Returns all attributes of the name @name stored in the @self.
 * See also e_vcard_get_attribute(), which returns the first
 * found attribute only.
 *
 * The returned #EVCardAttribute -s are owned by the @self and
 * they are valid until the @self changes. The returned #GList
 * should be freed with g_list_free(), when no longer needed.
 *
 * Returns: (transfer container) (nullable) (element-type EVCardAttribute): a new #GList
 *    of #EVCardAttribute objects, which are named @name and stored in the @self, or %NULL,
 *    when the @self does not contain any such attribute. The attributes are in the list
 *    in the order as they appear in the @self.
 *
 * Since: 3.60
 **/
GList *
e_vcard_get_attributes_by_name (EVCard *self,
				const gchar *name)
{
	AttrsByNameData data;

	g_return_val_if_fail (E_IS_VCARD (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	data.name = name;
	data.attrs = NULL;

	e_vcard_foreach (self, E_VCARD_FOREACH_FLAG_NONE, get_attributes_by_name_cb, &data);

	return g_list_reverse (data.attrs);
}

/**
 * e_vcard_get_attribute_if_parsed:
 * @evc: an #EVCard
 * @name: the name of the attribute to get
 *
 * Similar to e_vcard_get_attribute() but this method will not attempt to
 * parse the vCard if it is not already parsed.
 *
 * Returns: (transfer none) (nullable): An #EVCardAttribute if found, or %NULL.
 *
 * Since: 3.4
 **/
EVCardAttribute *
e_vcard_get_attribute_if_parsed (EVCard *evc,
                                 const gchar *name)
{
	GList *l;
	EVCardAttribute *attr;

	g_return_val_if_fail (E_IS_VCARD (evc), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (l = evc->priv->attributes; l != NULL; l = l->next) {
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
 * Returns: (nullable): The attribute's group name, or %NULL.
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
 * Gets the ordered list of values from @attr. The list and its
 * contents are owned by @attr, and must not be freed.
 *
 * For example, for an <code>ADR</code> (postal address) attribute, this will
 * return the components of the postal address.
 *
 * This may be called on a single-valued attribute (i.e. one for which
 * e_vcard_attribute_is_single_valued() returns %TRUE) and will return a
 * one-element list in that case. Alternatively, use
 * e_vcard_attribute_get_value() in such cases.
 *
 * Returns: (transfer none) (element-type utf8): A list of string values. They
 * will all be non-%NULL, but may be empty strings. The list itself may be
 * empty.
 **/
GList *
e_vcard_attribute_get_values (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	return attr->values;
}

/**
 * e_vcard_attribute_get_values_decoded:
 * @attr: an #EVCardAttribute
 *
 * Gets the ordered list of values from @attr, decoding them if
 * necessary according to the encoding given in the vCard’s
 * <code>ENCODING</code> attribute. The list and its contents are owned by
 * @attr, and must not be freed.
 *
 * This may be called on a single-valued attribute (i.e. one for which
 * e_vcard_attribute_is_single_valued() returns %TRUE) and will return a
 * one-element list in that case. Alternatively, use
 * e_vcard_attribute_get_value_decoded() in such cases.
 *
 * Returns: (transfer none) (element-type GString): A list of values of type #GString.
 **/
GList *
e_vcard_attribute_get_values_decoded (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, NULL);

	if (!attr->decoded_values) {
		GList *l;
		switch (attr->encoding) {
		case EVC_ENCODING_RAW:
			for (l = attr->values; l; l = l->next)
				attr->decoded_values = g_list_prepend (attr->decoded_values, g_string_new ((gchar *) l->data));
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
			for (l = attr->values; l; l = l->next) {
				gchar *decoded;

				decoded = e_vcard_qp_decode (l->data);
				attr->decoded_values = g_list_prepend (attr->decoded_values, g_string_new (decoded));
				g_free (decoded);
			}
			attr->decoded_values = g_list_reverse (attr->decoded_values);
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
 * For example, for a <code>FN</code> (full name) attribute, this will
 * return the contact’s full name as a single string.
 *
 * This will print a warning if called on an #EVCardAttribute which is not
 * single-valued (i.e. for which e_vcard_attribute_is_single_valued() returns
 * %FALSE). Use e_vcard_attribute_get_values() in such cases instead.
 *
 * Returns: (nullable): A newly allocated string representing
 * the value, or %NULL if the attribute has no value.
 **/
gchar *
e_vcard_attribute_get_value (EVCardAttribute *attr)
{
	GList *values;

	g_return_val_if_fail (attr != NULL, NULL);

	values = e_vcard_attribute_get_values (attr);

	if (!e_vcard_attribute_is_single_valued (attr))
		g_warning ("e_vcard_attribute_get_value called on multivalued attribute");

	return values ? g_strdup ((gchar *) values->data) : NULL;
}

/**
 * e_vcard_attribute_get_value_decoded:
 * @attr: an #EVCardAttribute
 *
 * Gets the value of a single-valued #EVCardAttribute, @attr, decoding
 * it if necessary according to the encoding given in the vCard’s
 * <code>ENCODING</code> attribute.
 *
 * This will print a warning if called on an #EVCardAttribute which is not
 * single-valued (i.e. for which e_vcard_attribute_is_single_valued() returns
 * %FALSE). Use e_vcard_attribute_get_values_decoded() in such cases instead.
 *
 * Returns: (nullable) (transfer full): A newly allocated #GString
 * representing the value, or %NULL if the attribute has no value.
 **/
GString *
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
 * e_vcard_attribute_get_n_values:
 * @attr: an #EVCardAttribute
 *
 * Gets how many values the @attr holds.
 *
 * Returns: how many values the @attr holds
 *
 * Since: 3.60
 **/
guint
e_vcard_attribute_get_n_values (EVCardAttribute *attr)
{
	g_return_val_if_fail (attr != NULL, 0);

	return g_list_length (e_vcard_attribute_get_values (attr));
}

/**
 * e_vcard_attribute_get_nth_value:
 * @attr: an #EVCardAttribute
 * @index: an index of the value to receive, counting from zero
 *
 * Gets the value at index @index (counting from zero), of the @attr.
 * The value is owned by the @attr and is valid until the @attr changes
 * or is freed. The @index can be out of bounds, then a %NULL is returned.
 * Use @e_vcard_attribute_get_n_values() to check how many values
 * the @attr has stored.
 *
 * Returns: (nullable): a value at index @index, or %NULL, when out of bounds.
 *
 * Since: 3.60
 **/
const gchar *
e_vcard_attribute_get_nth_value (EVCardAttribute *attr,
				  guint index)
{
	g_return_val_if_fail (attr != NULL, NULL);

	return g_list_nth_data (e_vcard_attribute_get_values (attr), index);
}

/**
 * e_vcard_attribute_has_type:
 * @attr: an #EVCardAttribute
 * @typestr: a string representing the type
 *
 * Checks if @attr has an #EVCardAttributeParam with name %EVC_TYPE and @typestr
 * as one of its values.
 *
 * For example, for the vCard attribute:
 * |[
 * TEL;TYPE=WORK,VOICE:(111) 555-1212
 * ]|
 * the following holds true:
 * |[
 * g_assert_true (e_vcard_attribute_has_type (attr, "WORK") == TRUE);
 * g_assert_true (e_vcard_attribute_has_type (attr, "voice") == TRUE);
 * g_assert_true (e_vcard_attribute_has_type (attr, "HOME") == FALSE);
 * ]|
 *
 * Comparisons against @typestr are case-insensitive.
 *
 * Returns: %TRUE if such a parameter exists, %FALSE otherwise.
 **/
gboolean
e_vcard_attribute_has_type (EVCardAttribute *attr,
                            const gchar *typestr)
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
				if (!g_ascii_strcasecmp ((gchar *) v->data, typestr))
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
 * Gets the list of parameters (of type #EVCardAttributeParam) from @attr. The
 * list and its contents are owned by @attr, and must not be freed.
 *
 * Returns: (transfer none) (element-type EVCardAttributeParam): A list of
 * elements of type #EVCardAttributeParam.
 **/
GList *
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
 * contents are owned by @attr, and must not be freed. If no parameter with the
 * given @name exists, %NULL is returned.
 *
 * Returns: (transfer none) (element-type utf8) (nullable): A list of string
 * elements representing the parameter's values, or %NULL.
 **/
GList *
e_vcard_attribute_get_param (EVCardAttribute *attr,
                             const gchar *name)
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
 * e_vcard_is_parsed:
 * @evc: an #EVCard
 *
 * Check if the @evc has been parsed already, as #EVCard implements lazy parsing
 * of its vCard data. Used for debugging.
 *
 * Return value: %TRUE if @evc has been parsed, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_vcard_is_parsed (EVCard *evc)
{
	g_return_val_if_fail (E_IS_VCARD (evc), FALSE);

	return (!evc->priv->vcard && evc->priv->attributes);
}

/**
 * e_vcard_attribute_param_get_name:
 * @param: an #EVCardAttributeParam
 *
 * Gets the name of @param.
 *
 * For example, for the only parameter of the vCard attribute:
 * |[
 * TEL;TYPE=WORK,VOICE:(111) 555-1212
 * ]|
 * this would return <code>TYPE</code> (which is string-equivalent to
 * %EVC_TYPE).
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
 * For example, for the <code>TYPE</code> parameter of the vCard attribute:
 * |[
 * TEL;TYPE=WORK,VOICE:(111) 555-1212
 * ]|
 * this would return the list <code>WORK</code>, <code>VOICE</code>.
 *
 * Returns: (transfer none) (element-type utf8): A list of string elements
 * representing the parameter's values.
 **/
GList *
e_vcard_attribute_param_get_values (EVCardAttributeParam *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return param->values;
}

/**
 * e_vcard_util_set_x_attribute:
 * @vcard: an #EVCard
 * @x_name: the attribute name, which starts with "X-"
 * @value: (nullable): the value to set, or %NULL to unset
 *
 * Sets an "X-" attribute @x_name to value @value in @vcard, or
 * removes it from @vcard, when @value is %NULL.
 *
 * Since: 3.26
 **/
void
e_vcard_util_set_x_attribute (EVCard *vcard,
			      const gchar *x_name,
			      const gchar *value)
{
	EVCardAttribute *attr;

	g_return_if_fail (E_IS_VCARD (vcard));
	g_return_if_fail (x_name != NULL);
	g_return_if_fail (g_str_has_prefix (x_name, "X-"));

	attr = e_vcard_get_attribute (vcard, x_name);

	if (attr) {
		e_vcard_attribute_remove_values (attr);
		if (value) {
			e_vcard_attribute_add_value (attr, value);
		} else {
			e_vcard_remove_attribute (vcard, attr);
		}
	} else if (value) {
		e_vcard_append_attribute_with_value (
			vcard,
			e_vcard_attribute_new (NULL, x_name),
			value);
	}
}

/**
 * e_vcard_util_dup_x_attribute:
 * @vcard: an #EVCard
 * @x_name: the attribute name, which starts with "X-"
 *
 * Returns: (nullable): Value of attribute @x_name, or %NULL,
 *    when there is no such attribute. Free the returned pointer with g_free(),
 *    when no longer needed.
 *
 * Since: 3.26
 **/
gchar *
e_vcard_util_dup_x_attribute (EVCard *vcard,
			      const gchar *x_name)
{
	EVCardAttribute *attr;
	GList *v = NULL;

	g_return_val_if_fail (E_IS_VCARD (vcard), NULL);
	g_return_val_if_fail (x_name != NULL, NULL);
	g_return_val_if_fail (g_str_has_prefix (x_name, "X-"), NULL);

	attr = e_vcard_get_attribute (vcard, x_name);

	if (attr)
		v = e_vcard_attribute_get_values (attr);

	return ((v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL);
}

/**
 * e_vcard_version_from_string:
 * @str: a version string, like "4.0"
 *
 * Converts the @str into one of the #EVCardVersion. If the string
 * does not match any of the known values the %E_VCARD_VERSION_UNKNOWN
 * is returned.
 *
 * Returns: an #EVCardVersion corresponding to @str, or %E_VCARD_VERSION_UNKNOWN
 *
 * Since: 3.60
 **/
EVCardVersion
e_vcard_version_from_string (const gchar *str)
{
	if (g_strcmp0 (str, "2.1") == 0)
		return E_VCARD_VERSION_21;
	else if (g_strcmp0 (str, "3.0") == 0)
		return E_VCARD_VERSION_30;
	else if (g_strcmp0 (str, "4.0") == 0)
		return E_VCARD_VERSION_40;

	return E_VCARD_VERSION_UNKNOWN;
}

/**
 * e_vcard_version_to_string:
 * @version: an #EVCardVersion
 *
 * Converts the @version into its string representation.
 * Returns %NULL, when the @version does not match any
 * known value (the %E_VCARD_VERSION_UNKNOWN corresponds
 * to the "unknown" string).
 *
 * Returns: a text representation of the @version,
 *    or %NULL for unknown values.
 *
 * Since: 3.60
 **/
const gchar *
e_vcard_version_to_string (EVCardVersion version)
{
	switch (version) {
	case E_VCARD_VERSION_UNKNOWN:
		return "unknown";
	case E_VCARD_VERSION_21:
		return "2.1";
	case E_VCARD_VERSION_30:
		return "3.0";
	case E_VCARD_VERSION_40:
		return "4.0";
	}
	return NULL;
}
