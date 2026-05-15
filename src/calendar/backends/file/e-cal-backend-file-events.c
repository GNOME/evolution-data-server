/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Federico Mena-Quintero <federico@ximian.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 */

#include "evolution-data-server-config.h"

#include "e-cal-backend-file-events.h"

G_DEFINE_TYPE (
	ECalBackendFileEvents,
	e_cal_backend_file_events,
	E_TYPE_CAL_BACKEND_FILE)

static void
e_cal_backend_file_events_class_init (ECalBackendFileEventsClass *class)
{
}

static void
e_cal_backend_file_events_init (ECalBackendFileEvents *cbfile)
{
	e_cal_backend_file_set_file_name (
		E_CAL_BACKEND_FILE (cbfile), "calendar.ics");
}

