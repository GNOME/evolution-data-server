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
#include <glib/gi18n-lib.h>
#include <libsoup/soup-session-sync.h>
#include <libsoup/soup-soap-message.h>
#include <libsoup/soup-misc.h>
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
	char *version;
	char *server_time ;
	GHashTable *categories_by_name;
	GHashTable *categories_by_id;
	GList *book_list;
	EGwSendOptions *opts;
	GMutex *reauth_mutex;
};

static EGwConnectionStatus 
reauthenticate (EGwConnection *cnc) 
{
	EGwConnectionPrivate  *priv;
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	EGwConnectionStatus status = -1;
	char *session = NULL;
	
	priv = cnc->priv;
	g_mutex_lock (priv->reauth_mutex);
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getCategoryListRequest");
	e_gw_message_write_footer (msg);

        /* just to make sure we still have invlaid session 
	   when multiple e_gw_connection apis see inavlid connection error 
	   at the sma time this prevents this function sending login requests multiple times */
	response = e_gw_connection_send_message (cnc, msg); 
        if (!response) {
                g_object_unref (msg);
		g_mutex_unlock (priv->reauth_mutex);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
        status = e_gw_connection_parse_response_status (response);
	g_object_unref (response);

	if (status == E_GW_CONNECTION_STATUS_OK) {
		g_mutex_unlock (priv->reauth_mutex);
		return status;
	}
	/* build the SOAP message */
	msg = e_gw_message_new_with_header (priv->uri, NULL, "loginRequest");
	soup_soap_message_start_element (msg, "auth", "types", NULL);
	soup_soap_message_add_attribute (msg, "type", "types:PlainText", "xsi",
					 "http://www.w3.org/2001/XMLSchema-instance");
	e_gw_message_write_string_parameter (msg, "username", "types", priv->username);
	e_gw_message_write_string_parameter (msg, "password", "types", priv->password);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (response) 
		status = e_gw_connection_parse_response_status (response);

	if (status == E_GW_CONNECTION_STATUS_OK) {
		param = soup_soap_response_get_first_parameter_by_name (response, "session");
		if (param) 
			session = soup_soap_parameter_get_string_value (param);
			
	}
		
	if (session) {
		g_free (priv->session_id);
		priv->session_id = session;
	} 
	g_object_unref (msg);
	if (response)
		g_object_unref (response);
	g_mutex_unlock (priv->reauth_mutex);
	return status;
	
}

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
	case 53505 : return E_GW_CONNECTION_STATUS_UNKNOWN_USER;
	case 59914: return E_GW_CONNECTION_STATUS_ITEM_ALREADY_ACCEPTED;
	case 59910: return E_GW_CONNECTION_STATUS_INVALID_CONNECTION;
	case 59923: return E_GW_CONNECTION_STATUS_REDIRECT;
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
	case E_GW_CONNECTION_STATUS_NO_RESPONSE:
		return _("No response from the server");
	case E_GW_CONNECTION_STATUS_OBJECT_NOT_FOUND :
		return _("Object not found");
	case E_GW_CONNECTION_STATUS_UNKNOWN_USER :
		return _("Unknown User");
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
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
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
	char *hash_key;
	gpointer orig_key, orig_value;
	
	g_return_if_fail (E_IS_GW_CONNECTION (cnc));
	
	priv = cnc->priv;
	printf ("gw connection dispose \n");
	
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

	
		if (priv->reauth_mutex) {
			g_mutex_free (priv->reauth_mutex);
			priv->reauth_mutex = NULL;
		}
	
		if (priv->categories_by_id) {
			g_hash_table_destroy (priv->categories_by_id);
			priv->categories_by_id = NULL;
		}	
		
		if (priv->categories_by_name) {
			g_hash_table_destroy (priv->categories_by_name);
			priv->categories_by_name = NULL;
		}
	
		if (priv->book_list) {
			g_list_foreach (priv->book_list, (GFunc) g_object_unref, NULL);
			g_list_free (priv->book_list);
			priv->book_list = NULL;
		}
		
		if (priv->opts) {
			g_object_unref (priv->opts);
			priv->opts = NULL;
		}
		
		if (priv->version) {
			g_free (priv->version) ;
			priv->opts = NULL ;
		}

		if (priv->server_time) {
			g_free (priv->server_time) ;
			priv->server_time = NULL ;
		}
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
	printf ("gw connection finalize\n");
	/* clean up */
	g_free (priv);
	cnc->priv = NULL;

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
	priv->reauth_mutex = g_mutex_new ();
	priv->categories_by_id = NULL;
	priv->categories_by_name = NULL;
	priv->book_list = NULL;
	priv->opts = NULL;
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

static SoupSoapMessage*
form_login_request (const char*uri, const char* username, const char* password)
{
	SoupSoapMessage *msg;
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
	return msg;
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
	char *redirected_uri = NULL;
	
	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;	
	
	g_static_mutex_lock (&connecting);

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
			g_static_mutex_unlock (&connecting);
			return cnc;
		}
	}

	
	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_GW_CONNECTION, NULL);

	msg = form_login_request (uri, username, password);
	
	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);

	if (!response) {
		g_object_unref (cnc);
		g_static_mutex_unlock (&connecting);
		g_object_unref (msg);
		return NULL;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_REDIRECT) {
		char *host, *port;
		char **tokens;
		SoupSoapParameter *subparam;

		param = soup_soap_response_get_first_parameter_by_name (response, "redirectToHost");
		subparam = soup_soap_parameter_get_first_child_by_name (param, "ipAddress");
		host = soup_soap_parameter_get_string_value (subparam);
		subparam = soup_soap_parameter_get_first_child_by_name (param, "port");
		port = soup_soap_parameter_get_string_value (subparam);
		if (host && port) {
			tokens = g_strsplit (uri, "://", 2);
			redirected_uri = g_strconcat (tokens[0], "://", host, ":", port, "/soap", NULL);
			g_object_unref (msg);
			g_object_unref (response);
			msg = form_login_request (redirected_uri, username, password);
			uri = redirected_uri;
			response = e_gw_connection_send_message (cnc, msg);
			status = e_gw_connection_parse_response_status (response);
			g_strfreev (tokens);
			g_free (host);
			g_free (port);
		}
		       
	}
	param = soup_soap_response_get_first_parameter_by_name (response, "session");
	if (!param) {
		g_object_unref (response);
		g_object_unref (msg);
		g_object_unref (cnc);
		g_static_mutex_unlock (&connecting);
		return NULL;
	}
	
	cnc->priv->uri = g_strdup (uri);
	cnc->priv->username = g_strdup (username);
	cnc->priv->password = g_strdup (password);
	cnc->priv->session_id = soup_soap_parameter_get_string_value (param);

	/* retrieve user information */
	param = soup_soap_response_get_first_parameter_by_name (response, "userinfo");
	
	if (param) {
		SoupSoapParameter *subparam;
		char *param_value;

		subparam = soup_soap_parameter_get_first_child_by_name (param, "email");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_email  = param_value;
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param, "name");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_name = param_value;
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param, "uuid");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_uuid = param_value;
		}
	}

	param = soup_soap_response_get_first_parameter_by_name (response, "gwVersion");
	if (param) {
		char *param_value;
		param_value = soup_soap_parameter_get_string_value (param);
		cnc->priv->version = param_value;
	} else
	       	cnc->priv->version = NULL;	

	param = soup_soap_response_get_first_parameter_by_name (response, "serverUTCTime");
	if (param) 
		cnc->priv->server_time = soup_soap_parameter_get_string_value (param);

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
	g_object_unref (msg);
	g_static_mutex_unlock (&connecting);
	g_free (redirected_uri);
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
e_gw_connection_get_container_list (EGwConnection *cnc, const char *top, GList **container_list)
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

	e_gw_message_write_string_parameter (msg, "parent", NULL, top);
	e_gw_message_write_string_parameter (msg, "recurse", NULL, "true");
	e_gw_message_write_footer (msg);

        /* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        g_object_unref (msg);

	if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                return status;
        }

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "folders");	
        if (param) {
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

        status = e_gw_connection_get_container_list (cnc, "folders", &container_list);
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
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
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

		item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, container, subparam);
		if (item)
			*list = g_list_append (*list, item);
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus
e_gw_connection_get_items_from_ids (EGwConnection *cnc, const char *container, const char *view, GPtrArray *item_ids, GList **list)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;
	int i;
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
      	soup_soap_message_start_element (msg, "items", NULL, NULL);
	for (i = 0; i < item_ids->len; i ++) {
		char *id = g_ptr_array_index (item_ids, i);
		e_gw_message_write_string_parameter (msg, "item", NULL, id);
	}
	soup_soap_message_end_element (msg);

	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
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

		item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, container, subparam);
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
		 // g_object_unref (cnc); 
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
		 // g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_NO_RESPONSE; 
         } 

         status = e_gw_connection_parse_response_status (response); 
         if (status != E_GW_CONNECTION_STATUS_OK) { 
 		g_object_unref (response); 
		g_object_unref (msg); 
		//	g_object_unref (cnc); 
 		return status; 
 	} 

 	/* if status is OK - parse result. return the list */	 
 	param = soup_soap_response_get_first_parameter_by_name (response, "changed"); 
         if (!param) { 
                 g_object_unref (response); 
                 g_object_unref (msg); 
		 // g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; 
         } 
	
         if (!g_ascii_strcasecmp ( soup_soap_parameter_get_string_value (param), "0")) { 
                 g_message ("No deltas"); 
		 // g_object_unref (cnc); 
                 return E_GW_CONNECTION_STATUS_OK; 
         }                 

         param = soup_soap_response_get_first_parameter_by_name (response, "deltas"); 
         if (!param) { 
                 g_object_unref (response); 
                 g_object_unref (msg); 
		 // g_object_unref (cnc); 
//                 return E_GW_CONNECTION_STATUS_INVALID_RESPONSE; 
		/* getting around the server behavior that deltas can be null
		 * though changes is true */
		 return E_GW_CONNECTION_STATUS_OK;
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
					 // g_object_unref (cnc); 
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
				item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, "Calendar", subparam);
                                if (!item) { 
                                         g_object_unref (response); 
                                         g_object_unref (msg); 
					 // g_object_unref (cnc); 
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
			 item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, "Calendar", subparam);
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
e_gw_connection_send_item (EGwConnection *cnc, EGwItem *item, GSList **id_list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	if (id_list)
		*id_list = NULL;

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
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_OK && id_list != NULL) {
		SoupSoapParameter *param;

		/* get the generated ID from the SOAP response */
		// for loop here to populate the list_ids.
		for (param = soup_soap_response_get_first_parameter_by_name (response, "id");
			param; param = soup_soap_response_get_next_parameter_by_name (response, param, "id")) {
			
			*id_list = g_slist_append (*id_list, soup_soap_parameter_get_string_value (param));
		}
	}
	else if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
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
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_OK) {
		param = soup_soap_response_get_first_parameter_by_name (response, "id");
		if (param != NULL) 
			*id = g_strdup (soup_soap_parameter_get_string_value (param));
	} else if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	
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
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (msg);
	g_object_unref (response);

	return status;
		
}

