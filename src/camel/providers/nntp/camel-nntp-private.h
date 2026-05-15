/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_NNTP_PRIVATE_H
#define CAMEL_NNTP_PRIVATE_H

/* need a way to configure and save this data, if this header is to
 * be installed.  For now, don't install it */

#include "evolution-data-server-config.h"

G_BEGIN_DECLS

struct _CamelNNTPFolderPrivate {
	GMutex cache_lock;     /* for locking the cache object */

	gboolean apply_filters;		/* persistent property */
};

#define CAMEL_NNTP_FOLDER_LOCK(f, l) \
	(g_mutex_lock (&((CamelNNTPFolder *) f)->priv->l))
#define CAMEL_NNTP_FOLDER_UNLOCK(f, l) \
	(g_mutex_unlock (&((CamelNNTPFolder *) f)->priv->l))

G_END_DECLS

#endif /* CAMEL_NNTP_PRIVATE_H */
