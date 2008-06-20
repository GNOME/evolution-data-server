/* Evolution calendar - Live search query listener implementation
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#ifndef E_CAL_VIEW_LISTENER_H
#define E_CAL_VIEW_LISTENER_H

#include <bonobo/bonobo-object.h>
#include <libecal/e-cal-types.h>

#include "Evolution-DataServer-Calendar.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_VIEW_LISTENER            (e_cal_view_listener_get_type ())
#define E_CAL_VIEW_LISTENER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_VIEW_LISTENER, ECalViewListener))
#define E_CAL_VIEW_LISTENER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_VIEW_LISTENER, ECalViewListenerClass))
#define E_IS_CAL_VIEW_LISTENER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_VIEW_LISTENER))
#define E_IS_CAL_VIEW_LISTENER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_VIEW_LISTENER))
#define E_CAL_VIEW_LISTENER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_VIEW_LISTENER, ECalViewListenerClass))

typedef struct _ECalViewListener ECalViewListener;
typedef struct _ECalViewListenerClass ECalViewListenerClass;
typedef struct _ECalViewListenerPrivate ECalViewListenerPrivate;

struct _ECalViewListener {
	BonoboObject xobject;

	/*< private >*/
	ECalViewListenerPrivate *priv;
};

struct _ECalViewListenerClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Calendar_CalViewListener__epv epv;

	void (*objects_added) (ECalViewListener *listener, GList *objects);
	void (*objects_modified) (ECalViewListener *listener, GList *objects);
	void (*objects_removed) (ECalViewListener *listener, GList *uids);
	void (*view_progress) (ECalViewListener *listener, const char *message, int percent);
	void (*view_done) (ECalViewListener *listener, ECalendarStatus status);
};

GType             e_cal_view_listener_get_type (void);
ECalViewListener *e_cal_view_listener_new      (void);

G_END_DECLS

#endif
