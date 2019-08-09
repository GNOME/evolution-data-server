/*
 * e-cal-backend.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
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

G_BEGIN_DECLS

typedef struct _ECalBackend ECalBackend;
typedef struct _ECalBackendClass ECalBackendClass;
typedef struct _ECalBackendPrivate ECalBackendPrivate;

/**
 * ECalBackend:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 */
struct _ECalBackend {
	/*< private >*/
	EBackend parent;
	ECalBackendPrivate *priv;
};

/**
 * ECalBackendClass:
 * @use_serial_dispatch_queue: Whether a serial dispatch queue should
 *                             be used for this backend or not. The default is %TRUE.
 * @impl_get_backend_property: Fetch a property value by name from the backend
 * @impl_open: Open the backend
 * @impl_refresh: Refresh the backend
 * @impl_get_object: Fetch a calendar object
 * @impl_get_object_list: FIXME: Document me
 * @impl_get_free_busy: FIXME: Document me
 * @impl_create_objects: FIXME: Document me
 * @impl_modify_objects: FIXME: Document me
 * @impl_remove_objects: FIXME: Document me
 * @impl_receive_objects: FIXME: Document me
 * @impl_send_objects: FIXME: Document me
 * @impl_get_attachment_uris: FIXME: Document me
 * @impl_discard_alarm: FIXME: Document me
 * @impl_get_timezone: FIXME: Document me
 * @impl_add_timezone: FIXME: Document me
 * @impl_start_view: Start up the specified view
 * @impl_stop_view: Stop the specified view
 * @closed: A signal notifying that the backend was closed
 * @shutdown: A signal notifying that the backend is being shut down
 *
 * Class structure for the #ECalBackend class.
 *
 * These virtual methods must be implemented when writing
 * a calendar backend.
 */
struct _ECalBackendClass {
	/*< private >*/
	EBackendClass parent_class;

	/*< public >*/

	/* Set this to TRUE to use a serial dispatch queue, instead
	 * of a concurrent dispatch queue.  A serial dispatch queue
	 * executes one method at a time in the order in which they
	 * were called.  This is generally slower than a concurrent
	 * dispatch queue, but helps avoid thread-safety issues. */
	gboolean use_serial_dispatch_queue;

	/* Virtual methods */
	gchar *		(*impl_get_backend_property)
						(ECalBackend *backend,
						 const gchar *prop_name);

	void		(*impl_open)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable);

	void		(*impl_refresh)		(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable);
	void		(*impl_get_object)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid);
	void		(*impl_get_object_list)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *sexp);
	void		(*impl_get_free_busy)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *users, /* gchar * */
						 time_t start,
						 time_t end);
	void		(*impl_create_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *calobjs, /* gchar * */
						 guint32 opflags); /* bit-or of ECalOperationFlags */
	void		(*impl_modify_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *calobjs, /* gchar * */
						 ECalObjModType mod,
						 guint32 opflags); /* bit-or of ECalOperationFlags */
	void		(*impl_remove_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const GSList *ids, /* ECalComponentId * */
						 ECalObjModType mod,
						 guint32 opflags); /* bit-or of ECalOperationFlags */
	void		(*impl_receive_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *calobj,
						 guint32 opflags); /* bit-or of ECalOperationFlags */
	void		(*impl_send_objects)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *calobj,
						 guint32 opflags); /* bit-or of ECalOperationFlags */
	void		(*impl_get_attachment_uris)
						(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid);
	void		(*impl_discard_alarm)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *auid,
						 guint32 opflags); /* bit-or of ECalOperationFlags */
	void		(*impl_get_timezone)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *tzid);
	void		(*impl_add_timezone)	(ECalBackend *backend,
						 EDataCal *cal,
						 guint32 opid,
						 GCancellable *cancellable,
						 const gchar *tzobject);

	void		(*impl_start_view)	(ECalBackend *backend,
						 EDataCalView *view);
	void		(*impl_stop_view)	(ECalBackend *backend,
						 EDataCalView *view);

	/* Signals */
	void		(*closed)		(ECalBackend *backend,
						 const gchar *sender);
	void		(*shutdown)		(ECalBackend *backend);

	/* Padding for future expansion */
	gpointer reserved_padding[20];
};

GType		e_cal_backend_get_type		(void) G_GNUC_CONST;
ICalComponentKind
		e_cal_backend_get_kind		(ECalBackend *backend);
EDataCal *	e_cal_backend_ref_data_cal	(ECalBackend *backend);
void		e_cal_backend_set_data_cal	(ECalBackend *backend,
						 EDataCal *data_cal);
