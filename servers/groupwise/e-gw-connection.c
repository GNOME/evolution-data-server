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
#include <ctype.h>
#include <libgnome/gnome-i18n.h>
#include <libsoup/soup-session-sync.h>
#include <libsoup/soup-soap-message.h>
#include "e-gw-connection.h"
#include "e-gw-message.h"
#include "e-gw-filter.h"

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

EGwConnectionStatus
e_gw_connection_parse_response_status (SoupSoapResponse *response)
{
	SoupSoapParameter *param, *subparam;

	param = soup_soap_response_get_first_parameter_by_name (response, "status");
	if (!param)
		return E_GW_CONNECTION_STATUS_UNKNOWN;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "code");
	if (!subparam)
		return E_GW_CONNECTION_STATUS_UNKNOWN;

	switch (soup_soap_parameter_get_int_value (subparam)) {
	case 0 : return E_GW_CONNECTION_STATUS_OK;
	case 59905 : return E_GW_CONNECTION_STATUS_BAD_PARAMETER;
		/* FIXME: map all error codes */
	}

	return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
}

const char *
e_gw_connection_get_error_message (EGwConnectionStatus status)
{
	switch (status) {
	case E_GW_CONNECTION_STATUS_OK :
		break;
	case E_GW_CONNECTION_STATUS_INVALID_CONNECTION :
		return _("Invalid connection");
	case E_GW_CONNECTION_STATUS_INVALID_OBJECT :
		return _("Invalid object");
	case E_GW_CONNECTION_STATUS_INVALID_RESPONSE :
		return _("Invalid response from server");
	case E_GW_CONNECTION_STATUS_OBJECT_NOT_FOUND :
		return _("Object not found");
	case E_GW_CONNECTION_STATUS_BAD_PARAMETER :
		return _("Bad parameter");
	case E_GW_CONNECTION_STATUS_OTHER :
	case E_GW_CONNECTION_STATUS_UNKNOWN :
	default :
		return _("Unknown error");
	}

	return NULL;
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

	status = e_gw_connection_parse_response_status (response);

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
	printf ("gw connection dispose \n");
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
	printf ("gw connection finalize\n");
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

	status = e_gw_connection_parse_response_status (response);

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

EGwConnectionStatus
e_gw_connection_get_container_list (EGwConnection *cnc, GList **container_list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (container_list != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getFolderListRequest");
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

        status = e_gw_connection_parse_response_status (response);
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
e_gw_connection_get_items (EGwConnection *cnc, const char *container, const char *view, EGwFilter *filter, GList **list)
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
	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view);
	if (filter) 
		e_gw_filter_append_to_soap_message (filter, msg);
	
	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
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

EGwConnectionStatus
e_gw_connection_get_deltas ( EGwConnection *cnc, GSList **adds, GSList **deletes, GSList **updates)
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
		 g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_UNKNOWN; 
         } 
        
	 /*FIXME  make this generic */
         soup_soap_message_start_element (msg, "CalendarItem", NULL, NULL); 
         soup_soap_message_end_element (msg); 
         e_gw_message_write_footer (msg); 

         /* send message to server */ 
         response = e_gw_connection_send_message (cnc, msg); 
         if (!response) { 
                 g_object_unref (msg); 
		 g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; 
         } 

         status = e_gw_connection_parse_response_status (response); 
         if (status != E_GW_CONNECTION_STATUS_OK) { 
 		g_object_unref (response); 
		g_object_unref (msg); 
 		g_object_unref (cnc); 
 		return status; 
 	} 

 	/* if status is OK - parse result. return the list */	 
 	param = soup_soap_response_get_first_parameter_by_name (response, "changed"); 
         if (!param) { 
                 g_object_unref (response); 
                 g_object_unref (msg); 
		 g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; 
         } 
	
         if (!g_ascii_strcasecmp ( soup_soap_parameter_get_string_value (param), "0")) { 
                 g_message ("No deltas"); 
		 g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_OK; 
         }                 

         param = soup_soap_response_get_first_parameter_by_name (response, "deltas"); 
         if (!param) { 
                 g_object_unref (response); 
                 g_object_unref (msg); 
		 g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; 
         } 
        
         /* process all deletes first*/ 
         param = soup_soap_parameter_get_first_child_by_name (param, "delete"); 
         if (param) { 
                 for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item"); 
                         subparam != NULL; 
                         subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {  
                                 /*process each item */  
                                 char *uid; 
                                 SoupSoapParameter *param_id; 

                                 param_id = soup_soap_parameter_get_first_child_by_name (subparam, "id"); 
                                 if (!param_id) { 
                                         g_object_unref (response); 
                                         g_object_unref (msg); 
                                         g_object_unref (cnc); 
                                 } 
                                 uid = (char *)soup_soap_parameter_get_string_value (param_id); 
                                 /*if (!e_cal_backend_cache_remove_component (cache, uid, NULL)) 
                                         g_message ("Could not remove %s", uid); */
				 *deletes = g_slist_append (*deletes, uid);
                 } 
         } 
        
         /* process adds*/ 
         param = soup_soap_parameter_get_first_child_by_name (param, "add"); 
         if (param) { 
                 for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item"); 
                         subparam != NULL; 
                         subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {  
				/*process each item */  
				EGwItem *item;
				/*FIXME  pass the container id */
				item = e_gw_item_new_from_soap_parameter ("Calendar", subparam);
                                if (!item) { 
                                         g_object_unref (response); 
                                         g_object_unref (msg); 
                                         g_object_unref (cnc); 
 					return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; 
                                 } 
                                 /*if (!e_cal_backend_cache_put_component (cache, comp)) 
                                         g_message ("Could not add the component"); */
				 *adds = g_slist_append (*adds, item);
                 } 
         } 
        
         /* process updates*/ 
         param = soup_soap_parameter_get_first_child_by_name (param, "update"); 
         if (param) { 
                 for (subparam = soup_soap_parameter_get_first_child_by_name(param, "item"); 
 		     subparam != NULL; 
 		     subparam = soup_soap_parameter_get_next_child (subparam)) {  
			 EGwItem *item;
			 /*process each item */
			 /*item = get_item_from_updates (subparam);*/
			 item = e_gw_item_new_from_soap_parameter ("Calendar", subparam);
			 if (item)
				 *updates = g_slist_append (*updates, item);
                 } 
         }
	 
	 /* free memory */ 
	 g_object_unref (response); 
	 g_object_unref (msg); 

        return E_GW_CONNECTION_STATUS_OK;        
}

