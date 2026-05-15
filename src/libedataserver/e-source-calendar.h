/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_CALENDAR_H
#define E_SOURCE_CALENDAR_H

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
/**
 * E_SOURCE_EXTENSION_CALENDAR:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceCalendar.  This is also used as a group name in key files.
 *
 * Since: 3.6
 **/
#define E_SOURCE_EXTENSION_CALENDAR  "Calendar"

G_BEGIN_DECLS

typedef struct _ESourceCalendar ESourceCalendar;
typedef struct _ESourceCalendarClass ESourceCalendarClass;
typedef struct _ESourceCalendarPrivate ESourceCalendarPrivate;

/**
 * ESourceCalendar:
 * Since: 3.6
 **/
struct _ESourceCalendar {
	/*< private >*/
	ESourceSelectable parent;
	ESourceCalendarPrivate *priv;
};

struct _ESourceCalendarClass {
	ESourceSelectableClass parent_class;
};

GType		e_source_calendar_get_type	(void);

G_END_DECLS

#endif /* E_SOURCE_CALENDAR_H */
