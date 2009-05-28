/* Evolution calendar - Live view client object
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

#ifndef E_CAL_VIEW_H
#define E_CAL_VIEW_H

#include <glib-object.h>
#include <libecal/e-cal-types.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_VIEW            (e_cal_view_get_type ())
#define E_CAL_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_VIEW, ECalView))
#define E_CAL_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_VIEW, ECalViewClass))
#define E_IS_CAL_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_VIEW))
#define E_IS_CAL_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_VIEW))

typedef struct _ECalView ECalView;
typedef struct _ECalViewClass ECalViewClass;
typedef struct _ECalViewPrivate ECalViewPrivate;
struct _ECal;

struct _ECalView {
	GObject object;

	/*< private >*/
	ECalViewPrivate *priv;
};

struct _ECalViewClass {
	GObjectClass parent_class;

	/* Notification signals */
	void (* objects_added) (ECalView *view, GList *objects);
	void (* objects_modified) (ECalView *view, GList *objects);
	void (* objects_removed) (ECalView *view, GList *uids);
	void (* view_progress) (ECalView *view, gchar *message, gint percent);
	void (* view_done) (ECalView *view, ECalendarStatus status);
};

GType      e_cal_view_get_type (void);

struct _ECal *e_cal_view_get_client (ECalView *view);
void e_cal_view_start (ECalView *view);

G_END_DECLS

#endif
