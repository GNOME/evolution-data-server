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

static char *
get_evo_date_from_string (const char * str1)
{
        /* Return a new string with the separators stripped. */
        char *str2;
        int i, j, len = strlen(str1);
        str2 = g_malloc0 (len);
        for (i = 0,j = 0; i < len; i++) {
                if ((str1[i] != '-') && (str1[i] != ':')) {
                                str2[j] = str1[i];
                                j++;
                }                
        }
        str2[j] = '\0';
        return str2;
}

static GSList*
get_attendee_list_from_string (const char *to_string)
{
        /* String is of format <name>[;''<name>]*   */
        GSList *list = NULL;
        ECalComponentAttendee *attendee;
        char *user;
        int i, j, len;
        
        len = strlen (to_string);
        user = g_malloc0 (len);
        for (i = 0, j = 0; i < len; i++) {
                if (to_string[i] != ';') {
                        user[j] = to_string[i];
                        j++;
                } else {
                        ECalComponentAttendee *attendee;
                        user[j] = '\0';
                        attendee = g_malloc0( sizeof (ECalComponentAttendee));
                        /*FIXME to fill member, value etc we need more data*/ 
                        attendee->cn = g_strdup (user);
                        list = g_slist_append (list, attendee);
                        j = 0;
                        i++; /* skips the space after ; */
                }        
        }
                
        user[j] = '\0';
        attendee = g_malloc0( sizeof (ECalComponentAttendee));
        attendee->cn = g_strdup (user);
        list = g_slist_append (list, attendee);

        g_free (user);
        return list;
        
}

