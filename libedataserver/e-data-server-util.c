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

#include <glib.h>

#ifdef G_OS_WIN32
#include <mbstring.h>
#endif

#include "e-data-server-util.h"

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
 * Find the first instance of @needle in @haystack, ignoring case. (No
 * proper case folding or decomposing is done.) Both @needle and
 * @haystack are UTF-8 strings.
 *
 * Returns: A pointer to the first instance of @needle in @haystack, or
 *          %NULL if no match is found, or if either of the strings are
 *          not legal UTF-8 strings.
 **/
const gchar *
e_util_utf8_strstrcase (const gchar *haystack, const gchar *needle)
{
        gunichar *nuni, unival;
        gint nlen;
        const gchar *o, *p;

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
        gunichar *decomp, retval;
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
        const gchar *o, *p;

        if (haystack == NULL) return NULL;
        if (needle == NULL) return NULL;
        if (strlen (needle) == 0) return haystack;
        if (strlen (haystack) == 0) return NULL;

        nuni = g_alloca (sizeof (gunichar) * strlen (needle));

        nlen = 0;
        for (p = e_util_unicode_get_utf8 (needle, &unival); p && unival; p = e_util_unicode_get_utf8 (p, &unival)) {
                gunichar sc;
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

gint
e_util_utf8_strcasecmp (const gchar *s1, const gchar *s2)
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
 *
 * Returns newly allocates string, copy of 'str', without accents.
 *
 * Since: 2.28
 **/
gchar *
e_util_utf8_remove_accents (const gchar *str)
{
	gchar *res;
	gint i, j;

	if (!str)
		return NULL;

	res = g_utf8_normalize (str, -1, G_NORMALIZE_NFD);
	if (!res)
		return g_strdup (str);

	for (i = 0, j = 0; res [i]; i++) {
		if ((guchar)res[i] != 0xCC || res [i + 1] == 0) {
			res [j] = res [i];
			j++;
		} else {
			i++;
		}
	}

	res [j] = 0;

	return res;
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
gsize e_strftime(gchar *s, gsize max, const gchar *fmt, const struct tm *tm)
{
	gsize ret;
#ifdef HAVE_LKSTRFTIME
	ret = strftime(s, max, fmt, tm);
#else
	gchar *c, *ffmt, *ff;

	ffmt = g_strdup(fmt);
	ff = ffmt;
	while ((c = strstr(ff, "%l")) != NULL) {
		c[1] = 'I';
		ff = c;
	}

	ff = ffmt;
	while ((c = strstr(ff, "%k")) != NULL) {
		c[1] = 'H';
		ff = c;
	}

#ifdef G_OS_WIN32
	/* The Microsoft strftime() doesn't have %e either */
	ff = ffmt;
	while ((c = strstr(ff, "%e")) != NULL) {
		c[1] = 'd';
		ff = c;
	}
#endif

	ret = strftime(s, max, ffmt, tm);
	g_free(ffmt);
#endif
	if (ret == 0 && max > 0)
		s[0] = '\0';
	return ret;
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
gsize
e_utf8_strftime(gchar *s, gsize max, const gchar *fmt, const struct tm *tm)
{
	gsize sz, ret;
	gchar *locale_fmt, *buf;

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
		gchar *tmp = buf + max - 1;
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

/**
 * e_util_pthread_id:
 * @t: A pthread_t value
 *
 * Returns a 64-bit integer hopefully uniquely identifying the
 * thread. To be used in debugging output and logging only. To test
 * whether two pthread_t values refer to the same thread, use
 * pthread_equal().
 *
 * There is no guarantee that calling e_util_pthread_id() on one
 * thread first and later after that thread has dies on another won't
 * return the same integer.
 *
 * On some platforms it might even be that when called two times on
 * the same thread's pthread_t (with some pthread API calls inbetween)
 * we will return different values (this of course makes this function
 * rather useless on such platforms).
 *
 * On Linux and Win32, known to really return a unique id for each
 * thread existing at a certain time. No guarantee that ids won't be
 * reused after a thread has terminated, though.
 *
 * Returns: A 64-bit integer.
 */
guint64
e_util_pthread_id (pthread_t t)
{
#ifdef HAVE_GUINT64_CASTABLE_PTHREAD_T
	/* We know that pthread_t is an integral type, or at least
	 * castable to such without loss of precision.
	 */
	return (guint64) t;
#elif defined (PTW32_VERSION)
	/* pthreads-win32 implementation on Windows: Return the
	 * pointer to the "actual object" (see pthread.h)
	 */
#if GLIB_SIZEOF_VOID_P == 8
	/* 64-bit Windows */
	return (guint64) t.p;
#else
	return (gint) t.p;
#endif
#else
	/* Just return a checksum of the contents of the pthread_t */
	{
		guint64 retval = 0;
		guchar *const tend = (guchar *) ((&t)+1);
		guchar *tp = (guchar *) &t;

		while (tp < tend)
			retval = (retval << 5) - retval * *tp++;

		return retval;
	}
#endif
}

/* This only makes a filename safe for usage as a filename.  It still may have shell meta-characters in it. */

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
		if (!g_unichar_isprint(c) || ( c < 0xff && strchr (unsafe_chars, c&0xff ))) {
			while (ts<p)
				*ts++ = '_';
		}
	}
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
	 DWORD     fdwReason,
	 LPVOID    lpvReserved)
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
	gchar *c_t_prefix_slash = g_strconcat (configure_time_prefix, "/",
					      NULL);
	gchar *retval;

        if (runtime_prefix &&
            g_str_has_prefix (configure_time_path, c_t_prefix_slash)) {
                retval = g_strconcat (runtime_prefix,
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
	return e_util_replace_prefix (E_DATA_SERVER_PREFIX,
				      runtime_prefix,
				      configure_time_path);
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
	full_pfx = g_win32_get_package_installation_directory_of_module(hmodule);
	cp_pfx = g_win32_locale_filename_from_utf8(full_pfx);

	prefix = g_strdup (full_pfx);
	cp_prefix = g_strdup (cp_pfx);

	g_free (full_pfx);
	g_free (cp_pfx);

	localedir = replace_prefix (cp_prefix, EVOLUTION_LOCALEDIR);
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
	GETTER_IMPL(varbl)

#define PUBLIC_GETTER(varbl)			\
const gchar *					\
e_util_get_##varbl (void)			\
	GETTER_IMPL(varbl)

PRIVATE_GETTER(extensiondir)
PRIVATE_GETTER(imagesdir)
PRIVATE_GETTER(ui_uidir)

PUBLIC_GETTER(prefix)
PUBLIC_GETTER(cp_prefix)
PUBLIC_GETTER(localedir)

#endif	/* G_OS_WIN32 */
