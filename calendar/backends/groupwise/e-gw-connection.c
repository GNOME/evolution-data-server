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
#include <string.h>
#include <libsoup/soup-session-sync.h>
#include <libsoup/soup-soap-message.h>
#include <libecal/e-cal-component.h>
#include "e-gw-connection.h"
#include "e-gw-message.h"

static GObjectClass *parent_class = NULL;
static GHashTable *loaded_connections = NULL;

struct _EGwConnectionPrivate {
	SoupSession *soup_session;

	char *uri;
	char *username;
	char *password;
	char *session_id;
	char *user_name;
	char *user_email;
	char *user_uuid;
};

static EGwConnectionStatus
parse_response_status (SoupSoapResponse *response)
{
	SoupSoapParameter *param, *subparam;

	param = soup_soap_response_get_first_parameter_by_name (response, "Status");
	if (!param)
		return E_GW_CONNECTION_STATUS_UNKNOWN;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "code");
	if (!subparam)
		return E_GW_CONNECTION_STATUS_UNKNOWN;

	switch (soup_soap_parameter_get_int_value (subparam)) {
	case 0 :     return E_GW_CONNECTION_STATUS_OK;
		/* FIXME: map all error codes */
	}

	return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
}

static EGwConnectionStatus
logout (EGwConnection *cnc)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "logoutRequest");
	e_gw_message_write_string_parameter (msg, "session", "types", cnc->priv->session_id);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (response);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);

	/* free memory */
	g_object_unref (response);

	return status;
}

static void
e_gw_connection_dispose (GObject *object)
{
	EGwConnection *cnc = (EGwConnection *) object;
	EGwConnectionPrivate *priv;

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;

	if (priv) {
		if (priv->session_id) {
			logout (cnc);
			priv->session_id = NULL;
		}

		if (priv->soup_session) {
			g_object_unref (priv->soup_session);
			priv->soup_session = NULL;
		}

		if (priv->uri) {
			g_free (priv->uri);
			priv->uri = NULL;
		}

		if (priv->username) {
			g_free (priv->username);
			priv->username = NULL;
		}

		if (priv->password) {
			g_free (priv->password);
			priv->password = NULL;
		}

		if (priv->user_name) {
			g_free (priv->user_name);
			priv->user_name = NULL;
		}

		if (priv->user_email) {
			g_free (priv->user_email);
			priv->user_email = NULL;
		}

		if (priv->user_uuid) {
			g_free (priv->user_uuid);
			priv->user_uuid = NULL;
		}
	}

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
} 

