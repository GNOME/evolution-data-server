/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef E_GW_ITEM_H
#define E_GW_ITEM_H

#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

#define E_TYPE_GW_ITEM            (e_gw_item_get_type ())
#define E_GW_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_ITEM, EGwItem))
#define E_GW_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_ITEM, EGwItemClass))
#define E_IS_GW_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_ITEM))
#define E_IS_GW_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_ITEM))

typedef struct _EGwItem        EGwItem;
typedef struct _EGwItemClass   EGwItemClass;
typedef struct _EGwItemPrivate EGwItemPrivate;

typedef enum {
	E_GW_ITEM_TYPE_APPOINTMENT,
	E_GW_ITEM_TYPE_UNKNOWN
} EGwItemType;

struct _EGwItem {
	GObject parent;
	EGwItemPrivate *priv;
};

struct _EGwItemClass {
	GObjectClass parent_class;
};

GType    e_gw_item_get_type (void);
EGwItem *e_gw_item_new_appointment (const char *container, ECalComponent *comp);

G_END_DECLS

#endif
