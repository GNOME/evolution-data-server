/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.h : Class for a IMAP folder */

/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_IMAPX_FOLDER_H
#define CAMEL_IMAPX_FOLDER_H

#include <camel/camel-offline-folder.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-folder-search.h>
#include <camel/camel-store.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_FOLDER \
	(camel_imapx_folder_get_type ())
#define CAMEL_IMAPX_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolder))
#define CAMEL_IMAPX_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolderClass))
#define CAMEL_IS_IMAPX_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_FOLDER))
#define CAMEL_IS_IMAPX_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_FOLDER))
#define CAMEL_IMAPX_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolderClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXFolder CamelIMAPXFolder;
typedef struct _CamelIMAPXFolderClass CamelIMAPXFolderClass;

struct _CamelIMAPXFolder {
	CamelOfflineFolder parent;

	gchar *raw_name;
	CamelDataCache *cache;
	CamelFolderSearch *search;

	guint32 exists_on_server;
	guint32 unread_on_server;
	guint64 modseq_on_server;
	guint64 uidvalidity_on_server;
	guint32 uidnext_on_server;

	/* hash table of UIDs to ignore as recent when updating folder */
	GHashTable *ignore_recent;

	GMutex *search_lock;
	GMutex *stream_lock;

	gboolean apply_filters;		/* persistent property */
};

struct _CamelIMAPXFolderClass {
	CamelOfflineFolderClass parent_class;
};

GType		camel_imapx_folder_get_type	(void);
CamelFolder *	camel_imapx_folder_new		(CamelStore *parent,
						 const gchar *path,
						 const gchar *raw,
						 GError **error);
gchar *		imapx_get_filename		(CamelFolder *folder,
						 const gchar *uid,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_IMAPX_FOLDER_H */
