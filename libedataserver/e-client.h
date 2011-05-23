/*
 * e-client.h
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

#ifndef E_CLIENT_H
#define E_CLIENT_H

#include <glib.h>
#include <gio/gio.h>

#include <libedataserver/e-credentials.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>

#define E_TYPE_CLIENT		(e_client_get_type ())
#define E_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CLIENT, EClient))
#define E_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_CLIENT, EClientClass))
#define E_IS_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CLIENT))
#define E_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CLIENT))
#define E_CLIENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CLIENT, EClientClass))

#define CLIENT_BACKEND_PROPERTY_OPENED			"opened"
#define CLIENT_BACKEND_PROPERTY_OPENING			"opening"
#define CLIENT_BACKEND_PROPERTY_ONLINE			"online"
#define CLIENT_BACKEND_PROPERTY_READONLY		"readonly"
#define CLIENT_BACKEND_PROPERTY_CACHE_DIR		"cache-dir"
#define CLIENT_BACKEND_PROPERTY_CAPABILITIES		"capabilities"

#define E_CLIENT_ERROR		e_client_error_quark ()

GQuark e_client_error_quark (void) G_GNUC_CONST;

typedef enum {
	E_CLIENT_ERROR_INVALID_ARG,
	E_CLIENT_ERROR_BUSY,
	E_CLIENT_ERROR_SOURCE_NOT_LOADED,
	E_CLIENT_ERROR_SOURCE_ALREADY_LOADED,
	E_CLIENT_ERROR_AUTHENTICATION_FAILED,
	E_CLIENT_ERROR_AUTHENTICATION_REQUIRED,
	E_CLIENT_ERROR_REPOSITORY_OFFLINE,
	E_CLIENT_ERROR_PERMISSION_DENIED,
	E_CLIENT_ERROR_CANCELLED,
	E_CLIENT_ERROR_COULD_NOT_CANCEL,
	E_CLIENT_ERROR_NOT_SUPPORTED,
	E_CLIENT_ERROR_DBUS_ERROR,
	E_CLIENT_ERROR_OTHER_ERROR
} EClientError;

const gchar *e_client_error_to_string (EClientError code);

typedef struct _EClient        EClient;
typedef struct _EClientClass   EClientClass;
typedef struct _EClientPrivate EClientPrivate;

struct _EClient {
	GObject parent;

	/*< private >*/
	EClientPrivate *priv;
};

struct _EClientClass {
	GObjectClass parent;

	/* virtual methods */
	GDBusProxy *	(* get_dbus_proxy) (EClient *client);
	void		(* unwrap_dbus_error) (EClient *client, GError *dbus_error, GError **out_error);

	void		(* get_backend_property) (EClient *client, const gchar *prop_name, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* get_backend_property_finish) (EClient *client, GAsyncResult *result, gchar **prop_value, GError **error);
	gboolean	(* get_backend_property_sync) (EClient *client, const gchar *prop_name, gchar **prop_value, GCancellable *cancellable, GError **error);

	void		(* set_backend_property) (EClient *client, const gchar *prop_name, const gchar *prop_value, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* set_backend_property_finish) (EClient *client, GAsyncResult *result, GError **error);
	gboolean	(* set_backend_property_sync) (EClient *client, const gchar *prop_name, const gchar *prop_value, GCancellable *cancellable, GError **error);

	void		(* open) (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* open_finish) (EClient *client, GAsyncResult *result, GError **error);
	gboolean	(* open_sync) (EClient *client, gboolean only_if_exists, GCancellable *cancellable, GError **error);

	void		(* remove) (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* remove_finish) (EClient *client, GAsyncResult *result, GError **error);
	gboolean	(* remove_sync) (EClient *client, GCancellable *cancellable, GError **error);

	void		(* refresh) (EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
	gboolean	(* refresh_finish) (EClient *client, GAsyncResult *result, GError **error);
	gboolean	(* refresh_sync) (EClient *client, GCancellable *cancellable, GError **error);

	void		(* handle_authentication) (EClient *client, const ECredentials *credentials);
	gchar *		(* retrieve_capabilities) (EClient *client);

	/* signals */
	gboolean	(* authenticate) (EClient *client, ECredentials *credentials);
	void		(* opened) (EClient *client, const GError *error);
	void		(* backend_error) (EClient *client, const gchar *error_msg);
	void		(* backend_died) (EClient *client);
};

GType		e_client_get_type			(void);

ESource *	e_client_get_source			(EClient *client);
const gchar *	e_client_get_uri			(EClient *client);
const GSList *	e_client_get_capabilities		(EClient *client);
gboolean	e_client_check_capability		(EClient *client, const gchar *capability);
gboolean	e_client_check_refresh_supported	(EClient *client);
gboolean	e_client_is_readonly			(EClient *client);
gboolean	e_client_is_online			(EClient *client);
gboolean	e_client_is_opened			(EClient *client);

void		e_client_cancel_all			(EClient *client);

void		e_client_get_backend_property		(EClient *client, const gchar *prop_name, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_get_backend_property_finish	(EClient *client, GAsyncResult *result, gchar **prop_value, GError **error);
gboolean	e_client_get_backend_property_sync	(EClient *client, const gchar *prop_name, gchar **prop_value, GCancellable *cancellable, GError **error);

void		e_client_set_backend_property		(EClient *client, const gchar *prop_name, const gchar *prop_value, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_set_backend_property_finish	(EClient *client, GAsyncResult *result, GError **error);
gboolean	e_client_set_backend_property_sync	(EClient *client, const gchar *prop_name, const gchar *prop_value, GCancellable *cancellable, GError **error);

void		e_client_open				(EClient *client, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_open_finish			(EClient *client, GAsyncResult *result, GError **error);
gboolean	e_client_open_sync			(EClient *client, gboolean only_if_exists, GCancellable *cancellable, GError **error);

void		e_client_remove				(EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_remove_finish			(EClient *client, GAsyncResult *result, GError **error);
gboolean	e_client_remove_sync			(EClient *client, GCancellable *cancellable, GError **error);

void		e_client_refresh			(EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_client_refresh_finish			(EClient *client, GAsyncResult *result, GError **error);
gboolean	e_client_refresh_sync			(EClient *client, GCancellable *cancellable, GError **error);

/* utility functions */
gchar **	e_client_util_slist_to_strv		(const GSList *strings);
GSList *	e_client_util_strv_to_slist		(const gchar * const *strv);
GSList *	e_client_util_copy_string_slist		(GSList *copy_to, const GSList *strings);
GSList *	e_client_util_copy_object_slist		(GSList *copy_to, const GSList *objects);
void		e_client_util_free_string_slist		(GSList *strings);
void		e_client_util_free_object_slist		(GSList *objects);
GSList *	e_client_util_parse_comma_strings	(const gchar *capabilities);

struct EClientErrorsList {
	const gchar *name;
	gint err_code;
};

gboolean	e_client_util_unwrap_dbus_error		(GError *dbus_error, GError **client_error, const struct EClientErrorsList *known_errors, guint known_errors_count, GQuark known_errors_domain, gboolean fail_when_none_matched);

G_END_DECLS

#endif /* E_CLIENT_H */
