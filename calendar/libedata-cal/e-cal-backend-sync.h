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
	void (*is_read_only_sync)  (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only, GError **perror);
	void (*get_cal_address_sync)  (ECalBackendSync *backend, EDataCal *cal, gchar **address, GError **perror);
	void (*get_alarm_email_address_sync)  (ECalBackendSync *backend, EDataCal *cal, gchar **address, GError **perror);
	void (*get_ldap_attribute_sync)  (ECalBackendSync *backend, EDataCal *cal, gchar **attribute, GError **perror);
	void (*get_static_capabilities_sync)  (ECalBackendSync *backend, EDataCal *cal, gchar **capabilities, GError **perror);

	void (*open_sync)  (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists, const gchar *username, const gchar *password, GError **perror);
	void (*refresh_sync)  (ECalBackendSync *backend, EDataCal *cal, GError **perror);
	void (*remove_sync)  (ECalBackendSync *backend, EDataCal *cal, GError **perror);

	void (*create_object_sync)  (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid, GError **perror);
	void (*modify_object_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, CalObjModType mod, gchar **old_object, gchar **new_object, GError **perror);
	void (*remove_object_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, CalObjModType mod, gchar **old_object, gchar **object, GError **perror);

	void (*discard_alarm_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *auid, GError **perror);

	void (*receive_objects_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GError **perror);
	void (*send_objects_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, GList **users,
						     gchar **modified_calobj, GError **perror);

	void (*get_default_object_sync)  (ECalBackendSync *backend, EDataCal *cal, gchar **object, GError **perror);
	void (*get_object_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, gchar **object, GError **perror);
	void (*get_object_list_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *sexp, GList **objects, GError **perror);

	void (*get_attachment_list_sync)  (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, GSList **attachments, GError **perror);

	void (*get_timezone_sync) (ECalBackendSync *backend, EDataCal *cal, const gchar *tzid, gchar **object, GError **perror);
	void (*add_timezone_sync) (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj, GError **perror);
	void (*set_default_zone_sync) (ECalBackendSync *backend, EDataCal *cal, const gchar *tz, GError **perror);

	void (*get_changes_sync) (ECalBackendSync *backend, EDataCal *cal, const gchar *change_id, GList **adds, GList **modifies, GList **deletes, GError **perror);
	void (*get_freebusy_sync) (ECalBackendSync *backend, EDataCal *cal, GList *users, time_t start, time_t end, GList **freebusy, GError **perror);

	/* Padding for future expansion */
	void (*_cal_reserved0) (void);
	void (*_cal_reserved1) (void);
	void (*_cal_reserved2) (void);
	void (*_cal_reserved3) (void);
	void (*_cal_reserved4) (void);

};

typedef ECalBackendSync * (*ECalBackendSyncFactoryFn) (void);
GType                e_cal_backend_sync_get_type                (void);

void	e_cal_backend_sync_set_lock		(ECalBackendSync *backend,
						 gboolean lock);

void	e_cal_backend_sync_is_read_only		(ECalBackendSync *backend,
						 EDataCal *cal,
						 gboolean *read_only,
						 GError **error);
void	e_cal_backend_sync_get_cal_address	(ECalBackendSync *backend,
						 EDataCal *cal,
						 gchar **address,
						 GError **error);
void	e_cal_backend_sync_get_alarm_email_address
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 gchar **address,
						 GError **error);
void	e_cal_backend_sync_get_ldap_attribute	(ECalBackendSync *backend,
						 EDataCal *cal,
						 gchar **attribute,
						 GError **error);
void	e_cal_backend_sync_get_static_capabilities
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 gchar **capabiliites,
						 GError **error);
void	e_cal_backend_sync_open			(ECalBackendSync *backend,
						 EDataCal *cal,
						 gboolean only_if_exists,
						 const gchar *username,
						 const gchar *password,
						 GError **error);
void	e_cal_backend_sync_refresh		(ECalBackendSync *backend,
						 EDataCal *cal,
						 GError **error);
void	e_cal_backend_sync_remove		(ECalBackendSync *backend,
						 EDataCal *cal,
						 GError **error);
void	e_cal_backend_sync_create_object	(ECalBackendSync *backend,
						 EDataCal *cal,
						 gchar **calobj,
						 gchar **uid,
						 GError **error);
void	e_cal_backend_sync_modify_object	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *calobj,
						 CalObjModType mod,
						 gchar **old_object,
						 gchar **new_object,
						 GError **error);
void	e_cal_backend_sync_remove_object	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *uid,
						 const gchar *rid,
						 CalObjModType mod,
						 gchar **old_object,
						 gchar **object,
						 GError **error);
void	e_cal_backend_sync_get_attachment_list	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *uid,
						 const gchar *rid,
						 GSList **attachments,
						 GError **error);

void	e_cal_backend_sync_discard_alarm	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *uid,
						 const gchar *auid,
						 GError **error);

void	e_cal_backend_sync_receive_objects	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *calobj,
						 GError **error);
void	e_cal_backend_sync_send_objects		(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *calobj,
						 GList **users,
						 gchar **modified_calobj,
						 GError **error);
void	e_cal_backend_sync_get_default_object	(ECalBackendSync *backend,
						 EDataCal *cal,
						 gchar **object,
						 GError **error);

void	e_cal_backend_sync_get_object		(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *uid,
						 const gchar *rid,
						 gchar **object,
						 GError **error);

void	e_cal_backend_sync_get_object_list	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *sexp,
						 GList **objects,
						 GError **error);

void	e_cal_backend_sync_get_timezone		(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *tzid,
						 gchar **object,
						 GError **error);
void	e_cal_backend_sync_add_timezone		(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *tzobj,
						 GError **error);
void	e_cal_backend_sync_set_default_zone	(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *tzobj,
						 GError **error);

void	e_cal_backend_sync_get_changes		(ECalBackendSync *backend,
						 EDataCal *cal,
						 const gchar *change_id,
						 GList **adds,
						 GList **modifies,
						 GList **deletes,
						 GError **error);
void	e_cal_backend_sync_get_free_busy	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GList *users,
						 time_t start,
						 time_t end,
						 GList **freebusy,
						 GError **error);

G_END_DECLS

#endif /* __E_CAL_BACKEND_SYNC_H__ */
