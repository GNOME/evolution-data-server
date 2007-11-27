/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Srinivasa Ragavan <sragavan@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <libmapi/libmapi.h>
#include "exchange-mapi-folder.h"

static GSList *folder_list = NULL;
static GStaticRecMutex folder_lock = G_STATIC_REC_MUTEX_INIT;

#define LOCK()		printf("%s(%d):%s: lock(folder_lock) \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_lock(&folder_lock)
#define UNLOCK()	printf("%s(%d):%s: unlock(folder_lock) \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_unlock(&folder_lock)

static ExchangeMAPIFolderType
container_class_to_type (const char *type)
{
	ExchangeMAPIFolderType folder_type = MAPI_FOLDER_TYPE_UNKNOWN;;

	if (!strcmp (type, IPF_APPOINTMENT)) 
		folder_type = MAPI_FOLDER_TYPE_APPOINTMENT;
	else if (!strcmp (type, IPF_CONTACT))
		folder_type = MAPI_FOLDER_TYPE_CONTACT;
	else if (!strcmp (type, IPF_STICKYNOTE))
		folder_type = MAPI_FOLDER_TYPE_MEMO;
	else if (!strcmp (type, IPF_TASK))
		folder_type = MAPI_FOLDER_TYPE_TASK;
	else if (!strcmp (type, IPF_NOTE))
		folder_type = MAPI_FOLDER_TYPE_MAIL;
	/* Fixme : no definition for this is available in mapidef.h */
	else if (!strcmp (type, "IPF.Note.HomePage"))
		folder_type = MAPI_FOLDER_TYPE_NOTE_HOMEPAGE;
	else if (!strcmp (type, IPF_JOURNAL))
		folder_type = MAPI_FOLDER_TYPE_JOURNAL;

	return folder_type;
}

ExchangeMAPIFolder *
exchange_mapi_folder_new (const char *folder_name, const char *parent_folder_name, const char *container_class,
			  uint64_t folder_id, uint64_t parent_folder_id, uint32_t child_count, uint32_t unread_count, uint32_t total)
{
	ExchangeMAPIFolder *folder;

	folder = g_new (ExchangeMAPIFolder, 1);
	folder->folder_name = g_strdup (folder_name);
	folder->parent_folder_name = parent_folder_name ? g_strdup (parent_folder_name) : NULL;
	folder->container_class = container_class_to_type (container_class);
	folder->folder_id = folder_id;
	folder->parent_folder_id = parent_folder_id;
	folder->child_count = child_count;
	folder->unread_count = unread_count;
	folder->total = total;

	return folder;
}

ExchangeMAPIFolderType
exchange_mapi_container_class (char *type)
{
	return container_class_to_type (type);
}

const gchar*
exchange_mapi_folder_get_name (ExchangeMAPIFolder *folder)
{
	return folder->folder_name;
}

const guint64
exchange_mapi_folder_get_fid (ExchangeMAPIFolder *folder)
{
	return folder->folder_id;
}

const guint64
exchange_mapi_folder_get_parent_id (ExchangeMAPIFolder *folder)
{
	return folder->parent_folder_id;
}

gboolean
exchange_mapi_folder_is_root (ExchangeMAPIFolder *folder)
{
	return (folder->parent_folder_id == NULL);
}

ExchangeMAPIFolderType
exchange_mapi_folder_get_type (ExchangeMAPIFolder *folder)
{
	return folder->container_class;
}

const guint32
exchange_mapi_folder_get_unread_count (ExchangeMAPIFolder *folder)
{
	return folder->unread_count;
}

const guint32
exchange_mapi_folder_get_total_count (ExchangeMAPIFolder *folder)
{
	return folder->total;
}


GSList *
exchange_mapi_peek_folder_list ()
{
	if (folder_list) 
		return folder_list;
	LOCK ();
	if (exchange_mapi_get_folders_list (&folder_list)) {
		printf ("Get folders list call is sucessful \n\a");
	}	
	UNLOCK ();
	return folder_list;
}

ExchangeMAPIFolder *
exchange_mapi_folder_get_folder (uint64_t fid)
{
	GSList *tmp = folder_list;

	if (!folder_list)
		exchange_mapi_peek_folder_list ();
	
	tmp = folder_list;
	while (tmp) {
		ExchangeMAPIFolder * folder = tmp->data;
		printf("%016llx %016llx\n", folder->folder_id, fid);
		if (folder->folder_id == fid)
			return folder;
		tmp=tmp->next;
	}

	return NULL;
}
