/* db3 utils
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
 * Authors: Chris Lahey <clahey@ximian.com>
 */

#ifndef __E_DB3_UTILS_H__
#define __E_DB3_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

gint e_db3_utils_maybe_recover (const gchar *filename);
gint e_db3_utils_upgrade_format (const gchar *filename);

G_END_DECLS

#endif /* __E_DB3_UTILS_H__ */

