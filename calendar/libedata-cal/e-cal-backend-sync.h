/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_CAL_BACKEND_SYNC_H
#define E_CAL_BACKEND_SYNC_H

#include <libedata-cal/e-cal-backend.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_SYNC \
	(e_cal_backend_sync_get_type ())
#define E_CAL_BACKEND_SYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSync))
#define E_CAL_BACKEND_SYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSyncClass))
#define E_IS_CAL_BACKEND_SYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_SYNC))
#define E_IS_CAL_BACKEND_SYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_SYNC))
#define E_CAL_BACKEND_SYNC_GET_CLASS(cls) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((cls), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSyncClass))

G_BEGIN_DECLS

typedef struct _ECalBackendSync ECalBackendSync;
typedef struct _ECalBackendSyncClass ECalBackendSyncClass;
typedef struct _ECalBackendSyncPrivate ECalBackendSyncPrivate;

struct _ECalBackendSync {
	ECalBackend parent;
	ECalBackendSyncPrivate *priv;
};

struct _ECalBackendSyncClass {
	ECalBackendClass parent_class;

	/* Virtual methods */
	void		(*open_sync)		(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 gboolean only_if_exists,
						 GError **error);
	void		(*refresh_sync)		(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 GError **error);

	/* This method is deprecated. */
	gboolean	(*set_backend_property_sync)
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *prop_name,
						 const gchar *prop_value,
						 GError **error);

	void		(*get_object_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 gchar **calobj,
						 GError **error);
	void		(*get_object_list_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *sexp,
						 GSList **calobjs,
						 GError **error);
	void		(*get_free_busy_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *users,
						 time_t start,
						 time_t end,
						 GSList **freebusyobjs,
						 GError **error);
	void		(*create_objects_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *calobjs,
						 GSList **uids,
						 GSList **new_components,
						 GError **error);
	void		(*modify_objects_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *calobjs,
						 ECalObjModType mod,
						 GSList **old_components,
						 GSList **new_components,
						 GError **error);
	void		(*remove_objects_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *ids,
						 ECalObjModType mod,
						 GSList **old_components,
						 GSList **new_components,
						 GError **error);
	void		(*receive_objects_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *calobj,
						 GError **error);
	void		(*send_objects_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *calobj,
						 GSList **users,
						 gchar **modified_calobj,
						 GError **error);
	void		(*get_attachment_uris_sync)
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 GSList **attachments,
						 GError **error);
	void		(*discard_alarm_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *auid,
						 GError **error);
	void		(*get_timezone_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *tzid,
						 gchar **tzobject,
						 GError **error);
	void		(*add_timezone_sync)	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *tzobject,
						 GError **error);
};

GType		e_cal_backend_sync_get_type	(void) G_GNUC_CONST;
void		e_cal_backend_sync_open		(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 gboolean only_if_exists,
						 GError **error);
void		e_cal_backend_sync_refresh	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_sync_get_object	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 gchar **calobj,
						 GError **error);
void		e_cal_backend_sync_get_object_list
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *sexp,
						 GSList **calobjs,
						 GError **error);
void		e_cal_backend_sync_get_free_busy
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *users,
						 time_t start,
						 time_t end,
						 GSList **freebusyobjects,
						 GError **error);
void		e_cal_backend_sync_create_objects
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *calobjs,
						 GSList **uids,
						 GSList **new_components,
						 GError **error);
void		e_cal_backend_sync_modify_objects
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *calobjs,
						 ECalObjModType mod,
						 GSList **old_components,
						 GSList **new_components,
						 GError **error);
void		e_cal_backend_sync_remove_objects
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const GSList *ids,
						 ECalObjModType mod,
						 GSList **old_components,
						 GSList **new_components,
						 GError **error);
void		e_cal_backend_sync_receive_objects
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *calobj,
						 GError **error);
void		e_cal_backend_sync_send_objects	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *calobj,
						 GSList **users,
						 gchar **modified_calobj,
						 GError **error);
void		e_cal_backend_sync_get_attachment_uris
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 GSList **attachments,
						 GError **error);
void		e_cal_backend_sync_discard_alarm
						(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *auid,
						 GError **error);
void		e_cal_backend_sync_get_timezone	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *tzid,
						 gchar **tzobject,
						 GError **error);
void		e_cal_backend_sync_add_timezone	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *tzobject,
						 GError **error);

G_END_DECLS

#endif /* E_CAL_BACKEND_SYNC_H */
