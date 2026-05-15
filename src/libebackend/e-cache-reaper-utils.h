/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CACHE_REAPER_UTILS_H
#define E_CACHE_REAPER_UTILS_H

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean	e_reap_trash_directory_sync	(GFile *trash_directory,
						 gint expiry_in_days,
						 GCancellable *cancellable,
						 GError **error);
void		e_reap_trash_directory		(GFile *trash_directory,
						 gint expiry_in_days,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_reap_trash_directory_finish	(GFile *trash_directory,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_CACHE_REAPER_UTILS_H */

