/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
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

#ifndef E_CAL_BACKEND_H
#define E_CAL_BACKEND_H

#include <libedataserver/e-credentials.h>
#include <libedataserver/e-source.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-component.h>
#include "e-data-cal-common.h"
#include <libedata-cal/e-data-cal-common.h>
#include <libedata-cal/e-data-cal.h>
#include "e-data-cal-types.h"

G_BEGIN_DECLS



#define E_TYPE_CAL_BACKEND            (e_cal_backend_get_type ())
#define E_CAL_BACKEND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND, ECalBackend))
#define E_CAL_BACKEND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND, ECalBackendClass))
#define E_IS_CAL_BACKEND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND))
#define E_IS_CAL_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND))
#define E_CAL_BACKEND_GET_CLASS(obj)  (E_CAL_BACKEND_CLASS (G_OBJECT_GET_CLASS (obj)))

#define CLIENT_BACKEND_PROPERTY_OPENED			"opened"
#define CLIENT_BACKEND_PROPERTY_OPENING			"opening"
#define CLIENT_BACKEND_PROPERTY_ONLINE			"online"
#define CLIENT_BACKEND_PROPERTY_READONLY		"readonly"
#define CLIENT_BACKEND_PROPERTY_CACHE_DIR		"cache-dir"
#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"
#define CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS		"cal-email-address"
#define CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS	"alarm-email-address"
#define CAL_BACKEND_PROPERTY_DEFAULT_OBJECT		"default-object"

struct _ECalBackendCache;

typedef struct _ECalBackendPrivate ECalBackendPrivate;

struct _ECalBackend {
	GObject object;

	ECalBackendPrivate *priv;
};

struct _ECalBackendClass {
	GObjectClass parent_class;

	/* Virtual methods */
        void	(* get_backend_property)	(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name);
        void	(* set_backend_property)	(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value);

	void	(* open)			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, gboolean only_if_exists);
	void	(* remove)			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable);
	void	(* set_online)			(ECalBackend *backend, gboolean is_online);
	void	(* authenticate_user)		(ECalBackend *backend, GCancellable *cancellable, ECredentials *credentials);

	void	(* refresh)			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable);
	void	(* get_object)			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid);
	void	(* get_object_list)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *sexp);
	void	(* get_free_busy)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const GSList *users, time_t start, time_t end);
	void	(* create_object)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj);
	void	(* modify_object)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj, CalObjModType mod);
	void	(* remove_object)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid, CalObjModType mod);
	void	(* receive_objects)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj);
	void	(* send_objects)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj);
	void	(* get_attachment_uris)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid);
	void	(* discard_alarm)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid, const gchar *auid);
	void	(* get_timezone)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *tzid);
	void	(* add_timezone)		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *tzobject);

	void	(* start_view)			(ECalBackend *backend, EDataCalView *view);
	void	(* stop_view)			(ECalBackend *backend, EDataCalView *view);

	/* Notification signals */
	void	(* last_client_gone)		(ECalBackend *backend);

	/* Internal methods for use only in the pcs */
	icaltimezone *(* internal_get_timezone) (ECalBackend *backend, const gchar *tzid);
};

GType		e_cal_backend_get_type			(void);

ESource *	e_cal_backend_get_source		(ECalBackend *backend);
const gchar *	e_cal_backend_get_uri			(ECalBackend *backend);
icalcomponent_kind e_cal_backend_get_kind		(ECalBackend *backend);
gboolean	e_cal_backend_is_online			(ECalBackend *backend);
gboolean	e_cal_backend_is_opened			(ECalBackend *backend);
gboolean	e_cal_backend_is_opening		(ECalBackend *backend);
gboolean	e_cal_backend_is_readonly		(ECalBackend *backend);
gboolean	e_cal_backend_is_removed		(ECalBackend *backend);

const gchar *	e_cal_backend_get_cache_dir		(ECalBackend *backend);
void		e_cal_backend_set_cache_dir		(ECalBackend *backend, const gchar *cache_dir);

