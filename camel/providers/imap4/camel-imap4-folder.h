/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Camel
 *  Copyright (C) 1999-2007 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifndef __CAMEL_IMAP4_FOLDER_H__
#define __CAMEL_IMAP4_FOLDER_H__

#include <camel/camel-store.h>
#include <camel/camel-folder.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-offline-journal.h>

#define CAMEL_TYPE_IMAP4_FOLDER            (camel_imap4_folder_get_type ())
#define CAMEL_IMAP4_FOLDER(obj)            (CAMEL_CHECK_CAST ((obj), CAMEL_TYPE_IMAP4_FOLDER, CamelIMAP4Folder))
#define CAMEL_IMAP4_FOLDER_CLASS(klass)    (CAMEL_CHECK_CLASS_CAST ((klass), CAMEL_TYPE_IMAP4_FOLDER, CamelIMAP4FolderClass))
#define CAMEL_IS_IMAP4_FOLDER(obj)         (CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_IMAP4_FOLDER))
#define CAMEL_IS_IMAP4_FOLDER_CLASS(klass) (CAMEL_CHECK_CLASS_TYPE ((klass), CAMEL_TYPE_IMAP4_FOLDER))
#define CAMEL_IMAP4_FOLDER_GET_CLASS(obj)  (CAMEL_CHECK_GET_CLASS ((obj), CAMEL_TYPE_IMAP4_FOLDER, CamelIMAP4FolderClass))

G_BEGIN_DECLS

typedef struct _CamelIMAP4Folder CamelIMAP4Folder;
typedef struct _CamelIMAP4FolderClass CamelIMAP4FolderClass;

struct _CamelIMAP4Journal;

enum {
	CAMEL_IMAP4_FOLDER_ARG_ENABLE_MLIST = CAMEL_OFFLINE_FOLDER_ARG_LAST,
	CAMEL_IMAP4_FOLDER_ARG_LAST = CAMEL_OFFLINE_FOLDER_ARG_LAST + 0x100
};

enum {
	CAMEL_IMAP4_FOLDER_ENABLE_MLIST = CAMEL_IMAP4_FOLDER_ARG_ENABLE_MLIST | CAMEL_ARG_BOO,
};

struct _CamelIMAP4Folder {
	CamelOfflineFolder parent_object;
	
	CamelFolderSearch *search;
	
	CamelOfflineJournal *journal;
	CamelDataCache *cache;
	
	char *cachedir;
	char *utf7_name;
	
	unsigned int read_only:1;
	unsigned int enable_mlist:1;
};

struct _CamelIMAP4FolderClass {
	CamelOfflineFolderClass parent_class;
	
};


CamelType camel_imap4_folder_get_type (void);

CamelFolder *camel_imap4_folder_new (CamelStore *store, const char *full_name, CamelException *ex);

const char *camel_imap4_folder_utf7_name (CamelIMAP4Folder *folder);

G_END_DECLS

#endif /* __CAMEL_IMAP4_FOLDER_H__ */
