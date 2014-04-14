/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Authors: Debarshi Ray <debarshir@gnome.org>
 */

/* XXX This is a rather hacked up version of GoaEwsClient from
 *     GNOME Online Accounts which returns the discovered URLs. */

#ifndef __GOA_EWS_CLIENT_H__
#define __GOA_EWS_CLIENT_H__

/* XXX Yeah, yeah... */
#define GOA_API_IS_SUBJECT_TO_CHANGE

#include <goa/goa.h>

G_BEGIN_DECLS

void		goa_ews_autodiscover		(GoaObject *goa_object,
						 GCancellable *cancellable,
						 GAsyncReadyCallback  callback,
						 gpointer user_data);
gboolean	goa_ews_autodiscover_finish	(GoaObject *goa_object,
						 GAsyncResult *result,
						 gchar **out_as_url,
						 gchar **out_oab_url,
						 GError **error);
gboolean	goa_ews_autodiscover_sync	(GoaObject *goa_object,
						 gchar **out_as_url,
						 gchar **out_oab_url,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* __GOA_EWS_CLIENT_H__ */
