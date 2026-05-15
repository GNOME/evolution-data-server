/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

/* defines protocol for lock helper process ipc */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_LOCK_HELPER_H
#define CAMEL_LOCK_HELPER_H

#include <glib.h>

G_BEGIN_DECLS

struct _CamelLockHelperMsg {
	guint32 magic;
	guint32 seq;
	guint32 id;
	guint32 data;
};

/* magic values */
enum {
	CAMEL_LOCK_HELPER_MAGIC = 0xABADF00D,
	CAMEL_LOCK_HELPER_RETURN_MAGIC = 0xDEADBEEF
};

/* return status */
enum {
	CAMEL_LOCK_HELPER_STATUS_OK = 0,
	CAMEL_LOCK_HELPER_STATUS_PROTOCOL,
	CAMEL_LOCK_HELPER_STATUS_NOMEM,
	CAMEL_LOCK_HELPER_STATUS_SYSTEM,
	CAMEL_LOCK_HELPER_STATUS_INVALID /* not allowed to lock/doesn't exist etc */
};

/* commands */
enum {
	CAMEL_LOCK_HELPER_LOCK = 0xf0f,
	CAMEL_LOCK_HELPER_UNLOCK = 0xf0f0
};

/* seconds between lock refreshes */
#define CAMEL_DOT_LOCK_REFRESH (30)

G_END_DECLS

#endif /* CAMEL_LOCK_HELPER_H */
