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
	case 0 : return E_GW_CONNECTION_STATUS_OK;
	case 59905 : return E_GW_CONNECTION_BAD_PARAMETER;
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
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

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

static void 
set_attendee_list_from_soap_parameter (ECalComponent *comp, SoupSoapParameter *param)
{
        SoupSoapParameter *param_recipient;
        GSList *list = NULL;
        ECalComponentAttendee *attendee;
        char *email, *cn;

        for (param_recipient = soup_soap_parameter_get_first_child_by_name (param, "recipient");
                param_recipient != NULL;
                param_recipient = soup_soap_parameter_get_next_child_by_name (param, "recipient")) {

                SoupSoapParameter *subparam;
                attendee = g_new0 (ECalComponentAttendee, 1);

                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "email");
                if (subparam) {
                        email = soup_soap_parameter_get_string_value (subparam);
                        if (email)
                                attendee->value = email;
                }        
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "displayName");
                if (subparam) {
                        cn = soup_soap_parameter_get_string_value (subparam);
                        if (cn)
                                attendee->cn = cn;
                }
                
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "distType");
                if (subparam) {
                        const char *dist_type;
                        dist_type = soup_soap_parameter_get_string_value (subparam);
                        if (!strcmp (dist_type, "TO")) 
                                attendee->role = ICAL_ROLE_REQPARTICIPANT;
                        else if (!strcmp (dist_type, "CC"))
                                attendee->role = ICAL_ROLE_OPTPARTICIPANT;
                        else
                                attendee->role = ICAL_ROLE_NONPARTICIPANT;
                }

                list = g_slist_append (list, attendee);
        }        
	if (list) {
		GSList *l;
		e_cal_component_set_attendee_list (comp, list);
		for (l = list; l != NULL; l = g_slist_next (l)) {
			ECalComponentAttendee *attendee = (ECalComponentAttendee *)l->data;
			g_free (attendee->cn);
			g_free (attendee->value);
		}	
		g_slist_foreach (list, (GFunc) g_free, NULL);
	}
}

EGwConnectionStatus
e_gw_connection_get_container_list (EGwConnection *cnc, GList **container_list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (container_list != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);

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
        g_object_unref (msg);

	if (status != E_GW_CONNECTION_STATUS_OK) {
                g_object_unref (response);
                return status;
        }

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "folders");	
        if (!param) {
                g_object_unref (response);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        } else {
		SoupSoapParameter *subparam;
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "folder");
		     subparam != NULL;
		     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "folder")) {
			EGwContainer *container;

			container = e_gw_container_new_from_soap_parameter (subparam);
			if (container)
				*container_list = g_list_append (*container_list, container);
		}
	}

	g_object_unref (response);

        return status;
}

void
e_gw_connection_free_container_list (GList *container_list)
{
	g_return_if_fail (container_list != NULL);

	g_list_foreach (container_list, (GFunc) g_object_unref, NULL);
	g_list_free (container_list);
}

char *
e_gw_connection_get_container_id (EGwConnection *cnc, const char *name)
{
        EGwConnectionStatus status;
	GList *container_list = NULL, *l;
	char *container_id = NULL;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
	g_return_val_if_fail (name != NULL, NULL);

        status = e_gw_connection_get_container_list (cnc, &container_list);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		e_gw_connection_free_container_list (container_list);
                return NULL;
        }

	/* search the container in the list */
	for (l = container_list; l != NULL; l = l->next) {
		EGwContainer *container = E_GW_CONTAINER (l->data);

		if (strcmp (e_gw_container_get_name (container), name) == 0) {
			container_id = g_strdup (e_gw_container_get_id (container));
			break;
		}
	}

	e_gw_connection_free_container_list (container_list);

	return container_id;
}

EGwConnectionStatus
e_gw_connection_get_items (EGwConnection *cnc, const char *container, const char * filter, GList **list)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getItemsRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

        e_gw_message_write_string_parameter (msg, "container", NULL, container);
        //e_gw_message_write_string_parameter (msg, "view", NULL, "recipients");
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
                g_object_unref (msg);
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
		EGwItem *item;

		item = e_gw_item_new_from_soap_parameter (container, subparam);
		if (item)
			*list = g_list_append (*list, item);
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
}

