/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-message-cache.h: Class for an IMAP message cache */

/*
 * Author:
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_IMAP_MESSAGE_CACHE_H
#define CAMEL_IMAP_MESSAGE_CACHE_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP_MESSAGE_CACHE \
	(camel_imap_message_cache_get_type ())
#define CAMEL_IMAP_MESSAGE_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_MESSAGE_CACHE, CamelImapMessageCache))
#define CAMEL_IMAP_MESSAGE_CACHE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_MESSAGE_CACHE, CamelImapMessageCacheClass))
#define CAMEL_IS_IMAP_MESSAGE_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_MESSAGE_CACHE))
#define CAMEL_IS_IMAP_MESSAGE_CACHE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_MESSAGE_CACHE))
#define CAMEL_IMAP_MESSAGE_CACHE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_MESSAGE_CACHE, CamelImapMessageCacheClass))

G_BEGIN_DECLS

typedef struct _CamelImapMessageCache CamelImapMessageCache;
typedef struct _CamelImapMessageCacheClass CamelImapMessageCacheClass;

struct _CamelImapMessageCache {
	CamelObject parent;

	gchar *path;
        /* parts contains two sorts of objects.
         * If the key contains '.' then it is a stream (also reverse-indexed in
         * cached).
         * Otherwise it is a g_ptr_array containing the subparts the message
         * has. (e.g. 0., or 0.MIME.1).
         */
        /* cached contains streams for recently accessed messages */
	GHashTable *parts, *cached;
	guint32 max_uid;
};

struct _CamelImapMessageCacheClass {
	CamelObjectClass parent_class;
};

/* public methods */
CamelImapMessageCache *camel_imap_message_cache_new (const gchar *path,
						     CamelFolderSummary *summary,
						     CamelException *ex);

void camel_imap_message_cache_set_path (CamelImapMessageCache *cache,
					const gchar *path);

guint32     camel_imap_message_cache_max_uid (CamelImapMessageCache *cache);

CamelStream *camel_imap_message_cache_insert (CamelImapMessageCache *cache,
					      const gchar *uid,
					      const gchar *part_spec,
					      const gchar *data,
					      gint len,
					      CamelException *ex);
void camel_imap_message_cache_insert_stream  (CamelImapMessageCache *cache,
					      const gchar *uid,
					      const gchar *part_spec,
					      CamelStream *data_stream,
					      CamelException *ex);
void camel_imap_message_cache_insert_wrapper (CamelImapMessageCache *cache,
					      const gchar *uid,
					      const gchar *part_spec,
					      CamelDataWrapper *wrapper,
					      CamelException *ex);

CamelStream *camel_imap_message_cache_get    (CamelImapMessageCache *cache,
					      const gchar *uid,
					      const gchar *part_spec,
					      CamelException *ex);

gchar *       camel_imap_message_cache_get_filename (CamelImapMessageCache *cache,
					      const gchar *uid,
					      const gchar *part_spec,
					      CamelException *ex);

void         camel_imap_message_cache_remove (CamelImapMessageCache *cache,
					      const gchar *uid);

void         camel_imap_message_cache_clear  (CamelImapMessageCache *cache);

void         camel_imap_message_cache_copy   (CamelImapMessageCache *source,
					      const gchar *source_uid,
					      CamelImapMessageCache *dest,
					      const gchar *dest_uid,
					      CamelException *ex);
gboolean     camel_imap_message_cache_delete (const gchar *path,
					      CamelException *ex);
GPtrArray *  camel_imap_message_cache_filter_cached(CamelImapMessageCache *,
                                              GPtrArray *uids,
                                              CamelException *ex);

GType camel_imap_message_cache_get_type (void);

G_END_DECLS

#endif /* CAMEL_IMAP_MESSAGE_CACHE_H */
