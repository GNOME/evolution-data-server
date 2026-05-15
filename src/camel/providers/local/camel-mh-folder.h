/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_MH_FOLDER_H
#define CAMEL_MH_FOLDER_H

#include "camel-local-folder.h"

#define CAMEL_TYPE_MH_FOLDER \
	(camel_mh_folder_get_type ())
#define CAMEL_MH_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MH_FOLDER, CamelMhFolder))
#define CAMEL_MH_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MH_FOLDER, CamelMhFolderClass))
#define CAMEL_IS_MH_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MH_FOLDER))
#define CAMEL_IS_MH_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MH_FOLDER))
#define CAMEL_MH_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MH_FOLDER, CamelMhFolderClass))

G_BEGIN_DECLS

typedef struct _CamelMhFolder CamelMhFolder;
typedef struct _CamelMhFolderClass CamelMhFolderClass;

struct _CamelMhFolder {
	CamelLocalFolder parent;
};

struct _CamelMhFolderClass {
	CamelLocalFolderClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mh_folder_get_type	(void);
CamelFolder *	camel_mh_folder_new		(CamelStore *parent_store,
						 const gchar *full_name,
						 guint32 flags,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_MH_FOLDER_H */
