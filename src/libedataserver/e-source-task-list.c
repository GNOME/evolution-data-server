/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-source-task-list
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for a task list
 *
 * The #ESourceCalendar extension identifies the #ESource as a task list.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceCalendar *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);
 * ]|
 **/

#include "e-source-task-list.h"

#include <libedataserver/e-data-server-util.h>

G_DEFINE_TYPE (
	ESourceTaskList,
	e_source_task_list,
	E_TYPE_SOURCE_SELECTABLE)

static void
e_source_task_list_class_init (ESourceTaskListClass *class)
{
	ESourceExtensionClass *extension_class;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_TASK_LIST;
}

static void
e_source_task_list_init (ESourceTaskList *extension)
{
}