static ECalComponent* 
get_e_cal_component_from_soap_parameter (SoupSoapParameter *param)
{
        SoupSoapParameter *subparam;
        const char *item_type, *classification, *accept_level, *to_list;
        char *dtstring;
        ECalComponent *comp;
        ECalComponentDateTime *dt;
        ECalComponentText summary;
        struct icaltimetype t;
        int type = 0; /* type : stores enum value of ECalcomponentVType for local access*/ 
        GSList *attendee_list = NULL;

        /* FIXME: need to add some validation code*/
        comp = e_cal_component_new();        
        item_type = xmlGetProp (param, "type");
        if ( !g_ascii_strcasecmp (item_type, "Appointment")) {
                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
                type = 1;
        } else if (!g_ascii_strcasecmp (item_type, "Task")) {
                type = 2;
                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
        } else if (!g_ascii_strcasecmp (item_type, "Note")) {
                type = 3;
                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
        } else {
                g_object_unref (comp);
                return NULL;
        }
        /* Property - uid */
        subparam = soup_soap_parameter_get_first_child_by_name (param, "id");
        if (!subparam) {
                g_object_unref (comp);
                return NULL;
        }
        e_cal_component_set_uid (comp, soup_soap_parameter_get_string_value (subparam));

        /* Property - created*/ 
        subparam = soup_soap_parameter_get_first_child_by_name (param, "created");
        if (!subparam){
                g_object_unref (comp);
                return NULL;
        }

        dtstring = get_evo_date_from_string (soup_soap_parameter_get_string_value (subparam));
        t = icaltime_from_string (dtstring);
        g_free (dtstring);
        e_cal_component_set_created (comp, &t);
        e_cal_component_set_dtstamp (comp, &t);
        
                
        /* Property - startDate*/ 
        subparam = soup_soap_parameter_get_first_child_by_name (param, "startDate");
        if (!subparam) {
                g_object_unref (comp);
                return NULL;
        }
        
        dtstring = get_evo_date_from_string (soup_soap_parameter_get_string_value (subparam));
        t = icaltime_from_string (dtstring);
        g_free (dtstring);
        dt = g_malloc0 (sizeof (ECalComponentDateTime));
        dt->value = &t;
        dt->tzid = "UTC"; 
        e_cal_component_set_dtstart (comp, dt); 
       
        /* Category - missing server implementation */

        /* Classification */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "class");
	if (subparam) {
		classification = soup_soap_parameter_get_string_value (subparam);
		if ( !g_ascii_strcasecmp (classification, "Public"))
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);
		else if (!g_ascii_strcasecmp (classification, "Private"))
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PRIVATE);
		else if (!g_ascii_strcasecmp (classification, "Confidential"))
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_CONFIDENTIAL);
		else
			e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_UNKNOWN);
	}

        /* Transparency - Busy, OutOfOffice, Free, Tentative*/
        /*FIXME  possible loss of information */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "acceptLevel");
	if (subparam) {
		accept_level = soup_soap_parameter_get_string_value (subparam);
		if ( !g_ascii_strcasecmp (accept_level, "Busy") || !g_ascii_strcasecmp (accept_level, "OutOfOffice"))
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
		else
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);
	}

        /* Property - summary*/ 
	subparam = soup_soap_parameter_get_first_child_by_name (param, "subject");
	if (!subparam) {
		g_object_unref (comp);
		return NULL;
	}
        summary.value = g_strdup (soup_soap_parameter_get_string_value (subparam));
        summary.altrep = NULL;
        e_cal_component_set_summary (comp, &summary);

        /* Property - attendee-list*/ 
        subparam = soup_soap_parameter_get_first_child_by_name (param, "distribution");
	if (subparam) {
		/* FIXME  what to do with 'from' data*/

		to_list = soup_soap_parameter_get_string_value ( soup_soap_parameter_get_first_child_by_name (subparam, "to"));
		if (to_list)
			attendee_list = get_attendee_list_from_string (to_list);

		if (!attendee_list) {
			g_object_unref (comp);
			return NULL;
		}
		/*e_cal_component_set_attendee_list (comp, attendee_list);*/
	}
                
        /* FIXME  Property - status*/
        /* FIXME  Property priority */  
        subparam = soup_soap_parameter_get_first_child_by_name (param, "options");
        if (!subparam) {
                g_object_unref (comp);
                return NULL;
        }
        subparam = soup_soap_parameter_get_first_child_by_name (param, "priority");
        if (!subparam) {
                g_object_unref (comp);
                return NULL;
        }
        else {
                const char *priority;
                int i;
                priority = soup_soap_parameter_get_string_value (subparam);
                if (!g_ascii_strcasecmp ("High", priority )) 
                        i = 3;
                else if (!g_ascii_strcasecmp ("Standard", priority)) 
                        i = 5;
                else if (!g_ascii_strcasecmp ("Low", priority))
                        i = 7;
                else 
                        i = -1;
                e_cal_component_set_priority (comp, &i);
        }
        
        /* EVENT -specific properties */
        if (type == 1) {
                /* Property - endDate*/ 
                subparam = soup_soap_parameter_get_first_child_by_name (param, "endDate");
                if (!subparam) {
                        g_object_unref (comp);
                        return NULL;
                }

                dtstring = get_evo_date_from_string (soup_soap_parameter_get_string_value (subparam));
                t = icaltime_from_string (dtstring);
                g_free (dtstring);
                
                dt = g_malloc0 (sizeof (ECalComponentDateTime));
                dt->value = &t;
                dt->tzid = "UTC"; 
                e_cal_component_set_dtend (comp, dt);

                e_cal_component_set_location (comp, 
                        soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (param, "place")));

        } else if (type == 2) {
                /* Property - dueDate*/ 
                subparam = soup_soap_parameter_get_first_child_by_name (param, "dueDate");
                if (!subparam) {
                        g_object_unref (comp);
                        return NULL;
                }

                dtstring = get_evo_date_from_string (soup_soap_parameter_get_string_value (subparam));
                t = icaltime_from_string (dtstring);
                g_free (dtstring);
                
                dt = g_malloc0 (sizeof (ECalComponentDateTime));
                dt->value = &t;
                dt->tzid = "UTC"; 
                e_cal_component_set_due (comp, dt);

                /*FIXME  Property - completed - missing server implementation  */
                /* Only 0 and 100 are legal values since server data is boolean */
                subparam = soup_soap_parameter_get_first_child_by_name (param, "completed");
                if (subparam) {
                        const char *completed = soup_soap_parameter_get_string_value (subparam);
                        int i =0;
                        if (!g_ascii_strcasecmp (completed, "true")) {
                                i = 100;
                                e_cal_component_set_percent (comp, &i);
                        } else 
                                e_cal_component_set_percent (comp, &i);
                }        

        } 
        return comp;                
}

EGwConnectionStatus
e_gw_connection_get_container_list (EGwConnection *cnc, SoupSoapResponse **response)
{
	SoupSoapMessage *msg;
        EGwConnectionStatus status;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getContainerListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
        
        e_gw_message_write_string_parameter (msg, "parent", NULL, "folders");
	e_gw_message_write_string_parameter (msg, "recursive", NULL, "true");
	e_gw_message_write_footer (msg);

        /* send message to server */
        *response = e_gw_connection_send_message (cnc, msg);
        if (!*response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = parse_response_status (*response);
        g_object_unref (msg);
        return status;
}

char *
e_gw_connection_get_container_id (EGwConnection *cnc, const char *name)
{
	SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;
	char *container_id;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
	g_return_val_if_fail (name != NULL, NULL);

        status = e_gw_connection_get_container_list (cnc, &response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
                g_object_unref (response);
                return NULL;
        }

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "folders");
	
        if (!param) {
                g_object_unref (response);
                return NULL;
        } else {
		SoupSoapParameter *subparam, *name_param, *id;
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "Folder");
		     subparam != NULL;
		     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "Folder")) {
			name_param = soup_soap_parameter_get_first_child_by_name (subparam, "name");
			if (name_param && (!strcmp (soup_soap_parameter_get_string_value (name_param), name))) {
				id = soup_soap_parameter_get_first_child_by_name (subparam, "id");
				if (id) {
					container_id = g_strdup (soup_soap_parameter_get_string_value (id));
					break;
                                }
			}
		}
		if (!subparam) {
			g_object_unref (response);
			return NULL;
		}
	}

	return container_id;
}

