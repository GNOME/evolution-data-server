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

#ifndef E_GW_SENDOPTIONS_H
#define E_GW_SENDOPTIONS_H

#include "soup-soap-response.h"

G_BEGIN_DECLS

#define E_TYPE_GW_SENDOPTIONS            (e_gw_sendoptions_get_type ())
#define E_GW_SENDOPTIONS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_GW_SENDOPTIONS, EGwSendOptions))
#define E_GW_SENDOPTIONS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_GW_SENDOPTIONS, EGwSendOptionsClass))
#define E_IS_GW_SENDOPTIONS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_GW_SENDOPTIONS))
#define E_IS_GW_SENDOPTIONS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_GW_SENDOPTIONS))

typedef struct _EGwSendOptions        EGwSendOptions;
typedef struct _EGwSendOptionsClass   EGwSendOptionsClass;
typedef struct _EGwSendOptionsPrivate EGwSendOptionsPrivate;

struct _EGwSendOptions {
	GObject parent;
	EGwSendOptionsPrivate *priv;
};

struct _EGwSendOptionsClass {
	GObjectClass parent_class;
};
typedef enum {
	E_GW_PRIORITY_UNDEFINED,
	E_GW_PRIORITY_HIGH,
	E_GW_PRIORITY_STANDARD,
	E_GW_PRIORITY_LOW
} EGwSendOptionsPriority;

typedef enum {
	E_GW_SECURITY_NORMAL,
	E_GW_SECURITY_PROPRIETARY,
	E_GW_SECURITY_CONFIDENTIAL,
	E_GW_SECURITY_SECRET,
	E_GW_SECURITY_TOP_SECRET,
	E_GW_SECURITY_FOR_YOUR_EYES_ONLY
} EGwSendOptionsSecurity;

typedef enum {
	E_GW_RETURN_NOTIFY_NONE,
	E_GW_RETURN_NOTIFY_MAIL
} EGwSendOptionsReturnNotify;

typedef enum {
	E_GW_DELIVERED = 1,
	E_GW_DELIVERED_OPENED = 2,
	E_GW_ALL = 3
} EGwTrackInfo;

typedef struct {
	EGwSendOptionsPriority priority;
	gboolean reply_enabled;
	gboolean reply_convenient;
	gint reply_within;
	gboolean expiration_enabled;
	gint expire_after;
	gboolean delay_enabled;
	gint delay_until;
} EGwSendOptionsGeneral;

typedef struct {
	gboolean tracking_enabled;
	EGwTrackInfo track_when;
	gboolean autodelete;
	EGwSendOptionsReturnNotify opened;
	EGwSendOptionsReturnNotify accepted;
	EGwSendOptionsReturnNotify declined;
	EGwSendOptionsReturnNotify completed;
} EGwSendOptionsStatusTracking;

GType e_gw_sendoptions_get_type (void);
EGwSendOptions* e_gw_sendoptions_new_from_soap_parameter (SoupSoapParameter *param);
EGwSendOptionsGeneral* e_gw_sendoptions_get_general_options (EGwSendOptions *opts);
EGwSendOptionsStatusTracking* e_gw_sendoptions_get_status_tracking_options (EGwSendOptions *opts, const gchar *type);
gboolean e_gw_sendoptions_form_message_to_modify (SoupSoapMessage *msg, EGwSendOptions *n_opts, EGwSendOptions *o_opts);
EGwSendOptions * e_gw_sendoptions_new (void);

#endif
