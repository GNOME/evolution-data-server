/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_CAL_META_BACKEND_H
#define E_CAL_META_BACKEND_H

#include <libebackend/libebackend.h>
#include <libedata-cal/e-cal-backend.h>
#include <libedata-cal/e-cal-cache.h>
#include <libecal/libecal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_META_BACKEND \
	(e_cal_meta_backend_get_type ())
#define E_CAL_META_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_META_BACKEND, ECalMetaBackend))
#define E_CAL_META_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_META_BACKEND, ECalMetaBackendClass))
#define E_IS_CAL_META_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_META_BACKEND))
#define E_IS_CAL_META_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_META_BACKEND))
#define E_CAL_META_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_META_BACKEND, ECalMetaBackendClass))

G_BEGIN_DECLS

typedef struct _ECalMetaBackendInfo {
	gchar *uid;
	gchar *rid;
	gchar *revision;
} ECalMetaBackendInfo;

#define E_TYPE_CAL_META_BACKEND_INFO (e_cal_meta_backend_info_get_type ())

GType		e_cal_meta_backend_info_get_type
						(void) G_GNUC_CONST;
ECalMetaBackendInfo *
		e_cal_meta_backend_info_new	(const gchar *uid,
						 const gchar *rid,
						 const gchar *revision);
ECalMetaBackendInfo *
		e_cal_meta_backend_info_copy	(const ECalMetaBackendInfo *src);
void		e_cal_meta_backend_info_free	(gpointer ptr /* ECalMetaBackendInfo * */);

typedef struct _ECalMetaBackend ECalMetaBackend;
typedef struct _ECalMetaBackendClass ECalMetaBackendClass;
typedef struct _ECalMetaBackendPrivate ECalMetaBackendPrivate;

/**
 * ECalMetaBackend:
 *
 * Contains only private data that should be read and manipulated using
 * the functions below.
 *
 * Since: 3.26
 **/
struct _ECalMetaBackend {
	/*< private >*/
	ECalBackendSync parent;
	ECalMetaBackendPrivate *priv;
};

/**
 * ECalMetaBackendClass:
 *
 * Class structure for the #ECalMetaBackend class.
 *
 * Since: 3.26
 */
struct _ECalMetaBackendClass {
	/*< private >*/
	ECalBackendSyncClass parent_class;

	/* Virtual methods */
	gboolean	(* connect_sync)	(ECalMetaBackend *meta_backend,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* disconnect_sync)	(ECalMetaBackend *meta_backend,
						 GCancellable *cancellable,
						 GError **error);

	gboolean	(* get_changes_sync)	(ECalMetaBackend *meta_backend,
						 const gchar *last_sync_tag,
						 gchar **out_new_sync_tag,
						 gboolean *out_repeat,
						 GSList **out_created_objects, /* ECalMetaBackendInfo * */
						 GSList **out_modified_objects, /* ECalMetaBackendInfo * */
						 GSList **out_removed_objects, /* ECalMetaBackendInfo * */
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* list_existing_sync)	(ECalMetaBackend *meta_backend,
						 gchar **out_new_sync_tag,
						 GSList **out_existing_objects, /* ECalMetaBackendInfo * */
						 GCancellable *cancellable,
						 GError **error);

	gboolean	(* save_component_sync)	(ECalMetaBackend *meta_backend,
						 gboolean overwrite_existing,
						 EConflictResolution conflict_resolution,
						 const GSList *instances, /* ECalComponent * */
						 const gchar *extra,
						 gchar **out_new_uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* load_component_sync)	(ECalMetaBackend *meta_backend,
						 const gchar *uid,
						 icalcomponent **out_instances,
						 gchar **out_extra,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(* remove_component_sync)
						(ECalMetaBackend *meta_backend,
						 EConflictResolution conflict_resolution,
						 const gchar *uid,
						 const gchar *extra,
						 GCancellable *cancellable,
						 GError **error);

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_cal_meta_backend_get_type	(void) G_GNUC_CONST;

const gchar *	e_cal_meta_backend_get_capabilities
						(ECalMetaBackend *meta_backend);
void		e_cal_meta_backend_set_cache	(ECalMetaBackend *meta_backend,
						 ECalCache *cache);
ECalCache *	e_cal_meta_backend_ref_cache	(ECalMetaBackend *meta_backend);
icalcomponent *	e_cal_meta_backend_merge_instances
						(ECalMetaBackend *meta_backend,
						 const GSList *instances, /* ECalComponent * */
						 gboolean replace_tzid_with_location);
gboolean	e_cal_meta_backend_inline_local_attachments_sync
						(ECalMetaBackend *meta_backend,
						 icalcomponent *component,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_store_inline_attachments_sync
						(ECalMetaBackend *meta_backend,
						 icalcomponent *component,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_connect_sync	(ECalMetaBackend *meta_backend,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_disconnect_sync
						(ECalMetaBackend *meta_backend,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_get_changes_sync
						(ECalMetaBackend *meta_backend,
						 const gchar *last_sync_tag,
						 gchar **out_new_sync_tag,
						 gboolean *out_repeat,
						 GSList **out_created_objects, /* ECalMetaBackendInfo * */
						 GSList **out_modified_objects, /* ECalMetaBackendInfo * */
						 GSList **out_removed_objects, /* ECalMetaBackendInfo * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_list_existing_sync
						(ECalMetaBackend *meta_backend,
						 gchar **out_new_sync_tag,
						 GSList **out_existing_objects, /* ECalMetaBackendInfo * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_save_component_sync
						(ECalMetaBackend *meta_backend,
						 gboolean overwrite_existing,
						 EConflictResolution conflict_resolution,
						 const GSList *instances, /* ECalComponent * */
						 const gchar *extra,
						 gchar **out_new_uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_load_component_sync
						(ECalMetaBackend *meta_backend,
						 const gchar *uid,
						 icalcomponent **out_component,
						 gchar **out_extra,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_cal_meta_backend_remove_component_sync
						(ECalMetaBackend *meta_backend,
						 EConflictResolution conflict_resolution,
						 const gchar *uid,
						 const gchar *extra,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_CAL_META_BACKEND_H */
