/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
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

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mbox_folder_get_type	(void);
CamelFolder *	camel_mbox_folder_new		(CamelStore *parent_store,
						 const gchar *full_name,
						 guint32 flags,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_MBOX_FOLDER_H */
