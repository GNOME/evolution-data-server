/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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

#include "oc.h"
#include "camel-mapi-store.h"
#include "camel-mapi-store-summary.h"
#include "oc_thread.h"
#include <glib.h>
#include <camel/camel-utf8.h>
/* get the default fid and set their flags */
void	OCSS_init_fid_array(ocStoreSummary_t *summary)
{
	mapi_object_t		obj_store;
	uint64_t		id_box;
	TALLOC_CTX		*mem_ctx;
	enum MAPISTATUS		retval;

	oc_thread_connect_lock();
	if (m_oc_initialize() == -1) {
		oc_thread_connect_unlock();
		return ;
	}
	mem_ctx = talloc_init("oc_get_folder_info");
	mapi_object_init(&obj_store);
	/* session::OpenMsgStore() */
	retval = OpenMsgStore(&obj_store);
/* 	mapi_errstr("OpenMsgStore", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) return ;

	summary->fid = g_malloc0(5 * sizeof(*(summary->fid)));
	summary->fid_flags = g_malloc0(5 * sizeof(*(summary->fid_flags)));

	/* setting inbox */
	retval = GetDefaultFolder(&obj_store, &id_box, olFolderInbox);
/* 	mapi_errstr("GetDefaultFolder", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) return ;
	summary->fid[0] = g_strdup(folder_mapi_ids_to_uid(id_box));
	summary->fid_flags[0] = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;
	/* setting outbox */
	retval = GetDefaultFolder(&obj_store, &id_box, olFolderOutbox);
/* 	mapi_errstr("GetDefaultFolder", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) return ;
	summary->fid[1] = g_strdup(folder_mapi_ids_to_uid(id_box));
	summary->fid_flags[1] = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_OUTBOX;
	/* setting sentbox */
	retval = GetDefaultFolder(&obj_store, &id_box, olFolderSentMail);
/* 	mapi_errstr("GetDefaultFolder", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) return ;
	summary->fid[2] = g_strdup(folder_mapi_ids_to_uid(id_box));
	summary->fid_flags[2] = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_SENT;
	/* setting trash */
	retval = GetDefaultFolder(&obj_store, &id_box, olFolderDeletedItems);
/* 	mapi_errstr("GetDefaultFolder", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) return ;
	summary->fid[3] = g_strdup(folder_mapi_ids_to_uid(id_box));
	summary->fid_flags[3] = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TRASH | CAMEL_FOLDER_VTRASH;


	oc_thread_connect_unlock();
}

/* return a new instancen, which is initialize */
ocStoreSummary_t	*oc_store_summary_new(CamelURL *url)
{
	static ocStoreSummary_t	*summary = NULL;

	if (summary != NULL)
		return (summary);
	summary = malloc(sizeof(*summary));
	if ((summary->mutex = malloc(sizeof(*(summary->mutex)))) == NULL)
		return (NULL);
	if (pthread_mutex_init(summary->mutex, NULL) == -1) {
		free(summary->mutex);
		free(summary);
		return (NULL);
	}
	summary->fid = NULL;
	summary->fid_flags = NULL;
	summary->fi_array = g_ptr_array_new();
	summary->fi = NULL;
	summary->fi_hash_name = g_hash_table_new(g_str_hash, g_str_equal);
	summary->fi_hash_fid = g_hash_table_new(g_str_hash, g_str_equal);
/* 	summary->summary = camel_store_summary_new(); */
	OCSS_init_fid_array(summary);
	summary->url = url;
	pthread_mutex_unlock(summary->mutex);
	return (summary);
}

/* release the summary but not the content(FolderInfo)*/
void	oc_store_summary_release(ocStoreSummary_t *summary)
{
	if (!summary) return ;
	g_free(summary->fid);
	g_free(summary->fid_flags);
	g_ptr_array_free(summary->fi_array, FALSE);
	g_hash_table_destroy(summary->fi_hash_name);
	g_hash_table_destroy(summary->fi_hash_fid);
/* 	camel_object_unref(summary->summary); */
}

/* add the folder_info in the summary */
int	oc_store_summary_add_info(ocStoreSummary_t *summary, CamelOpenchangeFolderInfo *folder_info)
{
	CamelFolderInfo *fi = (CamelFolderInfo *)folder_info;
	CamelStoreInfo *si;

	if (!summary) return (-1);
	pthread_mutex_lock(summary->mutex);
	if (g_hash_table_lookup(summary->fi_hash_fid, folder_info->fid) != NULL)
		goto end;;
	si = g_malloc0(sizeof(*si));
	si->path = fi->full_name;
	si->flags = fi->flags;
	si->unread = fi->unread;
	si->total = fi->total;
	si->uri = fi->uri;
	g_hash_table_insert(summary->fi_hash_fid, folder_info->fid, folder_info);
	g_hash_table_insert(summary->fi_hash_name, fi->name, folder_info);
	g_ptr_array_add(summary->fi_array, folder_info);
end :
	pthread_mutex_unlock(summary->mutex);
	return (0);
}

/* delete the folder_info in the summary */
int	oc_store_summary_del_info(ocStoreSummary_t *summary, CamelOpenchangeFolderInfo *folder_info)
{
	CamelFolderInfo *fi = (CamelFolderInfo *)folder_info;

	if (!summary) return (-1);
	pthread_mutex_lock(summary->mutex);
	if (g_hash_table_lookup(summary->fi_hash_fid, folder_info->fid) != NULL) {
		pthread_mutex_unlock(summary->mutex);
		return (-1);
	}
	g_ptr_array_remove(summary->fi_array, folder_info);
	g_hash_table_remove(summary->fi_hash_name, fi->full_name);
	g_hash_table_remove(summary->fi_hash_fid, folder_info->fid);
	pthread_mutex_unlock(summary->mutex);
	return (0);
}

/* delete all FolderInfo and their content */
int	oc_store_summary_del_all_info(ocStoreSummary_t *summary)
{
	int	retval;

	if (!summary) return (-1);
	while (summary->fi_array->len > 0) {
		retval = oc_store_summary_del_info(summary, g_ptr_array_index(summary->fi_array, 0));
		if (retval != 0) return retval;
	}
	return (0);
}

/* delete the folder_info(from folder id) in the summary */
int	oc_store_summary_del_info_by_fid(ocStoreSummary_t *summary, char *folder_id)
{
	CamelOpenchangeFolderInfo *folder_info;

	if (!summary) return (-1);
	pthread_mutex_lock(summary->mutex);
	if ((folder_info = g_hash_table_lookup(summary->fi_hash_fid, folder_id)) == NULL) {
		pthread_mutex_unlock(summary->mutex);
		return (-1);
	}
	pthread_mutex_unlock(summary->mutex);
	return (oc_store_summary_del_info(summary, folder_info));
}


/* return the folder_info by folder id */
CamelOpenchangeFolderInfo	*oc_store_summary_get_by_fid(ocStoreSummary_t *summary, char *folder_id)
{
	CamelOpenchangeFolderInfo *folder_info;

	if (!summary) return (NULL);
	pthread_mutex_lock(summary->mutex);
	folder_info = g_hash_table_lookup(summary->fi_hash_fid, folder_id);
	pthread_mutex_unlock(summary->mutex);
	return (folder_info);
}

/* return the folder_info by folder fullname */
CamelOpenchangeFolderInfo	*oc_store_summary_get_by_fullname(ocStoreSummary_t *summary, char *folder_fullname)
{
	CamelOpenchangeFolderInfo *folder_info;

	if (!summary) return (NULL);
	if (!folder_fullname || strlen(folder_fullname) == 0)
		return summary->fi;
	pthread_mutex_lock(summary->mutex);
	folder_info = g_hash_table_lookup(summary->fi_hash_name, folder_fullname);
	pthread_mutex_unlock(summary->mutex);
	return (folder_info);
}

/* return the store summary from a folder, NULL if cannot possible */
ocStoreSummary_t	*oc_store_summary_from_folder(CamelFolder *folder)
{
	if (!folder || !(folder->parent_store) || !(((CamelOpenchangeStore *)folder->parent_store)->summary))
		return (NULL);
	return (((CamelOpenchangeStore *)folder->parent_store)->summary);
}

/* set the filename in the summary */
int	oc_store_summary_set_filename(ocStoreSummary_t *summary, char *name)
{
	if (!summary) return (-1);
/* 	camel_store_summary_set_filename(summary->summary, name); */
	return (0);
}

/* load the summary */
int	oc_store_summary_load(ocStoreSummary_t *summary)
{
	if (!summary) return (-1);
	return (0);
/* 	return (camel_store_summary_load(summary->summary)); */
}

/* save the summary */
int	oc_store_summary_save(ocStoreSummary_t *summary)
{
	if (!summary) return (-1);
	return (0);
/* 	return (camel_store_summary_save(summary->summary)); */
}

/* set the signal when content of summary is modified without the std fonction */
int	oc_store_summary_touch(ocStoreSummary_t *summary)
{
	if (!summary) return (-1);
/* 	camel_store_summary_touch(summary->summary); */
	return (0);
}

unsigned int	get_folder_flags(ocStoreSummary_t *summary, char *fid)
{
	int	i;

	if (!fid || !summary) return (0);
	for (i = 0; i < 4; i++) {
		if (strcmp(summary->fid[i], fid) == 0) {
			return (summary->fid_flags[i]);
		}
	}
	if (i >= 4) return (0);
	return (summary->fid_flags[i]);
}

CamelOpenchangeFolderInfo *get_child_folders(TALLOC_CTX *mem_ctx, mapi_object_t *parent, mapi_id_t folder_id, int count, ocStoreSummary_t *summary, char *path)
{
	enum MAPISTATUS		retval;
	mapi_object_t		obj_folder;
	mapi_object_t		obj_htable;
	struct SPropTagArray	*SPropTagArray;
	struct SRowSet		rowset;
	const char	       	*name;
	const char	       	*comment;
	uint32_t	       	*total;
	uint32_t	       	*unread;
	uint32_t		*child;
	uint32_t		index;
	uint64_t		*fid;
	CamelFolderInfo		*tmp =NULL;
	CamelFolderInfo		*fi = NULL;
	CamelOpenchangeFolderInfo		*folder_info = NULL;
	CamelOpenchangeFolderInfo		*begin = NULL;
	char	*str_fid;
	CamelURL	*url;
	char	*new_name = NULL;

	mapi_object_init(&obj_folder);
	retval = OpenFolder(parent, folder_id, &obj_folder);
	if (retval != MAPI_E_SUCCESS) return FALSE;

	mapi_object_init(&obj_htable);
	retval = GetHierarchyTable(&obj_folder, &obj_htable);
	if (retval != MAPI_E_SUCCESS) return FALSE;

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x6,
					  PR_DISPLAY_NAME,
					  PR_FID,
					  PR_COMMENT,
					  PR_CONTENT_UNREAD,
					  PR_CONTENT_COUNT,
					  PR_FOLDER_CHILD_COUNT);
	retval = SetColumns(&obj_htable, SPropTagArray);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS) return FALSE;
	while ((retval = QueryRows(&obj_htable, 0x32, TBL_ADVANCE, &rowset) != MAPI_E_NOT_FOUND) && rowset.cRows) {
		for (index = 0; index < rowset.cRows; index++) {
			name = (const char*)find_SPropValue_data(&rowset.aRow[index], PR_DISPLAY_NAME/* _UNICODE */);
			comment = (const char *)find_SPropValue_data(&rowset.aRow[index], PR_COMMENT);
			total = (uint32_t *)find_SPropValue_data(&rowset.aRow[index], PR_CONTENT_COUNT);
			unread = (uint32_t *)find_SPropValue_data(&rowset.aRow[index], PR_CONTENT_UNREAD);
			child = (uint32_t *)find_SPropValue_data(&rowset.aRow[index], PR_FOLDER_CHILD_COUNT);
			/* create foldeR_info */
			tmp = g_malloc0(sizeof(CamelOpenchangeFolderInfo));
			((CamelOpenchangeFolderInfo *)tmp)->file_name = g_strdup(name);
			tmp->parent = (CamelFolderInfo *)oc_store_summary_get_by_fullname(summary, path);
			if (total) tmp->total = *total;
			if (unread) tmp->unread = *unread;
			new_name = windows_to_utf8(mem_ctx, name);
			tmp->name = g_strdup(new_name);
			MAPIFreeBuffer(new_name);
			if (strlen(path) != 0)
				tmp->full_name = camel_url_encode((const char*)(g_strdup_printf("%s/%s", path, tmp->name)), NULL);
			else
				tmp->full_name = camel_url_encode((const char*)(g_strdup(tmp->name)), NULL);
			fid = (uint64_t *)find_SPropValue_data(&rowset.aRow[index], PR_FID);
			if (*child) {
				tmp->child = (CamelFolderInfo *)get_child_folders(mem_ctx, &obj_folder, *fid, count + 1, summary, g_strdup_printf("%s/%s", path, tmp->name));
			}
			str_fid = g_strdup(folder_mapi_ids_to_uid(*fid));
			tmp->flags = get_folder_flags(summary, str_fid);
			if (tmp->flags) {
				if (fi) {
					if (!begin)
						begin = folder_info;
					fi->next = tmp;
				}
				else if (!begin)
					begin = (CamelOpenchangeFolderInfo *)tmp;
				fi = tmp;
				folder_info = (CamelOpenchangeFolderInfo *)fi;
				folder_info->fid = str_fid;
				url = camel_url_copy(summary->url);
				camel_url_set_path(url, g_strdup_printf("/%s", tmp->full_name));
				fi->uri = camel_url_to_string(url, CAMEL_URL_HIDE_PARAMS);
				folder_info->file_name = g_strdup(tmp->name);
				oc_store_summary_add_info(summary, folder_info);
			}
		}
	}
	mapi_object_release(&obj_folder);
	return (begin);
}

