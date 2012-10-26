/*
 * e-test-dbus-utils.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_TEST_DBUS_UTILS_H
#define E_TEST_DBUS_UTILS_H

#include <gio/gio.h>

G_BEGIN_DECLS

const gchar *	e_test_setup_base_directories	(void);
gboolean	e_test_clean_base_directories	(GError **error);

GTestDBus *	e_test_setup_dbus_session	(void);

G_END_DECLS

#endif /* E_TEST_DBUS_UTILS_H */
