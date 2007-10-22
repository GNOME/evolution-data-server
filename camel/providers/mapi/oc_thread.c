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
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <camel/camel-private.h>


#include "oc_thread.h"
#include "camel-mapi-folder.h"
#include "camel-mapi-store.h"
#include "camel-mapi-store-summary.h"
#include <camel/camel-string-utils.h>

int	oc_thread_fs_kill_all_thread(openchange_thread_t *);
void	*oc_thread_list_message(void *);

int oc_thread_initialize(openchange_thread_t **oc_thread, CamelFolder *folder)
{
	openchange_thread_t	*th;

	if (!folder) return (-1);
	if (*oc_thread == NULL) {
		if ((*oc_thread = malloc(sizeof(**oc_thread))) == NULL) {
			return (-1);
		}
	}
	th  = *oc_thread;
	memset(th, 0, sizeof(*th));

	if (pthread_mutex_init(&((**oc_thread).mutex_fs), NULL) == -1) {
		free(*oc_thread);
		return (-1);
	}

	if (pthread_mutex_init(&((**oc_thread).mutex_internal), NULL) == -1) {
		free(*oc_thread);
		return (-1);
	}

	pthread_mutex_unlock(&((**oc_thread).mutex_fs));
	pthread_mutex_unlock(&((**oc_thread).mutex_internal));
	th->folder = folder;
	th->thread = NULL;

	return (0);
}

void oc_thread_finalize(openchange_thread_t **oc_thread)
{
	openchange_thread_t	*th = *oc_thread;

	if (!th) return;
	free(th);
	*oc_thread = NULL;
}

int oc_thread_fs_lock(openchange_thread_t *th)
{
	return (pthread_mutex_lock(&(th->mutex_fs)));
}

int oc_thread_fs_try_lock(openchange_thread_t *th)
{
	return (pthread_mutex_trylock(&(th->mutex_fs)));
}

int oc_thread_fs_unlock(openchange_thread_t *th)
{
	return (pthread_mutex_unlock(&(th->mutex_fs)));
}

int oc_thread_i_lock(openchange_thread_t *th)
{
	return (pthread_mutex_lock(&(th->mutex_internal)));
}

int oc_thread_i_try_lock(openchange_thread_t *th)
{
	return (pthread_mutex_trylock(&(th->mutex_internal)));
}

int oc_thread_i_unlock(openchange_thread_t *th)
{
	return (pthread_mutex_unlock(&(th->mutex_internal)));
}


void oc_thread_remove_one(openchange_thread_t *th)
{
	oc_th_t	*tmp;

	oc_thread_i_lock(th);
	tmp = th->th;
	if (tmp)
		th->th = tmp->next;
	if (tmp && tmp->thread)
		free(tmp->thread);
	free(tmp);
	if (th->thread)
		free(th->thread);
	th->thread = NULL;
	oc_thread_i_unlock(th);
}

int oc_thread_fs_add_if_none(openchange_thread_t *th)
{
	oc_thread_i_lock(th);
	if (th->thread){
		oc_thread_i_unlock(th);
		return (0);
	}
	if (!th->thread){
		oc_thread_i_unlock(th);
		return (oc_thread_fs_add(th));
	}
	oc_thread_i_unlock(th);

	return (0);
}

int oc_thread_fs_add(openchange_thread_t *th)
{
	oc_thread_i_lock(th);
	if (th->thread != 0)
		return (-1);
	if (!(th->thread = malloc(sizeof(*(th->thread))))){
		oc_thread_i_unlock(th);
		return (-1);
	}
	th->live = 1;
	if (pthread_create(th->thread, NULL, oc_thread_list_message, th) != 0){
		oc_thread_i_unlock(th);
		return (-1);
	}
	
	oc_thread_i_unlock(th);

	return (0);
}

int oc_thread_fs_kill_all_thread(openchange_thread_t *th)
{
	if (!th)
		return (0);
	oc_thread_i_lock(th);
	th->live = 0;
	oc_thread_i_unlock(th);
	while(th->thread != NULL)
		usleep(10);

	return (0);
}

