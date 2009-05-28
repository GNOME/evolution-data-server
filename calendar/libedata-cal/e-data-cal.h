/* Evolution calendar client interface object
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

#ifndef E_DATA_CAL_H
#define E_DATA_CAL_H

#include <bonobo/bonobo-object.h>
#include <libedata-cal/Evolution-DataServer-Calendar.h>
#include <libedata-cal/e-data-cal-common.h>
#include <libedata-cal/e-data-cal-view.h>

G_BEGIN_DECLS



#define E_TYPE_DATA_CAL            (e_data_cal_get_type ())
#define E_DATA_CAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_DATA_CAL, EDataCal))
#define E_DATA_CAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_DATA_CAL, EDataCalClass))
#define E_IS_DATA_CAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_DATA_CAL))
#define E_IS_DATA_CAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_DATA_CAL))

typedef struct _EDataCalPrivate EDataCalPrivate;

struct _EDataCal {
	BonoboObject object;

	/* Private data */
	EDataCalPrivate *priv;
};

struct _EDataCalClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Calendar_Cal__epv epv;
};

GType e_data_cal_get_type (void);

EDataCal *e_data_cal_construct (EDataCal *cal,
		    ECalBackend *backend,
		    GNOME_Evolution_Calendar_CalListener listener);

EDataCal *e_data_cal_new (ECalBackend *backend, GNOME_Evolution_Calendar_CalListener listener);

ECalBackend *e_data_cal_get_backend (EDataCal *cal);
GNOME_Evolution_Calendar_CalListener e_data_cal_get_listener (EDataCal *cal);

void e_data_cal_notify_read_only           (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					    gboolean read_only);
void e_data_cal_notify_cal_address         (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					    const gchar *address);
void e_data_cal_notify_alarm_email_address (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					    const gchar *address);
void e_data_cal_notify_ldap_attribute      (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					    const gchar *attribute);
void e_data_cal_notify_static_capabilities (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					    const gchar *capabilities);

void e_data_cal_notify_open   (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status);
void e_data_cal_notify_remove (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status);

void e_data_cal_notify_object_created  (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					const gchar *uid, const gchar *object);
void e_data_cal_notify_object_modified (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					const gchar *old_object, const gchar *object);
void e_data_cal_notify_object_removed  (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					const ECalComponentId *id, const gchar *old_object, const gchar *object);
void e_data_cal_notify_alarm_discarded (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status);

void e_data_cal_notify_objects_received (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status);
void e_data_cal_notify_objects_sent     (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, GList *users,
					 const gchar *calobj);

void e_data_cal_notify_default_object (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
				       const gchar *object);
void e_data_cal_notify_object         (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
				       const gchar *object);
void e_data_cal_notify_object_list    (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
				       GList *objects);

void e_data_cal_notify_query (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
			      EDataCalView *query);

void e_data_cal_notify_timezone_requested   (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					     const gchar *object);
void e_data_cal_notify_timezone_added       (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
					     const gchar *tzid);
void e_data_cal_notify_default_timezone_set (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status);

void e_data_cal_notify_changes   (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
				  GList *adds, GList *modifies, GList *deletes);
void e_data_cal_notify_free_busy (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status,
				  GList *freebusy);

void e_data_cal_notify_mode  (EDataCal *cal,
			      GNOME_Evolution_Calendar_CalListener_SetModeStatus status,
			      GNOME_Evolution_Calendar_CalMode mode);

void e_data_cal_notify_auth_required (EDataCal *cal);

void e_data_cal_notify_error (EDataCal *cal, const gchar *message);

void e_data_cal_notify_attachment_list (EDataCal *cal, GNOME_Evolution_Calendar_CallStatus status, GSList *objects);

G_END_DECLS

#endif
