/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-source-calendar
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for a calendar
 *
 * The #ESourceCalendar extension identifies the #ESource as a calendar.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceCalendar *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR);
 * ]|
 **/

#include "e-source-calendar.h"

#include <libedataserver/e-data-server-util.h>

G_DEFINE_TYPE (
	ESourceCalendar,
	e_source_calendar,
	E_TYPE_SOURCE_SELECTABLE)

static void
e_source_calendar_class_init (ESourceCalendarClass *class)
{
	ESourceExtensionClass *extension_class;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_CALENDAR;
}

static void
e_source_calendar_init (ESourceCalendar *extension)
{
}
