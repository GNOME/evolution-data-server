/*
 * SPDX-FileCopyrightText: (C) 2024 Siemens AG
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#ifndef E_MS_OAPXBC_H
#define E_MS_OAPXBC_H

G_BEGIN_DECLS

#define E_TYPE_MS_OAPXBC e_ms_oapxbc_get_type ()
G_DECLARE_FINAL_TYPE (EMsOapxbc, e_ms_oapxbc, E, MS_OAPXBC, GObject)

EMsOapxbc *	e_ms_oapxbc_new_sync	(const gchar *client_id,
					 const gchar *authority,
					 GCancellable *cancellable,
					 GError **error);

JsonObject *	e_ms_oapxbc_get_accounts_sync
					(EMsOapxbc *self,
					 GCancellable *cancellable,
					 GError **error);

SoupCookie *	e_ms_oapxbc_acquire_prt_sso_cookie_sync
					(EMsOapxbc *self,
					 JsonObject *account,
					 const gchar *sso_url,
					 JsonArray *scopes,
					 const gchar *redirect_uri,
					 GCancellable *cancellable,
					 GError **error);

G_END_DECLS

#endif /* E_MS_OAPXBC_H */
