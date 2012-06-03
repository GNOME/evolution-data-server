/*
 * e-cal-client.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_CLIENT_H
#define E_CAL_CLIENT_H

#include <libedataserver/libedataserver.h>

#include <libecal/e-cal-client-view.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_CLIENT		(e_cal_client_get_type ())
#define E_CAL_CLIENT(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CAL_CLIENT, ECalClient))
#define E_CAL_CLIENT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_CAL_CLIENT, ECalClientClass))
#define E_IS_CAL_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CAL_CLIENT))
#define E_IS_CAL_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CAL_CLIENT))
#define E_CAL_CLIENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_CLIENT, ECalClientClass))

/**
 * CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS		"cal-email-address"

/**
 * CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS	"alarm-email-address"

/**
 * CAL_BACKEND_PROPERTY_DEFAULT_OBJECT:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CAL_BACKEND_PROPERTY_DEFAULT_OBJECT		"default-object"

/**
 * ECalClientSourceType:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
typedef enum {
	E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
	E_CAL_CLIENT_SOURCE_TYPE_TASKS,
	E_CAL_CLIENT_SOURCE_TYPE_MEMOS,
	E_CAL_CLIENT_SOURCE_TYPE_LAST
} ECalClientSourceType;

#define E_TYPE_CAL_CLIENT_SOURCE_TYPE \
	(e_cal_client_source_type_enum_get_type ())

GType e_cal_client_source_type_enum_get_type (void);

/**
 * E_CAL_CLIENT_ERROR:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define E_CAL_CLIENT_ERROR e_cal_client_error_quark ()

GQuark e_cal_client_error_quark (void) G_GNUC_CONST;

/**
 * ECalClientError:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
typedef enum {
	E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR,
	E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND,
	E_CAL_CLIENT_ERROR_INVALID_OBJECT,
	E_CAL_CLIENT_ERROR_UNKNOWN_USER,
	E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS,
	E_CAL_CLIENT_ERROR_INVALID_RANGE
} ECalClientError;

const gchar *	e_cal_client_error_to_string (ECalClientError code);
GError *	e_cal_client_error_create (ECalClientError code, const gchar *custom_msg);

typedef struct _ECalClient        ECalClient;
typedef struct _ECalClientClass   ECalClientClass;
typedef struct _ECalClientPrivate ECalClientPrivate;

/**
 * ECalClient:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.2
 **/
struct _ECalClient {
	EClient parent;

	/*< private >*/
	ECalClientPrivate *priv;
};

struct _ECalClientClass {
	EClientClass parent;

	/* Signals */
	void (* free_busy_data) (ECalClient *client, const GSList *free_busy_ecalcomps);
};

GType			e_cal_client_get_type			(void);

/* Creating a new calendar */
ECalClient *		e_cal_client_new			(ESource *source, ECalClientSourceType source_type, GError **error);

/* Calendar properties not managed by EClient */
ECalClientSourceType	e_cal_client_get_source_type		(ECalClient *client);
const gchar *		e_cal_client_get_local_attachment_store	(ECalClient *client);
void			e_cal_client_set_default_timezone	(ECalClient *client, /* const */ icaltimezone *zone);
icaltimezone *		e_cal_client_get_default_timezone	(ECalClient *client);

/* Check predefined capabilities */
gboolean		e_cal_client_check_one_alarm_only	(ECalClient *client);
gboolean		e_cal_client_check_save_schedules	(ECalClient *client);
gboolean		e_cal_client_check_organizer_must_attend (ECalClient *client);
gboolean		e_cal_client_check_organizer_must_accept (ECalClient *client);
gboolean		e_cal_client_check_recurrences_no_master (ECalClient *client);

/* Utility functions */
void		e_cal_client_free_icalcomp_slist		(GSList *icalcomps);
void		e_cal_client_free_ecalcomp_slist		(GSList *ecalcomps);

icaltimezone *	e_cal_client_resolve_tzid_cb			(const gchar *tzid, gpointer data);
void		e_cal_client_generate_instances			(ECalClient *client, time_t start, time_t end, GCancellable *cancellable, ECalRecurInstanceFn cb, gpointer cb_data, GDestroyNotify destroy_cb_data);
void		e_cal_client_generate_instances_sync		(ECalClient *client, time_t start, time_t end, ECalRecurInstanceFn cb, gpointer cb_data);
void		e_cal_client_generate_instances_for_object	(ECalClient *client, icalcomponent *icalcomp, time_t start, time_t end, GCancellable *cancellable, ECalRecurInstanceFn cb, gpointer cb_data, GDestroyNotify destroy_cb_data);
void		e_cal_client_generate_instances_for_object_sync	(ECalClient *client, icalcomponent *icalcomp, time_t start, time_t end, ECalRecurInstanceFn cb, gpointer cb_data);
gchar *		e_cal_client_get_component_as_string		(ECalClient *client, icalcomponent *icalcomp);

