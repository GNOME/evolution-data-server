/* Evolution calendar - Live view client object
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

#ifndef E_CAL_VIEW_H
#define E_CAL_VIEW_H

#include <glib-object.h>
#include <libecal/Evolution-DataServer-Calendar.h>
#include <libecal/e-cal-types.h>
#include <libecal/e-cal-view-listener.h>

G_BEGIN_DECLS

typedef struct _ECal ECal;



#define E_CAL_VIEW_TYPE            (e_cal_view_get_type ())
#define E_CAL_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_CAL_VIEW_TYPE, ECalView))
#define E_CAL_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_CAL_VIEW_TYPE, ECalViewClass))
#define IS_E_CAL_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_CAL_VIEW_TYPE))
#define IS_E_CAL_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_CAL_VIEW_TYPE))

typedef struct _ECalViewPrivate ECalViewPrivate;

typedef struct {
	GObject object;

	/* Private data */
	ECalViewPrivate *priv;
} ECalView;

typedef struct {
	GObjectClass parent_class;

	/* Notification signals */
	void (* objects_added) (ECalView *view, GList *objects);
	void (* objects_modified) (ECalView *view, GList *objects);
	void (* objects_removed) (ECalView *view, GList *uids);
	void (* view_progress) (ECalView *view, char *message, int percent);
	void (* view_done) (ECalView *view, ECalendarStatus status);
} ECalViewClass;

GType      e_cal_view_get_type (void);

ECalView *e_cal_view_new (GNOME_Evolution_Calendar_CalView corba_view, ECalViewListener *listener, ECal *client);
ECal *e_cal_view_get_client (ECalView *view);
void e_cal_view_start (ECalView *view);

G_END_DECLS

#endif
