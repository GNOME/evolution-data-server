/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_OFFLINE_FOLDER_H
#define CAMEL_OFFLINE_FOLDER_H

#include <camel/camel-folder.h>

#define CAMEL_OFFLINE_FOLDER_TYPE     (camel_offline_folder_get_type ())
#define CAMEL_OFFLINE_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_OFFLINE_FOLDER_TYPE, CamelOfflineFolder))
#define CAMEL_OFFLINE_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_OFFLINE_FOLDER_TYPE, CamelOfflineFolderClass))
#define CAMEL_IS_OFFLINE_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_OFFLINE_FOLDER_TYPE))
#define CAMEL_OFFLINE_FOLDER_GET_CLASS(obj) \
	((CamelOfflineFolderClass *) CAMEL_OBJECT_GET_CLASS (obj))

G_BEGIN_DECLS

typedef struct _CamelOfflineFolder CamelOfflineFolder;
typedef struct _CamelOfflineFolderClass CamelOfflineFolderClass;

enum {
	CAMEL_OFFLINE_FOLDER_ARG_SYNC_OFFLINE = CAMEL_FOLDER_ARG_LAST,
	CAMEL_OFFLINE_FOLDER_ARG_LAST = CAMEL_FOLDER_ARG_LAST + 0x100
};

enum {
	CAMEL_OFFLINE_FOLDER_SYNC_OFFLINE = CAMEL_OFFLINE_FOLDER_ARG_SYNC_OFFLINE | CAMEL_ARG_BOO
};

struct _CamelOfflineFolder {
	CamelFolder parent;

	guint sync_offline:1;
};

struct _CamelOfflineFolderClass {
	CamelFolderClass parent_class;

	gboolean (* downsync) (CamelOfflineFolder *folder, const gchar *expression, CamelException *ex);
};

CamelType camel_offline_folder_get_type (void);

gboolean camel_offline_folder_downsync (CamelOfflineFolder *offline, const gchar *expression, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_OFFLINE_FOLDER_H */
