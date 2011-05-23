/*
 * e-client-utils.h
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

#ifndef E_CLIENT_UTILS_H
#define E_CLIENT_UTILS_H

#include <glib.h>
#include <gtk/gtk.h>

#include <libedataserver/e-client.h>

G_BEGIN_DECLS

typedef enum {
	E_CLIENT_SOURCE_TYPE_CONTACTS,
	E_CLIENT_SOURCE_TYPE_EVENTS,
	E_CLIENT_SOURCE_TYPE_MEMOS,
	E_CLIENT_SOURCE_TYPE_TASKS,
	E_CLIENT_SOURCE_TYPE_LAST
} EClientSourceType;

typedef gboolean (* EClientUtilsAuthenticateHandler) (EClient *client, ECredentials *credentials, gpointer user_data);

EClient	*	e_client_utils_new			(ESource *source, EClientSourceType source_type, GError **error);
EClient *	e_client_utils_new_from_uri		(const gchar *uri, EClientSourceType source_type, GError **error);
EClient *	e_client_utils_new_system		(EClientSourceType source_type, GError **error);
EClient *	e_client_utils_new_default		(EClientSourceType source_type, GError **error);

gboolean	e_client_utils_set_default		(EClient *client, EClientSourceType source_type, GError **error);
gboolean	e_client_utils_set_default_source	(ESource *source, EClientSourceType source_type, GError **error);
gboolean	e_client_utils_get_sources		(ESourceList **sources, EClientSourceType source_type, GError **error);

void		e_client_utils_open_new			(ESource *source, EClientSourceType source_type, gboolean only_if_exists,
							 EClientUtilsAuthenticateHandler auth_handler, gpointer auth_handler_user_data,
							 GCancellable *cancellable, GAsyncReadyCallback async_cb, gpointer async_cb_user_data);
gboolean	e_client_utils_open_new_finish		(GAsyncResult *result, EClient **client, GError **error);

gboolean	e_client_utils_authenticate_handler	(EClient *client, ECredentials *credentials, gpointer unused_user_data);
gboolean	e_credentials_authenticate_helper	(ECredentials *credentials, GtkWindow *parent, gboolean *remember_password);

G_END_DECLS

#endif /* E_CLIENT_UTILS_H */