EGwConnectionStatus 
e_gw_connection_get_item (EGwConnection *cnc, const char *container, const char *id, const char *view, EGwItem **item)
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

	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view) ;
	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
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
	
       	*item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, container, param);
	
               
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
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_remove_items (EGwConnection *cnc, const char *container, GList *item_ids)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "removeItemsRequest");
	if (container && *container)
		e_gw_message_write_string_parameter (msg, "container", NULL, container);

	soup_soap_message_start_element (msg, "items", NULL, NULL);
	for (; item_ids != NULL; item_ids = g_list_next (item_ids))
		e_gw_message_write_string_parameter (msg, "item", NULL, item_ids->data);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_accept_request (EGwConnection *cnc, const char *id, const char *accept_level)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "acceptRequest");
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "item", NULL, id);
	soup_soap_message_end_element (msg);
	e_gw_message_write_string_parameter (msg, "acceptLevel", NULL, accept_level);
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (response);
	g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_decline_request (EGwConnection *cnc, const char *id)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "declineRequest");
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "item", NULL, id);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (response);
	g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_retract_request (EGwConnection *cnc, const char *id, const char *comment, gboolean retract_all, gboolean resend)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "retractRequest");
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "item", NULL, id);
	soup_soap_message_end_element (msg);
	/* comment, FALSE, FALSE to be filled in later. */
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (response);
	g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_complete_request (EGwConnection *cnc, const char *id)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "completeRequest");
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "item", NULL, id);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (response);
	g_object_unref (msg);
	return status;
}

