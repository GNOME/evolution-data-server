/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>

#include "camel-utf8.h"

/**
 * camel_utf8_putc:
 * @ptr:
 * @c:
 *
 * Output a 32 bit unicode character as utf8 octets.  At most 4 octets will
 * be written to @ptr.  @ptr will be advanced to the next character position.
 **/
void
camel_utf8_putc (guchar **ptr,
                 guint32 c)
{
	register guchar *p = *ptr;

	if (c <= 0x7f)
		*p++ = c;
	else if (c <= 0x7ff) {
		*p++ = 0xc0 | c >> 6;
		*p++ = 0x80 | (c & 0x3f);
	} else if (c <= 0xffff) {
		*p++ = 0xe0 | c >> 12;
		*p++ = 0x80 | ((c >> 6) & 0x3f);
		*p++ = 0x80 | (c & 0x3f);
	} else {
		/* see unicode standard 3.0, S 3.8, max 4 octets */
		*p++ = 0xf0 | c >> 18;
		*p++ = 0x80 | ((c >> 12) & 0x3f);
		*p++ = 0x80 | ((c >> 6) & 0x3f);
		*p++ = 0x80 | (c & 0x3f);
	}

	*ptr = p;
}

/**
 * camel_utf8_getc:
 * @ptr:
 *
 * Get a Unicode character from a utf8 stream.  @ptr will be advanced
 * to the next character position.  Invalid utf8 characters will be
 * silently skipped.  @ptr should point to a NUL terminated array.
 *
 * Returns: The next Unicode character.  @ptr will be advanced to
 * the next character always.
 **/
guint32
camel_utf8_getc (const guchar **ptr)
{
	register guchar *p = (guchar *) * ptr;
	register guchar c, r;
	register guint32 v, m;

again:
	r = *p++;
loop:
	if (r < 0x80) {
		*ptr = p;
		v = r;
	} else if (r < 0xf8) { /* valid start char? (max 4 octets) */
		v = r;
		m = 0x7f80;	/* used to mask out the length bits */
		do {
			c = *p++;
			if ((c & 0xc0) != 0x80) {
				r = c;
				goto loop;
			}
			v = (v << 6) | (c & 0x3f);
			r <<= 1;
			m <<= 5;
		} while (r & 0x40);

		*ptr = p;

		v &= ~m;
	} else {
		goto again;
	}

	return v;
}

/**
 * camel_utf8_getc_limit:
 * @ptr:
 * @end: must not be NULL.
 *
 * Get the next utf8 gchar at @ptr, and return it, advancing @ptr to
 * the next character.  If @end is reached before a full utf8
 * character can be read, then the invalid Unicode gchar 0xffff is
 * returned as a sentinel (Unicode 3.1, section 2.7), and @ptr is not
 * advanced.
 *
 * Returns: The next utf8 char, or 0xffff.
 **/
guint32
camel_utf8_getc_limit (const guchar **ptr,
                       const guchar *end)
{
	register guchar *p = (guchar *) * ptr;
	register guchar c, r;
	register guint32 v = 0xffff, m;

again:
	while (p < end) {
		r = *p++;
loop:
		if (r < 0x80) {
			*ptr = p;
			return r;
		} else if (r < 0xf8) { /* valid start char? (max 4 octets) */
			v = r;
			m = 0x7f80;	/* used to mask out the length bits */
			do {
				if (p >= end)
					return 0xffff;

				c = *p++;
				if ((c & 0xc0) != 0x80) {
					r = c;
					goto loop;
				}
				v = (v << 6) | (c & 0x3f);
				r <<= 1;
				m <<= 5;
			} while (r & 0x40);

			*ptr = p;

			v &= ~m;
			return v;
		} else {
			goto again;
		}
	}

	return 0xffff;
}

void
g_string_append_u (GString *out,
                   guint32 c)
{
	guchar buffer[8];
	guchar *p = buffer;

	camel_utf8_putc (&p, c);
	*p = 0;
	g_string_append (out, (const gchar *) buffer);
}

static const gchar utf7_alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

static const guchar utf7_rank[256] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x3e,0x3f,0xff,0xff,0xff,
	0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,
	0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0xff,0xff,0xff,0xff,0xff,
	0xff,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
	0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};

/**
 * camel_utf7_utf8:
 * @ptr:
 *
 * Convert a modified utf7 string to utf8.  If the utf7 string
 * contains 8 bit characters, they are treated as iso-8859-1.
 *
 * The IMAP rules [rfc2060] are used in the utf7 encoding.
 *
 * Returns: The converted string.
 **/
