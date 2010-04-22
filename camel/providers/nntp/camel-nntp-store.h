/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-store.h : class for an nntp store */

/*
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

#ifndef CAMEL_NNTP_STORE_H
#define CAMEL_NNTP_STORE_H

#include <camel/camel.h>

#include "camel-nntp-stream.h"
#include "camel-nntp-store-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_NNTP_STORE \
	(camel_nntp_store_get_type ())
#define CAMEL_NNTP_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_NNTP_STORE, CamelNNTPStore))
#define CAMEL_NNTP_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_NNTP_STORE, CamelNNTPStoreClass))
#define CAMEL_IS_NNTP_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_NNTP_STORE))
#define CAMEL_IS_NNTP_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_NNTP_STORE))
#define CAMEL_NNTP_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_NNTP_STORE, CamelNNTPStoreClass))

#define CAMEL_NNTP_EXT_SEARCH     (1<<0)
#define CAMEL_NNTP_EXT_SETGET     (1<<1)
#define CAMEL_NNTP_EXT_OVER       (1<<2)
#define CAMEL_NNTP_EXT_XPATTEXT   (1<<3)
#define CAMEL_NNTP_EXT_XACTIVE    (1<<4)
#define CAMEL_NNTP_EXT_LISTMOTD   (1<<5)
#define CAMEL_NNTP_EXT_LISTSUBSCR (1<<6)
#define CAMEL_NNTP_EXT_LISTPNAMES (1<<7)

G_BEGIN_DECLS

struct _CamelNNTPFolder;
struct _CamelException;

typedef struct _CamelNNTPStore CamelNNTPStore;
typedef struct _CamelNNTPStoreClass CamelNNTPStoreClass;
typedef struct _CamelNNTPStorePrivate CamelNNTPStorePrivate;

typedef enum _xover_t {
	XOVER_STRING = 0,
	XOVER_MSGID,
	XOVER_SIZE
} xover_t;

struct _xover_header {
	struct _xover_header *next;

	const gchar *name;
	guint skip:8;
	xover_t type:8;
};

struct _CamelNNTPStore {
	CamelDiscoStore parent;

	CamelNNTPStorePrivate *priv;

	guint32 extensions;

	guint posting_allowed:1;
	guint do_short_folder_notation:1;
	guint folder_hierarchy_relative:1;

	struct _CamelNNTPStoreSummary *summary;

	struct _CamelNNTPStream *stream;
	struct _CamelStreamMem *mem;

	struct _CamelDataCache *cache;

	gchar *current_folder, *storage_path, *base_url;

	struct _xover_header *xover;
};

struct _CamelNNTPStoreClass {
	CamelDiscoStoreClass parent_class;

};

GType camel_nntp_store_get_type (void);

gint camel_nntp_raw_commandv (CamelNNTPStore *store, struct _CamelException *ex, gchar **line, const gchar *fmt, va_list ap);
gint camel_nntp_raw_command(CamelNNTPStore *store, struct _CamelException *ex, gchar **line, const gchar *fmt, ...);
gint camel_nntp_raw_command_auth(CamelNNTPStore *store, struct _CamelException *ex, gchar **line, const gchar *fmt, ...);
gint camel_nntp_command (CamelNNTPStore *store, struct _CamelException *ex, struct _CamelNNTPFolder *folder, gchar **line, const gchar *fmt, ...);

G_END_DECLS

#endif /* CAMEL_NNTP_STORE_H */

