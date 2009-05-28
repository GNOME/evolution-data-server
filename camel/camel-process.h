/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef __CAMEL_PROCESS_H__
#define __CAMEL_PROCESS_H__

#include <sys/types.h>

#include <camel/camel-exception.h>

G_BEGIN_DECLS

pid_t camel_process_fork (const gchar *path, gchar **argv, gint *infd, gint *outfd, gint *errfd, CamelException *ex);

gint camel_process_wait (pid_t pid);

G_END_DECLS

#endif /* __CAMEL_PROCESS_H__ */

#endif /* CAMEL_DISABLE_DEPRECATED */
