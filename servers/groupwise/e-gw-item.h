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

#ifndef E_GW_ITEM_H
#define E_GW_ITEM_H

#include "soup-soap-message.h"
#include "soup-soap-response.h"
#include "e-gw-recur-utils.h"

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
	E_GW_ITEM_TYPE_MAIL,
	E_GW_ITEM_TYPE_APPOINTMENT,
	E_GW_ITEM_TYPE_TASK,
	E_GW_ITEM_TYPE_CONTACT,
	E_GW_ITEM_TYPE_GROUP,
	E_GW_ITEM_TYPE_ORGANISATION,
	E_GW_ITEM_TYPE_RESOURCE,
	E_GW_ITEM_TYPE_CATEGORY,
	E_GW_ITEM_TYPE_NOTIFICATION,
	E_GW_ITEM_TYPE_NOTE,
	E_GW_ITEM_TYPE_UNKNOWN

} EGwItemType;

typedef enum {
	E_GW_ITEM_CHANGE_TYPE_ADD,
	E_GW_ITEM_CHANGE_TYPE_UPDATE,
	E_GW_ITEM_CHANGE_TYPE_DELETE,
	E_GW_ITEM_CHNAGE_TYPE_UNKNOWN

} EGwItemChangeType;

typedef enum {
	E_GW_ITEM_STAT_ACCEPTED = 1<<0,
	E_GW_ITEM_STAT_COMPLETED = 1<<1,
	E_GW_ITEM_STAT_DELEGATED = 1<<2,
	E_GW_ITEM_STAT_DELETED   = 1<<3,
	E_GW_ITEM_STAT_FORWARDED = 1<<4,
	E_GW_ITEM_STAT_OPENED    = 1<<5,
	E_GW_ITEM_STAT_READ      = 1<<6,
	E_GW_ITEM_STAT_REPLIED   = 1<<7,
	E_GW_ITEM_STAT_DECLINED  = 1<<8,
	E_GW_ITEM_STAT_NONE      = 1<<31
} EGwItemStatus;

struct _EGwItem {
	GObject parent;
	EGwItemPrivate *priv;
};

struct _EGwItemClass {
	GObjectClass parent_class;
};

/* structures defined to hold contact item fields */
typedef struct {
	gchar *name_prefix;
	gchar *first_name;
	gchar *middle_name;
	gchar *last_name;
	gchar *name_suffix;
} FullName;

typedef struct {
	gchar *street_address;
	gchar *location;
	gchar *city;
	gchar *state;
	gchar *postal_code;
	gchar *country;
} PostalAddress;

typedef struct {

	gchar *service;
	gchar *address;
}IMAddress;

typedef struct {
	gchar *id;
	gchar *email;
	gchar *name;
} EGroupMember;

typedef struct {
	gchar *email;
	gchar *display_name;
} EGwItemOrganizer;

typedef struct {
	gchar *id;
	gchar *name;
	gchar *item_reference;
	gchar *contentid;
	gchar *contentType;
	gint size;
	gchar *date;
	gchar *data;
	gboolean hidden;
} EGwItemAttachment;

typedef enum {
	E_GW_ITEM_NOTIFY_NONE,
	E_GW_ITEM_NOTIFY_MAIL
} EGwItemReturnNotify;

typedef enum {
	E_GW_ITEM_NONE,
	E_GW_ITEM_DELIVERED,
	E_GW_ITEM_DELIVERED_OPENED,
	E_GW_ITEM_ALL
} EGwItemTrack;

typedef struct {
	gchar *id;
	gchar *type;
	gchar *thread;
} EGwItemLinkInfo;

typedef struct {
	gchar *item_id;
	gchar *ical_id;
	gchar *recur_key;
	gchar *start_date;
} EGwItemCalId;

GType       e_gw_item_get_type (void);
EGwItem    *e_gw_item_new_empty (void);
EGwItem    *e_gw_item_new_from_soap_parameter (const gchar *email, const gchar *container, SoupSoapParameter *param);

