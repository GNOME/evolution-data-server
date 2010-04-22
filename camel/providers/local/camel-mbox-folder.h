/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_MBOX_FOLDER_H
#define CAMEL_MBOX_FOLDER_H

#include "camel-local-folder.h"
#include "camel-mbox-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_MBOX_FOLDER \
	(camel_mbox_folder_get_type ())
#define CAMEL_MBOX_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MBOX_FOLDER, CamelMboxFolder))
#define CAMEL_MBOX_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MBOX_FOLDER, CamelMboxFolderClass))
#define CAMEL_IS_MBOX_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MBOX_FOLDER))
#define CAMEL_IS_MBOX_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MBOX_FOLDER))
#define CAMEL_MBOX_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MBOX_FOLDER, CamelMboxFolderClass))

G_BEGIN_DECLS

typedef struct _CamelMboxFolder CamelMboxFolder;
typedef struct _CamelMboxFolderClass CamelMboxFolderClass;

struct _CamelMboxFolder {
	CamelLocalFolder parent;

	gint lockfd;		/* for when we have a lock on the folder */
};

struct _CamelMboxFolderClass {
	CamelLocalFolderClass parent_class;
};

/* public methods */
/* flags are taken from CAMEL_STORE_FOLDER_* flags */
CamelFolder *camel_mbox_folder_new(CamelStore *parent_store, const gchar *full_name, guint32 flags, CamelException *ex);

GType camel_mbox_folder_get_type(void);

G_END_DECLS

#endif /* CAMEL_MBOX_FOLDER_H */
