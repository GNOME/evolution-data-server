/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003 Novell Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glib/gfileutils.h>
#include <glib/gmem.h>
#include <glib/gmessages.h>
#include <glib/gstrfuncs.h>
#include <glib/gunicode.h>
#include <glib/gutils.h>
#include <glib/galloca.h>
#include <glib/gconvert.h>
#include "e-util.h"

/**
 * e_util_mkdir_hier:
 * @path: The directory hierarchy to create.
 * @mode: The permissions to use for the directories.
 *
 * Creates a directory hierarchy based on the string @path. If @path
 * is prefixed by a '/', the directories will be created relative to
 * the root of the file system; otherwise, the directories will be
 * created relative to the current directory.
 *
 * Returns: 0 on success; -1 on failure.
 **/
int
e_util_mkdir_hier (const char *path, mode_t mode)
{
        char *copy, *p;
                                                                                
        if (path[0] == '/') {
                p = copy = g_strdup (path);
        } else {
                gchar *current_dir = g_get_current_dir();
                p = copy = g_build_filename (current_dir, path, NULL);
		g_free (current_dir);
        }
                                                                                
        do {
                p = strchr (p + 1, '/');
                if (p)
                        *p = '\0';
                if (access (copy, F_OK) == -1) {
                        if (mkdir (copy, mode) == -1) {
                                g_free (copy);
                                return -1;
                        }
               }
                if (p)
                        *p = '/';
        } while (p);
                                                                                
        g_free (copy);
        return 0;
}

/**
 * e_util_strstrcase:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 *
 * Find the first instance of @needle in @haystack, ignoring case.
 *
 * Returns: A pointer to the start of @needle in @haystack, or NULL if
 *          @needle is not found.
 **/
gchar *
e_util_strstrcase (const gchar *haystack, const gchar *needle)
{
        /* find the needle in the haystack neglecting case */
        const gchar *ptr;
        guint len;
                                                                                
        g_return_val_if_fail (haystack != NULL, NULL);
        g_return_val_if_fail (needle != NULL, NULL);
                                                                                
        len = strlen(needle);
        if (len > strlen(haystack))
                return NULL;
                                                                                
        if (len == 0)
                return (gchar *) haystack;
                                                                                
        for (ptr = haystack; *(ptr + len - 1) != '\0'; ptr++)
                if (!g_ascii_strncasecmp (ptr, needle, len))
                        return (gchar *) ptr;
                                                                                
        return NULL;
}

/** 
 * e_util_unicode_get_utf8:
 * @text: The string to take the UTF-8 character from.
 * @out: The location to store the UTF-8 character in.
 * 
 * Get a UTF-8 character from the beginning of @text.
 *
 * Returns: A pointer to the next character in @text after @out. 
 **/
gchar *
e_util_unicode_get_utf8 (const gchar *text, gunichar *out)
{
        *out = g_utf8_get_char (text);
        return (*out == (gunichar)-1) ? NULL : g_utf8_next_char (text);
}

/** 
 * e_util_utf8_strstrcase:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 * 
 * Find the first instance of @needle in @haystack, ignoring
 * case. Both @needle and @haystack are UTF-8 strings.
 *
 * Returns: A pointer to the first instance of @needle in @haystack, or
 *          %NULL if either of the strings are not legal UTF-8 strings.
 **/