EGwConnectionStatus
e_gw_connection_send_item (EGwConnection *cnc, EGwItem *item, char **id)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	if (id)
		*id = NULL;

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

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_OK && id != NULL) {
		SoupSoapParameter *param;

		/* get the generated ID from the SOAP response */
		param = soup_soap_response_get_first_parameter_by_name (response, "id");
		if (param)
			*id = soup_soap_parameter_get_string_value (param);
	}

	g_object_unref (msg);
	g_object_unref (response);

	return status;
}

EGwConnectionStatus
e_gw_connection_create_item (EGwConnection *cnc, EGwItem *item, char** id)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* compose SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "createItemRequest");
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

	status = e_gw_connection_parse_response_status (response);
	if ( status == E_GW_CONNECTION_STATUS_OK) {
		param = soup_soap_response_get_first_parameter_by_name (response, "id");
		if (param != NULL) 
			*id = g_strdup (soup_soap_parameter_get_string_value (param));
	}
	g_object_unref (msg);
	g_object_unref (response);

	return status;
}

EGwConnectionStatus 
e_gw_connection_modify_item (EGwConnection *cnc, const char *id , EGwItem *item)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "modifyItemRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

	e_gw_message_write_string_parameter (msg, "id", NULL, id);

	if (!e_gw_item_append_changes_to_soap_message (item, msg)) {
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

	status = e_gw_connection_parse_response_status (response);
	g_object_unref (msg);
	g_object_unref (response);

	return status;
		
}

EGwConnectionStatus 
e_gw_connection_get_item (EGwConnection *cnc, const char *container, const char *id, EGwItem **item)
{

	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getItemRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
      

	e_gw_message_write_string_parameter (msg, "id", NULL, id);
	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	}

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "item");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
       	*item = e_gw_item_new_from_soap_parameter (container, param);
	
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
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

	status = e_gw_connection_parse_response_status (response);

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

