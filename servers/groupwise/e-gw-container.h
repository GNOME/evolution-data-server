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

#ifndef E_GW_CONTAINER_H
#define E_GW_CONTAINER_H

#include "soup-soap-response.h"
#include "soup-soap-message.h"

G_BEGIN_DECLS

#define E_TYPE_GW_CONTAINER            (e_gw_container_get_type ())
#define E_GW_CONTAINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_CONTAINER, EGwContainer))
#define E_GW_CONTAINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_CONTAINER, EGwContainerClass))
#define E_IS_GW_CONTAINER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_CONTAINER))
#define E_IS_GW_CONTAINER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_CONTAINER))

typedef struct _EShUsers            EShUsers;
typedef struct _EGwContainer        EGwContainer;
typedef struct _EGwContainerClass   EGwContainerClass;
typedef struct _EGwContainerPrivate EGwContainerPrivate;

struct _EGwContainer {
	GObject parent;
	EGwContainerPrivate *priv;
};

struct _EGwContainerClass {
	GObjectClass parent_class;
};

struct _EShUsers {
	gchar *email;
	gint rights;
};

typedef enum {
	E_GW_CONTAINER_TYPE_ROOT,
	E_GW_CONTAINER_TYPE_INBOX,
	E_GW_CONTAINER_TYPE_SENT,
	E_GW_CONTAINER_TYPE_CALENDAR,
	E_GW_CONTAINER_TYPE_CONTACTS,
	E_GW_CONTAINER_TYPE_DOCUMENTS,
	E_GW_CONTAINER_TYPE_QUERY,
	E_GW_CONTAINER_TYPE_CHECKLIST,
	E_GW_CONTAINER_TYPE_DRAFT,
	E_GW_CONTAINER_TYPE_CABINET,
	E_GW_CONTAINER_TYPE_TRASH,
	E_GW_CONTAINER_TYPE_JUNK,
	E_GW_CONTAINER_TYPE_FOLDER

} EGwContainerType;

GType         e_gw_container_get_type (void);
EGwContainer *e_gw_container_new_from_soap_parameter (SoupSoapParameter *param);
gboolean      e_gw_container_set_from_soap_parameter (EGwContainer *container,
						      SoupSoapParameter *param);
const gchar   *e_gw_container_get_name (EGwContainer *container);
void          e_gw_container_set_name (EGwContainer *container, const gchar *new_name);
const gchar   *e_gw_container_get_id (EGwContainer *container);
void          e_gw_container_set_id (EGwContainer *container, const gchar *new_id);
const gchar   *e_gw_container_get_parent_id (EGwContainer *container);
void	      e_gw_container_set_parent_id (EGwContainer *container, const gchar *parent_id);
guint32       e_gw_container_get_total_count (EGwContainer *container);
guint32       e_gw_container_get_unread_count (EGwContainer *container);
gboolean      e_gw_container_get_is_writable (EGwContainer *container);
void          e_gw_container_set_is_writable (EGwContainer *container, gboolean writable);
gboolean     e_gw_container_get_is_frequent_contacts (EGwContainer *container);
void         e_gw_container_set_is_frequent_contacts (EGwContainer *container, gboolean is_frequent_contacts);
gboolean    e_gw_container_is_root (EGwContainer *container);
const gchar *  e_gw_container_get_owner(EGwContainer *container);
const gchar *  e_gw_container_get_modified(EGwContainer *container);
gint           e_gw_container_get_sequence(EGwContainer *container);
gboolean      e_gw_container_get_is_shared_by_me(EGwContainer *container);
gboolean      e_gw_container_get_is_shared_to_me(EGwContainer *container);
gint	      e_gw_container_get_rights(EGwContainer *container, gchar *email);
EGwContainerType e_gw_container_get_container_type (EGwContainer *container);
void	      e_gw_container_get_user_list(EGwContainer *container, GList **user_list);
void	      e_gw_container_form_message (SoupSoapMessage *msg, gchar *id, GList *new_list, const gchar *sub, const gchar *mesg, gint flag);
gboolean e_gw_container_get_is_system_folder (EGwContainer *container);
void e_gw_container_set_is_system_folder (EGwContainer *container, gboolean is_system_folder);

G_END_DECLS

#endif
