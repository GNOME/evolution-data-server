/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MAPI_FOLDER_H__
#define __MAPI_FOLDER_H__


#include <camel/camel-folder.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-offline-journal.h>
#include <libmapi/libmapi.h>
#include <exchange-mapi-connection.h>

#define PATH_FOLDER ".evolution/mail/mapi"

#define CAMEL_MAPI_FOLDER_TYPE     (camel_mapi_folder_get_type ())
#define CAMEL_MAPI_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MAPI_FOLDER_TYPE, CamelMapiFolder))
#define CAMEL_MAPI_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MAPI_FOLDER_TYPE, CamelMapiFolderClass))
#define CAMEL_IS_MAPI_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MAPI_FOLDER_TYPE))

/**
 * DATA STRUCTURES
 */

G_BEGIN_DECLS

typedef enum  {
	MAPI_ITEM_TYPE_MAIL=1,
	MAPI_ITEM_TYPE_APPOINTMENT,
	MAPI_ITEM_TYPE_CONTACT,
	MAPI_ITEM_TYPE_JOURNAL,
	MAPI_ITEM_TYPE_TASK
} MapiItemType;

typedef enum  {
	PART_TYPE_PLAIN_TEXT=1,
	PART_TYPE_TEXT_HTML
} MapiItemPartType;

typedef struct {
	gchar *subject;
	gchar *from;
	gchar *to;
	gchar *cc;
	gchar *bcc;

	int flags;
	glong size;
	time_t recieved_time;
	time_t send_time;
} MapiItemHeader;

typedef struct {
	//Temp : PLAIN
	gchar *body_plain_text;
	GSList *body_parts;
} MapiItemMessage;

typedef struct {
	gchar *filename;
	gchar *description;
	GByteArray *attach;
} MapiItemAttachment;

typedef struct  {
	mapi_id_t fid;
	mapi_id_t mid;

	MapiItemHeader header;
	MapiItemMessage msg;

	GSList *attachments;
	GSList *generic_streams;
}MapiItem;


typedef struct  _CamelMapiFolder CamelMapiFolder;
typedef struct  _CamelMapiFolderClass CamelMapiFolderClass;

struct _CamelMapiFolder {
	CamelOfflineFolder parent_object;

	struct _CamelMapiFolderPrivate *priv;

	CamelFolderSearch *search;

	CamelOfflineJournal *journal;
	CamelDataCache *cache;

	unsigned int need_rescan:1;
	unsigned int need_refresh:1;
	unsigned int read_only:1;
};

struct _CamelMapiFolderClass {
	CamelOfflineFolderClass parent_class;

	/* Virtual methods */	
	
} ;


/* Standard Camel function */
CamelType camel_mapi_folder_get_type (void);

/* implemented */
CamelFolder *
camel_mapi_folder_new(CamelStore *store, const char *folder_name, const char *folder_dir, guint32 flags, CamelException *ex);

void mapi_update_summary ( CamelFolder *folder, GList *item_list,CamelException *ex) ;
void mapi_refresh_folder(CamelFolder *folder, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_GROUPWISE_FOLDER_H */
