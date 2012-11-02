/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#include "config.h"

#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef G_OS_WIN32
#include <mbstring.h>
#endif

#include <glib-object.h>

#include "e-data-server-util.h"

/**
 * e_get_user_cache_dir:
 *
 * Returns a base directory in which to store user-specific,
 * non-essential cached data for Evolution or Evolution-Data-Server.
 *
 * The returned string is owned by libedataserver and must not be
 * modified or freed.
 *
 * Returns: base directory for user-specific, non-essential data
 *
 * Since: 2.32
 **/
const gchar *
e_get_user_cache_dir (void)
{
	static gchar *dirname = NULL;

	if (G_UNLIKELY (dirname == NULL)) {
		const gchar *cache_dir = g_get_user_cache_dir ();
		dirname = g_build_filename (cache_dir, "evolution", NULL);
		g_mkdir_with_parents (dirname, 0700);
	}

	return dirname;
}

/**
 * e_get_user_config_dir:
 *
 * Returns a base directory in which to store user-specific configuration
 * information for Evolution or Evolution-Data-Server.
 *
 * The returned string is owned by libedataserver and must not be
 * modified or freed.
 *
 * Returns: base directory for user-specific configuration information
 *
 * Since: 2.32
 **/
const gchar *
e_get_user_config_dir (void)
{
	static gchar *dirname = NULL;

	if (G_UNLIKELY (dirname == NULL)) {
		const gchar *config_dir = g_get_user_config_dir ();
		dirname = g_build_filename (config_dir, "evolution", NULL);
		g_mkdir_with_parents (dirname, 0700);
	}

	return dirname;
}

/**
 * e_get_user_data_dir:
 *
 * Returns a base directory in which to store user-specific data for
 * Evolution or Evolution-Data-Server.
 *
 * The returned string is owned by libedataserver and must not be
 * modified or freed.
 *
 * Returns: base directory for user-specific data
 *
 * Since: 2.32
 **/
const gchar *
e_get_user_data_dir (void)
{
	static gchar *dirname = NULL;

	if (G_UNLIKELY (dirname == NULL)) {
		const gchar *data_dir = g_get_user_data_dir ();
		dirname = g_build_filename (data_dir, "evolution", NULL);
		g_mkdir_with_parents (dirname, 0700);
	}

	return dirname;
}

/**
 * e_util_strdup_strip:
 * @string: (allow-none): a string value, or %NULL
 *
 * Duplicates @string and strips off any leading or trailing whitespace.
 * The resulting string is returned unless it is empty or %NULL, in which
 * case the function returns %NULL.
 *
 * Free the returned string with g_free().
 *
 * Returns: a newly-allocated, stripped copy of @string, or %NULL
 *
 * Since: 3.6
 **/
gchar *
e_util_strdup_strip (const gchar *string)
{
	gchar *duplicate;

	duplicate = g_strdup (string);
	if (duplicate != NULL) {
		g_strstrip (duplicate);
		if (*duplicate == '\0') {
			g_free (duplicate);
			duplicate = NULL;
		}
	}

	return duplicate;
}

/**
 * e_util_strstrcase:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 *
 * Find the first instance of @needle in @haystack, ignoring case for
 * bytes that are ASCII characters.
 *
 * Returns: A pointer to the start of @needle in @haystack, or NULL if
 *          @needle is not found.
 **/