/* Calendar methods */
void		e_cal_client_get_default_object			(ECalClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_default_object_finish		(ECalClient *client, GAsyncResult *result, icalcomponent **icalcomp, GError **error);
gboolean	e_cal_client_get_default_object_sync		(ECalClient *client, icalcomponent **icalcomp, GCancellable *cancellable, GError **error);

void		e_cal_client_get_object				(ECalClient *client, const gchar *uid, const gchar *rid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_object_finish			(ECalClient *client, GAsyncResult *result, icalcomponent **icalcomp, GError **error);
gboolean	e_cal_client_get_object_sync			(ECalClient *client, const gchar *uid, const gchar *rid, icalcomponent **icalcomp, GCancellable *cancellable, GError **error);

void		e_cal_client_get_objects_for_uid		(ECalClient *client, const gchar *uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_objects_for_uid_finish		(ECalClient *client, GAsyncResult *result, GSList **ecalcomps, GError **error);
gboolean	e_cal_client_get_objects_for_uid_sync		(ECalClient *client, const gchar *uid, GSList **ecalcomps, GCancellable *cancellable, GError **error);

void		e_cal_client_get_object_list			(ECalClient *client, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_object_list_finish		(ECalClient *client, GAsyncResult *result, GSList **icalcomps, GError **error);
gboolean	e_cal_client_get_object_list_sync		(ECalClient *client, const gchar *sexp, GSList **icalcomps, GCancellable *cancellable, GError **error);

void		e_cal_client_get_object_list_as_comps		(ECalClient *client, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_object_list_as_comps_finish	(ECalClient *client, GAsyncResult *result, GSList **ecalcomps, GError **error);
gboolean	e_cal_client_get_object_list_as_comps_sync	(ECalClient *client, const gchar *sexp, GSList **ecalcomps, GCancellable *cancellable, GError **error);

void		e_cal_client_get_free_busy			(ECalClient *client, time_t start, time_t end, const GSList *users, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_free_busy_finish		(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_get_free_busy_sync			(ECalClient *client, time_t start, time_t end, const GSList *users, GCancellable *cancellable, GError **error);

void		e_cal_client_create_object			(ECalClient *client, /* const */ icalcomponent *icalcomp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_create_object_finish		(ECalClient *client, GAsyncResult *result, gchar **uid, GError **error);
gboolean	e_cal_client_create_object_sync			(ECalClient *client, /* const */ icalcomponent *icalcomp, gchar **uid, GCancellable *cancellable, GError **error);

void		e_cal_client_create_objects			(ECalClient *client, GSList *icalcomps, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_create_objects_finish	(ECalClient *client, GAsyncResult *result, GSList **uids, GError **error);
gboolean	e_cal_client_create_objects_sync	(ECalClient *client, GSList *icalcomps, GSList **uids, GCancellable *cancellable, GError **error);

void		e_cal_client_modify_object			(ECalClient *client, /* const */ icalcomponent *icalcomp, CalObjModType mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_modify_object_finish		(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_modify_object_sync			(ECalClient *client, /* const */ icalcomponent *icalcomp, CalObjModType mod, GCancellable *cancellable, GError **error);

void		e_cal_client_modify_objects			(ECalClient *client, /* const */ GSList *comps, CalObjModType mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_modify_objects_finish	(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_modify_objects_sync	(ECalClient *client, /* const */ GSList *comps, CalObjModType mod, GCancellable *cancellable, GError **error);

void		e_cal_client_remove_object			(ECalClient *client, const gchar *uid, const gchar *rid, CalObjModType mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_remove_object_finish		(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_remove_object_sync			(ECalClient *client, const gchar *uid, const gchar *rid, CalObjModType mod, GCancellable *cancellable, GError **error);

void		e_cal_client_remove_objects			(ECalClient *client, const GSList *ids, CalObjModType mod, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_remove_objects_finish	(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_remove_objects_sync	(ECalClient *client, const GSList *ids, CalObjModType mod, GCancellable *cancellable, GError **error);

void		e_cal_client_receive_objects			(ECalClient *client, /* const */ icalcomponent *icalcomp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_receive_objects_finish		(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_receive_objects_sync		(ECalClient *client, /* const */ icalcomponent *icalcomp, GCancellable *cancellable, GError **error);

void		e_cal_client_send_objects			(ECalClient *client, /* const */ icalcomponent *icalcomp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_send_objects_finish		(ECalClient *client, GAsyncResult *result, GSList **users, icalcomponent **modified_icalcomp, GError **error);
gboolean	e_cal_client_send_objects_sync			(ECalClient *client, /* const */ icalcomponent *icalcomp, GSList **users, icalcomponent **modified_icalcomp, GCancellable *cancellable, GError **error);

void		e_cal_client_get_attachment_uris		(ECalClient *client, const gchar *uid, const gchar *rid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_attachment_uris_finish		(ECalClient *client, GAsyncResult *result, GSList **attachment_uris, GError **error);
gboolean	e_cal_client_get_attachment_uris_sync		(ECalClient *client, const gchar *uid, const gchar *rid, GSList **attachment_uris, GCancellable *cancellable, GError **error);

void		e_cal_client_discard_alarm			(ECalClient *client, const gchar *uid, const gchar *rid, const gchar *auid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_discard_alarm_finish		(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_discard_alarm_sync			(ECalClient *client, const gchar *uid, const gchar *rid, const gchar *auid, GCancellable *cancellable, GError **error);

void		e_cal_client_get_view				(ECalClient *client, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_view_finish			(ECalClient *client, GAsyncResult *result, ECalClientView **view, GError **error);
gboolean	e_cal_client_get_view_sync			(ECalClient *client, const gchar *sexp, ECalClientView **view, GCancellable *cancellable, GError **error);

void		e_cal_client_get_timezone			(ECalClient *client, const gchar *tzid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_get_timezone_finish		(ECalClient *client, GAsyncResult *result, icaltimezone **zone, GError **error);
gboolean	e_cal_client_get_timezone_sync			(ECalClient *client, const gchar *tzid, icaltimezone **zone, GCancellable *cancellable, GError **error);

void		e_cal_client_add_timezone			(ECalClient *client, /* const */ icaltimezone *zone, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_cal_client_add_timezone_finish		(ECalClient *client, GAsyncResult *result, GError **error);
gboolean	e_cal_client_add_timezone_sync			(ECalClient *client, /* const */ icaltimezone *zone, GCancellable *cancellable, GError **error);

G_END_DECLS

#endif /* E_CAL_CLIENT_H */
