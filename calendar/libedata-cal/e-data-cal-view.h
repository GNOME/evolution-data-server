/* Evolution calendar - Live search view implementation
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

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_DATA_CAL_VIEW_H
#define E_DATA_CAL_VIEW_H

#include <libecal/libecal.h>

#include <libedata-cal/e-data-cal-common.h>
#include <libedata-cal/e-cal-backend-sexp.h>

G_BEGIN_DECLS



#define E_DATA_CAL_VIEW_TYPE            (e_data_cal_view_get_type ())
#define E_DATA_CAL_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_DATA_CAL_VIEW_TYPE, EDataCalView))
#define E_DATA_CAL_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_DATA_CAL_VIEW_TYPE, EDataCalViewClass))
#define E_IS_DATA_CAL_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_DATA_CAL_VIEW_TYPE))

typedef struct _EDataCalViewPrivate EDataCalViewPrivate;

struct _EDataCalView {
	GObject parent;
	EDataCalViewPrivate *priv;
};

struct _EDataCalViewClass {
	GObjectClass parent_class;
};

GType			e_data_cal_view_get_type			(void);
EDataCalView *		e_data_cal_view_new				(ECalBackend *backend, ECalBackendSExp *sexp);
guint			e_data_cal_view_register_gdbus_object		(EDataCalView *view, GDBusConnection *connection, const gchar *object_path, GError **error);
const gchar *		e_data_cal_view_get_text			(EDataCalView *view);
ECalBackendSExp *	e_data_cal_view_get_object_sexp			(EDataCalView *view);
gboolean		e_data_cal_view_object_matches			(EDataCalView *view, const gchar *object);
gboolean		e_data_cal_view_component_matches		(EDataCalView *view, ECalComponent *component);
gboolean		e_data_cal_view_is_started			(EDataCalView *view);
gboolean		e_data_cal_view_is_completed			(EDataCalView *view);
gboolean		e_data_cal_view_is_stopped			(EDataCalView *view);
GHashTable *		e_data_cal_view_get_fields_of_interest		(EDataCalView *view);
ECalClientViewFlags    e_data_cal_view_get_flags			(EDataCalView *view);

gchar *			e_data_cal_view_get_component_string		(EDataCalView *view, /* const */ ECalComponent *component);

void			e_data_cal_view_notify_components_added		(EDataCalView *view, const GSList *ecalcomponents);
void			e_data_cal_view_notify_components_added_1	(EDataCalView *view, /* const */ ECalComponent *component);
void			e_data_cal_view_notify_components_modified	(EDataCalView *view, const GSList *ecalcomponents);
void			e_data_cal_view_notify_components_modified_1	(EDataCalView *view, /* const */ ECalComponent *component);

#ifndef E_CAL_DISABLE_DEPRECATED
void			e_data_cal_view_notify_objects_added		(EDataCalView *view, const GSList *objects);
void			e_data_cal_view_notify_objects_added_1		(EDataCalView *view, const gchar *object);
void			e_data_cal_view_notify_objects_modified		(EDataCalView *view, const GSList *objects);
void			e_data_cal_view_notify_objects_modified_1	(EDataCalView *view, const gchar *object);
#endif

void			e_data_cal_view_notify_objects_removed		(EDataCalView *view, const GSList *ids);
void			e_data_cal_view_notify_objects_removed_1	(EDataCalView *view, const ECalComponentId *id);
void			e_data_cal_view_notify_progress			(EDataCalView *view, gint percent, const gchar *message);
void			e_data_cal_view_notify_complete			(EDataCalView *view, const GError *error);

G_END_DECLS

#endif