void *oc_thread_list_message(void *ptr)
{
	openchange_thread_t	*th = ptr;
	CamelFolder		*folder = th->folder;
	char			**uids = NULL;
	int			n_uids = 0;
	int			i_uids;
	int			status;
	oc_message_headers_t	**headers;
	CamelMessageInfoBase	*mi;
	int			i_modif;
	int			n_modif;
	int			update;
	CamelFolderChangeInfo	*changes;
	ocStoreSummary_t	*store_summary = oc_store_summary_from_folder(folder);

	headers = NULL;
	if (!folder || !folder->summary || !folder->parent_store)
		goto end;
	oc_thread_connect_lock();
	status = oc_inbox_list_message_ids(&uids, &n_uids, &headers, ((CamelOpenchangeFolder *)folder)->folder_id);
	oc_thread_connect_unlock();
	if (status == -1)
		return NULL;
	if (oc_thread_fs_lock(th) == -1)
		return NULL;
	n_modif = ((CamelOpenchangeFolder *)folder)->n_modified;
	for (i_uids = 0; i_uids < n_uids; i_uids++){
/* 		printf("%s uids : %s\n", __FUNCTION__, uids[i_uids]); */
		if (folder->summary && uids[i_uids] && (camel_folder_summary_uid(folder->summary, uids[i_uids]) == NULL)){
			mi = (CamelMessageInfoBase *)camel_message_info_new(folder->summary);
			mi->subject = (char *)camel_pstring_strdup (headers[i_uids]->subject);
			mi->from = (char *)camel_pstring_strdup(headers[i_uids]->from);
			mi->to = (char *)camel_pstring_strdup(headers[i_uids]->to);
			mi->cc = (char *)camel_pstring_strdup(headers[i_uids]->cc);
			mi->flags = headers[i_uids]->flags;


			mi->user_flags = NULL;
			mi->user_tags = NULL;
			mi->date_received = 0;
			mi->date_sent = headers[i_uids]->send;
			mi->content = NULL;
			mi->summary = folder->summary;
			mi->uid = g_strdup(uids[i_uids]);

			camel_folder_summary_add(folder->summary, (CamelMessageInfo *)mi);
			changes = camel_folder_change_info_new();
			if (mi->uid) {
				camel_folder_change_info_add_source(changes, mi->uid);
/* 				camel_folder_change_info_add_uid(changes, mi->uid); */
				camel_object_trigger_event(folder, "folder_changed", changes);
			}
			camel_folder_change_info_free(changes);
		} else if (folder->summary && uids[i_uids] && ((mi = (CamelMessageInfoBase *)camel_folder_summary_uid(folder->summary, uids[i_uids])) == NULL)) {
			for (i_modif = 0, update = 1; i_modif < n_modif; i_modif++) {
				if (strcmp(g_ptr_array_index(((CamelOpenchangeFolder *)folder)->modified_key, i_modif), uids[i_uids]) == 0) {
					update = 0;
					break ;
				}
			}
			if (update) {
				mi->flags = headers[i_uids]->flags;
				camel_folder_summary_touch(folder->summary);
				changes = camel_folder_change_info_new();
				camel_folder_change_info_add_source(changes, mi->uid);
				camel_object_trigger_event(folder, "folder_changed", changes);
				camel_folder_change_info_free(changes);
				camel_folder_summary_touch(folder->summary);
			}
		}
		oc_store_summary_recount_msg(store_summary, camel_folder_get_message_count(folder),
					     camel_folder_get_unread_message_count(folder),
					     ((CamelOpenchangeFolder *)folder)->folder_id);
		free(uids[i_uids]);
		oc_message_headers_release(headers[i_uids]);
		free(headers[i_uids]);
	}
	if (uids)
		free(uids);
/* 	camel_folder_summary_save(folder->summary); */
	folder->summary->build_content = FALSE;
	oc_thread_fs_unlock(th);


end :
	oc_thread_remove_one(th);
	return (NULL);
}

oc_connect_t *oc_thread_connect_init()
{
	static oc_connect_t	*connect = NULL;

	if (connect)
		return (connect);
	connect = malloc(sizeof(*connect));
	if (pthread_mutex_init(&(connect->mutex), NULL) == -1) {
		free(connect);
		connect = NULL;
		return (connect);
	}

	pthread_mutex_unlock(&(connect->mutex));
	return (connect);
}

int oc_thread_connect_lock()
{
	oc_connect_t	*connect;

	connect = oc_thread_connect_init();
	return (pthread_mutex_lock(&(connect->mutex)));
}

int	oc_thread_connect_unlock()
{
	oc_connect_t	*connect;

	connect = oc_thread_connect_init();
	return (pthread_mutex_unlock(&(connect->mutex)));
}
