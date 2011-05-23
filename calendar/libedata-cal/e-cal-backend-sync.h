/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#ifndef __E_CAL_BACKEND_SYNC_H__
#define __E_CAL_BACKEND_SYNC_H__

#include <glib.h>
#include <libedata-cal/e-cal-backend.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_SYNC         (e_cal_backend_sync_get_type ())
#define E_CAL_BACKEND_SYNC(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSync))
#define E_CAL_BACKEND_SYNC_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSyncClass))
#define E_IS_CAL_BACKEND_SYNC(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CAL_BACKEND_SYNC))
#define E_IS_CAL_BACKEND_SYNC_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CAL_BACKEND_SYNC))
#define E_CAL_BACKEND_SYNC_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSyncClass))

typedef struct _ECalBackendSync ECalBackendSync;
typedef struct _ECalBackendSyncClass ECalBackendSyncClass;
typedef struct _ECalBackendSyncPrivate ECalBackendSyncPrivate;

struct _ECalBackendSync {
	ECalBackend parent_object;

	ECalBackendSyncPrivate *priv;
};

struct _ECalBackendSyncClass {
	ECalBackendClass parent_class;

	/* Virtual methods */
	void	(* open_sync)			(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, gboolean only_if_exists, GError **error);
	void	(* remove_sync)			(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, GError **error);

	void	(* refresh_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, GError **error);
	gboolean (* get_backend_property_sync)	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *prop_name, gchar **prop_value, GError **error);
	gboolean (* set_backend_property_sync)	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value, GError **error);
	void	(* get_object_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, gchar **calobj, GError **error);
	void	(* get_object_list_sync)	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *sexp, GSList **calobjs, GError **error);
	void	(* get_free_busy_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const GSList *users, time_t start, time_t end, GSList **freebusyobjs, GError **error);
	void	(* create_object_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, gchar **uid, gchar **new_object, GError **error);
	void	(* modify_object_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, CalObjModType mod, gchar **old_object, gchar **new_object, GError **error);
	void	(* remove_object_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, CalObjModType mod, gchar **old_object, gchar **new_object, GError **error);
	void	(* receive_objects_sync)	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, GError **error);
	void	(* send_objects_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, GSList **users, gchar **modified_calobj, GError **error);
	void	(* get_attachment_uris_sync)	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, GSList **attachments, GError **error);
	void	(* discard_alarm_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, const gchar *auid, GError **error);
	void	(* get_timezone_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *tzid, gchar **tzobject, GError **error);
	void	(* add_timezone_sync)		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *tzobject, GError **error);

	void	(* authenticate_user_sync)	(ECalBackendSync *backend, GCancellable *cancellable, ECredentials *credentials, GError **error);
};

GType	e_cal_backend_sync_get_type		(void);

void	e_cal_backend_sync_set_lock		(ECalBackendSync *backend, gboolean lock);

void	e_cal_backend_sync_open			(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, gboolean only_if_exists, GError **error);
void	e_cal_backend_sync_remove		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, GError **error);
void	e_cal_backend_sync_refresh		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, GError **error);
gboolean e_cal_backend_sync_get_backend_property(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *prop_name, gchar **prop_value, GError **error);
gboolean e_cal_backend_sync_set_backend_property(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *prop_name, const gchar *prop_value, GError **error);
void	e_cal_backend_sync_get_object		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, gchar **calobj, GError **error);
void	e_cal_backend_sync_get_object_list	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *sexp, GSList **calobjs, GError **error);
void	e_cal_backend_sync_get_free_busy	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const GSList *users, time_t start, time_t end, GSList **freebusyobjs, GError **error);
void	e_cal_backend_sync_create_object	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, gchar **uid, gchar **new_object, GError **error);
void	e_cal_backend_sync_modify_object	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, CalObjModType mod, gchar **old_object, gchar **new_object, GError **error);
void	e_cal_backend_sync_remove_object	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, CalObjModType mod, gchar **old_object, gchar **new_object, GError **error);
void	e_cal_backend_sync_receive_objects	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, GError **error);
void	e_cal_backend_sync_send_objects		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *calobj, GSList **users, gchar **modified_calobj, GError **error);
void	e_cal_backend_sync_get_attachment_uris	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, GSList **attachments, GError **error);
void	e_cal_backend_sync_discard_alarm	(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *uid, const gchar *rid, const gchar *auid, GError **error);
void	e_cal_backend_sync_get_timezone		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *tzid, gchar **tzobject, GError **error);
void	e_cal_backend_sync_add_timezone		(ECalBackendSync *backend, EDataCal *cal, GCancellable *cancellable, const gchar *tzobject, GError **error);

void	e_cal_backend_sync_authenticate_user	(ECalBackendSync *backend, GCancellable *cancellable, ECredentials *credentials, GError **error);

G_END_DECLS

#endif /* __E_CAL_BACKEND_SYNC_H__ */
