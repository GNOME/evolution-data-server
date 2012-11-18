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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_CLIENT_VIEW_H
#define E_CAL_CLIENT_VIEW_H

#include <glib-object.h>

/* Standard GObject macros */
#define E_TYPE_CAL_CLIENT_VIEW \
	(e_cal_client_view_get_type ())
#define E_CAL_CLIENT_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_CLIENT_VIEW, ECalClientView))
#define E_CAL_CLIENT_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_CLIENT_VIEW, ECalClientViewClass))
#define E_IS_CAL_CLIENT_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_CLIENT_VIEW))
#define E_IS_CAL_CLIENT_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_CLIENT_VIEW))
#define E_CAL_CLIENT_VIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_CLIENT_VIEW, ECalClientViewClass))

G_BEGIN_DECLS

typedef struct _ECalClientView ECalClientView;
typedef struct _ECalClientViewClass ECalClientViewClass;
typedef struct _ECalClientViewPrivate ECalClientViewPrivate;

struct _ECalClient;

/**
 * ECalClientViewFlags:
 * @E_CAL_CLIENT_VIEW_FLAGS_NONE:
 *   Symbolic value for no flags
 * @E_CAL_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL:
 *   If this flag is set then all objects matching the view's query will
 *   be sent as notifications when starting the view, otherwise only future
 *   changes will be reported.  The default for a #ECalClientView is %TRUE.
 *
 * Flags that control the behaviour of an #ECalClientView.
 *
 * Since: 3.6
 */
typedef enum {
	E_CAL_CLIENT_VIEW_FLAGS_NONE           = 0,
	E_CAL_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL = (1 << 0)
} ECalClientViewFlags;

/**
 * ECalClientView:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.2
 **/
struct _ECalClientView {
	GObject object;
	ECalClientViewPrivate *priv;
};

struct _ECalClientViewClass {
	GObjectClass parent_class;

	/* Signals */
	void		(*objects_added)	(ECalClientView *view,
						 const GSList *objects);
	void		(*objects_modified)	(ECalClientView *view,
						 const GSList *objects);
	void		(*objects_removed)	(ECalClientView *view,
						 const GSList *uids);
	void		(*progress)		(ECalClientView *view,
						 guint percent,
						 const gchar *message);
	void		(*complete)		(ECalClientView *view,
						 const GError *error);
};

GType		e_cal_client_view_get_type	(void) G_GNUC_CONST;
struct _ECalClient *
		e_cal_client_view_get_client	(ECalClientView *view);
GDBusConnection *
		e_cal_client_view_get_connection
						(ECalClientView *view);
const gchar *	e_cal_client_view_get_object_path
						(ECalClientView *view);
gboolean	e_cal_client_view_is_running	(ECalClientView *view);
void		e_cal_client_view_set_fields_of_interest
						(ECalClientView *view,
						 const GSList *fields_of_interest,
						 GError **error);
void		e_cal_client_view_start		(ECalClientView *view,
						 GError **error);
void		e_cal_client_view_stop		(ECalClientView *view,
						 GError **error);
void		e_cal_client_view_set_flags	(ECalClientView *view,
						 ECalClientViewFlags flags,
						 GError **error);

G_END_DECLS

#endif /* E_CAL_CLIENT_VIEW_H */