GProxyResolver *
		e_cal_backend_ref_proxy_resolver
						(ECalBackend *backend);
ESourceRegistry *
		e_cal_backend_get_registry	(ECalBackend *backend);
gboolean	e_cal_backend_get_writable	(ECalBackend *backend);
void		e_cal_backend_set_writable	(ECalBackend *backend,
						 gboolean writable);
gboolean	e_cal_backend_is_opened		(ECalBackend *backend);
gboolean	e_cal_backend_is_readonly	(ECalBackend *backend);

const gchar *	e_cal_backend_get_cache_dir	(ECalBackend *backend);
gchar *		e_cal_backend_dup_cache_dir	(ECalBackend *backend);
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

typedef gboolean (*ECalBackendForeachViewFunc)	(ECalBackend *backend,
						 EDataCalView *view,
						 gpointer user_data);

gboolean	e_cal_backend_foreach_view	(ECalBackend *backend,
						 ECalBackendForeachViewFunc func,
						 gpointer user_data);
void		e_cal_backend_foreach_view_notify_progress
						(ECalBackend *backend,
						 gboolean only_completed_views,
						 gint percent,
						 const gchar *message);

gchar *		e_cal_backend_get_backend_property
						(ECalBackend *backend,
						 const gchar *prop_name);
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
gchar *		e_cal_backend_get_object_sync	(ECalBackend *backend,
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
gchar *		e_cal_backend_get_object_finish	(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_get_object_list_sync
						(ECalBackend *backend,
						 const gchar *query,
						 GQueue *out_objects, /* gchar * */
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
						 GQueue *out_objects, /* gchar * */
						 GError **error);
gboolean	e_cal_backend_get_free_busy_sync
						(ECalBackend *backend,
						 time_t start,
						 time_t end,
						 const gchar * const *users,
						 GSList **out_freebusy, /* gchar * */
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
						 GSList **out_freebusy, /* gchar * */
						 GError **error);
gboolean	e_cal_backend_create_objects_sync
						(ECalBackend *backend,
						 const gchar * const *calobjs,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GQueue *out_uids,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_create_objects	(ECalBackend *backend,
						 const gchar * const *calobjs,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
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
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_modify_objects	(ECalBackend *backend,
						 const gchar * const *calobjs,
						 ECalObjModType mod,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_modify_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_cal_backend_remove_objects_sync
						(ECalBackend *backend,
						 GList *component_ids, /* ECalComponentId * */
						 ECalObjModType mod,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_remove_objects	(ECalBackend *backend,
						 GList *component_ids, /* ECalComponentId * */
						 ECalObjModType mod,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
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
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_receive_objects	(ECalBackend *backend,
						 const gchar *calobj,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_cal_backend_receive_objects_finish
						(ECalBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gchar *		e_cal_backend_send_objects_sync	(ECalBackend *backend,
						 const gchar *calobj,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GQueue *out_users,
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_send_objects	(ECalBackend *backend,
						 const gchar *calobj,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gchar *		e_cal_backend_send_objects_finish
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
						 guint32 opflags, /* bit-or of ECalOperationFlags */
						 GCancellable *cancellable,
						 GError **error);
void		e_cal_backend_discard_alarm	(ECalBackend *backend,
						 const gchar *uid,
						 const gchar *rid,
						 const gchar *alarm_uid,
						 guint32 opflags, /* bit-or of ECalOperationFlags */
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

GSimpleAsyncResult *
		e_cal_backend_prepare_for_completion
						(ECalBackend *backend,
						 guint opid,
						 GQueue **result_queue);

/**
 * ECalBackendCustomOpFunc:
 * @cal_backend: an #ECalBackend
 * @user_data: a function user data, as provided to e_cal_backend_schedule_custom_operation()
 * @cancellable: an optional #GCancellable, as provided to e_cal_backend_schedule_custom_operation()
 * @error: return location for a #GError, or %NULL
 *
 * A callback prototype being called in a dedicated thread, scheduled
 * by e_cal_backend_schedule_custom_operation().
 *
 * Since: 3.26
 **/
typedef void	(* ECalBackendCustomOpFunc)	(ECalBackend *cal_backend,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);

void		e_cal_backend_schedule_custom_operation
						(ECalBackend *cal_backend,
						 GCancellable *use_cancellable,
						 ECalBackendCustomOpFunc func,
						 gpointer user_data,
						 GDestroyNotify user_data_free);

G_END_DECLS

#endif /* E_CAL_BACKEND_H */