gchar *
camel_utf7_utf8 (const gchar *ptr)
{
	const guchar *p = (guchar *) ptr;
	guint c;
	guint32 v = 0, x;
	GString *out;
	gint i = 0;
	gint state = 0;
	gchar *ret;

	out = g_string_new ("");
	do {
		c = *p++;
		switch (state) {
		case 0:
			if (c == '&')
				state = 1;
			else
				g_string_append_c (out, c);
			break;
		case 1:
			if (c == '-') {
				g_string_append_c (out, '&');
				state = 0;
			} else if (utf7_rank[c] != 0xff) {
				v = utf7_rank[c];
				i = 6;
				state = 2;
			} else {
				/* invalid */
				g_string_append (out, "&-");
				state = 0;
			}
			break;
		case 2:
			if (c == '-') {
				state = 0;
			} else if (utf7_rank[c] != 0xff) {
				v = (v << 6) | utf7_rank[c];
				i+=6;
				if (i >= 16) {
					x = (v >> (i - 16)) & 0xffff;
					g_string_append_u (out, x);
					i-=16;
				}
			} else {
				g_string_append_u (out, c);
				state = 0;
			}
			break;
		}
	} while (c);

	ret = g_strdup (out->str);
	g_string_free (out, TRUE);

	return ret;
}

static void utf7_closeb64 (GString *out, guint32 v, guint32 i)
{
	guint32 x;

	if (i > 0) {
		x = (v << (6 - i)) & 0x3f;
		g_string_append_c (out, utf7_alphabet[x]);
	}
	g_string_append_c (out, '-');
}

/**
 * camel_utf8_utf7:
 * @ptr:
 *
 * Convert a utf8 string to a modified utf7 format.
 *
 * The IMAP rules [rfc2060] are used in the utf7 encoding.
 *
 * Returns:
 **/
gchar *
camel_utf8_utf7 (const gchar *ptr)
{
	const guchar *p = (guchar *) ptr;
	guint c;
	guint32 x, v = 0;
	gint state = 0;
	GString *out;
	gint i = 0;
	gchar *ret;

	out = g_string_new ("");

	while ((c = camel_utf8_getc (&p))) {
		if (c >= 0x20 && c <= 0x7e) {
			if (state == 1) {
				utf7_closeb64 (out, v, i);
				state = 0;
				i = 0;
			}
			if (c == '&')
				g_string_append (out, "&-");
			else
				g_string_append_c (out, c);
		} else {
			if (state == 0) {
				g_string_append_c (out, '&');
				state = 1;
			}
			v = (v << 16) | c;
			i += 16;
			while (i >= 6) {
				x = (v >> (i - 6)) & 0x3f;
				g_string_append_c (out, utf7_alphabet[x]);
				i -= 6;
			}
		}
	}

	if (state == 1)
		utf7_closeb64 (out, v, i);

	ret = g_strdup (out->str);
	g_string_free (out, TRUE);

	return ret;
}

/**
 * camel_utf8_ucs2:
 * @ptr:
 *
 * Convert a utf8 string into a ucs2 one.  The ucs string will be in
 * network byte order, and terminated with a 16 bit NULL.
 *
 * Returns:
 **/
gchar *
camel_utf8_ucs2 (const gchar *pptr)
{
	GByteArray *work = g_byte_array_new ();
	guint32 c;
	gchar *out;
	const guchar *ptr = (const guchar *) pptr;

	/* what if c is > 0xffff ? */

	while ((c = camel_utf8_getc (&ptr))) {
		guint16 s = g_htons (c);

		g_byte_array_append (work, (guchar *) &s, 2);
	}

	g_byte_array_append (work, (guchar *) "\000\000", 2);
	out = g_malloc (work->len);
	memcpy (out, work->data, work->len);
	g_byte_array_free (work, TRUE);

	return out;
}

/**
 * camel_ucs2_utf8:
 * @ptr:
 *
 * Convert a ucs2 string into a utf8 one.  The ucs2 string is treated
 * as network byte ordered, and terminated with a 16 bit NUL.
 *
 * Returns:
 **/
gchar *camel_ucs2_utf8 (const gchar *ptr)
{
	guint16 *ucs = (guint16 *) ptr;
	guint32 c;
	GString *work = g_string_new ("");
	gchar *out;

	while ((c = *ucs++))
		g_string_append_u (work, g_ntohs (c));

	out = g_strdup (work->str);
	g_string_free (work, TRUE);

	return out;
}

/**
 * camel_utf8_make_valid:
 * @text:
 *
 * Ensures the returned text will be valid UTF-8 string, with incorrect letters
 * changed to question marks. Returned pointer should be freed with g_free.
 *
 * Since: 2.26
 **/
gchar *
camel_utf8_make_valid (const gchar *text)
{
	gchar *res = g_strdup (text), *p;

	if (!res)
		return res;

	p = res;
	while (!g_utf8_validate (p, -1, (const gchar **) &p)) {
		/* make all invalid characters appear as question marks */
		*p = '?';
	}

	return res;
}
