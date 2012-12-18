/* GIO testing utilities
 *
 * Copyright (C) 2008-2010 Red Hat, Inc.
 * Copyright (C) 2012 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Xavier Claessens <xavier.claessens@collabora.co.uk>
 */

#ifndef __E_TEST_DBUS_H__
#define __E_TEST_DBUS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define E_TYPE_TEST_DBUS \
    (e_test_dbus_get_type ())
#define E_TEST_DBUS(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_TEST_DBUS, \
        GTestDBus))
#define E_IS_TEST_DBUS(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_TEST_DBUS))

typedef struct _ETestDBus  ETestDBus;

/**
 * ETestDBusFlags:
 * @E_TEST_DBUS_NONE: No flags.
 *
 * Flags to define future #ETestDBus behaviour.
 *
 */
typedef enum /*< flags >*/ {
  E_TEST_DBUS_NONE = 0
} ETestDBusFlags;

GType          e_test_dbus_get_type        (void) G_GNUC_CONST;

ETestDBus *    e_test_dbus_new             (ETestDBusFlags flags);

ETestDBusFlags e_test_dbus_get_flags       (ETestDBus     *self);

const gchar *  e_test_dbus_get_bus_address (ETestDBus     *self);

void           e_test_dbus_add_service_dir (ETestDBus     *self,
                                            const gchar   *path);

void           e_test_dbus_up              (ETestDBus     *self);

void           e_test_dbus_stop            (ETestDBus     *self);

void           e_test_dbus_down            (ETestDBus     *self);

void           e_test_dbus_unset           (void);

G_END_DECLS

#endif /* __E_TEST_DBUS_H__ */
