/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#ifndef __E_CAL_BACKEND_SYNC_H__
#define __E_CAL_BACKEND_SYNC_H__

#include <glib.h>
#include <libedata-cal/Evolution-DataServer-Calendar.h>
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

typedef GNOME_Evolution_Calendar_CallStatus ECalBackendSyncStatus;

struct _ECalBackendSync {
	ECalBackend parent_object;

	ECalBackendSyncPrivate *priv;
};

struct _ECalBackendSyncClass {
	ECalBackendClass parent_class;

	/* Virtual methods */
	ECalBackendSyncStatus (*is_read_only_sync)  (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only);
	ECalBackendSyncStatus (*get_cal_address_sync)  (ECalBackendSync *backend, EDataCal *cal, char **address);
	ECalBackendSyncStatus (*get_alarm_email_address_sync)  (ECalBackendSync *backend, EDataCal *cal, char **address);
	ECalBackendSyncStatus (*get_ldap_attribute_sync)  (ECalBackendSync *backend, EDataCal *cal, char **attribute);
	ECalBackendSyncStatus (*get_static_capabilities_sync)  (ECalBackendSync *backend, EDataCal *cal, char **capabilities);

	ECalBackendSyncStatus (*open_sync)  (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists, const char *username, const char *password);
	ECalBackendSyncStatus (*remove_sync)  (ECalBackendSync *backend, EDataCal *cal);

	ECalBackendSyncStatus (*create_object_sync)  (ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid);
	ECalBackendSyncStatus (*modify_object_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *calobj, CalObjModType mod, char **old_object, char **new_object);
	ECalBackendSyncStatus (*remove_object_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, CalObjModType mod, char **old_object, char **object);

	ECalBackendSyncStatus (*discard_alarm_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid);

	ECalBackendSyncStatus (*receive_objects_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *calobj);
	ECalBackendSyncStatus (*send_objects_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList **users,
						     char **modified_calobj);

	ECalBackendSyncStatus (*get_default_object_sync)  (ECalBackendSync *backend, EDataCal *cal, char **object);
	ECalBackendSyncStatus (*get_object_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object);
	ECalBackendSyncStatus (*get_object_list_sync)  (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects);

	ECalBackendSyncStatus (*get_timezone_sync) (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object);
	ECalBackendSyncStatus (*add_timezone_sync) (ECalBackendSync *backend, EDataCal *cal, const char *tzobj);
	ECalBackendSyncStatus (*set_default_timezone_sync) (ECalBackendSync *backend, EDataCal *cal, const char *tzid);

	ECalBackendSyncStatus (*get_changes_sync) (ECalBackendSync *backend, EDataCal *cal, const char *change_id, GList **adds, GList **modifies, GList **deletes);
	ECalBackendSyncStatus (*get_freebusy_sync) (ECalBackendSync *backend, EDataCal *cal, GList *users, time_t start, time_t end, GList **freebusy);

	/* Padding for future expansion */
	void (*_cal_reserved0) (void);
	void (*_cal_reserved1) (void);
	void (*_cal_reserved2) (void);
	void (*_cal_reserved3) (void);
	void (*_cal_reserved4) (void);

};

typedef ECalBackendSync * (*ECalBackendSyncFactoryFn) (void);
GType                e_cal_backend_sync_get_type                (void);
void e_cal_backend_sync_set_lock (ECalBackendSync  *backend, gboolean lock);

ECalBackendSyncStatus e_cal_backend_sync_is_read_only            (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       gboolean        *read_only);
ECalBackendSyncStatus e_cal_backend_sync_get_cal_address         (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       char           **address);
ECalBackendSyncStatus e_cal_backend_sync_get_alarm_email_address (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       char           **address);
ECalBackendSyncStatus e_cal_backend_sync_get_ldap_attribute      (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       char           **attribute);
ECalBackendSyncStatus e_cal_backend_sync_get_static_capabilities (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       char           **capabiliites);
ECalBackendSyncStatus e_cal_backend_sync_open                    (ECalBackendSync  *backend,
								  EDataCal             *cal,
								  gboolean         only_if_exists,
								  const char *username,
								  const char *password);
ECalBackendSyncStatus e_cal_backend_sync_remove                  (ECalBackendSync  *backend,
							       EDataCal             *cal);
ECalBackendSyncStatus e_cal_backend_sync_create_object           (ECalBackendSync  *backend,
								  EDataCal             *cal,
								  char           **calobj,
								  char           **uid);
ECalBackendSyncStatus e_cal_backend_sync_modify_object           (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       const char      *calobj,
							       CalObjModType    mod,
							       char           **old_object,
							       char           **new_object);
ECalBackendSyncStatus e_cal_backend_sync_remove_object           (ECalBackendSync  *backend,
							       EDataCal             *cal,
							       const char      *uid,
							       const char      *rid,
							       CalObjModType    mod,
							       char **old_object,
							       char **object);
ECalBackendSyncStatus e_cal_backend_sync_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid);

ECalBackendSyncStatus e_cal_backend_sync_receive_objects         (ECalBackendSync  *backend,
								  EDataCal         *cal,
								  const char       *calobj);
ECalBackendSyncStatus e_cal_backend_sync_send_objects            (ECalBackendSync  *backend,
								  EDataCal         *cal,
								  const char       *calobj,
								  GList **users,
								  char **modified_calobj);
ECalBackendSyncStatus e_cal_backend_sync_get_default_object         (ECalBackendSync  *backend,
								     EDataCal         *cal,
								     char            **object);

ECalBackendSyncStatus e_cal_backend_sync_get_object         (ECalBackendSync  *backend,
							     EDataCal             *cal,
							     const char *uid,
							     const char *rid,
							     char           **object);

ECalBackendSyncStatus e_cal_backend_sync_get_object_list         (ECalBackendSync  *backend,
								  EDataCal             *cal,
								  const char      *sexp,
								  GList          **objects);

ECalBackendSyncStatus e_cal_backend_sync_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object);
ECalBackendSyncStatus e_cal_backend_sync_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj);
ECalBackendSyncStatus e_cal_backend_sync_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid);

ECalBackendSyncStatus e_cal_backend_sync_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id, GList **adds, GList **modifies, GList **deletes);
ECalBackendSyncStatus e_cal_backend_sync_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users, time_t start, time_t end, GList **freebusy);

G_END_DECLS

#endif /* ! __E_CAL_BACKEND_SYNC_H__ */
