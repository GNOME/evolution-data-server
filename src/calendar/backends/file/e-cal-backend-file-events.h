/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Federico Mena-Quintero <federico@ximian.com>
 */

#ifndef E_CAL_BACKEND_FILE_EVENTS_H
#define E_CAL_BACKEND_FILE_EVENTS_H

#include "e-cal-backend-file.h"

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_FILE_EVENTS \
	(e_cal_backend_file_events_get_type ())
#define E_CAL_BACKEND_FILE_EVENTS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_FILE_EVENTS, ECalBackendFileEvents))
#define E_CAL_BACKEND_FILE_EVENTS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_FILE_EVENTS, ECalBackendFileEventsClass))
#define E_IS_CAL_BACKEND_FILE_EVENTS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_FILE_EVENTS))
#define E_IS_CAL_BACKEND_FILE_EVENTS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_FILE_EVENTS))
#define E_CAL_BACKEND_FILE_EVENTS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_FILE_EVENTS, ECalBackendFileEventsClass))

G_BEGIN_DECLS

typedef ECalBackendFile ECalBackendFileEvents;
typedef ECalBackendFileClass ECalBackendFileEventsClass;

GType		e_cal_backend_file_events_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_FILE_EVENTS_H */
