/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Federico Mena-Quintero <federico@ximian.com>
 */

#ifndef E_CAL_BACKEND_FILE_TODOS_H
#define E_CAL_BACKEND_FILE_TODOS_H

#include "e-cal-backend-file.h"

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_FILE_TODOS \
	(e_cal_backend_file_todos_get_type ())
#define E_CAL_BACKEND_FILE_TODOS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_FILE_TODOS, ECalBackendFileTodos))
#define E_CAL_BACKEND_FILE_TODOS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_FILE_TODOS, ECalBackendFileTodosClass))
#define E_IS_CAL_BACKEND_FILE_TODOS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_FILE_TODOS))
#define E_IS_CAL_BACKEND_FILE_TODOS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_FILE_TODOS))
#define E_CAL_BACKEND_FILE_TODOS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_FILE_TODOS, ECalBackendFileTodosClass))

G_BEGIN_DECLS

typedef ECalBackendFile ECalBackendFileTodos;
typedef ECalBackendFileClass ECalBackendFileTodosClass;

GType		e_cal_backend_file_todos_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_FILE_TODOS_H */
