/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  camel-win32.h: Private info for win32.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_WIN32_H
#define CAMEL_WIN32_H

/* need a way to configure and save this data, if this header is to
   be installed.  For now, don't install it */

#include "evolution-data-server-config.h"

#include <glib.h>

#ifdef G_OS_WIN32

G_BEGIN_DECLS

#define fsync(fd) _commit(fd)

const gchar *_camel_get_localedir (void) G_GNUC_CONST;
const gchar *_camel_get_libexecdir (void) G_GNUC_CONST;
const gchar *_camel_get_providerdir (void) G_GNUC_CONST;

#undef LOCALEDIR
#define LOCALEDIR _camel_get_localedir ()

#undef CAMEL_LIBEXECDIR
#define CAMEL_LIBEXECDIR _camel_get_libexecdir ()

#undef CAMEL_PROVIDERDIR
#define CAMEL_PROVIDERDIR _camel_get_providerdir ()

G_END_DECLS

#endif /* G_OS_WIN32 */

#endif /* CAMEL_WIN32_H */
