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

#include <config.h>
#include <string.h>
#include <libsoup/soup-uri.h>
#include "e-gw-message.h"

static void
print_header (gpointer name, gpointer value, gpointer data)
{
	g_print ("%s: %s\n", (char *) name, (char *) value);
}

static void
debug_handler (SoupMessage *msg, gpointer user_data)
{
	g_print ("%d %s\nSOAP-Debug: %p @ %lu\n",
                msg->status_code, msg->reason_phrase,
                msg, time (0));

	/* print headers */
	soup_message_foreach_header (msg->response_headers, print_header, NULL);

	/* print response */
	if (msg->response.length) {
		fputc ('\n', stdout);
		fwrite (msg->response.body, 1, msg->response.length, stdout);
		fputc ('\n', stdout);
	}
}

static void
setup_debug (SoupSoapMessage *msg)
{
	const SoupUri *suri;

	suri = soup_message_get_uri (SOUP_MESSAGE (msg));
	g_print ("%s %s%s%s HTTP/1.1\nSOAP-Debug: %p @ %lu\n",
		 SOUP_MESSAGE (msg)->method, suri->path,
		 suri->query ? "?" : "",
		 suri->query ? suri->query : "",
		 msg, (unsigned long) time (0));

	/* print message headers */
	print_header ("Host", suri->host, NULL);
	soup_message_foreach_header (SOUP_MESSAGE (msg)->request_headers, print_header, NULL);

	soup_message_add_handler (SOUP_MESSAGE (msg), SOUP_HANDLER_POST_BODY, debug_handler, NULL);
}

SoupSoapMessage *
e_gw_message_new_with_header (const char *uri, const char *session_id, const char *method_name)
{
	SoupSoapMessage *msg;

	msg = soup_soap_message_new (SOUP_METHOD_POST, uri, FALSE, NULL, NULL, NULL);
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return NULL;
	}

	soup_message_add_header (SOUP_MESSAGE (msg)->request_headers, "Content-Type", "text/xml");
	soup_message_add_header (SOUP_MESSAGE (msg)->request_headers, "User-Agent",
				 "Evolution/" VERSION);
	soup_message_add_header (SOUP_MESSAGE (msg)->request_headers,"Connection",  "Keep-Alive");
	soup_message_add_header (SOUP_MESSAGE (msg)->request_headers, "SOAPAction", method_name);

	if (g_getenv ("GROUPWISE_DEBUG"))
		setup_debug (msg);

	soup_soap_message_start_envelope (msg);
	if (session_id && *session_id) {
		soup_soap_message_start_element (msg, "Header","SOAP-ENV", NULL);
		soup_soap_message_add_attribute (msg, "encodingStyle", "", "SOAP-ENV", NULL);
		/* FIXME: cannot use e_gw_message_write_string_parameter as it sets prefix -types*/
		soup_soap_message_start_element (msg, "session", NULL, NULL);
		soup_soap_message_write_string (msg, session_id);
		soup_soap_message_end_element (msg);
		soup_soap_message_end_element (msg);
	}
	soup_soap_message_start_body (msg);
	soup_soap_message_add_attribute (msg, "encodingStyle", "", "SOAP-ENV", NULL);
	soup_soap_message_add_namespace (msg, "types", "http://schemas.novell.com/2003/10/NCSP/types.xsd");

	soup_soap_message_start_element (msg, method_name, NULL, NULL);

	return msg;
}

void
e_gw_message_write_string_parameter (SoupSoapMessage *msg, const char *name, const char *prefix, const char *value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_write_string (msg, value);
	soup_soap_message_end_element (msg);
}

void 
e_gw_message_write_string_parameter_with_attribute (SoupSoapMessage *msg,
						    const char *name,
						    const char *prefix,
						    const char *value, 
						    const char *attribute_name,
						    const char *attribute_value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_add_attribute (msg, attribute_name, attribute_value, NULL, NULL);
	soup_soap_message_write_string (msg, value);
	soup_soap_message_end_element (msg);
}

void
e_gw_message_write_base64_parameter (SoupSoapMessage *msg, const char *name, const char *prefix, const char *value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_write_base64 (msg, value, strlen (value));
	soup_soap_message_end_element (msg);
}

void
e_gw_message_write_int_parameter (SoupSoapMessage *msg, const char *name, const char *prefix, long value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_write_int (msg, value);
	soup_soap_message_end_element (msg);
}

void
e_gw_message_write_footer (SoupSoapMessage *msg)
{
	soup_soap_message_end_element (msg);
	soup_soap_message_end_body (msg);
	soup_soap_message_end_envelope (msg);

	soup_soap_message_persist (msg);

	if (g_getenv ("GROUPWISE_DEBUG")) {
		/* print request's body */
		fputc ('\n', stdout);
		fwrite (SOUP_MESSAGE (msg)->request.body, 1, SOUP_MESSAGE (msg)->request.length, stdout);
		fputc ('\n', stdout);
	}
}
