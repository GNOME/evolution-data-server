/* Evolution calendar - Live search query listener implementation
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_VIEW_LISTENER_H
#define E_CAL_VIEW_LISTENER_H

#include <bonobo/bonobo-object.h>
#include <libecal/Evolution-DataServer-Calendar.h>
#include <libecal/e-cal-types.h>

G_BEGIN_DECLS



#define E_TYPE_CAL_VIEW_LISTENER            (e_cal_view_listener_get_type ())
#define E_CAL_VIEW_LISTENER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_VIEW_LISTENER, ECalViewListener))
#define E_CAL_VIEW_LISTENER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_VIEW_LISTENER,	\
					ECalViewListenerClass))
#define E_IS_CAL_VIEW_LISTENER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_VIEW_LISTENER))
#define E_IS_CAL_VIEW_LISTENER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_VIEW_LISTENER))

typedef struct _ECalViewListenerPrivate ECalViewListenerPrivate;

typedef struct {
	BonoboObject xobject;

	/* Private data */
	ECalViewListenerPrivate *priv;
} ECalViewListener;

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Calendar_CalViewListener__epv epv;

	void (*objects_added) (ECalViewListener *listener, GList *objects);
	void (*objects_modified) (ECalViewListener *listener, GList *objects);
	void (*objects_removed) (ECalViewListener *listener, GList *uids);
	void (*query_progress) (ECalViewListener *listener, const char *message, int percent);
	void (*query_done) (ECalViewListener *listener, ECalendarStatus status);
} ECalViewListenerClass;

GType e_cal_view_listener_get_type (void);
ECalViewListener *e_cal_view_listener_new (void);



G_END_DECLS

#endif
