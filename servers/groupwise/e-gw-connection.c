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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <libsoup/soup-session.h>
#include <libsoup/soup-soap-message.h>
#include "e-gw-connection.h"

static GObjectClass *parent_class = NULL;
static SoupSession *soup_session = NULL;

struct _EGwConnectionPrivate {
};

static void
e_gw_connection_dispose (GObject *object)
{
	EGwConnection *cnc = (EGwConnection *) object;
	EGwConnectionPrivate *priv;

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;

	if (priv) {
	}

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
} 

static void
e_gw_connection_finalize (GObject *object)
{
	EGwConnection *cnc = (EGwConnection *) object;
	EGwConnectionPrivate *priv;

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;

	/* clean up */
	g_free (priv);
	cnc->priv = NULL;

	g_object_unref (soup_session);

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_connection_class_init (EGwConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_gw_connection_dispose;
	object_class->finalize = e_gw_connection_finalize;
}

static void
session_weak_ref_cb (gpointer user_data, GObject *where_the_object_was)
{
	soup_session = NULL;
}

static void
e_gw_connection_init (EGwConnection *cnc, EGwConnectionClass *klass)
{
	EGwConnectionPrivate *priv;

	/* create the SoupSession if not already created */
	if (soup_session)
		g_object_ref (soup_session);
	else {
		soup_session = soup_session_new ();
		g_object_weak_ref (G_OBJECT (soup_session), (GWeakNotify) session_weak_ref_cb, NULL);
	}

	/* allocate internal structure */
	priv = g_new0 (EGwConnectionPrivate, 1);
	cnc->priv = priv;
}

GType
e_gw_connection_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EGwConnectionClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_gw_connection_class_init,
                        NULL, NULL,
                        sizeof (EGwConnection),
                        0,
                        (GInstanceInitFunc) e_gw_connection_init
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EGwConnection", &info, 0);
	}

	return type;
}

EGwConnection *
e_gw_connection_new (void)
{
	EGwConnection *cnc;

	cnc = g_object_new (E_TYPE_GW_CONNECTION, NULL);

	return cnc;
}

EGwConnectionStatus
e_gw_connection_login (EGwConnection *cnc,
		       const char *uri,
		       const char *username,
		       const char *password)
{
	SoupSoapMessage *msg;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = soup_soap_message_new (SOUP_METHOD_GET, uri, FALSE, NULL, NULL, NULL);
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return E_GW_CONNECTION_STATUS_OTHER;
	}

	soup_soap_message_start_envelope (msg);
	soup_soap_message_start_body (msg);
	soup_soap_message_start_element (msg, "loginRequest", NULL, NULL);
	soup_soap_message_start_element (msg, "auth", "types", "http://schemas.novell.com/2003/10/NCSP/types.xsd");
	soup_soap_message_add_attribute (msg, "type", "types:PlainText", "xsi",
					 "http://www.w3.org/2001/XMLSchema-instance");
	soup_soap_message_start_element (msg, "username", "types", NULL);
	soup_soap_message_write_string (msg, username);
	soup_soap_message_end_element (msg);
	if (password && *password) {
		soup_soap_message_start_element (msg, "password", "types", NULL);
		soup_soap_message_write_string (msg, password);
		soup_soap_message_end_element (msg);
	}
	soup_soap_message_end_element (msg);
	soup_soap_message_end_element (msg);
	soup_soap_message_end_body (msg);
	soup_soap_message_end_envelope (msg);

	/* send message to server */
	soup_soap_message_persist (msg);
	soup_session_send_message (soup_session, SOUP_MESSAGE (msg));
	if (SOUP_MESSAGE (msg)->status_code != SOUP_STATUS_OK) {
		/* FIXME: map error codes */
		return E_GW_CONNECTION_STATUS_OTHER;
	}

	/* FIXME: process response */

	return E_GW_CONNECTION_STATUS_OK;
}
