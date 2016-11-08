/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "camel-trie.h"
#include "camel-url-scanner.h"
#include "camel-utf8.h"

struct _CamelUrlScanner {
	GPtrArray *patterns;
	CamelTrie *trie;
};

/**
 * camel_url_scanner_new: (skip)
 *
 * Returns: (transfer full): Creates a new #CamelUrlScanner
 **/
CamelUrlScanner *
camel_url_scanner_new (void)
{
	CamelUrlScanner *scanner;

	scanner = g_new (CamelUrlScanner, 1);
	scanner->patterns = g_ptr_array_new ();
	scanner->trie = camel_trie_new (TRUE);

	return scanner;
}

/**
 * camel_url_scanner_free: (skip)
 * @scanner: a #CamelUrlScanner
 *
 * Frees the @scanner.
 **/
void
camel_url_scanner_free (CamelUrlScanner *scanner)
{
	g_return_if_fail (scanner != NULL);

	g_ptr_array_free (scanner->patterns, TRUE);
	camel_trie_free (scanner->trie);
	g_free (scanner);
}

/**
 * camel_url_scanner_add: (skip)
 * @scanner: a #CamelUrlScanner
 * @pattern: a #CamelUrlPattern to add
 *
 * Adds a new @pattern into the scanner
 **/
void
camel_url_scanner_add (CamelUrlScanner *scanner,
                       CamelUrlPattern *pattern)
{
	g_return_if_fail (scanner != NULL);

	camel_trie_add (scanner->trie, pattern->pattern, scanner->patterns->len);
	g_ptr_array_add (scanner->patterns, pattern);
}

/**
 * camel_url_scanner_scan: (skip)
 * @scanner: a #CamelUrlScanner object.
 * @in: (array length=inlen) (type gchar): the url to scan.
 * @inlen: length of the in array.
 * @match: the #CamelUrlMatch structure containing the criterias.
 *
 * Scan the @in string with the @match criterias.
 *
 * Returns: %TRUE if there is a result.
 **/
gboolean
camel_url_scanner_scan (CamelUrlScanner *scanner,
                        const gchar *in,
                        gsize inlen,
                        CamelUrlMatch *match)
{
	const gchar *pos;
	const guchar *inptr, *inend;
	CamelUrlPattern *pat;
	gint pattern;

	g_return_val_if_fail (scanner != NULL, FALSE);
	g_return_val_if_fail (in != NULL, FALSE);

	inptr = (const guchar *) in;
	inend = inptr + inlen;

	/* check validity of a string first */
	if (!g_utf8_validate (in, inlen, NULL))
		return FALSE;

	do {
		if (!(pos = camel_trie_search (scanner->trie, (const gchar *) inptr, inlen, &pattern)))
			return FALSE;

		pat = g_ptr_array_index (scanner->patterns, pattern);

		match->pattern = pat->pattern;
		match->prefix = pat->prefix;

		if (pat->start (in, pos, (const gchar *) inend, match) && pat->end (in, pos, (const gchar *) inend, match))
			return TRUE;

		inptr = (const guchar *) pos;
		if (camel_utf8_getc_limit (&inptr, inend) == 0xffff)
			break;

		inlen = inend - inptr;
	} while (inptr < inend);

	return FALSE;
}

static guchar url_scanner_table[256] = {
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  9,  9,  1,  1,  9,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	 24,128,160,128,128,128,128,128,160,160,128,128,160,192,160,160,
	 68, 68, 68, 68, 68, 68, 68, 68, 68, 68,160,160, 32,128, 32,128,
	160, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,160,160,160,128,128,
	128, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,128,128,128,128,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1
};

enum {
	IS_CTRL = (1 << 0),
	IS_ALPHA = (1 << 1),
	IS_DIGIT = (1 << 2),
	IS_LWSP = (1 << 3),
	IS_SPACE = (1 << 4),
	IS_SPECIAL = (1 << 5),
	IS_DOMAIN = (1 << 6),
	IS_URLSAFE = (1 << 7)
};

#define is_ctrl(x) ((url_scanner_table[(guchar)(x)] & IS_CTRL) != 0)
#define is_lwsp(x) ((url_scanner_table[(guchar)(x)] & IS_LWSP) != 0)
#define is_atom(x) ((url_scanner_table[(guchar)(x)] & (IS_SPECIAL|IS_SPACE|IS_CTRL)) == 0)
#define is_alpha(x) ((url_scanner_table[(guchar)(x)] & IS_ALPHA) != 0)
#define is_digit(x) ((url_scanner_table[(guchar)(x)] & IS_DIGIT) != 0)
#define is_domain(x) ((url_scanner_table[(guchar)(x)] & IS_DOMAIN) != 0)
#define is_urlsafe(x) ((url_scanner_table[(guchar)(x)] & (IS_ALPHA|IS_DIGIT|IS_URLSAFE)) != 0)