/* static gboolean */
/* update_cache_item (ECalBackendCache *cache, SoupSoapParameter *param) */
/* { */
/*         SoupSoapParameter *subparam; */
/*         const char *uid, *item_type, *classification, *accept_level; */
/*         char *dtstring; */
/*         ECalComponent *comp; */
/*         ECalComponentDateTime dt; */
/*         ECalComponentText summary; */
/*         struct icaltimetype t; */
/*         int type = 0; /\* type : stores enum value of ECalcomponentVType for local access*\/  */

/*         /\* FIXME: need to add some validation code*\/ */
        
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "id"); */
/*         if (!subparam) { */
/*                 return FALSE; */
/*         } */

/*         uid = soup_soap_parameter_get_string_value (subparam); */
/*         if (!uid) */
/*                 return FALSE; */

/*         comp = e_cal_backend_cache_get_component (cache, uid, NULL ); */
/*         if (!comp) */
/*                 return FALSE; */

/*         /\* Ensure that the component type matches response data *\/ */
/*         item_type = xmlGetProp (param, "type"); */
/*         if ( !g_ascii_strcasecmp (item_type, "Appointment")) { */
/*                 if (e_cal_component_get_vtype (comp) != E_CAL_COMPONENT_EVENT) */
/*                         return FALSE; */
/*                 type = 1; */
/*         } */
/*         else if (!g_ascii_strcasecmp (item_type, "Task")) { */
/*                 if (e_cal_component_get_vtype (comp) != E_CAL_COMPONENT_TODO) */
/*                         return FALSE; */
/*                 type = 2; */
/*         } */
/*         else  */
/*                 return FALSE; */
        
/*         /\* Property - created*\/  */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "created"); */
/*         if (subparam) { */
/*                 dtstring = e_gw_connection_get_date_from_string (soup_soap_parameter_get_string_value (subparam)); */
/*                 t = icaltime_from_string (dtstring); */
/*                 g_free (dtstring); */
/*                 e_cal_component_set_created (comp, &t); */
/*                 e_cal_component_set_dtstamp (comp, &t); */
/*         } */
                
/*         /\* Property - startDate*\/  */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "startDate"); */
        
/*         if (subparam) { */
/*                 dtstring = e_gw_connection_get_date_from_string (soup_soap_parameter_get_string_value (subparam)); */
/*                 t = icaltime_from_string (dtstring); */
/*                 g_free (dtstring); */
/*                 dt.value = &t; */
/*                 dt.tzid = "UTC";  */
/*                 e_cal_component_set_dtstart (comp, &dt);  */
/*         } */
       
/*         /\* Category - missing server implementation *\/ */

/*         /\* Classification *\/ */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "class");  */
/*         if (subparam) { */
/*                 classification = soup_soap_parameter_get_string_value (subparam); */
/*                 if ( !g_ascii_strcasecmp (classification, "Public")) */
/*                         e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC); */
/*                 else if (!g_ascii_strcasecmp (classification, "Private")) */
/*                         e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PRIVATE); */
/*                 else if (!g_ascii_strcasecmp (classification, "Confidential")) */
/*                         e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_CONFIDENTIAL); */
/*                 else */
/*                         e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_UNKNOWN); */
/*         } */

/*         /\* Transparency - Busy, OutOfOffice, Free, Tentative*\/ */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "acceptLevel");  */
/*         if (subparam) { */
/*                 accept_level = soup_soap_parameter_get_string_value (subparam); */
/*                 if ( !g_ascii_strcasecmp (accept_level, "Busy") || !g_ascii_strcasecmp (accept_level, "OutOfOffice")) */
/*                         e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE); */
/*                 else */
/*                         e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT); */
/*         } */

/*         /\* Property - summary*\/  */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "subject");  */
/*         if (subparam) { */
/*                 summary.value = soup_soap_parameter_get_string_value (subparam); */
/*                 summary.altrep = NULL; */
/*                 e_cal_component_set_summary (comp, &summary); */
/*         } */

