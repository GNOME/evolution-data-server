/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_DATA_CAL_H
#define E_DATA_CAL_H

#include <gio/gio.h>
#include <libedata-cal/e-data-cal-view.h>

/* Standard GObject macros */
#define E_TYPE_DATA_CAL \
	(e_data_cal_get_type ())
#define E_DATA_CAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_CAL, EDataCal))
#define E_DATA_CAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DATA_CAL, EDataCalClass))
#define E_IS_DATA_CAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DATA_CAL))
#define E_IS_DATA_CAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DATA_CAL))
#define E_DATA_CAL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DATA_CAL, EDataCalClass))

/**
 * E_DATA_CAL_ERROR:
 *
 * Since: 2.30
 **/
#define E_DATA_CAL_ERROR e_data_cal_error_quark ()

G_BEGIN_DECLS

/**
 * e_return_data_cal_error_if_fail:
 *
 * Since: 2.32
 **/
#define e_return_data_cal_error_if_fail(expr, _code)				\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (error, E_DATA_CAL_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			return;							\
		}								\
	} G_STMT_END

/**
 * e_return_data_cal_error_val_if_fail:
 *
 * Same as e_return_data_cal_error_if_fail(), only returns FALSE on a failure
 *
 * Since: 3.2
 **/
#define e_return_data_cal_error_val_if_fail(expr, _code)			\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (error, E_DATA_CAL_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			return FALSE;						\
		}								\
	} G_STMT_END

struct _ECalBackend;

typedef struct _EDataCal EDataCal;
typedef struct _EDataCalClass EDataCalClass;
typedef struct _EDataCalPrivate EDataCalPrivate;

struct _EDataCal {
	GObject parent;
	EDataCalPrivate *priv;
};

struct _EDataCalClass {
	GObjectClass parent_class;
};

GQuark		e_data_cal_error_quark		(void);
GError *	e_data_cal_create_error		(EDataCalCallStatus status,
						 const gchar *custom_msg);
GError *	e_data_cal_create_error_fmt	(EDataCalCallStatus status,
						 const gchar *custom_msg_fmt,
						 ...) G_GNUC_PRINTF (2, 3);
const gchar *	e_data_cal_status_to_string	(EDataCalCallStatus status);

GType		e_data_cal_get_type		(void) G_GNUC_CONST;
EDataCal *	e_data_cal_new			(struct _ECalBackend *backend,
						 GDBusConnection *connection,
						 const gchar *object_path,
						 GError **error);
struct _ECalBackend *
		e_data_cal_get_backend		(EDataCal *cal);
GDBusConnection *
		e_data_cal_get_connection	(EDataCal *cal);
const gchar *	e_data_cal_get_object_path	(EDataCal *cal);

void		e_data_cal_respond_open		(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_respond_refresh	(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_respond_get_backend_property
						(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const gchar *prop_value);
void		e_data_cal_respond_set_backend_property
						(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_respond_get_object	(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const gchar *object);
void		e_data_cal_respond_get_object_list
						(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const GSList *objects);
void		e_data_cal_respond_get_free_busy
						(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_respond_create_objects
						(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const GSList *uids,
						 GSList *new_components);
void		e_data_cal_respond_modify_objects
						(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 GSList *old_components,
						 GSList *new_components);
void		e_data_cal_respond_remove_objects
						(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const GSList *ids,
						 GSList *old_components,
						 GSList *new_components);
void		e_data_cal_respond_receive_objects
						(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_respond_send_objects	(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const GSList *users,
						 const gchar *calobj);
void		e_data_cal_respond_get_attachment_uris
						(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const GSList *attachment_uris);
void		e_data_cal_respond_discard_alarm
						(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_respond_get_view	(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const gchar *view_path);
void		e_data_cal_respond_get_timezone	(EDataCal *cal,
						 guint32 opid,
						 GError *error,
						 const gchar *tzobject);
void		e_data_cal_respond_add_timezone	(EDataCal *cal,
						 guint32 opid,
						 GError *error);
void		e_data_cal_report_error		(EDataCal *cal,
						 const gchar *message);
void		e_data_cal_report_readonly	(EDataCal *cal,
						 gboolean is_readonly);
void		e_data_cal_report_online	(EDataCal *cal,
						 gboolean is_online);
void		e_data_cal_report_opened	(EDataCal *cal,
						 const GError *error);
void		e_data_cal_report_free_busy_data
						(EDataCal *cal,
						 const GSList *freebusy);
void		e_data_cal_report_backend_property_changed
						(EDataCal *cal,
						 const gchar *prop_name,
						 const gchar *prop_value);

G_END_DECLS

#endif /* E_DATA_CAL_H */
