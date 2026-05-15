/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Federico Mena-Quintero <federico@ximian.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 */

#include "evolution-data-server-config.h"

#include "e-cal-backend-file-todos.h"

G_DEFINE_TYPE (
	ECalBackendFileTodos,
	e_cal_backend_file_todos,
	E_TYPE_CAL_BACKEND_FILE)

static void
e_cal_backend_file_todos_class_init (ECalBackendFileTodosClass *class)
{
}

static void
e_cal_backend_file_todos_init (ECalBackendFileTodos *cbfile)
{
	e_cal_backend_file_set_file_name (
		E_CAL_BACKEND_FILE (cbfile), "tasks.ics");
}