/*         /\* Property - attendee-list*\/  */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "distribution"); */
/*         if (subparam) { */
/*                 /\* FIXME  what to do with 'from' data*\/ */
                
/*                 subparam = soup_soap_parameter_get_first_child_by_name (subparam, "recipients"); */
/*                 if (subparam)  */
/* 			set_attendee_list_from_soap_parameter (comp, subparam); */
/*         }         */

/*         /\* FIXME  Property - status*\/ */
/*         /\* FIXME  Property priority *\/   */
/*         subparam = soup_soap_parameter_get_first_child_by_name (param, "options"); */
/*         if (subparam) { */
/*                 subparam = soup_soap_parameter_get_first_child_by_name (param, "priority"); */
/*                 if (subparam) { */
/*                         const char *priority; */
/*                         int i; */
/*                         priority = soup_soap_parameter_get_string_value (subparam); */
/*                         if (!g_ascii_strcasecmp ("High", priority ))  */
/*                                 i = 3; */
/*                         else if (!g_ascii_strcasecmp ("Standard", priority))  */
/*                                 i = 5; */
/*                         else if (!g_ascii_strcasecmp ("Low", priority)) */
/*                                 i = 7; */
/*                         else  */
/*                                 i = -1; */
/*                         e_cal_component_set_priority (comp, &i); */
/*                 } */
/*         } */
        
/*         /\* EVENT -specific properties *\/ */
/*         if (type == 1) { */
/*                 /\* Property - endDate*\/  */
/*                 subparam = soup_soap_parameter_get_first_child_by_name (param, "endDate"); */

/*                 if (subparam) { */
/*                         dtstring = get_evo_date_from_string (soup_soap_parameter_get_string_value (subparam)); */
/*                         t = icaltime_from_string (dtstring); */
/*                         g_free (dtstring); */
                        
/*                         dt.value = &t; */
/*                         dt.tzid = "UTC";  */
/*                         e_cal_component_set_dtend (comp, &dt); */
/*                 } */

/*                 subparam = soup_soap_parameter_get_first_child_by_name (param, "place");  */
/*                 if (subparam)  */
/*                         e_cal_component_set_location (comp, soup_soap_parameter_get_string_value (subparam)); */

/*         } else if (type == 2) { */
/*                 /\* Property - dueDate*\/  */
/*                 subparam = soup_soap_parameter_get_first_child_by_name (param, "dueDate"); */
/*                 if (subparam) { */
/*                         dtstring = get_evo_date_from_string (soup_soap_parameter_get_string_value (subparam)); */
/*                         t = icaltime_from_string (dtstring); */
/*                         g_free (dtstring); */
                        
/*                         dt.value = &t; */
/*                         dt.tzid = "UTC";  */
/*                         e_cal_component_set_due (comp, &dt); */
/*                 }         */

/*                 /\*FIXME  Property - completed - missing server implementation  *\/ */
/*                 /\* Only 0 and 100 are legal values since server data is boolean *\/ */
/*                 subparam = soup_soap_parameter_get_first_child_by_name (param, "completed"); */
/*                 if (subparam) { */
/*                         const char *completed = soup_soap_parameter_get_string_value (subparam); */
/*                         int i =0; */
/*                         if (!g_ascii_strcasecmp (completed, "true")) { */
/*                                 i = 100; */
/*                                 e_cal_component_set_percent (comp, &i); */
/*                         } else  */
/*                                 e_cal_component_set_percent (comp, &i); */
/*                 }         */
/*         }  */

/*         return TRUE; */
/* } */

