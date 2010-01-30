/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *	     Michael Zucchi <NotZed@Ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef CAMEL_FILTER_SEARCH_H
#define CAMEL_FILTER_SEARCH_H

#include <glib.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-folder-summary.h>

G_BEGIN_DECLS

enum {
	CAMEL_SEARCH_ERROR    = -1,
	CAMEL_SEARCH_NOMATCH  =  0,
	CAMEL_SEARCH_MATCHED  =  1
};

typedef CamelMimeMessage * (*CamelFilterSearchGetMessageFunc) (gpointer data, CamelException *ex);

gint camel_filter_search_match (CamelSession *session,
			       CamelFilterSearchGetMessageFunc get_message, gpointer data,
			       CamelMessageInfo *info, const gchar *source,
			       const gchar *expression, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_FILTER_SEARCH_H */
