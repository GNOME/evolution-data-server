/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.h: class for an imap folder */

/* 
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com> 
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


#ifndef CAMEL_IMAP_FOLDER_H
#define CAMEL_IMAP_FOLDER_H 1

#include "camel-imap-types.h"
#include <camel/camel-offline-folder.h>
#include <camel/camel-folder-search.h>
#include <camel/camel-offline-journal.h>

#define CAMEL_IMAP_FOLDER_TYPE     (camel_imap_folder_get_type ())
#define CAMEL_IMAP_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_FOLDER_TYPE, CamelImapFolder))
#define CAMEL_IMAP_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_FOLDER_TYPE, CamelImapFolderClass))
#define CAMEL_IS_IMAP_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_FOLDER_TYPE))

G_BEGIN_DECLS

struct _CamelIMAP4Journal;

enum {
	CAMEL_IMAP_FOLDER_ARG_CHECK_FOLDER = CAMEL_OFFLINE_FOLDER_ARG_LAST,
	CAMEL_IMAP_FOLDER_ARG_LAST = CAMEL_OFFLINE_FOLDER_ARG_LAST + 0x100
};

enum {
	CAMEL_IMAP_FOLDER_CHECK_FOLDER = CAMEL_IMAP_FOLDER_ARG_CHECK_FOLDER | CAMEL_ARG_BOO,
};

typedef struct _CamelImapFolderClass CamelImapFolderClass;
typedef struct _CamelImapFolderPrivate CamelImapFolderPrivate;

struct _CamelImapFolder {
	CamelOfflineFolder parent_object;

	CamelImapFolderPrivate *priv;

	CamelFolderSearch *search;
	CamelImapMessageCache *cache;
	CamelOfflineJournal *journal;

	unsigned int need_rescan:1;
	unsigned int need_refresh:1;
	unsigned int read_only:1;
	unsigned int check_folder:1;
};

struct _CamelImapFolderClass {
	CamelOfflineFolderClass parent_class;

	/* Virtual methods */	
	
};


/* public methods */
CamelFolder *camel_imap_folder_new (CamelStore *parent,
				    const char *folder_name,
				    const char *folder_dir,
				    CamelException *ex);

void camel_imap_folder_selected (CamelFolder *folder,
				 CamelImapResponse *response,
				 CamelException *ex);

void camel_imap_folder_changed (CamelFolder *folder, int exists,
				GArray *expunged, CamelException *ex);

CamelStream *camel_imap_folder_fetch_data (CamelImapFolder *imap_folder,
					   const char *uid,
					   const char *section_text,
					   gboolean cache_only,
					   CamelException *ex);
void
imap_append_resyncing (CamelFolder *folder, CamelMimeMessage *message,
		       const CamelMessageInfo *info, char **appended_uid,
		       CamelException *ex);
void
imap_transfer_resyncing (CamelFolder *source, GPtrArray *uids,
			 CamelFolder *dest, GPtrArray **transferred_uids,
			 gboolean delete_originals, CamelException *ex);
void
imap_expunge_uids_resyncing (CamelFolder *folder, GPtrArray *uids, CamelException *ex);

/* Standard Camel function */
CamelType camel_imap_folder_get_type (void);

G_END_DECLS

#endif /* CAMEL_IMAP_FOLDER_H */
