/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Chris Lahey <clahey@ximian.com>
 */

#ifndef __E_DB3_UTILS_H__
#define __E_DB3_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

gint e_db3_utils_maybe_recover (const gchar *filename);
gint e_db3_utils_upgrade_format (const gchar *filename);

G_END_DECLS

#endif /* __E_DB3_UTILS_H__ */