const char *
e_gw_connection_get_version (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);

	return (const char *) cnc->priv->version;
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

const char * 
e_gw_connection_get_server_time (EGwConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL) ;

	return (const char *) cnc->priv->server_time ;
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

char *
e_gw_connection_format_date_string (const char *dtstring)
{
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
        return str2;
}

time_t
e_gw_connection_get_date_from_string (const char *dtstring)
{
        char *str2;
        int i, j, len = strlen (dtstring);
	time_t t;
	
        str2 = g_malloc0 (len+1);
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
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
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
	EGwConnectionPrivate *priv;
	SoupSoapParameter *param;
	SoupSoapParameter *type_param;
	char *value;
	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (container_list != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);

	priv = cnc->priv;
	g_static_mutex_lock (&connecting);

	if (priv->book_list) {
		*container_list = priv->book_list;
		g_static_mutex_unlock (&connecting);
		return E_GW_CONNECTION_STATUS_OK;
	}

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getAddressBookListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

       	e_gw_message_write_footer (msg);

        /* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        g_object_unref (msg);

	if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
                g_object_unref (response);
		g_static_mutex_unlock (&connecting);
                return status;
        }
	
	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "books");	
        if (!param) {
                g_object_unref (response);
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        } else {
		SoupSoapParameter *subparam;
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "book");
		     subparam != NULL;
		     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "book")) {
			EGwContainer *container;
				       
			container = e_gw_container_new_from_soap_parameter (subparam);
			if (container) {
				priv->book_list = g_list_append (priv->book_list, container);
				type_param = soup_soap_parameter_get_first_child_by_name (subparam, "isPersonal");
				value = NULL;
				if (type_param)
					value = soup_soap_parameter_get_string_value (type_param);
				if (value && g_str_equal(value , "1"))
					e_gw_container_set_is_writable (container, TRUE);
				else 
					e_gw_container_set_is_writable (container, FALSE);
				g_free (value);
				value = NULL;
			        type_param = soup_soap_parameter_get_first_child_by_name (subparam, "isFrequentContacts");
				if (type_param)
                                        value = soup_soap_parameter_get_string_value (type_param);
                                if (value && g_str_equal(value , "1"))
                                        e_gw_container_set_is_frequent_contacts (container, TRUE);
                                                                        
				g_free (value);
					
			}
				     
		}
	}

	g_object_unref (response);
	*container_list = priv->book_list;
	g_static_mutex_unlock (&connecting);

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

	return status;

}

EGwConnectionStatus
e_gw_connection_modify_settings (EGwConnection *cnc, EGwSendOptions *opts)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;
	EGwConnectionPrivate *priv;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	g_return_val_if_fail (opts != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	priv = cnc->priv;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "modifySettingsRequest");
	if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	
	if (!e_gw_sendoptions_form_message_to_modify (msg, opts, priv->opts)) {
		g_warning (G_STRLOC ": Could not append changes to SOAP message");
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_OBJECT;
	}

       	e_gw_message_write_footer (msg);
	
        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

	status = e_gw_connection_parse_response_status (response);
       	if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	} else {
		g_object_unref (priv->opts);
		priv->opts = NULL;
		priv->opts = opts;
	}

	g_object_unref (response);
        g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_get_settings (EGwConnection *cnc, EGwSendOptions **opts)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;
	SoupSoapParameter *param;
	EGwConnectionPrivate *priv;
	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;	


	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	priv = cnc->priv;

	g_static_mutex_lock (&connecting);

	if (priv->opts) {
		g_object_ref (priv->opts);
		*opts = priv->opts;
		g_static_mutex_unlock (&connecting);
		
		return E_GW_CONNECTION_STATUS_OK;
	}

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getSettingsRequest");
	if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
       	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
		return status;
	}
	
	param = soup_soap_response_get_first_parameter_by_name (response, "settings");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        } else 
		priv->opts = e_gw_sendoptions_new_from_soap_parameter (param);

	g_object_ref (priv->opts);
	*opts = priv->opts;
	g_object_unref (response);
        g_object_unref (msg);
	g_static_mutex_unlock (&connecting);
	
	return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus 
e_gw_connection_get_categories (EGwConnection *cnc, GHashTable **categories_by_id, GHashTable **categories_by_name)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
	EGwConnectionPrivate *priv;
        SoupSoapParameter *param, *subparam, *second_level_child;
	char *id, *name;
        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;

	priv = cnc->priv;
	g_static_mutex_lock (&connecting);

	if (priv->categories_by_id && priv->categories_by_name) {
		*categories_by_id = priv->categories_by_id;
		*categories_by_name = priv->categories_by_name;
		g_static_mutex_unlock (&connecting);
		return E_GW_CONNECTION_STATUS_OK;
	}

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getCategoryListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
       	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
		return status;
	}

	/* if status is OK - parse result. return the list */	
	param = soup_soap_response_get_first_parameter_by_name (response, "categories");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
		g_static_mutex_unlock (&connecting);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	
	priv->categories_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->categories_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

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
				g_hash_table_insert (priv->categories_by_id, g_strdup (id), g_strdup (name));
			if (categories_by_name) 
				g_hash_table_insert (priv->categories_by_name, g_strdup (name), g_strdup (id));
			g_strfreev (components);
			g_free (name);
		}
		
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);
	*categories_by_id = priv->categories_by_id;
	*categories_by_name = priv->categories_by_name;
	g_static_mutex_unlock (&connecting);

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

	for (; member_ids != NULL; member_ids = member_ids = g_list_next (member_ids)) {
		soup_soap_message_start_element (msg, "member", NULL, NULL);
		e_gw_message_write_string_parameter (msg, "id", NULL, member_ids->data);
		soup_soap_message_end_element(msg);
	}
	
	soup_soap_message_end_element(msg);
	e_gw_message_write_footer (msg);
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
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

	for (; member_ids != NULL; member_ids = member_ids = g_list_next (member_ids)) {
		soup_soap_message_start_element (msg, "member", NULL, NULL);
		e_gw_message_write_string_parameter (msg, "id", NULL, member_ids->data);
		soup_soap_message_end_element(msg);
	}

	soup_soap_message_end_element(msg);
	e_gw_message_write_footer (msg);
	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (response);
	g_object_unref (msg);
	return status;



}