void		e_cal_backend_add_client		(ECalBackend *backend, EDataCal *cal);
void		e_cal_backend_remove_client		(ECalBackend *backend, EDataCal *cal);

void		e_cal_backend_add_view			(ECalBackend *backend, EDataCalView *view);
void		e_cal_backend_remove_view		(ECalBackend *backend, EDataCalView *view);
void		e_cal_backend_foreach_view		(ECalBackend *backend, gboolean (* callback) (EDataCalView *view, gpointer user_data), gpointer user_data);

void		e_cal_backend_set_notification_proxy	(ECalBackend *backend, ECalBackend *proxy);

void		e_cal_backend_get_backend_property	(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name);
void		e_cal_backend_set_backend_property	(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value);

void		e_cal_backend_set_online		(ECalBackend *backend, gboolean is_online);
void		e_cal_backend_authenticate_user		(ECalBackend *backend, GCancellable *cancellable, ECredentials *credentials);

void		e_cal_backend_open			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, gboolean only_if_exists);
void		e_cal_backend_remove			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable);
void		e_cal_backend_refresh			(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable);
void		e_cal_backend_get_object		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid);
void		e_cal_backend_get_object_list		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *sexp);
void		e_cal_backend_get_free_busy		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const GSList *users, time_t start, time_t end);
void		e_cal_backend_create_object		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj);
void		e_cal_backend_modify_object		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj, CalObjModType mod);
void		e_cal_backend_remove_object		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid, CalObjModType mod);
void		e_cal_backend_receive_objects		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj);
void		e_cal_backend_send_objects		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *calobj);
void		e_cal_backend_get_attachment_uris	(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid);
void		e_cal_backend_discard_alarm		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *uid, const gchar *rid, const gchar *auid);
void		e_cal_backend_get_timezone		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *tzid);
void		e_cal_backend_add_timezone		(ECalBackend *backend, EDataCal *cal, guint32 opid, GCancellable *cancellable, const gchar *tzobject);
icaltimezone *	e_cal_backend_internal_get_timezone	(ECalBackend *backend, const gchar *tzid);
void		e_cal_backend_start_view		(ECalBackend *backend, EDataCalView *view);
void		e_cal_backend_stop_view			(ECalBackend *backend, EDataCalView *view);

void		e_cal_backend_notify_object_created	(ECalBackend *backend, const gchar *calobj);
void		e_cal_backend_notify_objects_added	(ECalBackend *backend, EDataCalView *view, const GSList *objects);
void		e_cal_backend_notify_object_modified	(ECalBackend *backend, const gchar *old_object, const gchar *object);
void		e_cal_backend_notify_objects_modified	(ECalBackend *backend, EDataCalView *view, const GSList *objects);
void		e_cal_backend_notify_object_removed	(ECalBackend *backend, const ECalComponentId *id, const gchar *old_object, const gchar *object);
void		e_cal_backend_notify_objects_removed	(ECalBackend *backend, EDataCalView *view, const GSList *ids);

void		e_cal_backend_notify_error		(ECalBackend *backend, const gchar *message);
void		e_cal_backend_notify_readonly		(ECalBackend *backend, gboolean is_readonly);
void		e_cal_backend_notify_online		(ECalBackend *backend, gboolean is_online);
void		e_cal_backend_notify_auth_required	(ECalBackend *backend, gboolean is_self, const ECredentials *credentials);
void		e_cal_backend_notify_opened		(ECalBackend *backend, GError *error);

void		e_cal_backend_empty_cache		(ECalBackend *backend, struct _ECalBackendCache *cache);

/* protected functions for subclasses */
void		e_cal_backend_set_is_removed		(ECalBackend *backend, gboolean is_removed);

void		e_cal_backend_respond_opened		(ECalBackend *backend, EDataCal *cal, guint32 opid, GError *error);

G_END_DECLS

#endif
