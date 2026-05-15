/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Christian Kellner <gicmo@gnome.org>
 */

#ifndef E_CAL_BACKEND_CALDAV_H
#define E_CAL_BACKEND_CALDAV_H

#include <libedata-cal/libedata-cal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_CALDAV \
	(e_cal_backend_caldav_get_type ())
#define E_CAL_BACKEND_CALDAV(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAV))
#define E_CAL_BACKEND_CALDAV_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAVClass))
#define E_IS_CAL_BACKEND_CALDAV(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_CALDAV))
#define E_IS_CAL_BACKEND_CALDAV_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_CALDAV))
#define E_CAL_BACKEND_CALDAV_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAVClass))

G_BEGIN_DECLS

typedef struct _ECalBackendCalDAV ECalBackendCalDAV;
typedef struct _ECalBackendCalDAVClass ECalBackendCalDAVClass;
typedef struct _ECalBackendCalDAVPrivate ECalBackendCalDAVPrivate;

struct _ECalBackendCalDAV {
	ECalMetaBackend parent;
	ECalBackendCalDAVPrivate *priv;
};

struct _ECalBackendCalDAVClass {
	ECalMetaBackendClass parent_class;
};

GType		e_cal_backend_caldav_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_CALDAV_H */
