/*
 * e-source-calendar.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_CALENDAR_H
#define E_SOURCE_CALENDAR_H

/* These are trivial but important ESourceSelectable subclasses.
 * They identify an ESource as a calendar, memo list or task list. */

#include <libedataserver/e-source-selectable.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CALENDAR \
	(e_source_calendar_get_type ())
#define E_SOURCE_CALENDAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CALENDAR, ESourceCalendar))
#define E_SOURCE_CALENDAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CALENDAR, ESourceCalendarClass))
#define E_IS_SOURCE_CALENDAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CALENDAR))
#define E_IS_SOURCE_CALENDAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CALENDAR))
#define E_SOURCE_CALENDAR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CALENDAR, ESourceCalendarClass))

/* Standard GObject macros */
#define E_TYPE_SOURCE_MEMO_LIST \
	(e_source_memo_list_get_type ())
#define E_SOURCE_MEMO_LIST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_MEMO_LIST, ESourceMemoList))
#define E_SOURCE_MEMO_LIST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_MEMO_LIST, ESourceMemoListClass))
#define E_IS_SOURCE_MEMO_LIST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_MEMO_LIST))
#define E_IS_SOURCE_MEMO_LIST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_MEMO_LIST))
#define E_SOURCE_MEMO_LIST_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_MEMO_LIST, ESourceMemoListClass))

/* Standard GObject macros */
#define E_TYPE_SOURCE_TASK_LIST \
	(e_source_task_list_get_type ())
#define E_SOURCE_TASK_LIST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_TASK_LIST, ESourceTaskList))
#define E_SOURCE_TASK_LIST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_TASK_LIST, ESourceTaskListClass))
#define E_IS_SOURCE_TASK_LIST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_TASK_LIST))
#define E_IS_SOURCE_TASK_LIST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_TASK_LIST))
#define E_SOURCE_TASK_LIST_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_TASK_LIST, ESourceTaskListClass))

/**
 * E_SOURCE_EXTENSION_CALENDAR:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceCalendar.  This is also used as a group name in key files.
 *
 * Since: 3.6
 **/
#define E_SOURCE_EXTENSION_CALENDAR  "Calendar"

/**
 * E_SOURCE_EXTENSION_MEMO_LIST:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceMemoList.  This is also used as a group name in key files.
 *
 * Since: 3.6
 **/
#define E_SOURCE_EXTENSION_MEMO_LIST "Memo List"

/**
 * E_SOURCE_EXTENSION_TASK_LIST:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceTaskList.  This is also used as a group name in key files.
 *
 * Since: 3.6
 **/
#define E_SOURCE_EXTENSION_TASK_LIST "Task List"

G_BEGIN_DECLS

typedef struct _ESourceCalendar ESourceCalendar;
typedef struct _ESourceCalendarClass ESourceCalendarClass;
typedef struct _ESourceCalendarPrivate ESourceCalendarPrivate;

typedef struct _ESourceMemoList ESourceMemoList;
typedef struct _ESourceMemoListClass ESourceMemoListClass;
typedef struct _ESourceMemoListPrivate ESourceMemoListPrivate;

typedef struct _ESourceTaskList ESourceTaskList;
typedef struct _ESourceTaskListClass ESourceTaskListClass;
typedef struct _ESourceTaskListPrivate ESourceTaskListPrivate;

/**
 * ESourceCalendar:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ESourceCalendar {
	ESourceSelectable parent;
	ESourceCalendarPrivate *priv;
};

struct _ESourceCalendarClass {
	ESourceSelectableClass parent_class;
};

/**
 * ESourceMemoList:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ESourceMemoList {
	ESourceSelectable parent;
	ESourceMemoListPrivate *priv;
};

struct _ESourceMemoListClass {
	ESourceSelectableClass parent_class;
};

/**
 * ESourceTaskList:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ESourceTaskList {
	ESourceSelectable parent;
	ESourceTaskListPrivate *priv;
};

struct _ESourceTaskListClass {
	ESourceSelectableClass parent_class;
};

GType		e_source_calendar_get_type	(void);
GType		e_source_memo_list_get_type	(void);
GType		e_source_task_list_get_type	(void);

G_END_DECLS

#endif /* E_SOURCE_CALENDAR_H */