EGwConnectionStatus
e_gw_connection_get_deltas (gpointer handle)
{
/* 	CacheUpdateHandle *update_handle; */
/* 	EGwConnection *cnc; */
/* 	ECalBackendCache *cache; */
/* 	SoupSoapMessage *msg; */
/*         SoupSoapResponse *response; */
/*         EGwConnectionStatus status; */
/*         SoupSoapParameter *param, *subparam; */

        
/* 	update_handle = (CacheUpdateHandle *) handle; */
/* 	cnc = update_handle->cnc; */
/* 	cache = update_handle->cache; */
/* 	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT); */

/* 	/\* build the SOAP message *\/ */
/*         msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getDeltaRequest"); */
/*         if (!msg) { */
/*                 g_warning (G_STRLOC ": Could not build SOAP message"); */
/* 		g_object_unref (cnc); */
/* 		g_object_unref (cache); */
/* 		g_free (update_handle); */
/*                 return E_GW_CONNECTION_STATUS_UNKNOWN; */
/*         } */
        
/*         soup_soap_message_start_element (msg, "CalendarItem", NULL, NULL); */
/*         soup_soap_message_end_element (msg); */
/*         e_gw_message_write_footer (msg); */

/*         /\* send message to server *\/ */
/*         response = e_gw_connection_send_message (cnc, msg); */
/*         if (!response) { */
/*                 g_object_unref (msg); */
/* 		g_object_unref (cnc); */
/* 		g_object_unref (cache); */
/* 		g_free (update_handle); */
/*                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; */
/*         } */

/*         status = parse_response_status (response); */
/*         if (status != E_GW_CONNECTION_STATUS_OK) { */
/* 		g_object_unref (response); */
/*                 g_object_unref (msg); */
/* 		g_object_unref (cnc); */
/* 		g_object_unref (cache); */
/* 		g_free (update_handle); */
/* 		return status; */
/* 	} */

/* 	/\* if status is OK - parse result. return the list *\/	 */
/* 	param = soup_soap_response_get_first_parameter_by_name (response, "changed"); */
/*         if (!param) { */
/*                 g_object_unref (response); */
/*                 g_object_unref (msg); */
/* 		g_object_unref (cnc); */
/* 		g_object_unref (cache); */
/* 		g_free (update_handle); */
/*                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; */
/*         } */
	
/*         if (!g_ascii_strcasecmp ( soup_soap_parameter_get_string_value (param), "0")) { */
/*                 g_message ("No deltas"); */
/* 		g_object_unref (cnc); */
/* 		g_object_unref (cache); */
/* 		g_free (update_handle); */
/*                 return E_GW_CONNECTION_STATUS_OK; */
/*         }                 */

/*         param = soup_soap_response_get_first_parameter_by_name (response, "deltas"); */
/*         if (!param) { */
/*                 g_object_unref (response); */
/*                 g_object_unref (msg); */
/* 		g_object_unref (cnc); */
/* 		g_object_unref (cache); */
/* 		g_free (update_handle); */
/*                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; */
/*         } */
        
/*         /\* process all deletes first*\/ */
/*         param = soup_soap_parameter_get_first_child_by_name (param, "delete"); */
/*         if (param) { */
/*                 for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item"); */
/*                         subparam != NULL; */
/*                         subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {  */
/*                                 /\*process each item *\/  */
/*                                 const char *uid; */
/*                                 SoupSoapParameter *param_id; */

/*                                 param_id = soup_soap_parameter_get_first_child_by_name (subparam, "id"); */
/*                                 if (!param_id) { */
/*                                         g_object_unref (response); */
/*                                         g_object_unref (msg); */
/*                                         g_object_unref (cnc); */
/* 					g_object_unref (cache); */
/* 					return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; */
/*                                 } */
/*                                 uid = soup_soap_parameter_get_string_value (param_id); */
/*                                 if (!e_cal_backend_cache_remove_component (cache, uid, NULL)) */
/*                                         g_message ("Could not remove %s", uid); */
/*                 } */
/*         } */
        
/*         /\* process adds*\/ */
/*         param = soup_soap_parameter_get_first_child_by_name (param, "add"); */
/*         if (param) { */
/*                 for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item"); */
/*                         subparam != NULL; */
/*                         subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {  */
/*                                 /\*process each item *\/  */
/*                                 ECalComponent *comp; */
/*                                 comp = get_e_cal_component_from_soap_parameter (subparam); */
/*                                 if (!comp) { */
/*                                         g_object_unref (response); */
/*                                         g_object_unref (msg); */
/*                                         g_object_unref (cnc); */
/* 					g_object_unref (cache); */
/* 					return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; */
/*                                 } */
/*                                 if (!e_cal_backend_cache_put_component (cache, comp)) */
/*                                         g_message ("Could not add the component"); */
/*                 } */
/*         } */
        
/*         /\* process updates*\/ */
/*         param = soup_soap_parameter_get_first_child_by_name (param, "update"); */
/*         if (param) { */
/*                 for (subparam = soup_soap_parameter_get_first_child_by_name(param, "item"); */
/* 		     subparam != NULL; */
/* 		     subparam = soup_soap_parameter_get_next_child (subparam)) {  */
/* 			/\*process each item *\/  */
/* 			update_cache_item (cache, subparam); */
/*                 } */
/*         } */
               
/* 	/\* free memory *\/ */
/*         g_object_unref (response); */
/* 	g_object_unref (msg); */
/* 	g_free (update_handle); */

        return E_GW_CONNECTION_STATUS_OK;        
}

