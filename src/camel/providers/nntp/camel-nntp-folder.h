/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Chris Toshok <toshok@ximian.com>
 */

#ifndef CAMEL_NNTP_FOLDER_H
#define CAMEL_NNTP_FOLDER_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_NNTP_FOLDER \
	(camel_nntp_folder_get_type ())
#define CAMEL_NNTP_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_NNTP_FOLDER, CamelNNTPFolder))
#define CAMEL_NNTP_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_NNTP_FOLDER, CamelNNTPFolderClass))
#define CAMEL_IS_NNTP_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_NNTP_FOLDER))
#define CAMEL_IS_NNTP_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_NNTP_FOLDER))
#define CAMEL_NNTP_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_NNTP_FOLDER, CamelNNTPFolderClass))

G_BEGIN_DECLS

typedef struct _CamelNNTPFolder CamelNNTPFolder;
typedef struct _CamelNNTPFolderClass CamelNNTPFolderClass;
typedef struct _CamelNNTPFolderPrivate CamelNNTPFolderPrivate;

struct _CamelNNTPFolder {
	CamelOfflineFolder parent;
	CamelNNTPFolderPrivate *priv;

	struct _CamelFolderChangeInfo *changes;
};

struct _CamelNNTPFolderClass {
	CamelOfflineFolderClass parent;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_nntp_folder_get_type	(void);
CamelFolder *	camel_nntp_folder_new		(CamelStore *parent,
						 const gchar *folder_name,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_nntp_folder_selected	(CamelNNTPFolder *folder,
						 gchar *line,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_NNTP_FOLDER_H */