EGwConnectionStatus
e_gw_connection_get_items (EGwConnection *cnc, const char *container, const char * filter, GSList **list)
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

EGwConnectionStatus
e_gw_connection_get_deltas (EGwConnection *cnc, GSList **list)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getDeltaRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
        
        soup_soap_message_start_element (msg, "CalendarItem", NULL, NULL);
        soup_soap_message_end_element (msg);
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
	param = soup_soap_response_get_first_parameter_by_name (response, "changed");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
        if (!g_ascii_strcasecmp ( soup_soap_parameter_get_string_value (param), "0")) {
                g_message ("No deltas");
                return E_GW_CONNECTION_STATUS_OK;
        }                

        param = soup_soap_response_get_first_parameter_by_name (response, "deltas");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
        
        /* process all deletes first*/
        param = soup_soap_parameter_get_first_child_by_name (param, "delete");
        if (param) {
                for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
                        subparam != NULL;
                        subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) { 
                                /*process each item */ 
                }
        }
        
        /* process adds*/
        param = soup_soap_parameter_get_first_child_by_name (param, "add");
        if (param) {
                for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
                        subparam != NULL;
                        subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) { 
                                /*process each item */ 
                }
        }
        
        /* process updates*/
        param = soup_soap_parameter_get_first_child_by_name (param, "update");
        if (param) {
                for (subparam = soup_soap_parameter_get_first_child_by_name(param, "item");
                        subparam != NULL;
                        subparam = soup_soap_parameter_get_next_child (subparam)) { 
                                /*process each item */ 
                }
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;        
}

EGwConnectionStatus
e_gw_connection_send_item (EGwConnection *cnc, EGwItem *item)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* FIXME: compose message */

	return status;
}

EGwConnectionStatus
e_gw_connection_send_appointment (EGwConnection *cnc, const char *container, ECalComponent *comp)
{
	EGwItem *item;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	item = e_gw_item_new_appointment (container, comp);
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
start_freebusy_session (EGwConnection *cnc, GSList *users, 
               time_t start, time_t end, const char **session)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;
        GSList *l;
        icaltimetype icaltime;
        const char *start_date, *end_date;


	if (users == NULL)
                return E_GW_CONNECTION_STATUS_INVALID_OBJECT;
        /* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "startFreeBusySessionRequest");
        /* FIXME users is just a buch of user names - associate it with uid,
         * email id apart from the name*/
        
        soup_soap_message_start_element (msg, "users", "types", NULL); 
        for ( l = users; l != NULL; l = g_slist_next (l)) {
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
e_gw_connection_get_freebusy_info (EGwConnection *cnc, GSList *users, time_t start, time_t end, GSList **freebusy)
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
                        SoupSoapParameter *param_blocks, *subparam_block;
                        const char *uuid, *email, *name;
                        email = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (subparam, "email"));
                        uuid = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (subparam, "uuid"));
                        name = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (subparam, "displayName"));
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
                                ECalComponent *comp;
                                ECalComponentOrganizer *organizer;
                                ECalComponentDateTime *dt;
                                icaltimetype t;
                                const char *start, *end; 
                                char *dtstring;

                                comp = e_cal_component_new ();
                                e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_FREEBUSY); 
                                organizer = g_malloc0 (sizeof (ECalComponentOrganizer));
                                /* FIXME  verify the mappings b/w response and ECalComponent */
                                organizer->cn = g_strdup (name);
                                organizer->sentby = g_strdup (email);
                                organizer->value = g_strdup (uuid);
                                e_cal_component_set_organizer (comp, organizer);

                                start = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (subparam_block, "startDate"));
                                dtstring = get_evo_date_from_string (start);
                                t = icaltime_from_string (dtstring);
                                g_free (dtstring);
                                dt = g_malloc0 (sizeof (ECalComponentDateTime));
                                dt->value = &t;
                                dt->tzid = "UTC"; 
                                e_cal_component_set_dtstart (comp, dt);
                               
                                end = soup_soap_parameter_get_string_value (soup_soap_parameter_get_first_child_by_name (subparam_block, "endDate"));
                                dtstring = get_evo_date_from_string (end);
                                t = icaltime_from_string (dtstring);
                                g_free (dtstring);
                                dt = g_malloc0 (sizeof (ECalComponentDateTime));
                                dt->value = &t;
                                dt->tzid = "UTC"; 
                                e_cal_component_set_dtend (comp, dt);

                                *freebusy = g_slist_append (*freebusy, comp);
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
