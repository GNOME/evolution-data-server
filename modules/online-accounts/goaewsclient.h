/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Debarshi Ray <debarshir@gnome.org>
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
