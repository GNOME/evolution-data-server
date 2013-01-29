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

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

void		e_client_set_capabilities	(EClient *client, const gchar *capabilities);
void		e_client_set_readonly		(EClient *client, gboolean readonly);
void		e_client_set_online		(EClient *client, gboolean is_online);

void		e_client_emit_backend_error	(EClient *client, const gchar *error_msg);
void		e_client_emit_backend_died	(EClient *client);
void		e_client_emit_backend_property_changed   (EClient *client, const gchar *prop_name, const gchar *prop_value);

/* protected functions simplifying sync/async calls */
GDBusProxy *	e_client_get_dbus_proxy		(EClient *client);

G_END_DECLS

#endif /* E_CLIENT_PRIVATE_H */
