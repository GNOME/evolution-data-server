/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-search.h: IMAP folder search */

/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
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

#ifndef CAMEL_IMAP_SEARCH_H
#define CAMEL_IMAP_SEARCH_H

#include <camel/camel.h>

/* Standard GObject class */
#define CAMEL_TYPE_IMAP_SEARCH \
	(camel_imap_search_get_type ())
#define CAMEL_IMAP_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_SEARCH, CamelImapSearch))
#define CAMEL_IMAP_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_SEARCH, CamelImapSearchClass))
#define CAMEL_IS_IMAP_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_SEARCH))
#define CAMEL_IS_IMAP_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_SEARCH))
#define CAMEL_IMAP_SEARCH_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_SEARCH, CamelImapSearchClass))

G_BEGIN_DECLS

typedef struct _CamelImapSearch CamelImapSearch;
typedef struct _CamelImapSearchClass CamelImapSearchClass;

struct _CamelImapSearch {
	CamelFolderSearch parent;

	guint32 lastuid;	/* current 'last uid' for the folder */
	guint32 validity;	/* validity of the current folder */

	CamelDataCache *cache;	/* disk-cache for searches */

	/* cache of body search matches */
	guint matches_count;
	CamelDList matches;
	GHashTable *matches_hash;
};

struct _CamelImapSearchClass {
	CamelFolderSearchClass parent_class;

};

GType              camel_imap_search_get_type (void);
CamelFolderSearch *camel_imap_search_new      (const gchar *cachedir);

G_END_DECLS

#endif /* CAMEL_IMAP_SEARCH_H */
