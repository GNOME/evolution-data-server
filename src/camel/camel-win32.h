/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
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
