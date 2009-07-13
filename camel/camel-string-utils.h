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

#ifndef __CAMEL_STRING_UTILS_H__
#define __CAMEL_STRING_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

gint   camel_strcase_equal (gconstpointer a, gconstpointer b);
guint camel_strcase_hash  (gconstpointer v);

void camel_string_list_free (GList *string_list);

gchar *camel_strstrcase (const gchar *haystack, const gchar *needle);

const gchar *camel_strdown (gchar *str);
gchar camel_tolower(gchar c);
gchar camel_toupper(gchar c);

const gchar *camel_pstring_add (gchar *str, gboolean own);
const gchar *camel_pstring_strdup(const gchar *s);
void camel_pstring_free(const gchar *s);
const gchar * camel_pstring_peek (const gchar *str);

G_END_DECLS

#endif /* __CAMEL_STRING_UTILS_H__ */