EGwConnectionStatus
e_gw_connection_create_cursor (EGwConnection *cnc, const char *container, const char *view, EGwFilter *filter, int *cursor)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param;
	char *value;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail ((container != NULL), E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "createCursorRequest");
	e_gw_message_write_string_parameter (msg, "container", NULL, container);
	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view);
	if (E_IS_GW_FILTER(filter))
		e_gw_filter_append_to_soap_message (filter, msg);
	
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
	
	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	}
	param = soup_soap_response_get_first_parameter_by_name (response, "cursor");
	if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }
	value = soup_soap_parameter_get_string_value(param);
	
	if (!value) {
		 g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

	*cursor =(int) g_ascii_strtod (value, NULL);
	
	g_free (value);
	g_object_unref (response);
	g_object_unref (msg);
        return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus
e_gw_connection_destroy_cursor (EGwConnection *cnc, const char *container,  int cursor)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail ((container != NULL), E_GW_CONNECTION_STATUS_UNKNOWN);
	
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "destroyCursorRequest");
	e_gw_message_write_string_parameter (msg, "container", NULL, container);

	e_gw_message_write_int_parameter (msg, "cursor", NULL, cursor);
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
	
	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (response);
	g_object_unref (msg);
	return status;
     
}



EGwConnectionStatus
e_gw_connection_position_cursor (EGwConnection *cnc, const char *container, int cursor, const char *seek, int offset)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail ((container != NULL), E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "positionCursorRequest");
	e_gw_message_write_int_parameter (msg, "cursor", NULL, cursor);
	e_gw_message_write_string_parameter (msg, "container", NULL, container);
	e_gw_message_write_string_parameter (msg, "seek", NULL, seek);
	e_gw_message_write_int_parameter (msg, "offset", NULL, offset);
	
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
	
	status = e_gw_connection_parse_response_status (response);
	g_object_unref (response);
        g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_read_cursor (EGwConnection *cnc, const char *container, int cursor, gboolean forward, int count, const char *cursor_seek, GList **item_list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param, *subparam;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail ((container != NULL), E_GW_CONNECTION_STATUS_UNKNOWN);
	
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "readCursorRequest");
	e_gw_message_write_int_parameter (msg, "cursor", NULL, cursor);
	/* there is problem in read curosr if you set this, uncomment after the problem 
	   is fixed in server */
	e_gw_message_write_string_parameter (msg, "forward", NULL, forward ? "true": "false");
	e_gw_message_write_string_parameter (msg, "position", NULL, cursor_seek);
	e_gw_message_write_string_parameter (msg, "container", NULL, container);
	e_gw_message_write_int_parameter (msg, "count", NULL, count);
	
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
	
	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
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

	for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {
		EGwItem *item;

		item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, container, subparam);
		if (item)
			*item_list = g_list_append (*item_list, item);
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);
        return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus e_gw_connection_get_quick_messages (EGwConnection *cnc, const char *container, const char *view, char **start_date, const char *message_list, const char *item_types, const char *item_sources, int count, GSList **item_list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param, *subparam;
	
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail (message_list != NULL, E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getQuickMessagesRequest");
	e_gw_message_write_string_parameter (msg, "list", NULL, message_list);
	if (start_date)
		e_gw_message_write_string_parameter (msg, "startDate", NULL, *start_date);
	if (container)
		e_gw_message_write_string_parameter (msg, "container", NULL, container);
	if (item_types) 
		e_gw_message_write_string_parameter (msg, "types", NULL, item_types);
	if (item_sources)
		e_gw_message_write_string_parameter (msg, "source", NULL, item_sources);
	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view);
	if (count > 0)
		e_gw_message_write_int_parameter (msg, "count", NULL, count);
	
	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
	
	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	}

	/* if status is OK - parse result. return the list */	
	*item_list = NULL;
	param = soup_soap_response_get_first_parameter_by_name (response, "items");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

	if (start_date && *start_date) {
		subparam = soup_soap_response_get_first_parameter_by_name (response, "startDate");
		if (subparam) {
			char *date;

			date = soup_soap_parameter_get_string_value (subparam);
			if (date)
				g_free (*start_date), *start_date = date;
			else 
				return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		} else 
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}
	
	if (!strcmp (message_list, "All")) { 
		/* We are  interested only in getting the ids */
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
	     	     subparam != NULL;
	             subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {
			SoupSoapParameter *param_id;
		     	char *id;
			
			param_id = soup_soap_parameter_get_first_child_by_name (subparam, "iCalId");
			if (!param_id) {
				g_object_unref (response);
		                g_object_unref (msg);
                		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
			}
		     
			id = g_strdup (soup_soap_parameter_get_string_value (param_id));
			if (id)
				*item_list = g_slist_append (*item_list, id);
		}
		
		g_object_unref (response);
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_OK;

	}

	for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {
		EGwItem *item;

		item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, container, subparam);
		if (item)
			*item_list = g_slist_append (*item_list, item);
        }
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);
        return E_GW_CONNECTION_STATUS_OK;
	

}


