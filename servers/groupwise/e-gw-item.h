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

#include <libsoup/soup-soap-message.h>
#include <libsoup/soup-soap-response.h>
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
	E_GW_ITEM_TYPE_TASK,
	E_GW_ITEM_TYPE_UNKNOWN
} EGwItemType;

struct _EGwItem {
	GObject parent;
	EGwItemPrivate *priv;
};

struct _EGwItemClass {
	GObjectClass parent_class;
};

GType               e_gw_item_get_type (void);
EGwItem            *e_gw_item_new_empty (void);
EGwItem            *e_gw_item_new_from_soap_parameter (const char *container, SoupSoapParameter *param);

EGwItemType         e_gw_item_get_item_type (EGwItem *item);
void                e_gw_item_set_item_type (EGwItem *item, EGwItemType new_type);
const char         *e_gw_item_get_container_id (EGwItem *item);
void                e_gw_item_set_container_id (EGwItem *item, const char *new_id);
const char         *e_gw_item_get_id (EGwItem *item);
void                e_gw_item_set_id (EGwItem *item, const char *new_id);
struct icaltimetype e_gw_item_get_creation_date (EGwItem *item);
void                e_gw_item_set_creation_date (EGwItem *item, struct icaltimetype new_date);
struct icaltimetype e_gw_item_get_start_date (EGwItem *item);
void                e_gw_item_set_start_date (EGwItem *item, struct icaltimetype new_date);
struct icaltimetype e_gw_item_get_end_date (EGwItem *item);
void                e_gw_item_set_end_date (EGwItem *item, struct icaltimetype new_date);
struct icaltimetype e_gw_item_get_due_date (EGwItem *item);
void                e_gw_item_set_due_date (EGwItem *item, struct icaltimetype new_date);
const char         *e_gw_item_get_subject (EGwItem *item);
void                e_gw_item_set_subject (EGwItem *item, const char *new_subject);
const char         *e_gw_item_get_message (EGwItem *item);
void                e_gw_item_set_message (EGwItem *item, const char *new_message);
const char         *e_gw_item_get_place (EGwItem *item);
void                e_gw_item_set_place (EGwItem *item, const char *new_place);
ECalComponentClassification e_gw_item_get_classification (EGwItem *item);
void                e_gw_item_set_classification (EGwItem *item, ECalComponentClassification new_class);
gboolean            e_gw_item_get_completed (EGwItem *item);
void                e_gw_item_set_completed (EGwItem *item, gboolean new_completed);

#define E_GW_ITEM_ACCEPT_LEVEL_BUSY          "Busy"
#define E_GW_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE "OutOfOffice"

const char         *e_gw_item_get_accept_level (EGwItem *item);
void                e_gw_item_set_accept_level (EGwItem *item, const char *new_level);

#define E_GW_ITEM_PRIORITY_HIGH     "High"
#define E_GW_ITEM_PRIORITY_STANDARD "Standard"
#define E_GW_ITEM_PRIORITY_LOW      "Low"

const char         *e_gw_item_get_priority (EGwItem *item);
void                e_gw_item_set_priority (EGwItem *item, const char *new_priority);

gboolean            e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg);

G_END_DECLS

#endif