const char *
e_gw_connection_get_uri (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->uri;
}

const char *
e_gw_connection_get_session_id (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->session_id;
}

const char *
e_gw_connection_get_user_name (EGwConnection *cnc)
{
	g_return_val_if_fail (cnc != NULL, NULL);
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->user_name;
}

const char* 
e_gw_connection_get_user_email (EGwConnection *cnc)
{
	g_return_val_if_fail (cnc != NULL, NULL);
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
  
	return (const char*) cnc->priv->user_email;
	
}

const char *
e_gw_connection_get_user_uuid (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->user_uuid;
}

static time_t
timet_from_string (const char *str)
{
	struct tm date;
        int len, i;
                                                              
        g_return_val_if_fail (str != NULL, -1);

	/* yyyymmdd[Thhmmss[Z]] */
        len = strlen (str);

        if (!(len == 8 || len == 15 || len == 16))
                return -1;

        for (i = 0; i < len; i++)
                if (!((i != 8 && i != 15 && isdigit (str[i]))
                      || (i == 8 && str[i] == 'T')
                      || (i == 15 && str[i] == 'Z')))
                        return -1;

#define digit_at(x,y) (x[y] - '0')

	date.tm_year = digit_at (str, 0) * 1000
                + digit_at (str, 1) * 100
                + digit_at (str, 2) * 10
                + digit_at (str, 3) -1900;
        date.tm_mon = digit_at (str, 4) * 10 + digit_at (str, 5) -1;
        date.tm_mday = digit_at (str, 6) * 10 + digit_at (str, 7);
        if (len > 8) {
                date.tm_hour = digit_at (str, 9) * 10 + digit_at (str, 10);
                date.tm_min  = digit_at (str, 11) * 10 + digit_at (str, 12);
                date.tm_sec  = digit_at (str, 13) * 10 + digit_at (str, 14);
        } else
		date.tm_hour = date.tm_min = date.tm_sec = 0; 

	return mktime (&date);
}

time_t
e_gw_connection_get_date_from_string (const char *dtstring)
{
        char *str2;
        int i, j, len = strlen (dtstring);
	time_t t;
	
        str2 = g_malloc0 (len);
        for (i = 0,j = 0; i < len; i++) {
                if ((dtstring[i] != '-') && (dtstring[i] != ':')) {
			str2[j] = dtstring[i];
			j++;
                }
        }

	str2[j] = '\0';
	t = timet_from_string (str2);
	g_free (str2);

        return t;
}

EGwConnectionStatus 
e_gw_connection_create_book (EGwConnection *cnc, char *book_name, char**id)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	char *value;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "createItemRequest");
	soup_soap_message_start_element (msg, "item", NULL, NULL);
	soup_soap_message_add_attribute (msg, "type", "AddressBook", "xsi", NULL);
	e_gw_message_write_string_parameter (msg, "name", NULL, book_name);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	}
	value = NULL;
	param = soup_soap_response_get_first_parameter_by_name (response, "id");
	if (param)
		value = soup_soap_parameter_get_string_value (param);
	if (value)
		*id = value;

	status = E_GW_CONNECTION_STATUS_OK;	
	return status;	
} 