EGwConnectionStatus 
e_gw_connection_create_folder(EGwConnection *cnc, const char *parent_name,const char *folder_name, char **container_id)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param ;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc), e_gw_connection_get_session_id(cnc), "createItemRequest");

	soup_soap_message_start_element (msg, "item", NULL, NULL);
	soup_soap_message_add_attribute (msg, "type", "Folder", "xsi", NULL);

	e_gw_message_write_string_parameter (msg, "name", NULL, folder_name);
	e_gw_message_write_string_parameter (msg, "parent", NULL,parent_name );

	soup_soap_message_end_element (msg);

	e_gw_message_write_footer (msg);

	response =  e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	} else {
		param = soup_soap_response_get_first_parameter_by_name (response, "id") ;
		*container_id = soup_soap_parameter_get_string_value(param) ;
		printf ("CONTAINER ID %s \n", *container_id) ;
	}

	return status ;

}

/*
 * 
 */
EGwConnectionStatus
e_gw_connection_get_attachment (EGwConnection *cnc, const char *id, int offset, int length, const char **attachment, int *attach_length)
{

	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param ;
	char *buffer, *buf_length ;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getAttachmentRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
      

	e_gw_message_write_string_parameter (msg, "id", NULL, id);
	e_gw_message_write_int_parameter (msg, "offset", NULL, offset);
	e_gw_message_write_int_parameter (msg, "length", NULL, length);

	e_gw_message_write_footer (msg);

        /* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }

        status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
		g_object_unref (response);
                g_object_unref (msg);
		return status;
	}

	param = soup_soap_response_get_first_parameter_by_name (response, "part") ;
	buf_length =  soup_soap_parameter_get_property (param, "length") ;
	buffer = soup_soap_parameter_get_string_value (param) ;
        
	if (buffer && length) {
		int len = atoi (buf_length) ;
		*attachment = soup_base64_decode (buffer,&len) ;
		*attach_length = len ;
	}

	/* free memory */
	g_free (buffer) ;
	g_free (buf_length) ;
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
}


