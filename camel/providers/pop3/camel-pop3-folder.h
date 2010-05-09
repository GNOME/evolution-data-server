/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-folder.h : Class for a POP3 folder */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_POP3_FOLDER_H
#define CAMEL_POP3_FOLDER_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_POP3_FOLDER \
	(camel_pop3_folder_get_type ())
#define CAMEL_POP3_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_POP3_FOLDER, CamelPOP3Folder))
#define CAMEL_POP3_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_POP3_FOLDER, CamelPOP3FolderClass))
#define CAMEL_IS_POP3_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_POP3_FOLDER))
#define CAMEL_IS_POP3_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_POP3_FOLDER))
#define CAMEL_POP3_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_POP3_FOLDER, CamelPOP3FolderClass))

G_BEGIN_DECLS

typedef struct _CamelPOP3Folder CamelPOP3Folder;
typedef struct _CamelPOP3FolderClass CamelPOP3FolderClass;
typedef struct _CamelPOP3FolderInfo CamelPOP3FolderInfo;

struct _CamelPOP3FolderInfo {
	guint32 id;
	guint32 size;
	guint32 flags;
	guint32 index;		/* index of request */
	gchar *uid;
	gint err;
	struct _CamelPOP3Command *cmd;
	struct _CamelStream *stream;
};

struct _CamelPOP3Folder {
	CamelFolder parent;

	GPtrArray *uids;
	GHashTable *uids_uid;	/* messageinfo by uid */
	GHashTable *uids_id;	/* messageinfo by id */
};

struct _CamelPOP3FolderClass {
	CamelFolderClass parent_class;
};

/* public methods */
CamelFolder *camel_pop3_folder_new (CamelStore *parent, GError **error);

GType camel_pop3_folder_get_type (void);

gint camel_pop3_delete_old(CamelFolder *folder, gint days_to_delete, GError **error);

G_END_DECLS

#endif /* CAMEL_POP3_FOLDER_H */
