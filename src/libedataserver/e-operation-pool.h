/*
 * SPDX-FileCopyrightText: (C) 2011 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_OPERATION_POOL_H
#define E_OPERATION_POOL_H

#include <gio/gio.h>

/**
 * EOperationPool:
 * Since: 3.2
 **/
typedef struct _EOperationPool EOperationPool;

EOperationPool *e_operation_pool_new (guint max_threads, GFunc thread_func, gpointer user_data);
void		e_operation_pool_free (EOperationPool *pool);
guint32		e_operation_pool_reserve_opid (EOperationPool *pool);
void		e_operation_pool_release_opid (EOperationPool *pool, guint32 opid);
void		e_operation_pool_push (EOperationPool *pool, gpointer opdata);

#endif /* E_OPERATION_POOL_H */