static const struct {
	const gchar open;
	const gchar close;
} url_braces[] = {
	{ '(', ')' },
	{ '{', '}' },
	{ '[', ']' },
	{ '<', '>' },
	{ '|', '|' },
	{ '\'', '\'' },
};

static gboolean
is_open_brace (gchar c)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (url_braces); i++) {
		if (c == url_braces[i].open)
			return TRUE;
	}

	return FALSE;
}

static char
url_stop_at_brace (const gchar *in,
                   gsize so,
                   gchar *open_brace)
{
	gint i;

	if (open_brace != NULL)
		*open_brace = '\0';

	if (so > 0) {
		for (i = 0; i < G_N_ELEMENTS (url_braces); i++) {
			if (in[so - 1] == url_braces[i].open) {
				if (open_brace != NULL)
					*open_brace = url_braces[i].open;
				return url_braces[i].close;
			}
		}
	}

	return '\0';
}

gboolean
camel_url_addrspec_start (const gchar *in,
                          const gchar *pos,
                          const gchar *inend,
                          CamelUrlMatch *match)
{
	register const gchar *inptr = pos;

	g_return_val_if_fail (*inptr == '@', FALSE);

	if (inptr > in)
		inptr--;

	while (inptr > in) {
		if (is_atom (*inptr))
			inptr--;
		else
			break;

		while (inptr > in && is_atom (*inptr))
			inptr--;

		if (inptr > in && *inptr == '.')
			inptr--;
	}

	while (!is_atom (*inptr) || is_open_brace (*inptr))
		inptr++;

	if (inptr >= pos)
		return FALSE;

	match->um_so = (inptr - in);

	return TRUE;
}

gboolean
camel_url_addrspec_end (const gchar *in,
                        const gchar *pos,
                        const gchar *inend,
                        CamelUrlMatch *match)
{
	const gchar *inptr = pos;
	gint parts = 0, digits;
	gboolean got_dot = FALSE;

	g_return_val_if_fail (*inptr == '@', FALSE);

	inptr++;

	if (*inptr == '[') {
		/* domain literal */
		do {
			inptr++;

			digits = 0;
			while (inptr < inend && is_digit (*inptr) && digits < 3) {
				inptr++;
				digits++;
			}

			parts++;

			if (*inptr != '.' && parts != 4)
				return FALSE;
		} while (parts < 4);

		if (*inptr == ']')
			inptr++;
		else
			return FALSE;

		got_dot = TRUE;
	} else {
		while (inptr < inend) {
			if (is_domain (*inptr))
				inptr++;
			else
				break;

			while (inptr < inend && is_domain (*inptr))
				inptr++;

			if (inptr < inend && *inptr == '.' && is_domain (inptr[1])) {
				if (*inptr == '.')
					got_dot = TRUE;
				inptr++;
			}
		}
	}

	/* don't allow toplevel domains */
	if (inptr == pos + 1 || !got_dot)
		return FALSE;

	match->um_eo = (inptr - in);

	return TRUE;
}

gboolean
camel_url_file_start (const gchar *in,
                      const gchar *pos,
                      const gchar *inend,
                      CamelUrlMatch *match)
{
	match->um_so = (pos - in);

	return TRUE;
}

gboolean
camel_url_file_end (const gchar *in,
                    const gchar *pos,
                    const gchar *inend,
                    CamelUrlMatch *match)
{
	register const gchar *inptr = pos;
	gchar close_brace;

	inptr += strlen (match->pattern);

	if (*inptr == '/')
		inptr++;

	close_brace = url_stop_at_brace (in, match->um_so, NULL);

	while (inptr < inend && is_urlsafe (*inptr) && *inptr != close_brace)
		inptr++;

	if (inptr == pos)
		return FALSE;

	match->um_eo = (inptr - in);

	return TRUE;
}

gboolean
camel_url_web_start (const gchar *in,
                     const gchar *pos,
                     const gchar *inend,
                     CamelUrlMatch *match)
{
	if (pos > in && !strncmp (pos, "www", 3)) {
		/* make sure we aren't actually part of another word */
		if (!is_open_brace (pos[-1]) && !isspace (pos[-1]))
			return FALSE;
	}

	match->um_so = (pos - in);

	return TRUE;
}

