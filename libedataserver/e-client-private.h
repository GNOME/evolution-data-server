/*
 * e-client-private.h
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

#ifndef E_CLIENT_PRIVATE_H
#define E_CLIENT_PRIVATE_H

#include <glib.h>
#include <gio/gio.h>

#include "libedataserver/e-credentials.h"
#include "libedataserver/e-source.h"
#include "libedataserver/e-source-list.h"
#include "libedataserver/e-client.h"

G_BEGIN_DECLS

void		e_client_set_capabilities	(EClient *client, const gchar *capabilities);
void		e_client_set_readonly		(EClient *client, gboolean readonly);
void		e_client_set_online		(EClient *client, gboolean is_online);
guint32		e_client_register_op		(EClient *client, GCancellable *cancellable);
void		e_client_unregister_op		(EClient *client, guint32 opid);
void		e_client_process_authentication	(EClient *client, const ECredentials *credentials);

gboolean	e_client_emit_authenticate	(EClient *client, ECredentials *credentials);
void		e_client_emit_opened		(EClient *client, const GError *error);
void		e_client_emit_backend_error	(EClient *client, const gchar *error_msg);
void		e_client_emit_backend_died	(EClient *client);

ESource *	e_client_util_get_system_source	(ESourceList *source_list);
gboolean	e_client_util_set_default	(ESourceList *source_list, ESource *source);
ESource *	e_client_util_get_source_for_uri(ESourceList *source_list, const gchar *uri);

/* protected functions simplifying sync/async calls */
GDBusProxy *	e_client_get_dbus_proxy		(EClient *client);
void		e_client_unwrap_dbus_error	(EClient *client, GError *dbus_error, GError **out_error);

void		e_client_proxy_return_async_error	(EClient *client, const GError *error, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag);

#define e_client_return_async_if_fail(_expr, _client, _callback, _user_data, _source_tag)	\
	G_STMT_START {										\
		if (!G_LIKELY (_expr)) {							\
			GError *error;								\
												\
			error = g_error_new (							\
				E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_ARG,			\
				"file %s:%d: %s: assertion `%s' failed",			\
				__FILE__, __LINE__, G_STRFUNC, # _expr);			\
												\
			g_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, "%s", error->message);	\
												\
			e_client_proxy_return_async_error (					\
				_client, error, _callback, _user_data, _source_tag);		\
												\
			g_error_free (error);							\
		}										\
	} G_STMT_END

typedef gboolean (* EClientProxyFinishVoidFunc)		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
typedef gboolean (* EClientProxyFinishBooleanFunc)	(GDBusProxy *proxy, GAsyncResult *result, gboolean *out_boolean, GError **error);
typedef gboolean (* EClientProxyFinishStringFunc)	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_string, GError **error);
typedef gboolean (* EClientProxyFinishStrvFunc)		(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_strv, GError **error);
typedef gboolean (* EClientProxyFinishUintFunc)		(GDBusProxy *proxy, GAsyncResult *result, guint *out_uint, GError **error);

void		e_client_proxy_call_void	(EClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
void		e_client_proxy_call_boolean	(EClient *client, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
void		e_client_proxy_call_string	(EClient *client, const gchar *in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, const gchar * in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
void		e_client_proxy_call_strv	(EClient *client, const gchar * const *in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, const gchar * const * in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);
void		e_client_proxy_call_uint	(EClient *client, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data, gpointer source_tag, void (*func) (GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data), EClientProxyFinishVoidFunc finish_void, EClientProxyFinishBooleanFunc finish_boolean, EClientProxyFinishStringFunc finish_string, EClientProxyFinishStrvFunc finish_strv, EClientProxyFinishUintFunc finish_uint);

gboolean	e_client_proxy_call_finish_void		(EClient *client, GAsyncResult *result, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_boolean	(EClient *client, GAsyncResult *result, gboolean *out_boolean, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_string	(EClient *client, GAsyncResult *result, gchar **out_string, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_strv		(EClient *client, GAsyncResult *result, gchar ***out_strv, GError **error, gpointer source_tag);
gboolean	e_client_proxy_call_finish_uint		(EClient *client, GAsyncResult *result, guint *out_uint, GError **error, gpointer source_tag);

gboolean	e_client_proxy_call_sync_void__void		(EClient *client, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__boolean		(EClient *client, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__string		(EClient *client, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__strv		(EClient *client, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_void__uint		(EClient *client, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__void		(EClient *client, gboolean in_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__boolean	(EClient *client, gboolean in_boolean, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__string	(EClient *client, gboolean in_boolean, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__strv		(EClient *client, gboolean in_boolean, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_boolean__uint		(EClient *client, gboolean in_boolean, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, gboolean in_boolean, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__void		(EClient *client, const gchar *in_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__boolean	(EClient *client, const gchar *in_string, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__string		(EClient *client, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__strv		(EClient *client, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_string__uint		(EClient *client, const gchar *in_string, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar *in_string, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__void		(EClient *client, const gchar * const *in_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__boolean		(EClient *client, const gchar * const *in_strv, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__string		(EClient *client, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__strv		(EClient *client, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_strv__uint		(EClient *client, const gchar * const *in_strv, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, const gchar * const *in_strv, guint *out_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__void		(EClient *client, guint in_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__boolean		(EClient *client, guint in_uint, gboolean *out_boolean, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gboolean *out_boolean, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__string		(EClient *client, guint in_uint, gchar **out_string, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gchar **out_string, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__strv		(EClient *client, guint in_uint, gchar ***out_strv, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, gchar ***out_strv, GCancellable *cancellable, GError **error));
gboolean	e_client_proxy_call_sync_uint__uint		(EClient *client, guint in_uint, guint *out_uint, GCancellable *cancellable, GError **error, gboolean (*func) (GDBusProxy *proxy, guint in_uint, guint *out_uint, GCancellable *cancellable, GError **error));

G_END_DECLS

#endif /* E_CLIENT_PRIVATE_H */
