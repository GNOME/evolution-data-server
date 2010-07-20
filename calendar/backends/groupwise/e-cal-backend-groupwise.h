/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_CAL_BACKEND_GROUPWISE_H
#define E_CAL_BACKEND_GROUPWISE_H

#include <libedata-cal/e-cal-backend-sync.h>
#include <servers/groupwise/e-gw-connection.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_GROUPWISE            (e_cal_backend_groupwise_get_type ())
#define E_CAL_BACKEND_GROUPWISE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_GROUPWISE,	ECalBackendGroupwise))
#define E_CAL_BACKEND_GROUPWISE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_GROUPWISE,	ECalBackendGroupwiseClass))
#define E_IS_CAL_BACKEND_GROUPWISE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_GROUPWISE))
#define E_IS_CAL_BACKEND_GROUPWISE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_GROUPWISE))

typedef struct _ECalBackendGroupwise        ECalBackendGroupwise;
typedef struct _ECalBackendGroupwiseClass   ECalBackendGroupwiseClass;

typedef struct _ECalBackendGroupwisePrivate ECalBackendGroupwisePrivate;

struct _ECalBackendGroupwise {
	ECalBackendSync backend;

	/* Private data */
	ECalBackendGroupwisePrivate *priv;
};

struct _ECalBackendGroupwiseClass {
	ECalBackendSyncClass parent_class;
};

typedef struct {
	ECalBackendGroupwise *cbgw;
	EGwSendOptions *opts;
} GwSettings;

GType   e_cal_backend_groupwise_get_type (void);
EGwConnection* e_cal_backend_groupwise_get_connection (ECalBackendGroupwise *cbgw);
GHashTable* e_cal_backend_groupwise_get_categories_by_id (ECalBackendGroupwise *cbgw);
GHashTable* e_cal_backend_groupwise_get_categories_by_name (ECalBackendGroupwise *cbgw);
icaltimezone* e_cal_backend_groupwise_get_default_zone (ECalBackendGroupwise *cbgw);
void    e_cal_backend_groupwise_notify_error_code (ECalBackendGroupwise *cbgw, EGwConnectionStatus status);
const gchar *e_cal_backend_groupwise_get_container_id (ECalBackendGroupwise *cbgw);
void e_cal_backend_groupwise_priv_lock (ECalBackendGroupwise *cbgw);
void e_cal_backend_groupwise_priv_unlock (ECalBackendGroupwise *cbgw);

G_END_DECLS

#endif
