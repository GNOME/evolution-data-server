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
	E_GW_ITEM_TYPE_CONTACT,
	E_GW_ITEM_TYPE_GROUP,
	E_GW_ITEM_TYPE_ORGANISATION,
	E_GW_ITEM_TYPE_RESOURCE,
	E_GW_ITEM_TYPE_CATEGORY,
	E_GW_ITEM_TYPE_UNKNOWN
	
} EGwItemType;

typedef enum {
	E_GW_ITEM_CHANGE_TYPE_ADD,
	E_GW_ITEM_CHANGE_TYPE_UPDATE,
	E_GW_ITEM_CHANGE_TYPE_DELETE,
	E_GW_ITEM_CHNAGE_TYPE_UNKNOWN

} EGwItemChangeType;

struct _EGwItem {
	GObject parent;
	EGwItemPrivate *priv;
};

struct _EGwItemClass {
	GObjectClass parent_class;
};

/* structures defined to hold contact item fields */
typedef struct {
	char *name_prefix;
	char *first_name;
	char *middle_name;
	char *last_name;
	char *name_suffix;
} FullName;

typedef struct {
	char *street_address;
	char *location;
	char *city;
	char *state;
	char *postal_code;
	char *country;
} PostalAddress;

typedef struct {

	char *service;
	char *address;
}IMAddress;

typedef struct {
	char *id;
	char *email;
} EGroupMember;

GType       e_gw_item_get_type (void);
EGwItem    *e_gw_item_new_empty (void);
EGwItem    *e_gw_item_new_from_soap_parameter (const char *container, SoupSoapParameter *param);

EGwItemType e_gw_item_get_item_type (EGwItem *item);
void        e_gw_item_set_item_type (EGwItem *item, EGwItemType new_type);
const char *e_gw_item_get_container_id (EGwItem *item);
void        e_gw_item_set_container_id (EGwItem *item, const char *new_id);
const char *e_gw_item_get_icalid (EGwItem *item);
void        e_gw_item_set_icalid (EGwItem *item, const char *new_icalid);
const char *e_gw_item_get_id (EGwItem *item);
void        e_gw_item_set_id (EGwItem *item, const char *new_id);
char       *e_gw_item_get_creation_date (EGwItem *item);
void        e_gw_item_set_creation_date (EGwItem *item, const char *new_date);
char       *e_gw_item_get_start_date (EGwItem *item);
void        e_gw_item_set_start_date (EGwItem *item, const char *new_date);
char       *e_gw_item_get_end_date (EGwItem *item);
void        e_gw_item_set_end_date (EGwItem *item, const char *new_date);
char       *e_gw_item_get_due_date (EGwItem *item);
void        e_gw_item_set_due_date (EGwItem *item, const char *new_date);
const char *e_gw_item_get_subject (EGwItem *item);
void        e_gw_item_set_subject (EGwItem *item, const char *new_subject);
const char *e_gw_item_get_message (EGwItem *item);
void        e_gw_item_set_message (EGwItem *item, const char *new_message);
const char *e_gw_item_get_place (EGwItem *item);
void        e_gw_item_set_place (EGwItem *item, const char *new_place);
gboolean    e_gw_item_get_completed (EGwItem *item);
void        e_gw_item_set_completed (EGwItem *item, gboolean new_completed);
char*       e_gw_item_get_field_value (EGwItem *item, char *field_name);
void        e_gw_item_set_field_value (EGwItem *item, char *field_name, char* field_value);
GList*      e_gw_item_get_email_list (EGwItem *item);
void        e_gw_item_set_email_list (EGwItem *item, GList *email_list);
FullName*   e_gw_item_get_full_name (EGwItem *item);
void        e_gw_item_set_full_name (EGwItem *item, FullName* full_name);
GList*      e_gw_item_get_member_list (EGwItem *item);
void        e_gw_item_set_member_list (EGwItem *item, GList *list);
PostalAddress* e_gw_item_get_address (EGwItem *item, char *address_type);
void        e_gw_item_set_address (EGwItem *item, char *addres_type, PostalAddress *address);
GList*      e_gw_item_get_im_list (EGwItem *item);
void        e_gw_item_set_im_list (EGwItem *item, GList *im_list);
void        e_gw_item_set_categories (EGwItem *item, GList *category_list);
GList*      e_gw_item_get_categories (EGwItem *item);
void e_gw_item_set_change (EGwItem *item, EGwItemChangeType change_type, char *field_name, gpointer field_value);
gboolean e_gw_item_append_changes_to_soap_message (EGwItem *item, SoupSoapMessage *msg);
void e_gw_item_set_category_name (EGwItem *item, char *cateogry_name);
char* e_gw_item_get_category_name (EGwItem *item);


#define E_GW_ITEM_CLASSIFICATION_PUBLIC       "Public"
#define E_GW_ITEM_CLASSIFICATION_PRIVATE      "Private"
#define E_GW_ITEM_CLASSIFICATION_CONFIDENTIAL "Confidential"

const char *e_gw_item_get_classification (EGwItem *item);
void        e_gw_item_set_classification (EGwItem *item, const char *new_class);

#define E_GW_ITEM_ACCEPT_LEVEL_BUSY          "Busy"
#define E_GW_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE "OutOfOffice"

const char *e_gw_item_get_accept_level (EGwItem *item);
void        e_gw_item_set_accept_level (EGwItem *item, const char *new_level);

#define E_GW_ITEM_PRIORITY_HIGH     "High"
#define E_GW_ITEM_PRIORITY_STANDARD "Standard"
#define E_GW_ITEM_PRIORITY_LOW      "Low"

const char *e_gw_item_get_priority (EGwItem *item);
void        e_gw_item_set_priority (EGwItem *item, const char *new_priority);

GSList *e_gw_item_get_recipient_list (EGwItem *item);
void e_gw_item_set_recipient_list (EGwItem *item, GSList *new_recipient_list);

int e_gw_item_get_trigger (EGwItem *item);
void e_gw_item_set_trigger (EGwItem *item, int trigger);

typedef struct {
	char *email;
	char *display_name;
	enum {
		E_GW_ITEM_RECIPIENT_TO,
		E_GW_ITEM_RECIPIENT_CC,
		E_GW_ITEM_RECIPIENT_NONE
	} type;
} EGwItemRecipient;

gboolean    e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg);

G_END_DECLS

#endif
