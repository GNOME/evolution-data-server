/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_MAILDIR_FOLDER_H
#define CAMEL_MAILDIR_FOLDER_H

#include "camel-local-folder.h"

/* Standard GObject macros */
#define CAMEL_TYPE_MAILDIR_FOLDER \
	(camel_maildir_folder_get_type ())
#define CAMEL_MAILDIR_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAILDIR_FOLDER, CamelMaildirFolder))
#define CAMEL_MAILDIR_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAILDIR_FOLDER, CamelMaildirFolderClass))
#define CAMEL_IS_MAILDIR_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAILDIR_FOLDER))
#define CAMEL_IS_MAILDIR_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAILDIR_FOLDER))
#define CAMEL_MAILDIR_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAILDIR_FOLDER, CamelMaildirFolderClass))

G_BEGIN_DECLS

typedef struct _CamelMaildirFolder CamelMaildirFolder;
typedef struct _CamelMaildirFolderClass CamelMaildirFolderClass;

struct _CamelMaildirFolder {
	CamelLocalFolder parent;
};

struct _CamelMaildirFolderClass {
	CamelLocalFolderClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_maildir_folder_get_type	(void);
CamelFolder *	camel_maildir_folder_new	(CamelStore *parent_store,
						 const gchar *full_name,
						 guint32 flags,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_MAILDIR_FOLDER_H */
