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

/* defines protocol for lock helper process ipc */

#ifndef _CAMEL_LOCK_CLIENT_H
#define _CAMEL_LOCK_CLIENT_H

#include <camel/camel-exception.h>

G_BEGIN_DECLS

gint camel_lock_helper_lock(const gchar *path , CamelException *ex);
gint camel_lock_helper_unlock(gint lockid);

G_END_DECLS

#endif /* _CAMEL_LOCK_HELPER_H */