static void
e_gw_connection_finalize (GObject *object)
{
	char *hash_key;
	gpointer orig_key, orig_value;
	EGwConnection *cnc = (EGwConnection *) object;
	EGwConnectionPrivate *priv;

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;

	/* clean up */
	g_free (priv);
	cnc->priv = NULL;

	/* removed the connection from the hash table */
	if (loaded_connections != NULL) {
		hash_key = g_strdup_printf ("%s:%s@%s",
					    priv->username ? priv->username : "",
					    priv->password ? priv->password : "",
					    priv->uri);
		if (g_hash_table_lookup_extended (loaded_connections, hash_key, &orig_key, &orig_value)) {
			g_hash_table_remove (loaded_connections, hash_key);
			if (g_hash_table_size (loaded_connections) == 0) {
				g_hash_table_destroy (loaded_connections);
				loaded_connections = NULL;
			}

			g_free (orig_key);
		}
		g_free (hash_key);
	}

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
e_gw_connection_init (EGwConnection *cnc, EGwConnectionClass *klass)
{
	EGwConnectionPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EGwConnectionPrivate, 1);
	cnc->priv = priv;

	/* create the SoupSession for this connection */
	priv->soup_session = soup_session_sync_new ();
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
e_gw_connection_new (const char *uri, const char *username, const char *password)
{
	EGwConnection *cnc;
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	EGwConnectionStatus status;
	char *hash_key;
	
	/* search the connection in our hash table */
	if (loaded_connections != NULL) {
		hash_key = g_strdup_printf ("%s:%s@%s",
					    username ? username : "",
					    password ? password : "",
					    uri);
		cnc = g_hash_table_lookup (loaded_connections, hash_key);
		g_free (hash_key);

		if (E_IS_GW_CONNECTION (cnc)) {
			g_object_ref (cnc);
			return cnc;
		}
	}

	
	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_GW_CONNECTION, NULL);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (uri, NULL, "loginRequest");
	soup_soap_message_start_element (msg, "auth", "types", NULL);
	soup_soap_message_add_attribute (msg, "type", "types:PlainText", "xsi",
					 "http://www.w3.org/2001/XMLSchema-instance");
	e_gw_message_write_string_parameter (msg, "username", "types", username);
	if (password && *password)
		e_gw_message_write_string_parameter (msg, "password", "types", password);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (cnc);
		return NULL;
	}

	status = parse_response_status (response);

	param = soup_soap_response_get_first_parameter_by_name (response, "session");
	if (!param) {
		g_object_unref (response);
		g_object_unref (cnc);
		return NULL;
	}
	
	cnc->priv->uri = g_strdup (uri);
	cnc->priv->username = g_strdup (username);
	cnc->priv->password = g_strdup (password);
	cnc->priv->session_id = g_strdup (soup_soap_parameter_get_string_value (param));

	/* retrieve user information */
	param = soup_soap_response_get_first_parameter_by_name (response, "UserInfo");
	if (param) {
		SoupSoapParameter *subparam;
		const char *param_value;

		subparam = soup_soap_parameter_get_first_child_by_name (param, "email");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_email  = g_strdup (param_value);
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param, "name");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_name = g_strdup (param_value);
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param, "uuid");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_uuid = g_strdup (param_value);
		}
	}

	/* add the connection to the loaded_connections hash table */
	hash_key = g_strdup_printf ("%s:%s@%s",
				    cnc->priv->username ? cnc->priv->username : "",
				    cnc->priv->password ? cnc->priv->password : "",
				    cnc->priv->uri);
	if (loaded_connections == NULL)
		loaded_connections = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (loaded_connections, hash_key, cnc);

	/* free memory */
	g_object_unref (response);

	return cnc;
}

SoupSoapResponse *
e_gw_connection_send_message (EGwConnection *cnc, SoupSoapMessage *msg)
{
	SoupSoapResponse *response;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), NULL);

	soup_session_send_message (cnc->priv->soup_session, SOUP_MESSAGE (msg));
	if (SOUP_MESSAGE (msg)->status_code != SOUP_STATUS_OK) {
		return NULL;
	}

	/* process response */
	response = soup_soap_message_parse_response (msg);

	return response;
}

EGwConnectionStatus
e_gw_connection_logout (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	g_object_unref (cnc);

	return E_GW_CONNECTION_STATUS_OK;
}

