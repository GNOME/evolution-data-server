/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef _CAMEL_VTRASH_FOLDER_H
#define _CAMEL_VTRASH_FOLDER_H

#include <camel/camel-folder.h>
#include <camel/camel-vee-folder.h>

#define CAMEL_VTRASH_FOLDER(obj)         CAMEL_CHECK_CAST (obj, camel_vtrash_folder_get_type (), CamelVTrashFolder)
#define CAMEL_VTRASH_FOLDER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vtrash_folder_get_type (), CamelVTrashFolderClass)
#define CAMEL_IS_VTRASH_FOLDER(obj)      CAMEL_CHECK_TYPE (obj, camel_vtrash_folder_get_type ())

G_BEGIN_DECLS

typedef struct _CamelVTrashFolder      CamelVTrashFolder;
typedef struct _CamelVTrashFolderClass CamelVTrashFolderClass;

#define CAMEL_VTRASH_NAME ".#evolution/Trash"
#define CAMEL_VJUNK_NAME ".#evolution/Junk"

typedef enum {
	CAMEL_VTRASH_FOLDER_TRASH,
	CAMEL_VTRASH_FOLDER_JUNK,
	CAMEL_VTRASH_FOLDER_LAST
} camel_vtrash_folder_t;

struct _CamelVTrashFolder {
	CamelVeeFolder parent;

	camel_vtrash_folder_t type;
	guint32 bit;
};

struct _CamelVTrashFolderClass {
	CamelVeeFolderClass parent_class;

};

CamelType       camel_vtrash_folder_get_type    (void);

CamelFolder    *camel_vtrash_folder_new		(CamelStore *parent_store, camel_vtrash_folder_t type);

G_END_DECLS

#endif /* _CAMEL_VTRASH_FOLDER_H */