const gchar *
e_util_utf8_strstrcase (const gchar *haystack, const gchar *needle)
{
        gunichar *nuni;
        gunichar unival;
        gint nlen;
        const guchar *o, *p;
                                                                                
        if (haystack == NULL) return NULL;
        if (needle == NULL) return NULL;
        if (strlen (needle) == 0) return haystack;
        if (strlen (haystack) == 0) return NULL;
                                                                                
        nuni = g_alloca (sizeof (gunichar) * strlen (needle));
                                                                                
        nlen = 0;
        for (p = e_util_unicode_get_utf8 (needle, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                nuni[nlen++] = g_unichar_tolower (unival);
        }
        /* NULL means there was illegal utf-8 sequence */
        if (!p) return NULL;

	o = haystack;
        for (p = e_util_unicode_get_utf8 (o, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gint sc;
                sc = g_unichar_tolower (unival);
                /* We have valid stripped char */
                if (sc == nuni[0]) {
                        const gchar *q = p;
                        gint npos = 1;
                        while (npos < nlen) {
                                q = e_util_unicode_get_utf8 (q, &unival);
                                if (!q || !unival) return NULL;
                                sc = g_unichar_tolower (unival);
                                if (sc != nuni[npos]) break;
                                npos++;
                        }
                        if (npos == nlen) {
                                return o;
                        }
                }
                o = p;
        }
                                                                                
        return NULL;
}


static gunichar
stripped_char (gunichar ch)
{
        gunichar *decomp, retval;
        GUnicodeType utype;
        gint dlen;
                                                                                
        utype = g_unichar_type (ch);
                                                                                
        switch (utype) {
        case G_UNICODE_CONTROL:
        case G_UNICODE_FORMAT:
        case G_UNICODE_UNASSIGNED:
        case G_UNICODE_COMBINING_MARK:
                /* Ignore those */
                return 0;
               break;
        default:
                /* Convert to lowercase, fall through */
                ch = g_unichar_tolower (ch);
        case G_UNICODE_LOWERCASE_LETTER:
                if ((decomp = g_unicode_canonical_decomposition (ch, &dlen))) {
                        retval = decomp[0];
                        g_free (decomp);
                        return retval;
                }
                break;
        }
                                                                                
        return 0;
}

/** 
 * e_util_utf8_strstrcasedecomp:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 * 
 * Find the first instance of @needle in @haystack, where both @needle
 * and @haystack are UTF-8 strings. Both strings are stripped and
 * decomposed for comparison, and case is ignored.
 *
 * Returns: A pointer to the first instance of @needle in @haystack, or
 *          %NULL if either of the strings are not legal UTF-8 strings.
 **/
const gchar *
e_util_utf8_strstrcasedecomp (const gchar *haystack, const gchar *needle)
{
        gunichar *nuni;
        gunichar unival;
        gint nlen;
        const guchar *o, *p;
                                                                                
        if (haystack == NULL) return NULL;
        if (needle == NULL) return NULL;
        if (strlen (needle) == 0) return haystack;
        if (strlen (haystack) == 0) return NULL;
                                                                                
        nuni = g_alloca (sizeof (gunichar) * strlen (needle));
                                                                                
        nlen = 0;
        for (p = e_util_unicode_get_utf8 (needle, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gint sc;
                sc = stripped_char (unival);
                if (sc) {
                       nuni[nlen++] = sc;
                }
        }
        /* NULL means there was illegal utf-8 sequence */
        if (!p) return NULL;
        /* If everything is correct, we have decomposed, lowercase, stripped needle */
        if (nlen < 1) return haystack;
                                                                                
        o = haystack;
        for (p = e_util_unicode_get_utf8 (o, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gint sc;
                sc = stripped_char (unival);
                if (sc) {
                        /* We have valid stripped char */
                        if (sc == nuni[0]) {
                                const gchar *q = p;
                                gint npos = 1;
                                while (npos < nlen) {
                                        q = e_util_unicode_get_utf8 (q, &unival);
                                        if (!q || !unival) return NULL;
                                        sc = stripped_char (unival);
                                        if ((!sc) || (sc != nuni[npos])) break;
                                        npos++;
                                }
                                if (npos == nlen) {
                                        return o;
                                }
                        }
                }
                o = p;
        }
                                                                                
        return NULL;
}

/** 
 * e_strftime:
 * @s: The string array to store the result in.
 * @max: The size of array @s.
 * @fmt: The formatting to use on @tm.
 * @tm: The time value to format.
 *
 * This function is a wrapper around the strftime(3) function, which
 * converts the &percnt;l and &percnt;k (12h and 24h) format variables if necessary.
 *
 * Returns: The number of characters placed in @s.
 **/
size_t e_strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
#ifdef HAVE_LKSTRFTIME
	return strftime(s, max, fmt, tm);
#else
	char *c, *ffmt, *ff;
	size_t ret;

	ffmt = g_strdup(fmt);
	ff = ffmt;
	while ((c = strstr(ff, "%l")) != NULL) {
		c[1] = 'I';
		ff = c;
	}

	ff = fmt;
	while ((c = strstr(ff, "%k")) != NULL) {
		c[1] = 'H';
		ff = c;
	}

	ret = strftime(s, max, ffmt, tm);
	g_free(ffmt);
	return ret;
#endif
}

/** 
 * e_utf8_strftime:
 * @s: The string array to store the result in.
 * @max: The size of array @s.
 * @fmt: The formatting to use on @tm.
 * @tm: The time value to format.
 *
 * The UTF-8 equivalent of e_strftime().
 *
 * Returns: The number of characters placed in @s.
 **/
size_t 
e_utf8_strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
	size_t sz, ret;
	char *locale_fmt, *buf;

	locale_fmt = g_locale_from_utf8(fmt, -1, NULL, &sz, NULL);
	if (!locale_fmt)
		return 0;

	ret = e_strftime(s, max, locale_fmt, tm);
	if (!ret) {
		g_free (locale_fmt);
		return 0;
	}

	buf = g_locale_to_utf8(s, ret, NULL, &sz, NULL);
	if (!buf) {
		g_free (locale_fmt);
		return 0;
	}

	if (sz >= max) {
		char *tmp = buf + max - 1;
		tmp = g_utf8_find_prev_char(buf, tmp);
		if (tmp)
			sz = tmp - buf;
		else
			sz = 0;
	}
	memcpy(s, buf, sz);
	s[sz] = '\0';
	g_free(locale_fmt);
	g_free(buf);
	return sz;
}
