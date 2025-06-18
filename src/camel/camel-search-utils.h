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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SEARCH_UTILS_H
#define CAMEL_SEARCH_UTILS_H

#include <glib.h>

#include <camel/camel-sexp.h>

G_BEGIN_DECLS

time_t		camel_search_util_add_months	(time_t t,
						 gint months);
gint64		camel_search_util_str_to_time	(const gchar *str);
time_t		camel_search_util_make_time	(gint argc,
						 CamelSExpResult **argv);
gint		camel_search_util_compare_date	(gint64 datetime1,
						 gint64 datetime2);
guint64		camel_search_util_hash_message_id
						(const gchar *message_id,
						 gboolean needs_decode);

G_END_DECLS

#endif /* CAMEL_SEARCH_UTILS_H */
