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

#ifndef E_GW_CONNECTION_H
#define E_GW_CONNECTION_H

#include <glib-object.h>
#include "soup-soap-message.h"
#include "e-gw-proxy.h"
#include "e-gw-container.h"
#include "e-gw-item.h"
#include "e-gw-filter.h"
#include "e-gw-sendoptions.h"
#include "e-gw-recur-utils.h"

G_BEGIN_DECLS

#define E_TYPE_GW_CONNECTION            (e_gw_connection_get_type ())
#define E_GW_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_CONNECTION, EGwConnection))
#define E_GW_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_CONNECTION, EGwConnectionClass))
#define E_IS_GW_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_CONNECTION))
#define E_IS_GW_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_CONNECTION))

typedef struct _EGwConnection        EGwConnection;
typedef struct _EGwConnectionClass   EGwConnectionClass;
typedef struct _EGwConnectionPrivate EGwConnectionPrivate;

typedef struct {
	int status;
	char *description;
}EGwConnectionErrors;

struct _EGwConnection {
	GObject parent;
	EGwConnectionPrivate *priv;
};

struct _EGwConnectionClass {
	GObjectClass parent_class;
};

/* TODO:This has to go either in a generic file or specific to junk*/
typedef struct {
	char *id ;
	char *match;
	char *matchType;
	char *lastUsed;
	int version;
	char *modified;
} EGwJunkEntry;

GType          e_gw_connection_get_type (void);
EGwConnection *e_gw_connection_new (const char *uri, const char *username, const char *password);

EGwConnection * e_gw_connection_new_with_error_handler (const char *uri, const char *username, const char *password, EGwConnectionErrors *errors);

typedef enum {
	E_GW_CONNECTION_STATUS_OK,
	E_GW_CONNECTION_STATUS_INVALID_CONNECTION,
	E_GW_CONNECTION_STATUS_INVALID_OBJECT,
	E_GW_CONNECTION_STATUS_INVALID_RESPONSE,
	E_GW_CONNECTION_STATUS_NO_RESPONSE,
	E_GW_CONNECTION_STATUS_OBJECT_NOT_FOUND,
	E_GW_CONNECTION_STATUS_UNKNOWN_USER,
	E_GW_CONNECTION_STATUS_BAD_PARAMETER,
	E_GW_CONNECTION_STATUS_ITEM_ALREADY_ACCEPTED,
	E_GW_CONNECTION_STATUS_REDIRECT,
	E_GW_CONNECTION_STATUS_OTHER,
	E_GW_CONNECTION_STATUS_UNKNOWN,
	E_GW_CONNECTION_STATUS_INVALID_PASSWORD = 53273
} EGwConnectionStatus;

#define E_GW_CURSOR_POSITION_CURRENT "current"
#define E_GW_CURSOR_POSITION_START "start"
#define E_GW_CURSOR_POSITION_END "end"

SoupSoapResponse   *e_gw_connection_send_message (EGwConnection *cnc, SoupSoapMessage *msg);
EGwConnectionStatus e_gw_connection_parse_response_status (SoupSoapResponse *response);
const char         *e_gw_connection_get_error_message (EGwConnectionStatus status);

EGwConnectionStatus e_gw_connection_logout (EGwConnection *cnc);
EGwConnectionStatus e_gw_connection_get_container_list (EGwConnection *cnc, const char *top, GList **container_list);
void                e_gw_connection_free_container_list (GList *container_list);
char               *e_gw_connection_get_container_id (EGwConnection *cnc, const char *name);
EGwConnectionStatus e_gw_connection_get_items (EGwConnection *cnc, const char *container,
					       const char *view, EGwFilter *filter, GList **list);
EGwConnectionStatus e_gw_connection_get_deltas ( EGwConnection *cnc, GSList **adds, GSList **deletes, GSList **updates);
EGwConnectionStatus e_gw_connection_send_item (EGwConnection *cnc, EGwItem *item, GSList **id_list);
EGwConnectionStatus e_gw_connection_remove_item (EGwConnection *cnc, const char *container, const char *id);
EGwConnectionStatus e_gw_connection_remove_items (EGwConnection *cnc, const char *container, GList *item_ids);
EGwConnectionStatus e_gw_connection_get_items_delta_info (EGwConnection *cnc, const char *container, gdouble *first_sequence, gdouble *last_sequence, gdouble *last_po_rebuild_time);
EGwConnectionStatus e_gw_connection_get_items_delta (EGwConnection *cnc, const char *container, const char *view, const char *count, const char * start_sequence, GList **add_list, GList **delete_list);


