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

#ifndef _OC_TRHEAD_H_
#define _OC_TRHEAD_H_

#include <pthread.h>
#include <stdlib.h>
#include <camel/camel-folder.h>

typedef struct oc_th_s {
	struct oc_th_s *next;
	pthread_t *thread;
	int	id;
	int		live;
}  oc_th_t;

/*
** openchange_mutex_t is use to have an access to all data
** with mutex protection
*/
typedef struct openchange_thread_s{
/*    struct openchange_thread_s *next; */
	CamelFolder *folder;
	pthread_mutex_t mutex_fs;
	pthread_mutex_t mutex_internal;
	oc_th_t	*th;
	int		live;
	pthread_t *thread;
} openchange_thread_t;

typedef struct {
	pthread_mutex_t mutex;
} oc_connect_t;


int	oc_thread_initialize(openchange_thread_t**, CamelFolder *);
void	oc_thread_finalize(openchange_thread_t**);
int	oc_thread_fs_lock(openchange_thread_t*);/* lock the mutex for summary */
int	oc_thread_fs_try_lock(openchange_thread_t*);/* try to lock the mutex for summary */
int	oc_thread_fs_unlock(openchange_thread_t*);/* unlock the mutex for summary */
int	oc_thread_fs_add_if_none(openchange_thread_t*); /* add a new thread if none exists*/
int	oc_thread_fs_add(openchange_thread_t*); /* add a new thread */
int	oc_thread_fs_kill_all_thread(openchange_thread_t *th); /* killed all thread include in th */

int	oc_thread_i_lock(openchange_thread_t*);/* lock internal mutex*/
int	oc_thread_i_unlock(openchange_thread_t*);/* unlock internal mutex */

int	oc_thread_connect_lock(void);
int	oc_thread_connect_unlock(void);

#endif /* !_OC_TRHEAD_H_ */
