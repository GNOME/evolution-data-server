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

#ifndef E_GW_CONNECTION_H
#define E_GW_CONNECTION_H

#include <glib-object.h>
#include <libsoup/soup-soap-message.h>

G_BEGIN_DECLS

#define E_TYPE_GW_CONNECTION            (e_gw_connection_get_type ())
#define E_GW_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_CONNECTION,	EGwConnection))
#define E_GW_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_CONNECTION,	EGwConnectionClass))
#define E_IS_GW_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_CONNECTION))
#define E_IS_GW_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_CONNECTION))

typedef struct _EGwConnection        EGwConnection;
typedef struct _EGwConnectionClass   EGwConnectionClass;
typedef struct _EGwConnectionPrivate EGwConnectionPrivate;

struct _EGwConnection {
	GObject parent;
	EGwConnectionPrivate *priv;
};

struct _EGwConnectionClass {
	GObjectClass parent_class;
};

GType          e_gw_connection_get_type (void);
EGwConnection *e_gw_connection_new (const char *uri, const char *username, const char *password);

typedef enum {
	E_GW_CONNECTION_STATUS_OK,
	E_GW_CONNECTION_STATUS_INVALID_OBJECT,
	E_GW_CONNECTION_STATUS_INVALID_RESPONSE,
	E_GW_CONNECTION_STATUS_OTHER,
	E_GW_CONNECTION_STATUS_UNKNOWN
} EGwConnectionStatus;

SoupSoapResponse   *e_gw_connection_send_message (EGwConnection *cnc, SoupSoapMessage *msg);

EGwConnectionStatus e_gw_connection_logout (EGwConnection *cnc);
EGwConnectionStatus e_gw_connection_get_items (EGwConnection *cnc, const char *filter, GSList **list);

const char         *e_gw_connection_get_user_email (EGwConnection *cnc);

G_END_DECLS

#endif
