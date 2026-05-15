/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MOVEMAIL_H
#define CAMEL_MOVEMAIL_H

#include <glib.h>

G_BEGIN_DECLS

gint camel_movemail (const gchar *source, const gchar *dest, GError **error);

G_END_DECLS

#endif /* CAMEL_MOVEMAIL_H */
