/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STRING_UTILS_H
#define CAMEL_STRING_UTILS_H

#include <glib.h>

G_BEGIN_DECLS

gint   camel_strcase_equal (gconstpointer a, gconstpointer b);
guint camel_strcase_hash  (gconstpointer v);

gchar *camel_strstrcase (const gchar *haystack, const gchar *needle);

const gchar *camel_strdown (gchar *str);

const gchar *camel_pstring_add (gchar *string, gboolean own);
const gchar *camel_pstring_strdup (const gchar *string);
void camel_pstring_free (const gchar *string);
const gchar * camel_pstring_peek (const gchar *string);
gboolean camel_pstring_contains (const gchar *string);
void camel_pstring_dump_stat (void);

gboolean	camel_string_is_all_ascii	(const gchar *str);

G_END_DECLS

#endif /* CAMEL_STRING_UTILS_H */
