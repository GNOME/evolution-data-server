/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_LOCAL_PRIVATE_H
#define CAMEL_LOCAL_PRIVATE_H

/* need a way to configure and save this data, if this header is to
 * be installed.  For now, don't install it */

#include "evolution-data-server-config.h"

#include <glib.h>

G_BEGIN_DECLS

struct _CamelLocalFolderPrivate {
	GRecMutex changes_lock; /* for locking changes member */
};

#define CAMEL_LOCAL_FOLDER_LOCK(f, l) \
	(g_mutex_lock (&((CamelLocalFolder *) f)->priv->l))
#define CAMEL_LOCAL_FOLDER_UNLOCK(f, l) \
	(g_mutex_unlock (&((CamelLocalFolder *) f)->priv->l))

gint		camel_local_frompos_sort	(gpointer enc,
						 gint len1,
						 gpointer data1,
						 gint len2,
						 gpointer data2);

G_END_DECLS

#endif /* CAMEL_LOCAL_PRIVATE_H */

