/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

/* defines protocol for lock helper process ipc */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_LOCK_CLIENT_H
#define CAMEL_LOCK_CLIENT_H

#include <glib.h>

G_BEGIN_DECLS

gint camel_lock_helper_lock (const gchar *path , GError **error);
gint camel_lock_helper_unlock (gint lockid);

G_END_DECLS

#endif /* CAMEL_LOCK_HELPER_H */