EGwItemType e_gw_item_get_item_type (EGwItem *item);
void        e_gw_item_set_item_type (EGwItem *item, EGwItemType new_type);
const gchar *e_gw_item_get_container_id (EGwItem *item);
void        e_gw_item_set_container_id (EGwItem *item, const gchar *new_id);
const gchar *e_gw_item_get_icalid (EGwItem *item);
void        e_gw_item_set_icalid (EGwItem *item, const gchar *new_icalid);
const gchar *e_gw_item_get_id (EGwItem *item);
void        e_gw_item_set_id (EGwItem *item, const gchar *new_id);
gchar       *e_gw_item_get_creation_date (EGwItem *item);
void        e_gw_item_set_creation_date (EGwItem *item, const gchar *new_date);
gchar       *e_gw_item_get_delivered_date (EGwItem *item);
void        e_gw_item_set_delivered_date (EGwItem *item, const gchar *new_date);
const gchar  *e_gw_item_get_modified_date (EGwItem *item);
void        e_gw_item_set_modified_date (EGwItem *item, const gchar *new_date);
gchar       *e_gw_item_get_start_date (EGwItem *item);
void        e_gw_item_set_start_date (EGwItem *item, const gchar *new_date);
gchar       *e_gw_item_get_completed_date (EGwItem *item);
void        e_gw_item_set_completed_date (EGwItem *item, const gchar *new_date);
gchar       *e_gw_item_get_end_date (EGwItem *item);
void        e_gw_item_set_end_date (EGwItem *item, const gchar *new_date);
gchar       *e_gw_item_get_due_date (EGwItem *item);
void        e_gw_item_set_due_date (EGwItem *item, const gchar *new_date);
const gchar *e_gw_item_get_subject (EGwItem *item);
void        e_gw_item_set_subject (EGwItem *item, const gchar *new_subject);
const gchar *e_gw_item_get_message (EGwItem *item);
void        e_gw_item_set_message (EGwItem *item, const gchar *new_message);
const gchar *e_gw_item_get_place (EGwItem *item);
void        e_gw_item_set_place (EGwItem *item, const gchar *new_place);
const gchar *e_gw_item_get_security (EGwItem *item);
void        e_gw_item_set_security (EGwItem *item, const gchar *new_class);
gboolean    e_gw_item_get_completed (EGwItem *item);
void        e_gw_item_set_completed (EGwItem *item, gboolean new_completed);
gboolean    e_gw_item_get_is_allday_event (EGwItem *item);
void	    e_gw_item_set_is_allday_event (EGwItem *item, gboolean is_allday);
gchar *       e_gw_item_get_field_value (EGwItem *item, const gchar *field_name);
void        e_gw_item_set_field_value (EGwItem *item, const gchar *field_name, gchar * field_value);
GList*      e_gw_item_get_email_list (EGwItem *item);
void        e_gw_item_set_email_list (EGwItem *item, GList *email_list);
FullName*   e_gw_item_get_full_name (EGwItem *item);
void        e_gw_item_set_full_name (EGwItem *item, FullName* full_name);
GList*      e_gw_item_get_member_list (EGwItem *item);
void        e_gw_item_set_member_list (EGwItem *item, GList *list);
PostalAddress* e_gw_item_get_address (EGwItem *item, const gchar *address_type);
void        e_gw_item_set_address (EGwItem *item, const gchar *addres_type, PostalAddress *address);
GList*      e_gw_item_get_im_list (EGwItem *item);
void        e_gw_item_set_im_list (EGwItem *item, GList *im_list);
void        e_gw_item_set_categories (EGwItem *item, GList *category_list);
GList*      e_gw_item_get_categories (EGwItem *item);
void	    e_gw_item_set_to (EGwItem *item, const gchar *to);
const gchar * e_gw_item_get_to (EGwItem *item);
const gchar *e_gw_item_get_msg_content_type (EGwItem *item);
guint32     e_gw_item_get_item_status (EGwItem *item);
void	    e_gw_item_set_content_type (EGwItem *item, const gchar *content_type);
void	    e_gw_item_set_link_info (EGwItem *item, EGwItemLinkInfo *info);
EGwItemLinkInfo *e_gw_item_get_link_info (EGwItem *item);
gchar	    *e_gw_item_get_content_type (EGwItem *item);
const gchar *e_gw_item_get_msg_body_id (EGwItem *item);
gint	    e_gw_item_get_mail_size (EGwItem *item);
void e_gw_item_set_change (EGwItem *item, EGwItemChangeType change_type, const gchar *field_name, gpointer field_value);
gboolean e_gw_item_append_changes_to_soap_message (EGwItem *item, SoupSoapMessage *msg);
void e_gw_item_set_category_name (EGwItem *item, gchar *cateogry_name);
gchar * e_gw_item_get_category_name (EGwItem *item);
void e_gw_item_set_sendoptions (EGwItem *item, gboolean set);
void e_gw_item_set_reply_request (EGwItem *item, gboolean set);
gboolean e_gw_item_get_reply_request (EGwItem *item);
void e_gw_item_set_reply_within (EGwItem *item, gchar *reply_within);
gchar *e_gw_item_get_reply_within (EGwItem *item);
void e_gw_item_set_track_info (EGwItem *item, EGwItemTrack track_info);
EGwItemTrack e_gw_item_get_track_info (EGwItem *item);
void e_gw_item_set_autodelete (EGwItem *item, gboolean set);
gboolean e_gw_item_get_autodelete (EGwItem *item);
void e_gw_item_set_notify_completed (EGwItem *item, EGwItemReturnNotify notify);
EGwItemReturnNotify e_gw_item_get_notify_completed (EGwItem *item);
void e_gw_item_set_notify_accepted (EGwItem *item, EGwItemReturnNotify notify);
EGwItemReturnNotify e_gw_item_get_notify_accepted (EGwItem *item);
void e_gw_item_set_notify_declined (EGwItem *item, EGwItemReturnNotify notify);
EGwItemReturnNotify e_gw_item_get_notify_declined (EGwItem *item);
void e_gw_item_set_notify_opened (EGwItem *item, EGwItemReturnNotify notify);
EGwItemReturnNotify e_gw_item_get_notify_opened (EGwItem *item);
void e_gw_item_set_notify_deleted (EGwItem *item, EGwItemReturnNotify notify);
EGwItemReturnNotify e_gw_item_get_notify_deleted (EGwItem *item);
void e_gw_item_set_expires (EGwItem *item, gchar *expires);
gchar *e_gw_item_get_expires (EGwItem *item);
void e_gw_item_set_delay_until (EGwItem *item, gchar *delay_until);
gchar *e_gw_item_get_delay_until (EGwItem *item);
void e_gw_item_free_cal_id (EGwItemCalId *calid);