const char         *e_gw_connection_get_uri (EGwConnection *cnc);
const char         *e_gw_connection_get_session_id (EGwConnection *cnc);
const char         *e_gw_connection_get_user_name (EGwConnection *cnc);
const char         *e_gw_connection_get_user_email (EGwConnection *cnc);
const char         *e_gw_connection_get_user_uuid (EGwConnection *cnc);
const char 	   *e_gw_connection_get_version (EGwConnection *cnc);
const char	   *e_gw_connection_get_server_time (EGwConnection *cnc) ;


time_t              e_gw_connection_get_date_from_string (const char *dtstring);
char               *e_gw_connection_format_date_string (const char *dtstring);


EGwConnectionStatus e_gw_connection_create_item (EGwConnection *cnc, EGwItem *item, char** id);
EGwConnectionStatus e_gw_connection_get_item (EGwConnection *cnc, const char *container, const char *id, const char *view, EGwItem **item);
EGwConnectionStatus e_gw_connection_modify_item (EGwConnection *cnc, const char *id, EGwItem *item);
EGwConnectionStatus e_gw_connection_accept_request (EGwConnection *cnc, const char *id, const char *accept_level, const char *accept_comment, const char *recurrence_key);
EGwConnectionStatus e_gw_connection_decline_request (EGwConnection *cnc, const char *id, const char *decline_comment, const char *recurrence_key);
EGwConnectionStatus e_gw_connection_retract_request (EGwConnection *cnc, const char *id, const char *comment, gboolean retract_all, gboolean resend);
EGwConnectionStatus e_gw_connection_complete_request (EGwConnection *cnc, const char *id);
EGwConnectionStatus e_gw_connection_delegate_request (EGwConnection *cnc, EGwItem *item, const char *id, const char *comments_org, const char *comments_del, const char *recur_key);
EGwConnectionStatus e_gw_connection_create_book (EGwConnection *cnc, char *book_name, char**id);
EGwConnectionStatus e_gw_connection_remove_book (EGwConnection *cnc, char *book_uid);
EGwConnectionStatus e_gw_connection_get_address_book_list (EGwConnection *cnc, GList **container_list);
EGwConnectionStatus e_gw_connection_get_address_book_id ( EGwConnection *cnc, char *book_name, char**id , gboolean *is_writable);
EGwConnectionStatus e_gw_connection_get_categories  (EGwConnection *cnc, GHashTable **categories_by_id, GHashTable **categoreis_by_name);
EGwConnectionStatus e_gw_connection_add_members (EGwConnection *cnc, const char *group_id, GList *member_ids);
EGwConnectionStatus e_gw_connection_remove_members (EGwConnection *cnc, const char *group_id, GList *member_ids);
EGwConnectionStatus e_gw_connection_get_items_from_ids (EGwConnection *cnc, const char *container, const char *view, GPtrArray *item_ids, GList **list);

EGwConnectionStatus e_gw_connection_create_cursor (EGwConnection *cnc, const char *container, const char *view, EGwFilter *filter, int *cursor);
EGwConnectionStatus e_gw_connection_destroy_cursor (EGwConnection *cnc, const char *container,  int cursor);
EGwConnectionStatus e_gw_connection_read_cursor (EGwConnection *cnc, const char *container, int cursor, gboolean forward, int count, const char *cursor_seek, GList **item_list);
EGwConnectionStatus e_gw_connection_position_cursor (EGwConnection *cnc, const char *container, int cursor, const char *seek, int offset);

EGwConnectionStatus e_gw_connection_get_quick_messages (EGwConnection *cnc, const char *container, const char *view, char **start_date, const char *message_list, const char *item_types, const char *item_sources, int count, GSList **item_list);

