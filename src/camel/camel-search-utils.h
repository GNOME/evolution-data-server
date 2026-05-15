/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
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