/* refresh the summary */
int	oc_store_summary_update_info(ocStoreSummary_t *summary)
{
	enum MAPISTATUS			retval;
	mapi_id_t			id_mailbox;
	mapi_object_t			obj_store;
	struct SPropTagArray		*SPropTagArray;
	struct SPropValue		*lpProps;
	uint32_t			cValues;
	TALLOC_CTX		*mem_ctx = NULL;

	if (!summary) return (-1);
	pthread_mutex_lock(summary->mutex);
	oc_thread_connect_lock();
	if (m_oc_initialize() == -1)
		goto error;
	mem_ctx = talloc_init("oc_store_summary_update_info");
	mapi_object_init(&obj_store);

	retval = OpenMsgStore(&obj_store);
/* 	mapi_errstr("OpenMsgStore", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) goto error;
	/* Retrieve the mailbox folder name */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x1, PR_DISPLAY_NAME);
	retval = GetProps(&obj_store, SPropTagArray, &lpProps, &cValues);
/* 	mapi_errstr("GetProps", GetLastError()); */
	if (retval != MAPI_E_SUCCESS) goto error;

	/* Prepare the directory listing */
	retval = GetDefaultFolder(&obj_store, &id_mailbox, olFolderTopInformationStore);
	if (retval != MAPI_E_SUCCESS) goto error;

	pthread_mutex_unlock(summary->mutex);
	if (!summary->fi)
		summary->fi = get_child_folders(mem_ctx, &obj_store, id_mailbox, 0, summary, "");
	mapi_object_release(&obj_store);
	oc_thread_connect_unlock();
	talloc_free(mem_ctx);
	return (0);
error:
	if (mem_ctx) talloc_free(mem_ctx);
	oc_thread_connect_unlock();
	pthread_mutex_unlock(summary->mutex);
	return (-1);
}

void	oc_store_summary_recount_msg(ocStoreSummary_t *summary, int total, int unread, char *fid)
{
	CamelOpenchangeFolderInfo	*fi;
	CamelFolderInfo			*folder_info;

	if (!summary || !fid) return ;
	if (!(fi = oc_store_summary_get_by_fid(summary, fid))) return ;
	folder_info = (CamelFolderInfo *)fi;
	folder_info->total = total;
	folder_info->unread = unread;
	oc_store_summary_touch(summary);
}