static ECalComponent* 
get_e_cal_component_from_soap_parameter (SoupSoapParameter *param)
{
        SoupSoapParameter *subparam;
        const char *item_type;
        const char *classification;
        const char *accept_level;
        ECalComponent *comp;
        ECalComponentDateTime *dt;
        struct icaltimetype *t;
        int type = 0; /* type : stores enum value of ECalcomponentVType for local access*/ 
        /* FIXME: should add support for notes. */
        /* FIXME: need to add some validation code*/
        comp = e_cal_component_new();        
        item_type = xmlGetProp (param, "type");
        if ( !g_ascii_strcasecmp (item_type, "Appointment")) {
                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
                type = 1;
        }
        else if (!g_ascii_strcasecmp (item_type, "Task")) {
                type = 2;
                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
        }
        else { /* FIXME: Should this be an error. */
                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_NO_TYPE);
                return NULL;
        }
        subparam = soup_soap_parameter_get_first_child_by_name (param, "iCalId");
        if (!subparam)
                return NULL;
        e_cal_component_set_uid (comp, soup_soap_parameter_get_string_value (subparam));
        /* Create icaltimetype from param*/
        subparam = soup_soap_parameter_get_first_child_by_name (param, "created");
        if (!subparam)
                return NULL;
        t = (icaltimetype *)g_malloc ( sizeof (icaltimetype));
        *t = icaltime_from_string (soup_soap_parameter_get_string_value (subparam)); 
        e_cal_component_set_created (comp, t);
        e_cal_component_set_dtstamp (comp, t);
        
        dt = (ECalComponentDateTime *)g_malloc (sizeof (ECalComponentDateTime));
        subparam = soup_soap_parameter_get_first_child_by_name (param, "endDate");
        if (!subparam)
                return NULL;
        t = (icaltimetype *)g_malloc ( sizeof (icaltimetype));
        *t = icaltime_from_string (soup_soap_parameter_get_string_value (subparam)); 
        dt->value = t;
        dt->tzid = g_strdup("UTC"); 
        e_cal_component_set_dtend (comp, dt);
        
        dt = (ECalComponentDateTime *)g_malloc (sizeof (ECalComponentDateTime));
        subparam = soup_soap_parameter_get_first_child_by_name (param, "startDate");
        if (!subparam)
                return NULL;
        t = (icaltimetype *)g_malloc ( sizeof (icaltimetype));
        *t = icaltime_from_string (soup_soap_parameter_get_string_value (subparam)); 
        dt->value = t;
        dt->tzid = g_strdup("UTC"); 
        e_cal_component_set_dtstart (comp, dt); 
       
        /* Classification */
        classification = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (param, "class"));
        if ( !g_ascii_strcasecmp (classification, "Public"))
                e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
        else
                e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_UNKNOWN);
        /* Transparency */
        accept_level = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (param, "acceptLevel"));
        if ( !g_ascii_strcasecmp (accept_level, "Busy"))
                e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
        else
                e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);
        /* EVENT -specific properties */
        if (type == 1) 
                e_cal_component_set_location (comp, 
                        soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (param, "place")));
         
        return comp;                
}

EGwConnectionStatus
e_gw_connection_get_items (EGwConnection *cnc, const char * filter, GSList **list)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;
	const char *calendar_uid;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
        
	/* FIXME: Make this a separate function. obtain the uid for the container - 'Calendar' */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getContainerListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
        
        e_gw_message_write_string_parameter (msg, "parent", NULL, "folders");
	e_gw_message_write_string_parameter (msg, "recursive", NULL, "true");
	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		g_object_unref (response);
		return status;
	}

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "folders");
	
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        } else {
		SoupSoapParameter *subparam, *name, *id;
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "Folder");
		     subparam != NULL;
		     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "Folder")) {
			name = soup_soap_parameter_get_first_child_by_name (subparam, "name");
			if (name && (!strcmp (soup_soap_parameter_get_string_value (name), "Calendar"))) {
				id = soup_soap_parameter_get_first_child_by_name (subparam, "id");
				if (id) 
					calendar_uid = g_strdup (soup_soap_parameter_get_string_value (id));
                                else
                                        return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
                                                
				break;
			}
			
		}
		if (!subparam) {
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}
			
	}	
			
	
	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getItemsRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
        
        e_gw_message_write_string_parameter (msg, "container", NULL, calendar_uid);
	if (filter)
		e_gw_message_write_string_parameter (msg, "Filter", NULL, filter);
	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		g_object_unref (response);
		return status;
	}

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "items");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
        /* parse these parameters into ecalcomponents*/
        for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
                subparam != NULL;
                subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) { 
                        ECalComponent *comp = NULL;
                        comp = get_e_cal_component_from_soap_parameter (subparam);
                        if (comp)
                                *list = g_slist_append (*list, comp);
                        else
                                continue; /*FIXME: skips element if error. need to generate proper error*/
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;        
}

const char* 
e_gw_connection_get_user_email (EGwConnection *cnc)
{
    g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
  
    return (const char*) cnc->priv->user_email;
	
}

