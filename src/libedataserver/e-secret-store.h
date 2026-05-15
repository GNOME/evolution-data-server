/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SECRET_STORE_H
#define E_SECRET_STORE_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean	e_secret_store_store_sync	(const gchar *uid,
						 const gchar *secret,
						 const gchar *label,
						 gboolean permanently,
						 GCancellable *cancellable,
						 GError **error);

gboolean	e_secret_store_lookup_sync	(const gchar *uid,
						 gchar **out_secret,
						 GCancellable *cancellable,
						 GError **error);

gboolean	e_secret_store_delete_sync	(const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_SECRET_STORE_H */