gchar *
e_util_strstrcase (const gchar *haystack,
                   const gchar *needle)
{
	/* find the needle in the haystack neglecting case */
	const gchar *ptr;
	guint len;

	g_return_val_if_fail (haystack != NULL, NULL);
	g_return_val_if_fail (needle != NULL, NULL);

	len = strlen (needle);
	if (len > strlen (haystack))
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
e_util_unicode_get_utf8 (const gchar *text,
                         gunichar *out)
{
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	*out = g_utf8_get_char (text);
	return (*out == (gunichar) -1) ? NULL : g_utf8_next_char (text);
}

/**
 * e_util_utf8_strstrcase:
 * @haystack: The string to search in.
 * @needle: The string to search for.
 *
 * Find the first instance of @needle in @haystack, ignoring case. (No
 * proper case folding or decomposing is done.) Both @needle and
 * @haystack are UTF-8 strings.
 *
 * Returns: A pointer to the first instance of @needle in @haystack, or
 *          %NULL if no match is found, or if either of the strings are
 *          not legal UTF-8 strings.
 **/
const gchar *
e_util_utf8_strstrcase (const gchar *haystack,
                        const gchar *needle)
{
	gunichar *nuni, unival;
	gint nlen;
	const gchar *o, *p;

	if (haystack == NULL)
		return NULL;

	if (needle == NULL)
		return NULL;

	if (strlen (needle) == 0)
		return haystack;

	if (strlen (haystack) == 0)
		return NULL;

	nuni = g_alloca (sizeof (gunichar) * strlen (needle));

	nlen = 0;
	for (p = e_util_unicode_get_utf8 (needle, &unival);
	     p && unival;
	     p = e_util_unicode_get_utf8 (p, &unival)) {
		nuni[nlen++] = g_unichar_tolower (unival);
	}
	/* NULL means there was illegal utf-8 sequence */
	if (!p) return NULL;

	o = haystack;
	for (p = e_util_unicode_get_utf8 (o, &unival);
	     p && unival;
	     p = e_util_unicode_get_utf8 (p, &unival)) {
		gunichar sc;
		sc = g_unichar_tolower (unival);
		/* We have valid stripped gchar */
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
	gunichar decomp[4];
	gunichar retval;
	GUnicodeType utype;
	gsize dlen;

	utype = g_unichar_type (ch);

	switch (utype) {
	case G_UNICODE_CONTROL:
	case G_UNICODE_FORMAT:
	case G_UNICODE_UNASSIGNED:
	case G_UNICODE_COMBINING_MARK:
		/* Ignore those */
		return 0;
	default:
		/* Convert to lowercase, fall through */
		ch = g_unichar_tolower (ch);
	case G_UNICODE_LOWERCASE_LETTER:
		if ((dlen = g_unichar_fully_decompose (ch, FALSE, decomp, 4))) {
			retval = decomp[0];
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
e_util_utf8_strstrcasedecomp (const gchar *haystack,
                              const gchar *needle)
{
	gunichar *nuni;
	gunichar unival;
	gint nlen;
	const gchar *o, *p;

	if (haystack == NULL)
		return NULL;

	if (needle == NULL)
		return NULL;

	if (strlen (needle) == 0)
		return haystack;

	if (strlen (haystack) == 0)
		return NULL;

	nuni = g_alloca (sizeof (gunichar) * strlen (needle));

	nlen = 0;
	for (p = e_util_unicode_get_utf8 (needle, &unival);
	     p && unival;
	     p = e_util_unicode_get_utf8 (p, &unival)) {
		gunichar sc;
		sc = stripped_char (unival);
		if (sc) {
		       nuni[nlen++] = sc;
		}
	}
	/* NULL means there was illegal utf-8 sequence */
	if (!p) return NULL;
	/* If everything is correct, we have decomposed,
	 * lowercase, stripped needle */
	if (nlen < 1)
		return haystack;

	o = haystack;
	for (p = e_util_unicode_get_utf8 (o, &unival);
	     p && unival;
	     p = e_util_unicode_get_utf8 (p, &unival)) {
		gunichar sc;
		sc = stripped_char (unival);
		if (sc) {
			/* We have valid stripped gchar */
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
 * e_util_utf8_strcasecmp:
 * @s1: a UTF-8 string
 * @s2: another UTF-8 string
 *
 * Compares two UTF-8 strings using approximate case-insensitive ordering.
 *
 * Returns: < 0 if @s1 compares before @s2, 0 if they compare equal,
 *          > 0 if @s1 compares after @s2
 **/
gint
e_util_utf8_strcasecmp (const gchar *s1,
                        const gchar *s2)
{
	gchar *folded_s1, *folded_s2;
	gint retval;

	g_return_val_if_fail (s1 != NULL && s2 != NULL, -1);

	if (strcmp (s1, s2) == 0)
		return 0;

	folded_s1 = g_utf8_casefold (s1, -1);
	folded_s2 = g_utf8_casefold (s2, -1);

	retval = g_utf8_collate (folded_s1, folded_s2);

	g_free (folded_s2);
	g_free (folded_s1);

	return retval;
}

/**
 * e_util_utf8_remove_accents:
 * @str: a UTF-8 string, or %NULL
 *
 * Returns a newly-allocated copy of @str with accents removed.
 *
 * Returns: a newly-allocated string
 *
 * Since: 2.28
 **/
gchar *
e_util_utf8_remove_accents (const gchar *str)
{
	gchar *res;
	gint i, j;

	if (str == NULL)
		return NULL;

	res = g_utf8_normalize (str, -1, G_NORMALIZE_NFD);
	if (!res)
		return g_strdup (str);

	for (i = 0, j = 0; res[i]; i++) {
		if ((guchar) res[i] != 0xCC || res[i + 1] == 0) {
			res[j] = res[i];
			j++;
		} else {
			i++;
		}
	}

	res[j] = 0;

	return res;
}

/**
 * e_util_utf8_make_valid:
 * @str: a UTF-8 string
 *
 * Returns a newly-allocated copy of @str, with invalid characters
 * replaced by Unicode replacement characters (U+FFFD).
 *
 * Returns: a newly-allocated string
 *
 * Since: 3.0
 **/
gchar *
e_util_utf8_make_valid (const gchar *str)
{
	return e_util_utf8_data_make_valid (str, strlen (str));
}

/**
 * e_util_utf8_data_make_valid:
 * @data: UTF-8 binary data
 * @data_bytes: length of the binary data
 *
 * Returns a newly-allocated NULL-terminated string with invalid characters
 * replaced by Unicode replacement characters (U+FFFD).
 *
 * Returns: a newly-allocated string
 *
 * Since: 3.6
 */
gchar *
e_util_utf8_data_make_valid (const gchar *data,
                             gsize data_bytes)
{
	/* almost identical copy of glib's _g_utf8_make_valid() */
	GString *string;
	const gchar *remainder, *invalid;
	gint remaining_bytes, valid_bytes;

	g_return_val_if_fail (data != NULL, NULL);

	string = NULL;
	remainder = (gchar *) data,
	remaining_bytes = data_bytes;

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
		return g_strndup ((gchar *) data, data_bytes);

	g_string_append (string, remainder);

	g_warn_if_fail (g_utf8_validate (string->str, -1, NULL));

	return g_string_free (string, FALSE);
}

/**
 * e_util_ensure_gdbus_string:
 * @str: a possibly invalid UTF-8 string, or %NULL
 * @gdbus_str: return location for the corrected string
 *
 * If @str is a valid UTF-8 string, the function returns @str and does
 * not set @gdbus_str.
 *
 * If @str is an invalid UTF-8 string, the function calls
 * e_util_utf8_make_valid() and points @gdbus_str to the newly-allocated,
 * valid UTF-8 string, and also returns it.  The caller should free the
 * string pointed to by @gdbus_str with g_free().
 *
 * If @str is %NULL, the function returns an empty string and does not
 * set @gdbus_str.
 *
 * Admittedly, the function semantics are a little awkward.  The example
 * below illustrates the easiest way to cope with the @gdbus_str argument:
 *
 * <informalexample>
 *   <programlisting>
 *     const gchar *trusted_utf8;
 *     gchar *allocated = NULL;
 *
 *     trusted_utf8 = e_util_ensure_gdbus_string (untrusted_utf8, &allocated);
 *
 *     Do stuff with trusted_utf8, then clear it.
 *
 *     trusted_utf8 = NULL;
 *
 *     g_free (allocated);
 *     allocated = NULL;
 *   </programlisting>
 * </informalexample>
 *
 * Returns: a valid UTF-8 string
 *
 * Since: 3.0
 **/
const gchar *
e_util_ensure_gdbus_string (const gchar *str,
                            gchar **gdbus_str)
{
	g_return_val_if_fail (gdbus_str != NULL, NULL);

	*gdbus_str = NULL;

	if (!str || !*str)
		return "";

	if (g_utf8_validate (str, -1, NULL))
		return str;

	*gdbus_str = e_util_utf8_make_valid (str);

	return *gdbus_str;
}

/**
 * e_strftime:
 * @string: The string array to store the result in.
 * @max: The size of array @s.
 * @fmt: The formatting to use on @tm.
 * @tm: The time value to format.
 *
 * This function is a wrapper around the strftime (3) function, which
 * converts the &percnt;l and &percnt;k (12h and 24h) format variables
 * if necessary.
 *
 * Returns: The number of characters placed in @s.
 **/
gsize
e_strftime (gchar *string,
            gsize max,
            const gchar *fmt,
            const struct tm *tm)
{
#ifndef HAVE_LKSTRFTIME
	gchar *c, *ffmt, *ff;
#endif
	gsize ret;

	g_return_val_if_fail (string != NULL, 0);
	g_return_val_if_fail (fmt != NULL, 0);
	g_return_val_if_fail (tm != NULL, 0);

#ifdef HAVE_LKSTRFTIME
	ret = strftime (string, max, fmt, tm);
#else
	ffmt = g_strdup (fmt);
	ff = ffmt;
	while ((c = strstr (ff, "%l")) != NULL) {
		c[1] = 'I';
		ff = c;
	}

	ff = ffmt;
	while ((c = strstr (ff, "%k")) != NULL) {
		c[1] = 'H';
		ff = c;
	}

#ifdef G_OS_WIN32
	/* The Microsoft strftime () doesn't have %e either */
	ff = ffmt;
	while ((c = strstr (ff, "%e")) != NULL) {
		c[1] = 'd';
		ff = c;
	}
#endif

	ret = strftime (string, max, ffmt, tm);
	g_free (ffmt);
#endif

	if (ret == 0 && max > 0)
		string[0] = '\0';

	return ret;
}

/**
 * e_utf8_strftime:
 * @string: The string array to store the result in.
 * @max: The size of array @s.
 * @fmt: The formatting to use on @tm.
 * @tm: The time value to format.
 *
 * The UTF-8 equivalent of e_strftime ().
 *
 * Returns: The number of characters placed in @s.
 **/
gsize
e_utf8_strftime (gchar *string,
                 gsize max,
                 const gchar *fmt,
                 const struct tm *tm)
{
	gsize sz, ret;
	gchar *locale_fmt, *buf;

	g_return_val_if_fail (string != NULL, 0);
	g_return_val_if_fail (fmt != NULL, 0);
	g_return_val_if_fail (tm != NULL, 0);

	locale_fmt = g_locale_from_utf8 (fmt, -1, NULL, &sz, NULL);
	if (!locale_fmt)
		return 0;

	ret = e_strftime (string, max, locale_fmt, tm);
	if (!ret) {
		g_free (locale_fmt);
		return 0;
	}

	buf = g_locale_to_utf8 (string, ret, NULL, &sz, NULL);
	if (!buf) {
		g_free (locale_fmt);
		return 0;
	}

	if (sz >= max) {
		gchar *tmp = buf + max - 1;
		tmp = g_utf8_find_prev_char (buf, tmp);
		if (tmp)
			sz = tmp - buf;
		else
			sz = 0;
	}

	memcpy (string, buf, sz);
	string[sz] = '\0';

	g_free (locale_fmt);
	g_free (buf);

	return sz;
}

/**
 * e_util_gthread_id:
 * @thread: A #GThread pointer
 *
 * Returns a 64-bit integer hopefully uniquely identifying the
 * thread. To be used in debugging output and logging only.
 * The returned value is just a cast of a pointer to the 64-bit integer.
 *
 * There is no guarantee that calling e_util_gthread_id () on one
 * thread first and later after that thread has dies on another won't
 * return the same integer.
 *
 * On Linux and Win32, known to really return a unique id for each
 * thread existing at a certain time. No guarantee that ids won't be
 * reused after a thread has terminated, though.
 *
 * Returns: A 64-bit integer.
 *
 * Since: 2.32
 */
guint64
e_util_gthread_id (GThread *thread)
{
#if GLIB_SIZEOF_VOID_P == 8
	/* 64-bit Windows */
	return (guint64) thread;
#else
	return (gint) thread;
#endif
}

/* This only makes a filename safe for usage as a filename.
 * It still may have shell meta-characters in it. */

/* This code is rather misguided and mostly pointless, but can't be
 * changed because of backward compatibility, I guess.
 *
 * It replaces some perfectly safe characters like '%' with an
 * underscore. (Recall that on Unix, the only bytes not allowed in a
 * file name component are '\0' and '/'.) On the other hand, the UTF-8
 * for a printable non-ASCII Unicode character (that thus consists of
 * several very nonprintable non-ASCII bytes) is let through as
 * such. But those bytes are of course also allowed in filenames, so
 * it doesn't matter as such...
 */
void
e_filename_make_safe (gchar *string)
{
	gchar *p, *ts;
	gunichar c;
#ifdef G_OS_WIN32
	const gchar *unsafe_chars = " /'\"`&();|<>$%{}!\\:*?#";
#else
	const gchar *unsafe_chars = " /'\"`&();|<>$%{}!#";
#endif

	g_return_if_fail (string != NULL);

	p = string;

	while (p && *p) {
		c = g_utf8_get_char (p);
		ts = p;
		p = g_utf8_next_char (p);
		/* I wonder what this code is supposed to actually
		 * achieve, and whether it does that as currently
		 * written?
		 */
		if (!g_unichar_isprint (c) ||
			(c < 0xff && strchr (unsafe_chars, c & 0xff))) {
			while (ts < p)
				*ts++ = '_';
		}
	}
}

/**
 * e_filename_mkdir_encoded:
 * @basepath: base path of a file name; this is left unchanged
 * @fileprefix: prefix for the filename; this is encoded
 * @filename: file name to use; this is encoded; can be %NULL
 * @fileindex: used when @filename is NULL, then the filename
 *        is generated as "file" + fileindex
 *
 * Creates a local path constructed from @basepath / @fileprefix + "-" + @filename,
 * and makes sure the path @basepath exists. If creation of
 * the path fails, then NULL is returned.
 *
 * Returns: Full local path like g_build_filename() except that @fileprefix
 * and @filename are encoded to create a proper file elements for
 * a file system. Free returned pointer with g_free().
 *
 * Since: 3.4
 **/
gchar *
e_filename_mkdir_encoded (const gchar *basepath,
                          const gchar *fileprefix,
                          const gchar *filename,
                          gint fileindex)
{
	gchar *elem1, *elem2, *res, *fn;

	g_return_val_if_fail (basepath != NULL, NULL);
	g_return_val_if_fail (*basepath != 0, NULL);
	g_return_val_if_fail (fileprefix != NULL, NULL);
	g_return_val_if_fail (*fileprefix != 0, NULL);
	g_return_val_if_fail (!filename || *filename, NULL);

	if (g_mkdir_with_parents (basepath, 0700) < 0)
		return NULL;

	elem1 = g_strdup (fileprefix);
	if (filename)
		elem2 = g_strdup (filename);
	else
		elem2 = g_strdup_printf ("file%d", fileindex);

	e_filename_make_safe (elem1);
	e_filename_make_safe (elem2);

	fn = g_strconcat (elem1, "-", elem2, NULL);

	res = g_build_filename (basepath, fn, NULL);

	g_free (fn);
	g_free (elem1);
	g_free (elem2);

	return res;
}

/**
 * e_util_slist_to_strv:
 * @strings: (element-type utf8): a #GSList of strings (const gchar *)
 *
 * Convert list of strings into NULL-terminates array of strings.
 *
 * Returns: (transfer full): Newly allocated %NULL-terminated array of strings.
 * Returned pointer should be freed with g_strfreev().
 *
 * Note: Pair function for this is e_util_strv_to_slist().
 *
 * Since: 3.4
 **/
gchar **
e_util_slist_to_strv (const GSList *strings)
{
	const GSList *iter;
	GPtrArray *array;

	array = g_ptr_array_sized_new (g_slist_length ((GSList *) strings) + 1);

	for (iter = strings; iter; iter = iter->next) {
		const gchar *str = iter->data;

		if (str)
			g_ptr_array_add (array, g_strdup (str));
	}

	/* NULL-terminated */
	g_ptr_array_add (array, NULL);

	return (gchar **) g_ptr_array_free (array, FALSE);
}

/**
 * e_util_strv_to_slist:
 * @strv: a NULL-terminated array of strings (const gchar *)
 *
 * Convert NULL-terminated array of strings to a list of strings.
 *
 * Returns: (transfer full) (element-type utf8): Newly allocated #GSList of
 * newly allocated strings. The returned pointer should be freed with
 * e_util_free_string_slist().
 *
 * Note: Pair function for this is e_util_slist_to_strv().
 *
 * Since: 3.4
 **/
GSList *
e_util_strv_to_slist (const gchar * const *strv)
{
	GSList *slist = NULL;
	gint ii;

	if (!strv)
		return NULL;

	for (ii = 0; strv[ii]; ii++) {
		slist = g_slist_prepend (slist, g_strdup (strv[ii]));
	}

	return g_slist_reverse (slist);
}

/**
 * e_util_copy_string_slist:
 * @copy_to: (element-type utf8) (allow-none): Where to copy; can be %NULL
 * @strings: (element-type utf8): #GSList of strings to be copied
 *
 * Copies #GSList of strings at the end of @copy_to.
 *
 * Returns: (transfer full) (element-type utf8): New head of @copy_to.
 * Returned pointer can be freed with e_util_free_string_slist().
 *
 * Since: 3.4
 **/
GSList *
e_util_copy_string_slist (GSList *copy_to,
                          const GSList *strings)
{
	if (strings != NULL) {
		const GSList *iter;
		GSList *strings_copy = NULL;
		/* Make deep copy of strings */
		for (iter = strings; iter; iter = iter->next)
			strings_copy = g_slist_prepend (strings_copy, g_strdup (iter->data));

		/* Concatenate the two lists */
		return g_slist_concat (copy_to, g_slist_reverse (strings_copy));
	}

	return copy_to;
}

/**
 * e_util_copy_object_slist:
 * @copy_to: (element-type GObject) (allow-none): Where to copy; can be %NULL
 * @objects: (element-type GObject): #GSList of #GObject<!-- -->s to be copied
 *
 * Copies #GSList of #GObject<!-- -->s at the end of @copy_to.
 *
 * Returns: (transfer full) (element-type GObject): New head of @copy_to.
 * Returned pointer can be freed with e_util_free_object_slist().
 *
 * Since: 3.4
 **/
GSList *
e_util_copy_object_slist (GSList *copy_to,
                          const GSList *objects)
{
	if (objects != NULL) {
		const GSList *iter;
		GSList *objects_copy = NULL;
		/* Make deep copy of objects */
		for (iter = objects; iter; iter = iter->next)
			objects_copy = g_slist_prepend (
				objects_copy, g_object_ref (iter->data));

		/* Concatenate the two lists */
		return g_slist_concat (
			copy_to, g_slist_reverse (objects_copy));
	}

	return copy_to;
}

/**
 * e_util_free_string_slist:
 * @strings: (element-type utf8): a #GSList of strings (gchar *)
 *
 * Frees memory previously allocated by e_util_strv_to_slist().
 *
 * Since: 3.4
 **/
void
e_util_free_string_slist (GSList *strings)
{
	g_slist_free_full (strings, (GDestroyNotify) g_free);
}

/**
 * e_util_free_object_slist:
 * @objects: (element-type GObject): a #GSList of #GObject<!-- -->s
 *
 * Calls g_object_unref() on each member of @objects and then frees
 * also @objects itself.
 *
 * Since: 3.4
 **/
void
e_util_free_object_slist (GSList *objects)
{
	g_slist_free_full (objects, (GDestroyNotify) g_object_unref);
}

/**
 * e_util_free_nullable_object_slist:
 * @objects: (element-type GObject): a #GSList of nullable #GObject<!-- -->s
 *
 * Calls g_object_unref() on each member of @objects if non-%NULL and then frees
 * also @objects itself.
 *
 * Since: 3.6
 **/
void
e_util_free_nullable_object_slist (GSList *objects)
{
	const GSList *l;
	for (l = objects; l; l = l->next) {
		if (l->data)
			g_object_unref (l->data);
	}
	g_slist_free (objects);
}

/* Helper for e_file_recursive_delete() */
static void
file_recursive_delete_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	GError *error = NULL;

	e_file_recursive_delete_sync (G_FILE (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

/**
 * e_file_recursive_delete_sync:
 * @file: a #GFile to delete
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes @file.  If @file is a directory, its contents are deleted
 * recursively before @file itself is deleted.  The recursive delete
 * operation will stop on the first error.
 *
 * If @cancellable is not %NULL, then the operation can be cancelled
 * by triggering the cancellable object from another thread.  If the
 * operation was cancelled, the error #G_IO_ERROR_CANCELLED will be
 * returned.
 *
 * Returns: %TRUE if the file was deleted, %FALSE otherwise
 *
 * Since: 3.6
 **/
gboolean
e_file_recursive_delete_sync (GFile *file,
                              GCancellable *cancellable,
                              GError **error)
{
	GFileEnumerator *file_enumerator;
	GFileInfo *file_info;
	GFileType file_type;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	file_type = g_file_query_file_type (
		file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable);

	/* If this is not a directory, delete like normal. */
	if (file_type != G_FILE_TYPE_DIRECTORY)
		return g_file_delete (file, cancellable, error);

	/* Note, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS is critical here
	 * so we only delete files inside the directory being deleted. */
	file_enumerator = g_file_enumerate_children (
		file, G_FILE_ATTRIBUTE_STANDARD_NAME,
		G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
		cancellable, error);

	if (file_enumerator == NULL)
		return FALSE;

	file_info = g_file_enumerator_next_file (
		file_enumerator, cancellable, &local_error);

	while (file_info != NULL) {
		GFile *child;
		const gchar *name;

		name = g_file_info_get_name (file_info);

		/* Here's the recursive part. */
		child = g_file_get_child (file, name);
		success = e_file_recursive_delete_sync (
			child, cancellable, error);
		g_object_unref (child);

		g_object_unref (file_info);

		if (!success)
			break;

		file_info = g_file_enumerator_next_file (
			file_enumerator, cancellable, &local_error);
	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	g_object_unref (file_enumerator);

	if (!success)
		return FALSE;

	/* The directory should be empty now. */
	return g_file_delete (file, cancellable, error);
}

/**
 * e_file_recursive_delete:
 * @file: a #GFile to delete
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously deletes @file.  If @file is a directory, its contents
 * are deleted recursively before @file itself is deleted.  The recursive
 * delete operation will stop on the first error.
 *
 * If @cancellable is not %NULL, then the operation can be cancelled
 * by triggering the cancellable object before the operation finishes.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_file_recursive_delete_finish() to get the result of the operation.
 *
 * Since: 3.6
 **/
void
e_file_recursive_delete (GFile *file,
                         gint io_priority,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (G_IS_FILE (file));

	simple = g_simple_async_result_new (
		G_OBJECT (file), callback, user_data,
		e_file_recursive_delete);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, file_recursive_delete_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

/**
 * e_file_recursive_delete_finish:
 * @file: a #GFile to delete
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_file_recursive_delete().
 *
 * If the operation was cancelled, the error #G_IO_ERROR_CANCELLED will be
 * returned.
 *
 * Returns: %TRUE if the file was deleted, %FALSE otherwise
 *
 * Since: 3.6
 **/
gboolean
e_file_recursive_delete_finish (GFile *file,
                                GAsyncResult *result,
                                GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (file), e_file_recursive_delete), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_binding_transform_enum_value_to_nick:
 * @binding: a #GBinding
 * @source_value: a #GValue whose type is derived from #G_TYPE_ENUM
 * @target_value: a #GValue of type #G_TYPE_STRING
 * @not_used: not used
 *
 * Transforms an enumeration value to its corresponding nickname.
 *
 * Returns: %TRUE if the enum value has a corresponding nickname
 *
 * Since: 3.4
 **/
gboolean
e_binding_transform_enum_value_to_nick (GBinding *binding,
                                        const GValue *source_value,
                                        GValue *target_value,
                                        gpointer not_used)
{
	GEnumClass *enum_class;
	GEnumValue *enum_value;
	gint value;
	gboolean success = FALSE;

	g_return_val_if_fail (G_IS_BINDING (binding), FALSE);

	enum_class = g_type_class_peek (G_VALUE_TYPE (source_value));
	g_return_val_if_fail (G_IS_ENUM_CLASS (enum_class), FALSE);

	value = g_value_get_enum (source_value);
	enum_value = g_enum_get_value (enum_class, value);
	if (enum_value != NULL) {
		g_value_set_string (target_value, enum_value->value_nick);
		success = TRUE;
	}

	return success;
}

/**
 * e_binding_transform_enum_nick_to_value:
 * @binding: a #GBinding
 * @source_value: a #GValue of type #G_TYPE_STRING
 * @target_value: a #GValue whose type is derived from #G_TYPE_ENUM
 * @not_used: not used
 *
 * Transforms an enumeration nickname to its corresponding value.
 *
 * Returns: %TRUE if the enum nickname has a corresponding value
 *
 * Since: 3.4
 **/
gboolean
e_binding_transform_enum_nick_to_value (GBinding *binding,
                                        const GValue *source_value,
                                        GValue *target_value,
                                        gpointer not_used)
{
	GEnumClass *enum_class;
	GEnumValue *enum_value;
	const gchar *string;
	gboolean success = FALSE;

	g_return_val_if_fail (G_IS_BINDING (binding), FALSE);

	enum_class = g_type_class_peek (G_VALUE_TYPE (target_value));
	g_return_val_if_fail (G_IS_ENUM_CLASS (enum_class), FALSE);

	string = g_value_get_string (source_value);
	enum_value = g_enum_get_value_by_nick (enum_class, string);
	if (enum_value != NULL) {
		g_value_set_enum (target_value, enum_value->value);
		success = TRUE;
	}

	return success;
}

/**
 * EAsyncClosure:
 *
 * #EAsyncClosure provides a simple way to run an asynchronous function
 * synchronously without blocking a running #GMainLoop or using threads.
 *
 * 1) Create an #EAsyncClosure with e_async_closure_new().
 *
 * 2) Call the asynchronous function passing e_async_closure_callback() as
 *    the #GAsyncReadyCallback argument and the #EAsyncClosure as the data
 *    argument.
 *
 * 3) Call e_async_closure_wait() and collect the #GAsyncResult.
 *
 * 4) Call the corresponding asynchronous "finish" function, passing the
 *    #GAsyncResult returned by e_async_closure_wait().
 *
 * 5) If needed, repeat steps 2-4 for additional asynchronous functions
 *    using the same #EAsyncClosure.
 *
 * 6) Finally, free the #EAsyncClosure with e_async_closure_free().
 *
 * Since: 3.6
 **/
struct _EAsyncClosure {
	GMainLoop *loop;
	GMainContext *context;
	GAsyncResult *result;
};

/**
 * e_async_closure_new:
 *
 * Creates a new #EAsyncClosure for use with asynchronous functions.
 *
 * Returns: a new #EAsyncClosure
 *
 * Since: 3.6
 **/
EAsyncClosure *
e_async_closure_new (void)
{
	EAsyncClosure *closure;

	closure = g_slice_new0 (EAsyncClosure);
	closure->context = g_main_context_new ();
	closure->loop = g_main_loop_new (closure->context, FALSE);

	g_main_context_push_thread_default (closure->context);

	return closure;
}

/**
 * e_async_closure_wait:
 * @closure: an #EAsyncClosure
 *
 * Call this function immediately after starting an asynchronous operation.
 * The function waits for the asynchronous operation to complete and returns
 * its #GAsyncResult to be passed to the operation's "finish" function.
 *
 * This function can be called repeatedly on the same #EAsyncClosure to
 * easily string together multiple asynchronous operations.
 *
 * Returns: (transfer none): a #GAsyncResult which is owned by the closure
 *
 * Since: 3.6
 **/
GAsyncResult *
e_async_closure_wait (EAsyncClosure *closure)
{
	g_return_val_if_fail (closure != NULL, NULL);

	g_main_loop_run (closure->loop);

	return closure->result;
}

/**
 * e_async_closure_free:
 * @closure: an #EAsyncClosure
 *
 * Frees the @closure and the resources it holds.
 *
 * Since: 3.6
 **/
void
e_async_closure_free (EAsyncClosure *closure)
{
	g_return_if_fail (closure != NULL);

	g_main_context_pop_thread_default (closure->context);

	g_main_loop_unref (closure->loop);
	g_main_context_unref (closure->context);

	if (closure->result != NULL)
		g_object_unref (closure->result);

	g_slice_free (EAsyncClosure, closure);
}

/**
 * e_async_closure_callback:
 * @object: a #GObject or %NULL, it is not used by the function at all
 * @result: a #GAsyncResult
 * @closure: an #EAsyncClosure
 *
 * Pass this function as the #GAsyncReadyCallback argument of an asynchronous
 * function, and the #EAsyncClosure as the data argument.
 *
 * This causes e_async_closure_wait() to terminate and return @result.
 *
 * Since: 3.6
 **/
void
e_async_closure_callback (GObject *object,
                          GAsyncResult *result,
                          gpointer closure)
{
	EAsyncClosure *real_closure;

	g_return_if_fail (G_IS_ASYNC_RESULT (result));
	g_return_if_fail (closure != NULL);

	real_closure = closure;

	/* Replace any previous result. */
	if (real_closure->result != NULL)
		g_object_unref (real_closure->result);
	real_closure->result = g_object_ref (result);

	g_main_loop_quit (real_closure->loop);
}

#ifdef G_OS_WIN32

#include <windows.h>

static const gchar *prefix = NULL;
static const gchar *cp_prefix;

static const gchar *localedir;
static const gchar *extensiondir;
static const gchar *imagesdir;
static const gchar *ui_uidir;

static HMODULE hmodule;
G_LOCK_DEFINE_STATIC (mutex);

/* Silence gcc with a prototype. Yes, this is silly. */
BOOL WINAPI DllMain (HINSTANCE hinstDLL,
		     DWORD     fdwReason,
		     LPVOID    lpvReserved);

/* Minimal DllMain that just tucks away the DLL's HMODULE */
BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
         DWORD fdwReason,
         LPVOID lpvReserved)
{
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		hmodule = hinstDLL;
		break;
	}
	return TRUE;
}

gchar *
e_util_replace_prefix (const gchar *configure_time_prefix,
                       const gchar *runtime_prefix,
                       const gchar *configure_time_path)
{
	gchar *c_t_prefix_slash;
	gchar *retval;

	c_t_prefix_slash = g_strconcat (configure_time_prefix, "/", NULL);

	if (runtime_prefix &&
	    g_str_has_prefix (configure_time_path, c_t_prefix_slash)) {
		retval = g_strconcat (
			runtime_prefix,
			configure_time_path + strlen (configure_time_prefix),
			NULL);
	} else
		retval = g_strdup (configure_time_path);

	g_free (c_t_prefix_slash);

	return retval;
}

static gchar *
replace_prefix (const gchar *runtime_prefix,
                const gchar *configure_time_path)
{
	return e_util_replace_prefix (
		E_DATA_SERVER_PREFIX, runtime_prefix, configure_time_path);
}

static void
setup (void)
{
	gchar *full_pfx;
	gchar *cp_pfx;

	G_LOCK (mutex);
	if (prefix != NULL) {
		G_UNLOCK (mutex);
		return;
	}

	/* This requires that the libedataserver DLL is installed in $bindir */
	full_pfx = g_win32_get_package_installation_directory_of_module (hmodule);
	cp_pfx = g_win32_locale_filename_from_utf8 (full_pfx);

	prefix = g_strdup (full_pfx);
	cp_prefix = g_strdup (cp_pfx);

	g_free (full_pfx);
	g_free (cp_pfx);

	localedir = replace_prefix (cp_prefix, E_DATA_SERVER_LOCALEDIR);
	extensiondir = replace_prefix (prefix, E_DATA_SERVER_EXTENSIONDIR);
	imagesdir = replace_prefix (prefix, E_DATA_SERVER_IMAGESDIR);
	ui_uidir = replace_prefix (prefix, E_DATA_SERVER_UI_UIDIR);

	G_UNLOCK (mutex);
}

#include "libedataserver-private.h" /* For prototypes */

#define GETTER_IMPL(varbl)			\
{						\
	setup ();				\
	return varbl;				\
}

#define PRIVATE_GETTER(varbl)			\
const gchar *					\
_libedataserver_get_##varbl (void)		\
	GETTER_IMPL (varbl)

#define PUBLIC_GETTER(varbl)			\
const gchar *					\
e_util_get_##varbl (void)			\
	GETTER_IMPL (varbl)

PRIVATE_GETTER (extensiondir)
PRIVATE_GETTER (imagesdir)
PRIVATE_GETTER (ui_uidir)

PUBLIC_GETTER (prefix)
PUBLIC_GETTER (cp_prefix)
PUBLIC_GETTER (localedir)

#endif	/* G_OS_WIN32 */

static gint default_dbus_timeout = DEFAULT_EDS_DBUS_TIMEOUT;

/**
 * e_data_server_util_set_dbus_call_timeout:
 * @timeout_msec: default timeout for D-Bus calls in miliseconds
 *
 * Sets default timeout, in milliseconds, for calls of g_dbus_proxy_call()
 * family functions.
 *
 * -1 means the default value as set by D-Bus itself.
 * G_MAXINT means no timeout at all.
 *
 * Default value is set also by configure option --with-dbus-call-timeout=ms
 * and -1 is used when not set.
 *
 * Since: 3.0
 **/
void
e_data_server_util_set_dbus_call_timeout (gint timeout_msec)
{
	default_dbus_timeout = timeout_msec;
}

/**
 * e_data_server_util_get_dbus_call_timeout:
 *
 * Returns the value set by e_data_server_util_set_dbus_call_timeout().
 *
 * Returns: the D-Bus call timeout in milliseconds
 *
 * Since: 3.0
 **/
gint
e_data_server_util_get_dbus_call_timeout (void)
{
	return default_dbus_timeout;
}