EGwConnectionStatus
e_gw_connection_add_item (EGwConnection *cnc, const char *container, const char *id)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "addItemRequest");

	if (container && *container)
		e_gw_message_write_string_parameter (msg, "container", NULL, container);
	e_gw_message_write_string_parameter (msg, "id", NULL, id);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_add_items (EGwConnection *cnc, const char *container, GList *item_ids)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (item_ids != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "addItemsRequest");

	if (container && *container)
		e_gw_message_write_string_parameter (msg, "container", NULL, container);
	
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	for (; item_ids != NULL; item_ids = g_list_next (item_ids))
		e_gw_message_write_string_parameter (msg, "item", NULL, item_ids->data);
	soup_soap_message_end_element (msg);

	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus 
e_gw_connection_rename_folder (EGwConnection *cnc, const char *id ,const char *new_name)
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

	soup_soap_message_start_element (msg, "updates", NULL, NULL);
	soup_soap_message_start_element (msg, "update", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "name", NULL, new_name);
	soup_soap_message_end_element (msg) ;
	soup_soap_message_end_element (msg) ;
	
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	g_object_unref (msg);
	g_object_unref (response);

	return status;
		
}

EGwConnectionStatus 
e_gw_connection_share_folder(EGwConnection *cnc, gchar *id, GList *new_list, const char *sub, const char *mesg ,int flag) 
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc), e_gw_connection_get_session_id (cnc), "modifyItemRequest");
	e_gw_container_form_message (msg, id, new_list, sub, mesg, flag);
	e_gw_message_write_footer (msg);
	response =  e_gw_connection_send_message (cnc, msg);
	
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_move_item (EGwConnection *cnc, const char *id, const char *dest_container_id, const char *from_container_id)

