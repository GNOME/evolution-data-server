/*
 * e-cal-backend.h
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
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_CAL_BACKEND_H
#define E_CAL_BACKEND_H

#include <libecal/libecal.h>
#include <libebackend/libebackend.h>

#include <libedata-cal/e-data-cal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND \
	(e_cal_backend_get_type ())
#define E_CAL_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND, ECalBackend))
#define E_CAL_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND, ECalBackendClass))
#define E_IS_CAL_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND))
#define E_IS_CAL_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND))
#define E_CAL_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND, ECalBackendClass))

/**
 * CLIENT_BACKEND_PROPERTY_CAPABILITIES:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"

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
 * CAL_BACKEND_PROPERTY_REVISION:
 *
 * The current overall revision string, this can be used as
 * a quick check to see if data has changed at all since the
 * last time the calendar revision was observed.
 *
 * Since: 3.4
 **/
#define CAL_BACKEND_PROPERTY_REVISION			"revision"

G_BEGIN_DECLS

struct _ECalBackendCache;

typedef struct _ECalBackend ECalBackend;
typedef struct _ECalBackendClass ECalBackendClass;
typedef struct _ECalBackendPrivate ECalBackendPrivate;

struct _ECalBackend {
	EBackend parent;
	ECalBackendPrivate *priv;
};

struct _ECalBackendClass {
	EBackendClass parent_class;

	/* Virtual methods */
	void		(*get_backend_property)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *prop_name);

	void		(*open)			(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 gboolean only_if_exists);

	void		(*refresh)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable);
	void		(*get_object)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid);
	void		(*get_object_list)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *sexp);
	void		(*get_free_busy)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *users,
						 time_t start,
						 time_t end);
	void		(*create_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *calobjs);
	void		(*modify_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *calobjs,
						 ECalObjModType mod);
	void		(*remove_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *ids,
						 ECalObjModType mod);
	void		(*receive_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *calobj);
	void		(*send_objects)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *calobj);
	void		(*get_attachment_uris)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid);
	void		(*discard_alarm)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *auid);
	void		(*get_timezone)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *tzid);
	void		(*add_timezone)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *tzobject);

	void		(*start_view)		(ECalBackend *backend,
						 EDataCalView *view);
	void		(*stop_view)		(ECalBackend *backend,
						 EDataCalView *view);

	/* Signals */
	void		(*closed)		(ECalBackend *backend,
						 const gchar *sender);
};

GType		e_cal_backend_get_type		(void) G_GNUC_CONST;
icalcomponent_kind
		e_cal_backend_get_kind		(ECalBackend *backend);
EDataCal *	e_cal_backend_ref_data_cal	(ECalBackend *backend);
void		e_cal_backend_set_data_cal	(ECalBackend *backend,
						 EDataCal *data_cal);
ESourceRegistry *
		e_cal_backend_get_registry	(ECalBackend *backend);
gboolean	e_cal_backend_get_writable	(ECalBackend *backend);
void		e_cal_backend_set_writable	(ECalBackend *backend,
						 gboolean writable);
gboolean	e_cal_backend_is_opened		(ECalBackend *backend);
gboolean	e_cal_backend_is_readonly	(ECalBackend *backend);
gboolean	e_cal_backend_is_removed	(ECalBackend *backend);

const gchar *	e_cal_backend_get_cache_dir	(ECalBackend *backend);
void		e_cal_backend_set_cache_dir	(ECalBackend *backend,
						 const gchar *cache_dir);
gchar *		e_cal_backend_create_cache_filename
						(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *filename,
						 gint fileindex);

void		e_cal_backend_add_view		(ECalBackend *backend,
						 EDataCalView *view);
void		e_cal_backend_remove_view	(ECalBackend *backend,
						 EDataCalView *view);
GList *		e_cal_backend_list_views	(ECalBackend *backend);