EGwConnectionStatus
e_gw_connection_get_address_book_list (EGwConnection *cnc, GList **container_list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param;
	SoupSoapParameter *is_personal_param;
	char *value;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (container_list != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getAddressBookListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

       	e_gw_message_write_footer (msg);

        /* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        g_object_unref (msg);

	if (status != E_GW_CONNECTION_STATUS_OK) {
                g_object_unref (response);
                return status;
        }
	
	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "books");	
        if (!param) {
                g_object_unref (response);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        } else {
		SoupSoapParameter *subparam;
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "book");
		     subparam != NULL;
		     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "book")) {
			EGwContainer *container;
				       
			container = e_gw_container_new_from_soap_parameter (subparam);
			if (container) {
				*container_list = g_list_append (*container_list, container);
				is_personal_param = soup_soap_parameter_get_first_child_by_name (subparam, "isPersonal");
				value = NULL;
				if (is_personal_param)
					value = soup_soap_parameter_get_string_value (is_personal_param);
				if (value && g_str_equal(value , "1"))
					e_gw_container_set_is_writable (container, TRUE);
				else 
					e_gw_container_set_is_writable (container, FALSE);
				g_free (value);
					
			}
				     
		}
	}

	g_object_unref (response);

        return status;
}


EGwConnectionStatus 
e_gw_connection_get_address_book_id ( EGwConnection *cnc, char *book_name, char**id , gboolean *is_writable)
{
	EGwConnectionStatus status;
	GList *container_list = NULL, *l;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	g_return_val_if_fail (book_name != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	status = e_gw_connection_get_address_book_list (cnc, &container_list);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		e_gw_connection_free_container_list (container_list);
                return status;
        }

	/* search the container in the list */
	for (l = container_list; l != NULL; l = l->next) {
		EGwContainer *container = E_GW_CONTAINER (l->data);
		if (strcmp (e_gw_container_get_name (container), book_name) == 0) {
			
			*id = g_strdup (e_gw_container_get_id (container));
			*is_writable = e_gw_container_get_is_writable (container);
			break;
		}
	}

	e_gw_connection_free_container_list (container_list);

	return status;

}


EGwConnectionStatus 
e_gw_connection_get_categories (EGwConnection *cnc, GHashTable *categories_by_id, GHashTable *categories_by_name)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam, *second_level_child;
	const char *id, *name;
        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	
	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getCategoryListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
       	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	}

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "categories");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
	for (subparam = soup_soap_parameter_get_first_child_by_name (param, "category");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "category")) {
		id = name = NULL;
		second_level_child = soup_soap_parameter_get_first_child_by_name (subparam, "id");
		if (second_level_child)
			id = soup_soap_parameter_get_string_value (second_level_child);
		second_level_child = soup_soap_parameter_get_first_child_by_name (subparam, "name");
		if (second_level_child)
			name = soup_soap_parameter_get_string_value (second_level_child);
		if (id && name) {
			char **components = g_strsplit (id, "@", -1);
			g_free (id);
			id = components[0];
			if (categories_by_id) 
				g_hash_table_insert (categories_by_id, g_strdup (id), g_strdup (name));
			if (categories_by_name) 
				g_hash_table_insert (categories_by_name, g_strdup (name), g_strdup (id));
			g_strfreev (components);
			g_free (name);
		}
		
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;


}

EGwConnectionStatus 
e_gw_connection_add_members (EGwConnection *cnc, const char *group_id, GList *member_ids)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (member_ids != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (group_id != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);
	
	 msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "addMembersRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	e_gw_message_write_string_parameter (msg, "container", NULL, group_id);
	soup_soap_message_start_element (msg, "members", NULL, NULL);
	soup_soap_message_start_element (msg, "member", NULL, NULL);
	for (; member_ids != NULL; member_ids = member_ids = g_list_next (member_ids))
		e_gw_message_write_string_parameter (msg, "id", NULL, member_ids->data);
	soup_soap_message_end_element(msg);
	soup_soap_message_end_element(msg);
	e_gw_message_write_footer (msg);
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	g_object_unref (response);
	g_object_unref (msg);
	return status;
	

}

EGwConnectionStatus 
e_gw_connection_remove_members (EGwConnection *cnc, const char *group_id, GList *member_ids)
{
	
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (member_ids != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (group_id != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);
	
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "removeMembersRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	e_gw_message_write_string_parameter (msg, "container", NULL, group_id);
	soup_soap_message_start_element (msg, "members", NULL, NULL);
	soup_soap_message_start_element (msg, "member", NULL, NULL);
	for (; member_ids != NULL; member_ids = member_ids = g_list_next (member_ids))
		e_gw_message_write_string_parameter (msg, "id", NULL, member_ids->data);
	soup_soap_message_end_element(msg);
	soup_soap_message_end_element(msg);
	e_gw_message_write_footer (msg);
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	g_object_unref (response);
	g_object_unref (msg);
	return status;



}