EGwConnectionStatus
e_gw_connection_send_item (EGwConnection *cnc, EGwItem *item)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* compose SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "sendItemRequest");
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return E_GW_CONNECTION_STATUS_UNKNOWN;
	}

	if (!e_gw_item_append_to_soap_message (item, msg)) {
		g_warning (G_STRLOC ": Could not append item to SOAP message");
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_OBJECT;
	}

	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);

	g_object_unref (msg);
	g_object_unref (response);

	return status;
}

EGwConnectionStatus
e_gw_connection_send_appointment (EGwConnection *cnc, const char *container, ECalComponent *comp)
{
	EGwItem *item;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	item = e_gw_item_new_from_cal_component (container, comp);
	status = e_gw_connection_send_item (cnc, item);
	g_object_unref (item);

	return status;
}

EGwConnectionStatus
e_gw_connection_remove_item (EGwConnection *cnc, const char *container, const char *id)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "removeItemRequest");
	
	if (container && *container)
		e_gw_message_write_string_parameter (msg, "container", NULL, container);
	e_gw_message_write_string_parameter (msg, "id", NULL, id);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

static EGwConnectionStatus
start_freebusy_session (EGwConnection *cnc, GList *users, 
               time_t start, time_t end, const char **session)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;
        GList *l;
        icaltimetype icaltime;
        const char *start_date, *end_date;

	if (users == NULL)
                return E_GW_CONNECTION_STATUS_INVALID_OBJECT;

        /* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "startFreeBusySessionRequest");
        /* FIXME users is just a buch of user names - associate it with uid,
         * email id apart from the name*/
        
        soup_soap_message_start_element (msg, "users", "types", NULL); 
        for ( l = users; l != NULL; l = g_list_next (l)) {
                e_gw_message_write_string_parameter (msg, "user", NULL, l->data);
        }
	
        soup_soap_message_end_element (msg);

        /*FIXME check if this needs to be formatted into GW form with separators*/
        /*FIXME  the following code converts time_t to String representation
         * through icaltime. Find if a direct conversion exists.  */ 
        /* Timezone in server is assumed to be UTC */
        icaltime = icaltime_from_timet(start, FALSE );
        start_date = icaltime_as_ical_string (icaltime);
        
        icaltime = icaltime_from_timet(end, FALSE);
        end_date = icaltime_as_ical_string (icaltime);
        	
        e_gw_message_write_string_parameter (msg, "startDate", "http://www.w3.org/2001/XMLSchema", start_date);
        e_gw_message_write_string_parameter (msg, "endDate", "http://www.w3.org/2001/XMLSchema", end_date);
        
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK)
        {
                g_object_unref (msg);
                g_object_unref (response);
                return status;
        }
        
       	/* if status is OK - parse result, return the list */
        param = soup_soap_response_get_first_parameter_by_name (response, "freeBusySessionId");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
	*session = soup_soap_parameter_get_string_value (param); 
        /* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

static EGwConnectionStatus 
close_freebusy_session (EGwConnection *cnc, const char *session)
{
        SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

        /* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "closeFreeBusySessionRequest");
       	e_gw_message_write_string_parameter (msg, "freeBusySessionId", NULL, session);
        e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);

        g_object_unref (msg);
        g_object_unref (response);
        return status;
}

EGwConnectionStatus
e_gw_connection_get_freebusy_info (EGwConnection *cnc, GList *users, time_t start, time_t end, GList **freebusy)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;
        const char *session;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

        /* Perform startFreeBusySession */
        status = start_freebusy_session (cnc, users, start, end, &session); 
        /*FIXME log error messages  */
        if (status != E_GW_CONNECTION_STATUS_OK)
                return status;

        /* getFreeBusy */
        /* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getFreeBusyRequest");
       	e_gw_message_write_string_parameter (msg, "session", NULL, session);
        e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
                g_object_unref (msg);
                g_object_unref (response);
                return status;
        }

        /* FIXME  the FreeBusyStats are not used currently.  */
        param = soup_soap_response_get_first_parameter_by_name (response, "freeBusyInfo");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        for (subparam = soup_soap_parameter_get_first_child_by_name (param, "user");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "user")) {
		SoupSoapParameter *param_blocks, *subparam_block, *tmp;
		const char *uuid = NULL, *email = NULL, *name = NULL;

		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "email");
		if (tmp)
			email = soup_soap_parameter_get_string_value (tmp);
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "uuid");
		if (tmp)
			uuid = soup_soap_parameter_get_string_value (tmp);
		tmp = soup_soap_parameter_get_first_child_by_name (subparam, "displayName");
		if (tmp)
			name = soup_soap_parameter_get_string_value (tmp);

		param_blocks = soup_soap_parameter_get_first_child_by_name (subparam, "blocks");
		if (!param_blocks) {
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}
        
		for (subparam_block = soup_soap_parameter_get_first_child_by_name (param_blocks, "block");
		     subparam_block != NULL;
		     subparam_block = soup_soap_parameter_get_next_child_by_name (subparam_block, "block")) {

			/* process each block and create ECal free/busy components.*/ 
			SoupSoapParameter *tmp;
			ECalComponent *comp;
			ECalComponentOrganizer organizer;
			ECalComponentDateTime dt;
			icaltimetype t;
			const char *start, *end;

			comp = e_cal_component_new ();
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_FREEBUSY); 
			/* FIXME  verify the mappings b/w response and ECalComponent */
			if (name)
				organizer.cn = name;
			if (email)
				organizer.sentby = email;
			if (uuid)
				organizer.value = uuid;
			e_cal_component_set_organizer (comp, &organizer);

			tmp = soup_soap_parameter_get_first_child_by_name (subparam_block, "startDate");
			if (tmp) {
				start = soup_soap_parameter_get_string_value (tmp);
				t = e_gw_connection_get_date_from_string (start);
				dt.value = &t;
				dt.tzid = "UTC"; 
				e_cal_component_set_dtstart (comp, &dt);
			}        

			tmp = soup_soap_parameter_get_first_child_by_name (subparam, "endDate");
			if (tmp) {
				end = soup_soap_parameter_get_string_value (tmp);
				t = e_gw_connection_get_date_from_string (end);
				dt.value = &t;
				dt.tzid = "UTC"; 
				e_cal_component_set_dtend (comp, &dt);
			}

			*freebusy = g_list_append (*freebusy, comp);
		}
	}

        g_object_unref (msg);
        g_object_unref (response);

        /* closeFreeBusySession*/
        return close_freebusy_session (cnc, session);
}

        
const char *
e_gw_connection_get_user_name (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->user_name;
}

const char* 
e_gw_connection_get_user_email (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
  
	return (const char*) cnc->priv->user_email;
	
}

const char *
e_gw_connection_get_user_uuid (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->user_uuid;
}

struct icaltimetype
e_gw_connection_get_date_from_string (const char *dtstring)
{
	struct icaltimetype t;
        char *str2;
        int i, j, len = strlen (dtstring);
	
        str2 = g_malloc0 (len);
        for (i = 0,j = 0; i < len; i++) {
                if ((dtstring[i] != '-') && (dtstring[i] != ':')) {
			str2[j] = dtstring[i];
			j++;
                }
        }

	str2[j] = '\0';
	t = icaltime_from_string (str2);
	g_free (str2);

        return t;
}
