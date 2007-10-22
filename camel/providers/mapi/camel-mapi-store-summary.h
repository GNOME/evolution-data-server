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

#ifndef __OPENCHANGE_STORE_SUMMARY__
#define __OPENCHANGE_STORE_SUMMARY__

#include <camel/camel-store-summary.h>
#include "camel-mapi-store.h"
#include <pthread.h>

typedef struct {
	CamelFolderInfo		fi;
	char			*fid;
	char			*file_name;
} CamelOpenchangeFolderInfo;

/* WRAPPER to the CamelStoreSummary */
typedef struct {
	CamelURL		*url;
	CamelOpenchangeFolderInfo *fi;
/* 	CamelStoreSummary	*summary; */
	char			**fid;		/* contain the default_fid */
	unsigned int		*fid_flags;	/* contain the default flags (index = index of summary->fid)*/
	GPtrArray		*fi_array;	/* contain openchange_folder_info */
	GHashTable		*fi_hash_fid;	/* key = char *folder_id */
 	GHashTable		*fi_hash_name;	/* key = char *folder_full_name */
	pthread_mutex_t		*mutex;		/* mutex using in internal */
} ocStoreSummary_t;

ocStoreSummary_t	*oc_store_summary_new(CamelURL *url); /* return a new instancen, which is initialize */
void			oc_store_summary_release(ocStoreSummary_t *summary);/* release the summary but not the content(FolderInfo)*/
int			oc_store_summary_add_info(ocStoreSummary_t *summary, CamelOpenchangeFolderInfo *folder_info); /* add the folder_info in the summary */
int			oc_store_summary_del_info(ocStoreSummary_t *summary, CamelOpenchangeFolderInfo *folder_info); /* delete the folder_info in the summary */
int			oc_store_summary_del_all_info(ocStoreSummary_t *summary); /* delete all FolderInfo and their content */
int			oc_store_summary_del_info_by_fid(ocStoreSummary_t *summary, char *folder_id); /* delete the folder_info(from folder id) in the summary */
CamelOpenchangeFolderInfo		*oc_store_summary_get_by_fid(ocStoreSummary_t *summary, char *folder_id); /* return the folder_info by folder id */
CamelOpenchangeFolderInfo		*oc_store_summary_get_by_name(ocStoreSummary_t *summary, char *folder_name); /* return the folder_info by folder name */
CamelOpenchangeFolderInfo		*oc_store_summary_get_by_fullname(ocStoreSummary_t *summary, char *folder_fullname); /* return the folder_info by folder fullname */
int			oc_store_summary_update_info(ocStoreSummary_t *summary); /* refresh the summary */
ocStoreSummary_t	*oc_store_summary_from_folder(CamelFolder *folder); /* return the store summary from a folder, NULL if cannot possible */
int			oc_store_summary_set_filename(ocStoreSummary_t *summary, char *name);
int			oc_store_summary_load(ocStoreSummary_t *summary); /* load the summary */
int			oc_store_summary_save(ocStoreSummary_t *summary); /* save the summary */
int			oc_store_summary_touch(ocStoreSummary_t *summary);
void			oc_store_summary_recount_msg(ocStoreSummary_t *summary, int total, int unread, char *fid);
char *utf8tolinux(TALLOC_CTX *mem_ctx, const char *wstring);
#endif /* !__OPENCHANGE_STORE_SUMMARY__ */
