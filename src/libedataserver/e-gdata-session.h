/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_GDATA_SESSION_H
#define E_GDATA_SESSION_H

#ifndef __GI_SCANNER__

#include <json-glib/json-glib.h>
#include <libedataserver/e-gdata-query.h>
#include <libedataserver/e-soup-session.h>

/* Standard GObject macros */
#define E_TYPE_GDATA_SESSION \
	(e_gdata_session_get_type ())
#define E_GDATA_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GDATA_SESSION, EGDataSession))
#define E_GDATA_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_GDATA_SESSION, EGDataSessionClass))
#define E_IS_GDATA_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_GDATA_SESSION))
#define E_IS_GDATA_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_GDATA_SESSION))
#define E_GDATA_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_GDATA_SESSION, EGDataSessionClass))

G_BEGIN_DECLS

typedef struct _EGDataSession EGDataSession;
typedef struct _EGDataSessionClass EGDataSessionClass;
typedef struct _EGDataSessionPrivate EGDataSessionPrivate;

/**
 * EGDataObjectCallback:
 * @gdata: an #EGDataSession
 * @object: a #JSonObject with received content
 * @user_data: user data passed to the call
 *
 * Callback used to traverse response from the server, which is
 * an array.
 *
 * Returns: whether the traverse can continue
 *
 * Since: 3.46
 **/
typedef gboolean (*EGDataObjectCallback) (EGDataSession *gdata,
					  JsonObject *object,
					  gpointer user_data);

/**
 * EGDataSession:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.46
 **/
struct _EGDataSession {
	/*< private >*/
	ESoupSession parent;
	EGDataSessionPrivate *priv;
};

struct _EGDataSessionClass {
	ESoupSessionClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_gdata_session_get_type		(void) G_GNUC_CONST;

EGDataSession *	e_gdata_session_new			(ESource *source);

const gchar *	e_gdata_tasklist_get_id			(JsonObject *tasklist);
void		e_gdata_tasklist_add_id			(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_gdata_tasklist_get_etag		(JsonObject *tasklist);
const gchar *	e_gdata_tasklist_get_title		(JsonObject *tasklist);
void		e_gdata_tasklist_add_title		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_gdata_tasklist_get_self_link		(JsonObject *tasklist);
gint64		e_gdata_tasklist_get_updated		(JsonObject *tasklist);

gboolean	e_gdata_session_tasklists_delete_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasklists_get_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 JsonObject **out_tasklist,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasklists_insert_sync	(EGDataSession *self,
							 const gchar *title,
							 JsonObject **out_inserted_tasklist,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasklists_list_sync	(EGDataSession *self,
							 EGDataQuery *query,
							 EGDataObjectCallback cb,
							 gpointer user_data,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasklists_patch_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 JsonBuilder *tasklist_properties,
							 JsonObject **out_patched_tasklist,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasklists_update_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 JsonBuilder *tasklist,
							 JsonObject **out_updated_tasklist,
							 GCancellable *cancellable,
							 GError **error);

/**
 * EGDataTaskStatus:
 * @E_GDATA_TASK_STATUS_UNKNOWN: unknown status
 * @E_GDATA_TASK_STATUS_NEEDS_ACTION: the task needs action
 * @E_GDATA_TASK_STATUS_COMPLETED: the task is completed
 *
 * Holds status of a task.
 *
 * Since: 3.46
 **/
typedef enum {
	E_GDATA_TASK_STATUS_UNKNOWN,
	E_GDATA_TASK_STATUS_NEEDS_ACTION,
	E_GDATA_TASK_STATUS_COMPLETED
} EGDataTaskStatus;

const gchar *	e_gdata_task_get_id			(JsonObject *task);
void		e_gdata_task_add_id			(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_gdata_task_get_etag			(JsonObject *task);
const gchar *	e_gdata_task_get_title			(JsonObject *task);
void		e_gdata_task_add_title			(JsonBuilder *builder,
							 const gchar *value);
gint64		e_gdata_task_get_updated		(JsonObject *task);
const gchar *	e_gdata_task_get_self_link		(JsonObject *task);
const gchar *	e_gdata_task_get_parent			(JsonObject *task);
const gchar *	e_gdata_task_get_position		(JsonObject *task);
const gchar *	e_gdata_task_get_notes			(JsonObject *task);
void		e_gdata_task_add_notes			(JsonBuilder *builder,
							 const gchar *value);
EGDataTaskStatus
		e_gdata_task_get_status			(JsonObject *task);
void		e_gdata_task_add_status			(JsonBuilder *builder,
							 EGDataTaskStatus value);
gint64		e_gdata_task_get_due			(JsonObject *task);
void		e_gdata_task_add_due			(JsonBuilder *builder,
							 gint64 value);
gint64		e_gdata_task_get_completed		(JsonObject *task);
void		e_gdata_task_add_completed		(JsonBuilder *builder,
							 gint64 value);
gboolean	e_gdata_task_get_deleted		(JsonObject *task);
gboolean	e_gdata_task_get_hidden			(JsonObject *task);

gboolean	e_gdata_session_tasks_clear_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_delete_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 const gchar *task_id,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_get_sync		(EGDataSession *self,
							 const gchar *tasklist_id,
							 const gchar *task_id,
							 JsonObject **out_task,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_insert_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 JsonBuilder *task,
							 const gchar *parent_task_id,
							 const gchar *previous_task_id,
							 JsonObject **out_inserted_task,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_list_sync		(EGDataSession *self,
							 const gchar *tasklist_id,
							 EGDataQuery *query,
							 EGDataObjectCallback cb,
							 gpointer user_data,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_move_sync		(EGDataSession *self,
							 const gchar *tasklist_id,
							 const gchar *task_id,
							 const gchar *parent_task_id,
							 const gchar *previous_task_id,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_patch_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 const gchar *task_id,
							 JsonBuilder *task_properties,
							 JsonObject **out_patched_task,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_gdata_session_tasks_update_sync	(EGDataSession *self,
							 const gchar *tasklist_id,
							 const gchar *task_id,
							 JsonBuilder *task,
							 JsonObject **out_updated_task,
							 GCancellable *cancellable,
							 GError **error);

#endif /* __GI_SCANNER__ */

G_END_DECLS

#endif /* E_GDATA_SESSION_H */
