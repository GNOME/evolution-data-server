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

#ifndef E_GW_MESSAGE_H
#define E_GW_MESSAGE_H

#include <libsoup/soup-soap-message.h>

G_BEGIN_DECLS

SoupSoapMessage *e_gw_message_new_with_header (const char *uri, const char *session_id, const char *method_name);
void             e_gw_message_write_string_parameter (SoupSoapMessage *msg, const char *name, const char *value);
void             e_gw_message_write_footer (SoupSoapMessage *msg);

G_END_DECLS

#endif
