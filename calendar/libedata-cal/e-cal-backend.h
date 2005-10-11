/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_BACKEND_H
#define E_CAL_BACKEND_H

#include <libedataserver/e-list.h>
#include <libedataserver/e-source.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-component.h>
#include <libedata-cal/Evolution-DataServer-Calendar.h>
#include <libedata-cal/e-data-cal-common.h>
#include <libedata-cal/e-data-cal.h>
#include <libedata-cal/e-data-cal-view.h>

G_BEGIN_DECLS



#define E_TYPE_CAL_BACKEND            (e_cal_backend_get_type ())
#define E_CAL_BACKEND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND, ECalBackend))
#define E_CAL_BACKEND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND,		\
				     ECalBackendClass))
#define E_IS_CAL_BACKEND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND))
#define E_IS_CAL_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND))

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
	void (* opened) (ECalBackend *backend, int status);
	void (* removed) (ECalBackend *backend, int status);
	void (* obj_updated) (ECalBackend *backend, const char *uid);

	/* Virtual methods */
	void (* is_read_only) (ECalBackend *backend, EDataCal *cal);
	void (* get_cal_address) (ECalBackend *backend, EDataCal *cal);
	void (* get_alarm_email_address) (ECalBackend *backend, EDataCal *cal);
	void (* get_ldap_attribute) (ECalBackend *backend, EDataCal *cal);
	void (* get_static_capabilities) (ECalBackend *backend, EDataCal *cal);
	
	void (* open) (ECalBackend *backend, EDataCal *cal, gboolean only_if_exists, const char *username, const char *password);
	void (* remove) (ECalBackend *backend, EDataCal *cal);

	/* Object related virtual methods */
	void (* create_object) (ECalBackend *backend, EDataCal *cal, const char *calobj);
	void (* modify_object) (ECalBackend *backend, EDataCal *cal, const char *calobj, CalObjModType mod);
	void (* remove_object) (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid, CalObjModType mod);

	void (* discard_alarm) (ECalBackend *backend, EDataCal *cal, const char *uid, const char *auid);

	void (* receive_objects) (ECalBackend *backend, EDataCal *cal, const char *calobj);
	void (* send_objects) (ECalBackend *backend, EDataCal *cal, const char *calobj);

	void (* get_default_object) (ECalBackend *backend, EDataCal *cal);
	void (* get_object) (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid);	
	void (* get_object_list) (ECalBackend *backend, EDataCal *cal, const char *sexp);

	void (* get_attachment_list) (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid);	
	
	/* Timezone related virtual methods */
	void (* get_timezone) (ECalBackend *backend, EDataCal *cal, const char *tzid);
	void (* add_timezone) (ECalBackend *backend, EDataCal *cal, const char *object);
	void (* set_default_timezone) (ECalBackend *backend, EDataCal *cal, const char *tzid);

	void (* start_query) (ECalBackend *backend, EDataCalView *query);

	/* Mode relate virtual methods */
	CalMode (* get_mode) (ECalBackend *backend);
	void    (* set_mode) (ECalBackend *backend, CalMode mode);

	void (* get_free_busy) (ECalBackend *backend, EDataCal *cal, GList *users, time_t start, time_t end);
	void (* get_changes) (ECalBackend *backend, EDataCal *cal, const char *change_id);

	/* Internal methods for use only in the pcs */
	icaltimezone *(* internal_get_default_timezone) (ECalBackend *backend);
	icaltimezone *(* internal_get_timezone) (ECalBackend *backend, const char *tzid);
};

GType e_cal_backend_get_type (void);

ESource *e_cal_backend_get_source (ECalBackend *backend);
const char *e_cal_backend_get_uri (ECalBackend *backend);
icalcomponent_kind e_cal_backend_get_kind (ECalBackend *backend);

void e_cal_backend_add_client (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_remove_client (ECalBackend *backend, EDataCal *cal);

void e_cal_backend_add_query (ECalBackend *backend, EDataCalView *query);
EList *e_cal_backend_get_queries (ECalBackend *backend);

void e_cal_backend_is_read_only (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal);

void e_cal_backend_open (ECalBackend *backend, EDataCal *cal, gboolean only_if_exists, const char *username, const char *password);
void e_cal_backend_remove (ECalBackend *backend, EDataCal *cal);

void e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, const char *calobj);
void e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, const char *calobj, CalObjModType mod);
void e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid, CalObjModType mod);

void e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, const char *uid, const char *auid);

void e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, const char *calobj);
void e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, const char *calobj);

void e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal);
void e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid);
void e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, const char *sexp);

void e_cal_backend_get_attachment_list (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid);

gboolean e_cal_backend_is_loaded (ECalBackend *backend);

void e_cal_backend_start_query (ECalBackend *backend, EDataCalView *query);

CalMode e_cal_backend_get_mode (ECalBackend *backend);
void e_cal_backend_set_mode (ECalBackend *backend, CalMode mode);

void e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, const char *tzid);
void e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, const char *object);
void e_cal_backend_set_default_timezone (ECalBackend *backend, EDataCal *cal, const char *tzid);

void e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, const char *change_id);
void e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, GList *users, time_t start, time_t end);

icaltimezone* e_cal_backend_internal_get_default_timezone (ECalBackend *backend);
icaltimezone* e_cal_backend_internal_get_timezone (ECalBackend *backend, const char *tzid);

void e_cal_backend_last_client_gone (ECalBackend *backend);

void e_cal_backend_set_notification_proxy (ECalBackend *backend, ECalBackend *proxy);
void e_cal_backend_notify_object_created  (ECalBackend *backend, const char *calobj);
void e_cal_backend_notify_object_modified (ECalBackend *backend, const char *old_object, const char *object);
void e_cal_backend_notify_object_removed  (ECalBackend *backend, const ECalComponentId *id, const char *old_object, const char *object);

void e_cal_backend_notify_mode      (ECalBackend *backend,
				     GNOME_Evolution_Calendar_CalListener_SetModeStatus status, 
				     GNOME_Evolution_Calendar_CalMode mode);
void e_cal_backend_notify_auth_required (ECalBackend *backend);
void e_cal_backend_notify_error     (ECalBackend *backend, const char *message);

void e_cal_backend_notify_view_done (ECalBackend *backend, GNOME_Evolution_Calendar_CallStatus status);
void e_cal_backend_notify_view_progress (ECalBackend *backend, const char *message, int percent);
void e_cal_backend_notify_readonly (ECalBackend *backend, gboolean read_only);
void e_cal_backend_notify_cal_address (ECalBackend *backend, char *address);



G_END_DECLS

#endif
