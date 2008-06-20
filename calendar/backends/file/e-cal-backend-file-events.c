/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-backend-file-events.h"



/* Private part of the CalBackendFileEvents structure */
struct _ECalBackendFileEventsPrivate {
	guint reserved;
};



static void e_cal_backend_file_events_class_init (ECalBackendFileEventsClass *class);
static void e_cal_backend_file_events_init (ECalBackendFileEvents *cbfile, ECalBackendFileEventsClass *class);
static void e_cal_backend_file_events_dispose (GObject *object);
static void e_cal_backend_file_events_finalize (GObject *object);

static GObjectClass *parent_class;



/**
 * e_cal_backend_file_events_get_type:
 * @void:
 *
 * Registers the #ECalBackendFileEvents class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #ECalBackendFileEvents class.
 **/
GType
e_cal_backend_file_events_get_type (void)
{
	static GType e_cal_backend_file_events_type = 0;

	if (!e_cal_backend_file_events_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendFileEventsClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_file_events_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendFileEvents),
                        0,
                        (GInstanceInitFunc) e_cal_backend_file_events_init
                };
		e_cal_backend_file_events_type = g_type_register_static (E_TYPE_CAL_BACKEND_FILE,
								      "ECalBackendFileEvents", &info, 0);
	}

	return e_cal_backend_file_events_type;
}

/* Class initialization function for the file backend */
static void
e_cal_backend_file_events_class_init (ECalBackendFileEventsClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;

	object_class = G_OBJECT_CLASS (klass);
	backend_class = E_CAL_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_cal_backend_file_events_dispose;
	object_class->finalize = e_cal_backend_file_events_finalize;

//	backend_class->get_uri = e_cal_backend_file_events_get_uri;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_file_events_init (ECalBackendFileEvents *cbfile, ECalBackendFileEventsClass *class)
{
	ECalBackendFileEventsPrivate *priv;

	priv = g_new0 (ECalBackendFileEventsPrivate, 1);
	cbfile->priv = priv;

	e_cal_backend_file_set_file_name (E_CAL_BACKEND_FILE (cbfile), "calendar.ics");
}

/* Dispose handler for the file backend */
static void
e_cal_backend_file_events_dispose (GObject *object)
{
	ECalBackendFileEvents *cbfile;
	ECalBackendFileEventsPrivate *priv;

	cbfile = E_CAL_BACKEND_FILE_EVENTS (object);
	priv = cbfile->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_file_events_finalize (GObject *object)
{
	ECalBackendFileEvents *cbfile;
	ECalBackendFileEventsPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_FILE_EVENTS (object));

	cbfile = E_CAL_BACKEND_FILE_EVENTS (object);
	priv = cbfile->priv;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

