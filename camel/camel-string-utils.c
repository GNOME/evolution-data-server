/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-string-utils.h"

gint
camel_strcase_equal (gconstpointer a,
                     gconstpointer b)
{
	return (g_ascii_strcasecmp ((const gchar *) a, (const gchar *) b) == 0);
}

guint
camel_strcase_hash (gconstpointer v)
{
	const gchar *p = (gchar *) v;
	guint h = 0, g;

	for (; *p != '\0'; p++) {
		h = (h << 4) + g_ascii_toupper (*p);
		if ((g = h & 0xf0000000)) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h;
}

static void
free_string (gpointer string,
             gpointer user_data)
{
	g_free (string);
}

void
camel_string_list_free (GList *string_list)
{
	if (string_list == NULL)
		return;

	g_list_foreach (string_list, free_string, NULL);
	g_list_free (string_list);
}

gchar *
camel_strstrcase (const gchar *haystack,
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

const gchar *
camel_strdown (gchar *str)
{
	register gchar *s = str;

	while (*s) {
		if (*s >= 'A' && *s <= 'Z')
			*s += 0x20;
		s++;
	}

	return str;
}

/**
 * camel_tolower:
 * @c:
 *
 * ASCII to-lower function.
 *
 * Returns:
 **/
gchar camel_tolower (gchar c)
{
	if (c >= 'A' && c <= 'Z')
		c |= 0x20;

	return c;
}

/**
 * camel_toupper:
 * @c:
 *
 * ASCII to-upper function.
 *
 * Returns:
 **/
gchar camel_toupper (gchar c)
{
	if (c >= 'a' && c <= 'z')
		c &= ~0x20;

	return c;
}

/* working stuff for pstrings */
static GStaticMutex pstring_lock = G_STATIC_MUTEX_INIT;
static GHashTable *pstring_table = NULL;

/**
 * camel_pstring_add:
 * @str: string to add to the string pool
 * @own: whether the string pool will own the memory pointed to by @str, if @str is not yet in the pool
 *
 * Add the string to the pool.
 *
 * The NULL and empty strings are special cased to constant values.
 *
 * Returns: A pointer to an equivalent string of @s.  Use
 * camel_pstring_free() when it is no longer needed.
 **/
const gchar *
camel_pstring_add (gchar *str,
                   gboolean own)
{
	gpointer pcount;
	gpointer pstr;

	if (str == NULL)
		return NULL;

	if (str[0] == '\0') {
		if (own)
			g_free (str);
		return "";
	}

	g_static_mutex_lock (&pstring_lock);
	if (pstring_table == NULL)
		pstring_table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	if (g_hash_table_lookup_extended (pstring_table, str, &pstr, &pcount)) {
		gint *count = pcount;
		*count = (*count) + 1;
		if (own)
			g_free (str);
	} else {
		gint *count = g_new0 (gint, 1);
		*count = 1;
		pstr = own ? str : g_strdup (str);
		g_hash_table_insert (pstring_table, pstr, count);
	}

	g_static_mutex_unlock (&pstring_lock);

	return pstr;
}

/**
 * camel_pstring_peek:
 * @str: string to fetch to the string pool
 *
 * Add return the string from the pool.
 *
 * The NULL and empty strings are special cased to constant values.
 *
 * Returns: A pointer to an equivalent string of @s.  Use
 * camel_pstring_free() when it is no longer needed.
 *
 * Since: 2.24
 **/
const gchar *
camel_pstring_peek (const gchar *str)
{
	gpointer pcount;
	gpointer pstr;

	if (str == NULL)
		return NULL;

	if (str[0] == '\0') {
		return "";
	}

	g_static_mutex_lock (&pstring_lock);
	if (pstring_table == NULL)
		pstring_table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	if (!g_hash_table_lookup_extended (pstring_table, str, &pstr, &pcount)) {
		gint *count = g_new0 (gint, 1);
		*count = 1;
		pstr = g_strdup (str);
		g_hash_table_insert (pstring_table, pstr, count);
	}

	g_static_mutex_unlock (&pstring_lock);

	return pstr;
}
/**
 * camel_pstring_strdup:
 * @s: String to copy.
 *
 * Create a new pooled string entry for the string @s.  A pooled
 * string is a table where common strings are uniquified to the same
 * pointer value.  They are also refcounted, so freed when no longer
 * in use.  In a thread-safe manner.
 *
 * The NULL and empty strings are special cased to constant values.
 *
 * Returns: A pointer to an equivalent string of @s.  Use
 * camel_pstring_free() when it is no longer needed.
 **/
const gchar *
camel_pstring_strdup (const gchar *s)
{
	return camel_pstring_add ((gchar *) s, FALSE);
}

/**
 * camel_pstring_free:
 * @s: String to free.
 *
 * De-ref a pooled string. If no more refs exist to this string, it will be deallocated.
 *
 * NULL and the empty string are special cased.
 **/
void
camel_pstring_free (const gchar *s)
{
	gpointer pstr;
	gpointer pcount;

	if (pstring_table == NULL)
		return;
	if (s == NULL || s[0] == 0)
		return;

	g_static_mutex_lock (&pstring_lock);
	if (g_hash_table_lookup_extended (pstring_table, s, &pstr, &pcount)) {
		gint *count = pcount;
		*count = (*count) - 1;
		if ((*count) == 0) {
			g_hash_table_remove (pstring_table, pstr);
			g_free (pstr);
			if (g_getenv("CDS_DEBUG")) {
				if (pstr != s) /* Only for debugging purposes */
					g_assert (0);
			}
		}
	} else {
		if (g_getenv("CDS_DEBUG")) {
			g_warning("Trying to free string not allocated from the pool '%s'", s);
			/*Only for debugging purposes */
			g_assert (0);
		}
	}
	g_static_mutex_unlock (&pstring_lock);
}

static void
count_pstring_memory_cb (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
	const gchar *str = key;
	guint64 *pbytes = user_data;

	g_return_if_fail (str != NULL);
	g_return_if_fail (pbytes != NULL);

	*pbytes += strlen (str);
}

/**
 * camel_pstring_dump_stat:
 *
 * Dumps to stdout memory statistic about the string pool.
 **/
void
camel_pstring_dump_stat (void)
{
	g_static_mutex_lock (&pstring_lock);

	if (!pstring_table) {
		g_print ("   String Pool Statistics: Not used yet\n");
	} else {
		guint64 bytes = 0;

		g_hash_table_foreach (pstring_table, count_pstring_memory_cb, &bytes);
		g_print ("   String Pool Statistics: Holds %d strings of total %" G_GUINT64_FORMAT " bytes\n", g_hash_table_size (pstring_table), bytes);
	}

	g_static_mutex_unlock (&pstring_lock);
}