#define E_GW_ITEM_CLASSIFICATION_PUBLIC       "Public"
#define E_GW_ITEM_CLASSIFICATION_PRIVATE      "Private"
#define E_GW_ITEM_CLASSIFICATION_CONFIDENTIAL "Confidential"

const gchar *e_gw_item_get_classification (EGwItem *item);
void        e_gw_item_set_classification (EGwItem *item, const gchar *new_class);

#define E_GW_ITEM_ACCEPT_LEVEL_BUSY          "Busy"
#define E_GW_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE "OutOfOffice"
#define E_GW_ITEM_ACCEPT_LEVEL_FREE	     "Free"

const gchar *e_gw_item_get_accept_level (EGwItem *item);
void        e_gw_item_set_accept_level (EGwItem *item, const gchar *new_level);

#define E_GW_ITEM_PRIORITY_HIGH     "High"
#define E_GW_ITEM_PRIORITY_STANDARD "Standard"
#define E_GW_ITEM_PRIORITY_LOW      "Low"

const gchar *e_gw_item_get_priority (EGwItem *item);
void        e_gw_item_set_priority (EGwItem *item, const gchar *new_priority);

const gchar *e_gw_item_get_task_priority (EGwItem *item);
void        e_gw_item_set_task_priority (EGwItem *item, const gchar *new_priority);

GSList *e_gw_item_get_recipient_list (EGwItem *item);
void e_gw_item_set_recipient_list (EGwItem *item, GSList *new_recipient_list);

EGwItemOrganizer *e_gw_item_get_organizer (EGwItem *item);
void e_gw_item_set_organizer (EGwItem  *item, EGwItemOrganizer *organizer);

GSList * e_gw_item_get_attach_id_list (EGwItem *item);
void e_gw_item_set_attach_id_list (EGwItem *item, GSList *attach_list);

GSList *e_gw_item_get_recurrence_dates (EGwItem *item);
void e_gw_item_set_recurrence_dates (EGwItem  *item, GSList *new_recurrence_dates);

GSList *e_gw_item_get_exdate_list (EGwItem *item);
void e_gw_item_set_exdate_list (EGwItem  *item, GSList *new_exdate_list);

void e_gw_item_set_rrule (EGwItem *item, EGwItemRecurrenceRule *rrule);
EGwItemRecurrenceRule *e_gw_item_get_rrule (EGwItem *item);

gint e_gw_item_get_recurrence_key (EGwItem *item);
void e_gw_item_set_recurrence_key (EGwItem *item, gint recurrence_key);

void e_gw_item_set_source (EGwItem *item, const gchar *source);

gint e_gw_item_get_trigger (EGwItem *item);
void e_gw_item_set_trigger (EGwItem *item, gint trigger);

gboolean e_gw_item_has_attachment (EGwItem *item);

gboolean e_gw_item_is_from_internet (EGwItem *item);

const gchar *e_gw_item_get_parent_thread_ids (EGwItem *item);
const gchar * e_gw_item_get_message_id (EGwItem *item);

typedef struct {
	gchar *email;
	gchar *display_name;
	gboolean status_enabled;
	gchar *delivered_date;
	gchar *opened_date;
	gchar *accepted_date;
	gchar *deleted_date;
	gchar *declined_date;
	gchar *completed_date;
	gchar *undelivered_date;
	enum {
		E_GW_ITEM_RECIPIENT_TO,
		E_GW_ITEM_RECIPIENT_CC,
		E_GW_ITEM_RECIPIENT_BC,
		E_GW_ITEM_RECIPIENT_NONE
	} type;

	EGwItemStatus status;
} EGwItemRecipient;

gboolean    e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg);
void e_gw_item_add_distribution_to_soap_message (EGwItem *item, SoupSoapMessage *msg);
G_END_DECLS

#endif
