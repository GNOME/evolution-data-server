/*
 * SPDX-FileCopyrightText: (C) 2011 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CLIENT_PRIVATE_H
#define E_CLIENT_PRIVATE_H

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

void		e_client_set_capabilities	(EClient *client, const gchar *capabilities);
void		e_client_set_readonly		(EClient *client, gboolean readonly);
void		e_client_set_online		(EClient *client, gboolean is_online);

G_END_DECLS

#endif /* E_CLIENT_PRIVATE_H */
