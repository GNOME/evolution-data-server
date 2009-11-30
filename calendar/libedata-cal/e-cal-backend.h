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

#include "libedataserver/e-list.h"
#include "libedataserver/e-source.h"
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-component.h>
#include "e-data-cal-common.h"
#include <libedata-cal/e-data-cal-common.h>
#include <libedata-cal/e-data-cal.h>
#include "e-data-cal-types.h"

G_BEGIN_DECLS



#define E_TYPE_CAL_BACKEND            (e_cal_backend_get_type ())
#define E_CAL_BACKEND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND, ECalBackend))
#define E_CAL_BACKEND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND,		\
				     ECalBackendClass))
#define E_IS_CAL_BACKEND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND))
#define E_IS_CAL_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND))
#define E_CAL_BACKEND_GET_CLASS(obj) (E_CAL_BACKEND_CLASS (G_OBJECT_GET_CLASS (obj)))

struct _ECalBackendCache;

typedef struct _ECalBackendPrivate ECalBackendPrivate;

struct _ECalBackend {
	GObject object;

	ECalBackendPrivate *priv;
};

struct _ECalBackendClass {
	GObjectClass parent_class;

	/* Notification signals */
	void (* last_client_gone) (ECalBackend *backend);
	void (* cal_added) (ECalBackend *backend, EDataCal *cal);

	gboolean (* is_loaded) (ECalBackend *backend);

	/* FIXME What to pass back here */
	void (* opened) (ECalBackend *backend, gint status);
	void (* removed) (ECalBackend *backend, gint status);
	void (* obj_updated) (ECalBackend *backend, const gchar *uid);

	/* Virtual methods */
	void (* is_read_only) (ECalBackend *backend, EDataCal *cal);
	void (* get_cal_address) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
	void (* get_alarm_email_address) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
	void (* get_ldap_attribute) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
	void (* get_static_capabilities) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);

	void (* open) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, gboolean only_if_exists, const gchar *username, const gchar *password);
	void (* refresh) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
	void (* remove) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);

	/* Object related virtual methods */
	void (* create_object) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj);
	void (* modify_object) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj, CalObjModType mod);
	void (* remove_object) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid, CalObjModType mod);

	void (* discard_alarm) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *auid);

	void (* receive_objects) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj);
	void (* send_objects) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj);

	void (* get_default_object) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
	void (* get_object) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid);
	void (* get_object_list) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *sexp);

	void (* get_attachment_list) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid);

	/* Timezone related virtual methods */
	void (* get_timezone) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid);
	void (* add_timezone) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *object);
	void (* set_default_zone) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj);
	void (* set_default_timezone) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid);

	void (* start_query) (ECalBackend *backend, EDataCalView *query);

	/* Mode relate virtual methods */
	CalMode (* get_mode) (ECalBackend *backend);
	void    (* set_mode) (ECalBackend *backend, CalMode mode);

	void (* get_free_busy) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, GList *users, time_t start, time_t end);
	void (* get_changes) (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *change_id);

	/* Internal methods for use only in the pcs */
	icaltimezone *(* internal_get_default_timezone) (ECalBackend *backend);
	icaltimezone *(* internal_get_timezone) (ECalBackend *backend, const gchar *tzid);
};

GType e_cal_backend_get_type (void);

ESource *e_cal_backend_get_source (ECalBackend *backend);
const gchar *e_cal_backend_get_uri (ECalBackend *backend);
icalcomponent_kind e_cal_backend_get_kind (ECalBackend *backend);

void e_cal_backend_add_client (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_remove_client (ECalBackend *backend, EDataCal *cal);

void e_cal_backend_add_query (ECalBackend *backend, EDataCalView *query);
EList *e_cal_backend_get_queries (ECalBackend *backend);
void e_cal_backend_remove_query (ECalBackend *backend, EDataCalView *query);

void e_cal_backend_is_read_only (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
void e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
void e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
void e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);

void e_cal_backend_open (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, gboolean only_if_exists, const gchar *username, const gchar *password);
void e_cal_backend_refresh (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
void e_cal_backend_remove (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);

void e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj);
void e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj, CalObjModType mod);
void e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid, CalObjModType mod);

void e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *auid);

void e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj);
void e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj);

void e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context);
void e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid);
void e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *sexp);
void e_cal_backend_get_attachment_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *rid);

gboolean e_cal_backend_is_loaded (ECalBackend *backend);

void e_cal_backend_start_query (ECalBackend *backend, EDataCalView *query);

CalMode e_cal_backend_get_mode (ECalBackend *backend);
void e_cal_backend_set_mode (ECalBackend *backend, CalMode mode);

void e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid);
void e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *object);
void e_cal_backend_set_default_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid);
void e_cal_backend_set_default_zone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj);

void e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *change_id);
void e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, GList *users, time_t start, time_t end);

icaltimezone* e_cal_backend_internal_get_default_timezone (ECalBackend *backend);
icaltimezone* e_cal_backend_internal_get_timezone (ECalBackend *backend, const gchar *tzid);

void e_cal_backend_set_notification_proxy (ECalBackend *backend, ECalBackend *proxy);
void e_cal_backend_notify_object_created  (ECalBackend *backend, const gchar *calobj);
void e_cal_backend_notify_object_modified (ECalBackend *backend, const gchar *old_object, const gchar *object);
void e_cal_backend_notify_object_removed  (ECalBackend *backend, const ECalComponentId *id, const gchar *old_object, const gchar *object);

void e_cal_backend_notify_mode      (ECalBackend *backend,
				     EDataCalViewListenerSetModeStatus status,
				     EDataCalMode mode);
void e_cal_backend_notify_auth_required (ECalBackend *backend);
void e_cal_backend_notify_error     (ECalBackend *backend, const gchar *message);

void e_cal_backend_notify_view_done (ECalBackend *backend, EDataCalCallStatus status);
void e_cal_backend_notify_view_progress_start (ECalBackend *backend);
void e_cal_backend_notify_view_progress (ECalBackend *backend, const gchar *message, gint percent);
void e_cal_backend_notify_readonly (ECalBackend *backend, gboolean read_only);
void e_cal_backend_notify_cal_address (ECalBackend *backend, EServerMethodContext context, gchar *address);

void e_cal_backend_notify_objects_added (ECalBackend *backend, EDataCalView *query, const GList *objects);
void e_cal_backend_notify_objects_removed (ECalBackend *backend, EDataCalView *query, const GList *ids);
void e_cal_backend_notify_objects_modified (ECalBackend *backend, EDataCalView *query, const GList *objects);

void e_cal_backend_empty_cache (ECalBackend *backend, struct _ECalBackendCache *cache);

G_END_DECLS

#endif
