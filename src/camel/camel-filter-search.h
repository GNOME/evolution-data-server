/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 * SPDX-FileContributor: Michael Zucchi <NotZed@Ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_FILTER_SEARCH_H
#define CAMEL_FILTER_SEARCH_H

#include <stdio.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-folder-summary.h>

G_BEGIN_DECLS

struct _CamelSession;

enum {
	CAMEL_SEARCH_ERROR = -1,
	CAMEL_SEARCH_NOMATCH = 0,
	CAMEL_SEARCH_MATCHED = 1
};

typedef CamelMimeMessage * (*CamelFilterSearchGetMessageFunc) (gpointer data, GCancellable *cancellable, GError **error);

gint camel_filter_search_match (struct _CamelSession *session,
				CamelFilterSearchGetMessageFunc get_message,
				gpointer user_data,
				CamelMessageInfo *info,
				const gchar *source,
				struct _CamelFolder *folder,
				const gchar *expression,
				GCancellable *cancellable,
				GError **error);
gint camel_filter_search_match_with_log
				(struct _CamelSession *session,
				 CamelFilterSearchGetMessageFunc get_message,
				 gpointer user_data,
				 CamelMessageInfo *info,
				 const gchar *source,
				 struct _CamelFolder *folder,
				 const gchar *expression,
				 FILE *logfile,
				 GCancellable *cancellable,
				 GError **error);

G_END_DECLS

#endif /* CAMEL_FILTER_SEARCH_H */
