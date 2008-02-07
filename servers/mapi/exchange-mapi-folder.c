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


#include <glib.h>
#include "exchange-mapi-connection.h"
#include "exchange-mapi-folder.h"

static GSList *folder_list = NULL;

/* we use a static mutex - even the same thread *may not* use the static vars concurrently */
static GStaticMutex folder_lock = G_STATIC_MUTEX_INIT;

#define LOCK() 		g_message("%s(%d): %s: lock(folder_lock)", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_mutex_lock(&folder_lock)
#define UNLOCK() 	g_message("%s(%d): %s: unlock(folder_lock)", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_mutex_unlock(&folder_lock)
#define d(x) x

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
exchange_mapi_folder_new (const char *folder_name, const char *parent_folder_name, const char *container_class, ExchangeMAPIFolderCategory category, uint64_t folder_id, uint64_t parent_folder_id, uint32_t child_count, uint32_t unread_count, uint32_t total)
{
	ExchangeMAPIFolder *folder;

	folder = g_new0 (ExchangeMAPIFolder, 1);
	folder->is_default = FALSE;
	folder->folder_name = g_strdup (folder_name);
	folder->parent_folder_name = parent_folder_name ? g_strdup (parent_folder_name) : NULL;
	folder->container_class = container_class_to_type (container_class);
	folder->folder_id = folder_id;
	folder->parent_folder_id = parent_folder_id;
	folder->child_count = child_count;
	folder->unread_count = unread_count;
	folder->total = total;
	folder->category = category;

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

guint64
exchange_mapi_folder_get_fid (ExchangeMAPIFolder *folder)
{
	return folder->folder_id;
}

guint64
exchange_mapi_folder_get_parent_id (ExchangeMAPIFolder *folder)
{
	return folder->parent_folder_id;
}

gboolean
exchange_mapi_folder_is_root (ExchangeMAPIFolder *folder)
{
	return (folder->parent_folder_id == 0);
}

ExchangeMAPIFolderType
exchange_mapi_folder_get_type (ExchangeMAPIFolder *folder)
{
	return folder->container_class;
}

guint32
exchange_mapi_folder_get_unread_count (ExchangeMAPIFolder *folder)
{
	return folder->unread_count;
}

guint32
exchange_mapi_folder_get_total_count (ExchangeMAPIFolder *folder)
{
	return folder->total;
}

GSList *
exchange_mapi_peek_folder_list ()
{
	LOCK ();
	if (!folder_list && !exchange_mapi_get_folders_list (&folder_list))
		g_warning ("Get folders list call failed \n\a");
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
		g_print ("%016llX %016llX\n", folder->folder_id, fid);
		if (folder->folder_id == fid)
			return folder;
		tmp=tmp->next;
	}

	return NULL;
}

void
exchange_mapi_folder_list_free ()
{
	GSList *tmp = folder_list;
	LOCK ();
	while (tmp) {
		ExchangeMAPIFolder *data = tmp->data;
		g_free (data);
		data = NULL;
		tmp = tmp->next;
	}
	g_slist_free (folder_list);
	folder_list = NULL;
	UNLOCK ();

	d(g_print("Folder list freed\n"));
	return;
}

void
exchange_mapi_folder_list_add (ExchangeMAPIFolder *folder)
{
	GSList *tmp = folder_list;
	LOCK ();
	while (tmp) {
		ExchangeMAPIFolder *data = tmp->data;
		if (data->folder_id == folder->parent_folder_id) {
			/* Insert it here */
			d(g_print ("Inserted below the parent\n"));
			folder_list = g_slist_insert_before (folder_list, tmp->next, folder);
			UNLOCK ();
			return;
		}
		tmp = tmp->next;
	}

	/* Append at the end */
	folder_list = g_slist_append (folder_list, folder);
	UNLOCK ();
	d(g_print("Appended folder at the end\n"));
}