gchar *		e_cal_backend_get_backend_property_sync
						(ECalBackend *backend,
						 const gchar *prop_name,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_get_backend_property
						(ECalBackend *backend,
						 const gchar *prop_name,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gchar *		e_cal_backend_get_backend_property_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_open_sync		(ECalBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_open		(ECalBackend *backend,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_open_finish	(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_refresh_sync	(ECalBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_refresh		(ECalBackend *backend,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_refresh_finish	(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
ECalComponent *	e_cal_backend_get_object_sync	(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_get_object	(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
ECalComponent *	e_cal_backend_get_object_finish	(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_get_object_list_sync
						(ECalBackend *backend,
						 const gchar *query,
						 GQueue *out_objects,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_get_object_list	(ECalBackend *backend,
						 const gchar *query,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_get_object_list_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_objects,
						 GError **error);
gboolean	e_cal_backend_get_free_busy_sync
						(ECalBackend *backend,
						 time_t start,
						 time_t end,
						 const gchar * const *users,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_get_free_busy	(ECalBackend *backend,
						 time_t start,
						 time_t end,
						 const gchar * const *users,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_get_free_busy_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_create_objects_sync
						(ECalBackend *backend,
						 const gchar * const *calobjs,
						 GQueue *out_uids,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_create_objects	(ECalBackend *backend,
						 const gchar * const *calobjs,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_create_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_uids,
						 GError **error);
gboolean	e_cal_backend_modify_objects_sync
						(ECalBackend *backend,
						 const gchar * const *calobjs,
						 ECalObjModType mod,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_modify_objects	(ECalBackend *backend,
						 const gchar * const *calobjs,
						 ECalObjModType mod,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_modify_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_remove_objects_sync
						(ECalBackend *backend,
						 GList *component_ids,
						 ECalObjModType mod,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_remove_objects	(ECalBackend *backend,
						 GList *component_ids,
						 ECalObjModType mod,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_remove_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_receive_objects_sync
						(ECalBackend *backend,
						 const gchar *calobj,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_receive_objects	(ECalBackend *backend,
						 const gchar *calobj,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_receive_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
ECalComponent *	e_cal_backend_send_objects_sync	(ECalBackend *backend,
						 const gchar *calobj,
						 GQueue *out_users,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_send_objects	(ECalBackend *backend,
						 const gchar *calobj,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
ECalComponent *	e_cal_backend_send_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_users,
						 GError **error);
gboolean	e_cal_backend_get_attachment_uris_sync
						(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 GQueue *out_attachment_uris,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_get_attachment_uris
						(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_get_attachment_uris_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GQueue *out_attachment_uris,
						 GError **error);
gboolean	e_cal_backend_discard_alarm_sync
						(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *alarm_uid,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_discard_alarm	(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *alarm_uid,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_discard_alarm_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gchar *		e_cal_backend_get_timezone_sync	(ECalBackend *backend,
						 const gchar *tzid,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_get_timezone	(ECalBackend *backend,
						 const gchar *tzid,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gchar *		e_cal_backend_get_timezone_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_add_timezone_sync	(ECalBackend *backend,
						 const gchar *tzobject,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_add_timezone	(ECalBackend *backend,
						 const gchar *tzobject,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_add_timezone_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
void		e_cal_backend_start_view	(ECalBackend *backend,
						 EDataCalView *view);
void		e_cal_backend_stop_view		(ECalBackend *backend,
						 EDataCalView *view);

void		e_cal_backend_notify_component_created
						(ECalBackend *backend,
						 ECalComponent *component);
void		e_cal_backend_notify_component_modified
						(ECalBackend *backend,
						 ECalComponent *old_component,
						 ECalComponent *new_component);
void		e_cal_backend_notify_component_removed
						(ECalBackend *backend,
						 const ECalComponentId *id,
						 ECalComponent *old_component,
						 ECalComponent *new_component);

void		e_cal_backend_notify_error	(ECalBackend *backend,
						 const gchar *message);
void		e_cal_backend_notify_property_changed
						(ECalBackend *backend,
						 const gchar *prop_name,
						 const gchar *prop_value);

void		e_cal_backend_empty_cache	(ECalBackend *backend,
						 struct _ECalBackendCache *cache);

/* protected functions for subclasses */
void		e_cal_backend_set_is_removed	(ECalBackend *backend,
						 gboolean is_removed);

GSimpleAsyncResult *
		e_cal_backend_prepare_for_completion
						(ECalBackend *backend,
						 guint opid,
						 GQueue **result_queue);

G_END_DECLS

#endif /* E_CAL_BACKEND_H */
