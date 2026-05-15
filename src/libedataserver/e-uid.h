/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_UID_H
#define E_UID_H

#include <glib.h>

G_BEGIN_DECLS

#ifndef EDS_DISABLE_DEPRECATED
gchar *		e_uid_new			(void);
#endif /* EDS_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* E_UID_H */