EGwConnectionStatus e_gw_connection_create_folder(EGwConnection *cnc, const char *parent_name,const char *folder_name, char **container_id) ;
EGwConnectionStatus
e_gw_connection_get_attachment (EGwConnection *cnc, const char *id, int offset, int length, const char **attachment, int *attach_length) ;
EGwConnectionStatus e_gw_connection_get_attachment_base64 (EGwConnection *cnc, const char *id, int offset, int length, const char **attachment, int *attach_length, int *offset_r);
EGwConnectionStatus e_gw_connection_add_item (EGwConnection *cnc, const char *container, const char *id) ;
EGwConnectionStatus e_gw_connection_add_items (EGwConnection *cnc, const char *container, GList *item_ids) ;
EGwConnectionStatus e_gw_connection_move_item (EGwConnection *cnc, const char *id, const char *dest_container_id, const char *from_container_id);
EGwConnectionStatus e_gw_connection_rename_folder (EGwConnection *cnc, const char *id ,const char *new_name) ;
EGwConnectionStatus e_gw_connection_get_settings (EGwConnection *cnc, EGwSendOptions **opts);
EGwConnectionStatus e_gw_connection_modify_settings (EGwConnection *cnc, EGwSendOptions *opts);
EGwConnectionStatus e_gw_connection_add_items (EGwConnection *cnc, const char *container, GList *item_ids) ;
EGwConnectionStatus e_gw_connection_rename_folder (EGwConnection *cnc, const char *id ,const char *new_name) ;
EGwConnectionStatus e_gw_connection_share_folder (EGwConnection *cnc, gchar *id, GList *new_list, const char *sub, const char *mesg ,int flag);
EGwConnectionStatus e_gw_connection_accept_shared_folder (EGwConnection *cnc, gchar *folder_name, gchar *container_id, gchar *item_id, gchar *desc);
EGwConnectionStatus e_gw_connection_purge_deleted_items (EGwConnection *cnc);
EGwConnectionStatus e_gw_connection_purge_selected_items (EGwConnection *cnc, GList *item_ids);

EGwConnectionStatus e_gw_connection_mark_read(EGwConnection *cnc, GList *item_ids) ;
EGwConnectionStatus e_gw_connection_mark_unread(EGwConnection *cnc, GList *item_ids) ;
EGwConnectionStatus e_gw_connection_reply_item (EGwConnection *cnc, const char *id, const char *view, EGwItem **item) ;
EGwConnectionStatus e_gw_connection_forward_item (EGwConnection *cnc, const char *id, const char *view, gboolean embed, EGwItem **item);
EGwConnectionStatus e_gw_connection_create_junk_entry (EGwConnection *cnc, const char *value, const char *match_type , const char *list_type);
EGwConnectionStatus e_gw_connection_get_junk_settings (EGwConnection *cnc, int *use_junk, int *use_block, int *use_pab,  int *persistence);
EGwConnectionStatus e_gw_connection_modify_junk_settings (EGwConnection *cnc, int use_junk, int use_block, int use_pab , int persistence);
EGwConnectionStatus e_gw_connection_get_junk_entries (EGwConnection *cnc, GList **entries);
EGwConnectionStatus  e_gw_connection_remove_junk_entry (EGwConnection *cnc, const char *id);
EGwConnectionStatus e_gw_connection_read_cal_ids (EGwConnection *cnc, const char *container, int cursor, gboolean forward, int count, const char *cursor_seek, GList **list);
EGwConnectionStatus e_gw_connection_get_proxy_access_list (EGwConnection *cnc, GList **proxy_list);
EGwConnectionStatus e_gw_connection_add_proxy (EGwConnection *cnc, proxyHandler *new_proxy);
EGwConnectionStatus e_gw_connection_remove_proxy (EGwConnection *cnc, proxyHandler *newProxy);
EGwConnectionStatus e_gw_connection_modify_proxy (EGwConnection *cnc, proxyHandler *newProxy);
EGwConnectionStatus e_gw_connection_get_proxy_list (EGwConnection *cnc, GList **proxy_info);
EGwConnection *e_gw_connection_get_proxy_connection (EGwConnection *cnc1, char *username, const char *password, const char *proxy, int* permissions);
EGwConnectionStatus e_gw_connection_get_all_mail_uids (EGwConnection *cnc, const char *container, int cursor, gboolean forward, int count, const char *cursor_seek, GList **list);

G_END_DECLS

#endif