{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	g_return_val_if_fail (dest_container_id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	
	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "moveItemRequest");
	e_gw_message_write_string_parameter (msg, "id", NULL, id);
	e_gw_message_write_string_parameter (msg, "container", NULL,dest_container_id);
	if (from_container_id)
		e_gw_message_write_string_parameter (msg, "from", NULL,from_container_id);	
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);
	return status;


}

EGwConnectionStatus
e_gw_connection_accept_shared_folder (EGwConnection *cnc, gchar *name, gchar *container_id, gchar *item_id, gchar *desc)
{

	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (container_id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	g_return_val_if_fail (item_id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	g_return_val_if_fail (name != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "acceptShareRequest");
	e_gw_message_write_string_parameter (msg, "id", NULL, item_id);
	e_gw_message_write_string_parameter (msg, "name", NULL, name);
	e_gw_message_write_string_parameter (msg, "container", NULL, container_id);
	e_gw_message_write_string_parameter (msg, "description", NULL, desc);
	e_gw_message_write_footer (msg);
	response =  e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}
	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;

}

EGwConnectionStatus 
e_gw_connection_purge_deleted_items (EGwConnection *cnc)
{

	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;
	
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "purgeDeletedItemsRequest");
	e_gw_message_write_footer (msg);
	response =  e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}
	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;

}

EGwConnectionStatus
e_gw_connection_mark_read(EGwConnection *cnc, GList *item_ids)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "markReadRequest");

	soup_soap_message_start_element (msg, "items", NULL, NULL);
	for (; item_ids != NULL; item_ids = g_list_next (item_ids))
		e_gw_message_write_string_parameter (msg, "item", NULL, item_ids->data);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_mark_unread(EGwConnection *cnc, GList *item_ids)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "markUnReadRequest");

	soup_soap_message_start_element (msg, "items", NULL, NULL);
	for (; item_ids != NULL; item_ids = g_list_next (item_ids))
		e_gw_message_write_string_parameter (msg, "item", NULL, item_ids->data);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);

	/* send message to server */
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_reply_item (EGwConnection *cnc, const char *id, const char *view, EGwItem **item)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;
        
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	
	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "replyRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	
	e_gw_message_write_string_parameter (msg, "id", NULL, id);

	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view) ;
	e_gw_message_write_footer (msg);
        
	/* send message to server */
        response = e_gw_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_NO_RESPONSE;
        }
        
	status = e_gw_connection_parse_response_status (response);
        if (status != E_GW_CONNECTION_STATUS_OK) {
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			reauthenticate (cnc);
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
       	
	*item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, "", param);
	
               
	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
}
