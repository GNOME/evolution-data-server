/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Author: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef _CAMEL_LOCK_H
#define _CAMEL_LOCK_H

#include <camel/camel-exception.h>

/* for .lock locking, retry, delay and stale counts */
#define CAMEL_LOCK_DOT_RETRY (5) /* number of times to retry lock */
#define CAMEL_LOCK_DOT_DELAY (2) /* delay between locking retries */
#define CAMEL_LOCK_DOT_STALE (60) /* seconds before a lock becomes stale */

/* for locking folders, retry/interretry delay */
#define CAMEL_LOCK_RETRY (5) /* number of times to retry lock */
#define CAMEL_LOCK_DELAY (2) /* delay between locking retries */

G_BEGIN_DECLS

typedef enum {
	CAMEL_LOCK_READ,
	CAMEL_LOCK_WRITE
} CamelLockType;

/* specific locking strategies */
gint camel_lock_dot(const gchar *path, CamelException *ex);
gint camel_lock_fcntl(gint fd, CamelLockType type, CamelException *ex);
gint camel_lock_flock(gint fd, CamelLockType type, CamelException *ex);

void camel_unlock_dot(const gchar *path);
void camel_unlock_fcntl(gint fd);
void camel_unlock_flock(gint fd);

/* lock a folder in a standard way */
gint camel_lock_folder(const gchar *path, gint fd, CamelLockType type, CamelException *ex);
void camel_unlock_folder(const gchar *path, gint fd);

G_END_DECLS

#endif /* _CAMEL_LOCK_H */
