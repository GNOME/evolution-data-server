/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc.
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Debarshi Ray <debarshir@gnome.org>
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
