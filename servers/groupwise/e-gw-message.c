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

#include "e-gw-message.h"

SoupSoapMessage *
e_gw_message_new_with_header (const char *uri, const char *method_name)
{
	SoupSoapMessage *msg;

	msg = soup_soap_message_new (SOUP_METHOD_GET, uri, FALSE, NULL, NULL, NULL);
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return NULL;
	}

	soup_soap_message_start_envelope (msg);
	soup_soap_message_start_body (msg);
	soup_soap_message_start_element (msg, method_name, NULL, NULL);

	return msg;
}

void
e_gw_message_write_string_parameter (SoupSoapMessage *msg, const char *name, const char *value)
{
	soup_soap_message_start_element (msg, name, "types", NULL);
	soup_soap_message_write_string (msg, value);
	soup_soap_message_end_element (msg);
}

void
e_gw_message_write_footer (SoupSoapMessage *msg)
{
	soup_soap_message_end_element (msg);
	soup_soap_message_end_body (msg);
	soup_soap_message_end_envelope (msg);
}