gboolean
camel_url_web_end (const gchar *in,
                   const gchar *pos,
                   const gchar *inend,
                   CamelUrlMatch *match)
{
	register const gchar *inptr = pos;
	gboolean passwd = FALSE;
	const gchar *save;
	gchar close_brace, open_brace;
	gint brace_stack = 0;
	gint port;

	inptr += strlen (match->pattern);

	close_brace = url_stop_at_brace (in, match->um_so, &open_brace);

	/* find the end of the domain */
	if (is_atom (*inptr)) {
		/* might be a domain or user@domain */
		save = inptr;
		while (inptr < inend) {
			if (!is_atom (*inptr))
				break;

			inptr++;

			while (inptr < inend && is_atom (*inptr))
				inptr++;

			if ((inptr + 1) < inend && *inptr == '.' && (is_atom (inptr[1]) || inptr[1] == '/'))
					inptr++;
		}

		if (*inptr != '@')
			inptr = save;
		else
			inptr++;

		goto domain;
	} else if (is_domain (*inptr)) {
	domain:
		while (inptr < inend) {
			if (!is_domain (*inptr))
				break;

			inptr++;

			while (inptr < inend && is_domain (*inptr))
				inptr++;

			if ((inptr + 1) < inend && *inptr == '.' && (is_domain (inptr[1]) || inptr[1] == '/'))
					inptr++;
		}
	} else {
		return FALSE;
	}

	if (inptr < inend) {
		switch (*inptr) {
		case ':': /* we either have a port or a password */
			inptr++;

			if (is_digit (*inptr) || passwd) {
				port = (*inptr++ - '0');

				while (inptr < inend && is_digit (*inptr) && port < 65536)
					port = (port * 10) + (*inptr++ - '0');

				if (!passwd && (port >= 65536 || *inptr == '@')) {
					if (inptr < inend) {
						/* this must be a password? */
						goto passwd;
					}

					inptr--;
				}
			} else {
			passwd:
				passwd = TRUE;
				save = inptr;

				while (inptr < inend && is_atom (*inptr))
					inptr++;

				if ((inptr + 2) < inend) {
					if (*inptr == '@') {
						inptr++;
						if (is_domain (*inptr))
							goto domain;
					}

					return FALSE;
				}
			}

			if (inptr >= inend || *inptr != '/')
				break;

			/* we have a '/' so there could be a path - fall through */
		case '/': /* we've detected a path component to our url */
			inptr++;
			/* coverity[fallthrough] */
		case '?':
			while (inptr < inend && is_urlsafe (*inptr)) {
				if (*inptr == open_brace) {
					brace_stack++;
				} else if (*inptr == close_brace) {
					brace_stack--;
					if (brace_stack == -1)
						break;
				}
				inptr++;
			}

			break;
		default:
			break;
		}
	}

	/* urls are extremely unlikely to end with any
	 * punctuation, so strip any trailing
	 * punctuation off. Also strip off any closing
	 * double-quotes. */
	while (inptr > pos && strchr (",.:;?!-|}])\"", inptr[-1]))
		inptr--;

	match->um_eo = (inptr - in);

	return TRUE;
}

#ifdef BUILD_TABLE

/* got these from rfc1738 */
#define CHARS_LWSP " \t\n\r"               /* linear whitespace chars */
#define CHARS_SPECIAL "()<>@,;:\\\".[]"

/* got these from rfc1738 */
#define CHARS_URLSAFE "$-_.+!*'(),{}|\\^~[]`#%\";/?:@&="

static void
table_init_bits (guint mask,
                 const guchar *vals)
{
	gint i;

	for (i = 0; vals[i] != '\0'; i++)
		url_scanner_table[vals[i]] |= mask;
}

static void
url_scanner_table_init (void)
{
	gint i;

	for (i = 0; i < 256; i++) {
		url_scanner_table[i] = 0;
		if (i < 32)
			url_scanner_table[i] |= IS_CTRL;
		if ((i >= '0' && i <= '9'))
			url_scanner_table[i] |= IS_DIGIT | IS_DOMAIN;
		if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z'))
			url_scanner_table[i] |= IS_ALPHA | IS_DOMAIN;
		if (i >= 127)
			url_scanner_table[i] |= IS_CTRL;
	}

	url_scanner_table[' '] |= IS_SPACE;
	url_scanner_table['-'] |= IS_DOMAIN;

	/* not defined to be special in rfc0822, but when scanning
	 * backwards to find the beginning of the email address we do
	 * not want to include this gchar if we come accross it - so
	 * this is kind of a hack */
	url_scanner_table['/'] |= IS_SPECIAL;

	table_init_bits (IS_LWSP, CHARS_LWSP);
	table_init_bits (IS_SPECIAL, CHARS_SPECIAL);
	table_init_bits (IS_URLSAFE, CHARS_URLSAFE);
}

gint main (gint argc, gchar **argv)
{
	gint i;

	url_scanner_table_init ();

	printf ("static guchar url_scanner_table[256] = {");
	for (i = 0; i < 256; i++) {
		printf (
			"%s%3d%s", (i % 16) ? "" : "\n\t",
			url_scanner_table[i], i != 255 ? "," : "\n");
	}
	printf ("};\n\n");

	return 0;
}

#endif /* BUILD_TABLE */
