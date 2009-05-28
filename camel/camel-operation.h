/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <NotZed@Ximian.com>
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

#ifndef CAMEL_OPERATION_H
#define CAMEL_OPERATION_H 1

#include <glib.h>

G_BEGIN_DECLS

/* cancellation helper stuff, not yet finalised */

typedef struct _CamelOperation CamelOperation;

typedef void (*CamelOperationStatusFunc)(struct _CamelOperation *op, const gchar *what, gint pc, gpointer data);

typedef enum _camel_operation_status_t {
	CAMEL_OPERATION_START = -1,
	CAMEL_OPERATION_END = -2
} camel_operation_status_t;

/* main thread functions */
CamelOperation *camel_operation_new(CamelOperationStatusFunc status, gpointer status_data);
void camel_operation_mute(CamelOperation *cc);
void camel_operation_ref(CamelOperation *cc);
void camel_operation_unref(CamelOperation *cc);
void camel_operation_cancel(CamelOperation *cc);
void camel_operation_uncancel(CamelOperation *cc);
/* subthread functions */
CamelOperation *camel_operation_register(CamelOperation *cc);
void camel_operation_unregister (CamelOperation *cc);

/* called internally by camel, for the current thread */
void camel_operation_cancel_block(CamelOperation *cc);
void camel_operation_cancel_unblock(CamelOperation *cc);
gint camel_operation_cancel_check(CamelOperation *cc);
gint camel_operation_cancel_fd(CamelOperation *cc);
#ifdef HAVE_NSS
struct PRFileDesc *camel_operation_cancel_prfd(CamelOperation *cc);
#endif
/* return the registered operation for this thread, if there is one */
CamelOperation *camel_operation_registered(void);

void camel_operation_start(CamelOperation *cc, const gchar *what, ...);
void camel_operation_start_transient(CamelOperation *cc, const gchar *what, ...);
void camel_operation_progress(CamelOperation *cc, gint pc);
void camel_operation_progress_count(CamelOperation *cc, gint sofar);
void camel_operation_end(CamelOperation *cc);

G_END_DECLS

#endif /* CAMEL_OPERATION_H */
