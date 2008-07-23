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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <ctype.h>
#include <glib/gi18n-lib.h>
#include <libedataserver/e-proxy.h>
#include <libsoup/soup.h>
#include "soup-soap-message.h"
#include "e-gw-connection.h"
#include "e-gw-message.h"
#include "e-gw-filter.h"
#include "build-timestamp.h"

/* For soup sync session timeout */
#define GW_SOUP_SESSION_TIMEOUT 30

/* Soup session proxy-uri property */
#define SOUP_SESSION_PROXY_URI "proxy-uri"

static GObjectClass *parent_class = NULL;
static GHashTable *loaded_connections_permissions = NULL;

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
	GMutex *msg_lock;
	EProxy *proxy;
};

static void
update_soup_session_proxy_settings (EProxy *proxy, SoupSession* session, 
				    const char* uri)
{
	SoupURI *proxy_uri = NULL;
	
	if (!session || !uri || !proxy)
		return;
	
	if (e_proxy_require_proxy_for_uri (proxy, uri))
		proxy_uri = e_proxy_peek_uri (proxy);

	g_object_set (session, SOUP_SESSION_PROXY_URI,
		      proxy_uri, NULL);	
}

static void
proxy_settings_changed (EProxy *proxy, gpointer user_data)
{
	EGwConnection* conn = (EGwConnection *)user_data;
	if (!conn || !conn->priv || !conn->priv->soup_session)
		return;

	update_soup_session_proxy_settings (proxy, 
					    conn->priv->soup_session,
					    conn->priv->uri);
}

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
	if (!priv)
		return E_GW_CONNECTION_STATUS_INVALID_CONNECTION;

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

static gboolean 
e_gw_connection_response_parse_status_and_description (SoupSoapResponse *response, int *status, char **description)
{
	SoupSoapParameter *param, *subparam;

	param = soup_soap_response_get_first_parameter_by_name (response, "status");
	if (!param)
		return FALSE;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "code");
	if (!subparam)
		return FALSE;

	*status = soup_soap_parameter_get_int_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "description");
	if (!subparam)
		return FALSE;
	
	*description =  soup_soap_parameter_get_string_value (subparam);

	return TRUE;
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
	case 53530: return E_GW_CONNECTION_STATUS_OTHER;
	/* FIXME: 58652 should be changed with an enum.*/
	case 58652: return 58652;
	case 59922: return 59922; /*Very big attachment, get in chunks*/
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

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;
	printf ("gw connection dispose \n");

	/* removed the connection from the hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = g_strdup_printf ("%s:%s@%s",
					    priv->username ? priv->username : "",
					    priv->password ? priv->password : "",
					    priv->uri ? priv->uri : "");
		g_hash_table_remove (loaded_connections_permissions, hash_key);
		if (g_hash_table_size (loaded_connections_permissions) == 0) {
			g_hash_table_destroy (loaded_connections_permissions);
			loaded_connections_permissions = NULL;
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

		if (priv->user_uuid) {
			g_free (priv->user_uuid);
			priv->user_uuid = NULL;
		}

		if (priv->reauth_mutex) {
			g_mutex_free (priv->reauth_mutex);
			priv->reauth_mutex = NULL;
		}

		if (priv->msg_lock) {
			g_mutex_free (priv->msg_lock);
			priv->msg_lock = NULL;
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

		if (priv->proxy) {
			g_object_unref (priv->proxy);
			priv->proxy = NULL;
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
	guint timeout = GW_SOUP_SESSION_TIMEOUT;

	/* allocate internal structure */
	priv = g_new0 (EGwConnectionPrivate, 1);
	cnc->priv = priv;

	/* Set a default timeout value of 30 seconds.
	   FIXME: Make timeout configurable
	*/
	if (g_getenv ("SOUP_SESSION_TIMEOUT"))
		timeout = atoi (g_getenv ("SOUP_SESSION_TIMEOUT"));

        /* Initialize proxy settings */
        priv->proxy = e_proxy_new ();
        e_proxy_setup_proxy (priv->proxy);
        g_signal_connect (priv->proxy, "changed", G_CALLBACK (proxy_settings_changed), cnc);

	/* create the SoupSession for this connection */
	priv->soup_session = soup_session_sync_new_with_options (SOUP_SESSION_TIMEOUT, timeout, NULL);
	priv->reauth_mutex = g_mutex_new ();
	priv->msg_lock = g_mutex_new ();
	priv->categories_by_id = NULL;
	priv->categories_by_name = NULL;
	priv->book_list = NULL;
	priv->opts = NULL;

	/* README: We do not use libsoup logger and use our own as we need formatted output etc. */
	/*
	   if (g_getenv ("GROUPWISE_DEBUG")) {
		if (atoi (g_getenv ("GROUPWISE_DEBUG")) == 1) {
			SoupLogger *logger;

			logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
			soup_logger_attach (logger, priv->soup_session);
			g_object_unref (logger);
		}
	}
	*/
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
	e_gw_message_write_string_parameter (msg, "application", "types", build_timestamp);
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
e_gw_connection_new_with_error_handler (const char *uri, const char *username, const char *password, EGwConnectionErrors *errors)
{
	EGwConnection *cnc;
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	EGwConnectionStatus status;
	char *hash_key;
	char *redirected_uri = NULL;
	int code;
	char *description = NULL;

	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;	


	g_static_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = g_strdup_printf ("%s:%s@%s",
				username ? username : "",
				password ? password : "",
				uri);
		cnc = g_hash_table_lookup (loaded_connections_permissions, hash_key);
		g_free (hash_key);

		if (E_IS_GW_CONNECTION (cnc)) {
			g_object_ref (cnc);
			g_static_mutex_unlock (&connecting);
			return cnc;
		}
	}


	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_GW_CONNECTION, NULL);

	/* Set proxy details for the Soup session before any 
	   communication. */
	update_soup_session_proxy_settings (cnc->priv->proxy, 
					    cnc->priv->soup_session,
					    uri);

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
		}

		g_free (host);
		g_free (port);
	}
	param = soup_soap_response_get_first_parameter_by_name (response, "session");
	if (!param) {
		if (errors && e_gw_connection_response_parse_status_and_description (response, &code, &description) ) {
			errors->status = code;
			errors->description = description;
		}
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

	/* add the connection to the loaded_connections_permissions hash table */
	hash_key = g_strdup_printf ("%s:%s@%s",
			cnc->priv->username ? cnc->priv->username : "",
			cnc->priv->password ? cnc->priv->password : "",
			cnc->priv->uri);
	if (loaded_connections_permissions == NULL)
		loaded_connections_permissions = g_hash_table_new_full (g_str_hash, g_str_equal,
				g_free, NULL);
	g_hash_table_insert (loaded_connections_permissions, hash_key, cnc);

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);
	g_static_mutex_unlock (&connecting);
	g_free (redirected_uri);
	return cnc;

}


EGwConnection *
e_gw_connection_new (const char *uri, const char *username, const char *password)
{
	/* This is where I miss function-overloading and default-parameters */

	return e_gw_connection_new_with_error_handler (uri, username, password, NULL);
}

SoupSoapResponse *
e_gw_connection_send_message (EGwConnection *cnc, SoupSoapMessage *msg)
{
	SoupSoapResponse *response;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), NULL);

	g_mutex_lock (cnc->priv->msg_lock);
	soup_session_send_message (cnc->priv->soup_session, SOUP_MESSAGE (msg));
	g_mutex_unlock (cnc->priv->msg_lock);

	/* process response */
	response = soup_soap_message_parse_response (msg);
	
	if (response && g_getenv ("GROUPWISE_DEBUG")) {

		/* README: The stdout can be replaced with Evolution's
		Logging framework also */

		soup_soap_response_dump_response (response, stdout);
		g_print ("\n------\n");
	}

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

	/* when user cancels password dialog, then the cnc is NULL */
	if (!cnc)
		return E_GW_CONNECTION_STATUS_UNKNOWN;

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
e_gw_connection_get_items_delta_info (EGwConnection *cnc, const char *container, gdouble *first_sequence,
					gdouble *last_sequence, gdouble *last_po_rebuild_time )
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam;
	char *tmp = NULL;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getDeltaInfoRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

	e_gw_message_write_string_parameter (msg, "container", NULL, container);

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

	param = soup_soap_response_get_first_parameter_by_name (response, "deltaInfo");
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        /* parse these parameters */

	subparam = soup_soap_parameter_get_first_child_by_name (param, "firstSequence");

	if (subparam) {
		tmp = soup_soap_parameter_get_string_value(subparam);
		*first_sequence = strtod (tmp, NULL);
		g_free (tmp);
	} else
		*first_sequence = -1;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "lastSequence");

	if (subparam) {
		tmp = soup_soap_parameter_get_string_value(subparam);
		*last_sequence = strtod (tmp, NULL);
		g_free (tmp);
	}
	else
		*last_sequence = -1;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "lastTimePORebuild");

	if (subparam) {
		tmp = soup_soap_parameter_get_string_value(subparam);
		*last_po_rebuild_time = strtod (tmp, NULL);
		g_free (tmp);
	} else
		*last_po_rebuild_time = -1;

        g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_get_items_delta (EGwConnection *cnc, const char *container, const char *view, const char *count, const char * start_sequence, GList **add_list, GList **delete_list)
{
        SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param, *subparam, *subsubparam;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getDeltasRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

	e_gw_message_write_string_parameter (msg, "container", NULL, container);
	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view);

      	soup_soap_message_start_element (msg, "deltaInfo", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "firstSequence", NULL, start_sequence);
	e_gw_message_write_string_parameter (msg, "count", NULL, count);
	soup_soap_message_end_element(msg);

        /* send message to server */

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

        /* parse these parameters */
        for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
	     subparam != NULL;
	     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {
		EGwItem *item;


		item = e_gw_item_new_from_soap_parameter (cnc->priv->user_email, container, subparam);

		subsubparam =  soup_soap_parameter_get_first_child_by_name(subparam, "sync");
		if (subsubparam) {
			char *value;

			value = soup_soap_parameter_get_string_value (subsubparam);
			if (!strcmp (value, "add") || !strcmp (value, "update")) {
				*add_list = g_list_append (*add_list, item);
			} else if (!strcmp (value, "delete")) {
				*delete_list = g_list_append (*delete_list, item);
			}
			g_free (value);
		}
        }

	/* free memory */
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;

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
	char *tmp;

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

	tmp = soup_soap_parameter_get_string_value (param);
	if (!g_ascii_strcasecmp (tmp, "0")) {
		g_free (tmp);
                 g_message ("No deltas");
		 // g_object_unref (cnc);
                 return E_GW_CONNECTION_STATUS_OK;
        }

	g_free (tmp);

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
			*id = soup_soap_parameter_get_string_value (param);
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

	if (!*item)
		return E_GW_CONNECTION_STATUS_INVALID_OBJECT;

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
e_gw_connection_accept_request (EGwConnection *cnc, const char *id, const char *accept_level, const char *accept_comment, const char *recurrence_key)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "acceptRequest");
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "item", NULL, id);
	soup_soap_message_end_element (msg);
	e_gw_message_write_string_parameter (msg, "acceptLevel", NULL, accept_level);

	if (recurrence_key)
		e_gw_message_write_string_parameter (msg, "recurrenceAllInstances", NULL, recurrence_key);

	if (accept_comment)
		e_gw_message_write_string_parameter (msg, "comment", NULL, accept_comment);

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
e_gw_connection_decline_request (EGwConnection *cnc, const char *id, const char *decline_comment, const char *recurrence_key)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "declineRequest");
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "item", NULL, id);
	soup_soap_message_end_element (msg);

	if (decline_comment)
		e_gw_message_write_string_parameter (msg, "comment", NULL, decline_comment);

	if (recurrence_key)
		e_gw_message_write_string_parameter (msg, "recurrenceAllInstances", NULL, recurrence_key);

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

EGwConnectionStatus
e_gw_connection_delegate_request (EGwConnection *cnc, EGwItem *item, const char *id, const char *comments_org, const char *comments_del, const char *recur_key)
{
	SoupSoapMessage *msg;
	int status;
	SoupSoapResponse *response;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "delegateRequest");

	if (id)
		e_gw_message_write_string_parameter (msg, "id", NULL, id);

	e_gw_item_add_distribution_to_soap_message (item, msg);
	if (comments_org)
		e_gw_message_write_string_parameter (msg, "commentToOrganizer", NULL, comments_org);
	if (comments_del)
		e_gw_message_write_string_parameter (msg, "commentToDelegatee", NULL, comments_del);
	if (recur_key)
		e_gw_message_write_string_parameter (msg, "recurrenceAllInstances", NULL, recur_key);

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
	if (len <= 0)
		return str2;

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
	int i, j, len;
	time_t t;

	g_return_val_if_fail (dtstring != NULL, 0);

	len = strlen (dtstring);
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
	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;

        g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

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
			id = NULL;
		}
		g_free (id);
		g_free (name);
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

	for (; member_ids != NULL; member_ids = g_list_next (member_ids)) {
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

	for (; member_ids != NULL; member_ids = g_list_next (member_ids)) {
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
	e_gw_message_write_string_parameter (msg, "position", NULL, cursor_seek);
	e_gw_message_write_string_parameter (msg, "forward", NULL, forward ? "true": "false");
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
	if (start_date && *start_date)
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
	if (start_date && *start_date && (((!strcmp (message_list, "New")) || (!strcmp (message_list, "Modified"))))) {
		subparam = soup_soap_response_get_first_parameter_by_name (response, "startDate");
		if (subparam) {
			char *date;

			date = soup_soap_parameter_get_string_value (subparam);
			if (date)
				g_free (*start_date), *start_date = NULL, *start_date = date;
			else
				return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		} else
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}

	if (!strcmp (message_list, "All")) {
		gboolean view_is_id = FALSE;

		if (!strcmp (view, "id"))
			view_is_id = TRUE;
		/* We are  interested only in getting the ids */
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "item");
	     	     subparam != NULL;
	             subparam = soup_soap_parameter_get_next_child_by_name (subparam, "item")) {
			SoupSoapParameter *param_id;
		     	char *id;

			if (view_is_id) {
				param_id = soup_soap_parameter_get_first_child_by_name (subparam, "id");
				if (!param_id) {
					g_object_unref (response);
					g_object_unref (msg);
					return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
				}
			} else {
				param_id = soup_soap_parameter_get_first_child_by_name (subparam, "iCalId");
				if (!param_id) {
					g_object_unref (response);
					g_object_unref (msg);
					return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
				}
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
	char *buffer = NULL, *buf_length = NULL ;

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
	if (param) {
		buf_length =  soup_soap_parameter_get_property (param, "length") ;
		buffer = soup_soap_parameter_get_string_value (param) ;
	}

	if (buffer && buf_length) {
		gsize len = atoi (buf_length) ;
		*attachment = g_base64_decode (buffer,&len) ;
		*attach_length = len ;
	}

	/* free memory */
	g_free (buffer) ;
	g_free (buf_length) ;
        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
}

/*
 *
 */
EGwConnectionStatus
e_gw_connection_get_attachment_base64 (EGwConnection *cnc, const char *id, int offset, int length, const char **attachment, int *attach_length, int *offset_r)
{

	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
	SoupSoapParameter *param ;
	char *buffer = NULL, *buf_length = NULL, *o_return = NULL;

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
	if (param) {
		buf_length =  soup_soap_parameter_get_property (param, "length") ;
		o_return =  soup_soap_parameter_get_property (param, "offset") ;
		buffer = soup_soap_parameter_get_string_value (param) ;
	}

	if (buffer && buf_length) {
		int len = atoi (buf_length) ;
		*attachment = g_strdup (buffer);
		*attach_length = len;
		if (len && o_return) 
			*offset_r = atoi (o_return);
		else 
			*offset_r = 0;
	}

	/* free memory */
	g_free (buffer) ;
	g_free (buf_length) ;
	g_free (o_return);
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
e_gw_connection_purge_selected_items (EGwConnection *cnc, GList *item_ids)
{

	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status = E_GW_CONNECTION_STATUS_UNKNOWN;

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "purgeRequest");
	/* Now write the elements that need to be deleted */
	soup_soap_message_start_element (msg, "items", NULL, NULL);
	for (; item_ids != NULL; item_ids = g_list_next (item_ids))
		e_gw_message_write_string_parameter (msg, "item", NULL, item_ids->data);
	soup_soap_message_end_element (msg);
	/*End message*/
	e_gw_message_write_footer (msg);
	/* Send to server */
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

	if (!*item)
		return E_GW_CONNECTION_STATUS_INVALID_OBJECT;

        return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus
e_gw_connection_forward_item (EGwConnection *cnc, const char *id, const char *view, gboolean embed, EGwItem **item)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "forwardRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }

	e_gw_message_write_string_parameter (msg, "id", NULL, id);

	if (view)
		e_gw_message_write_string_parameter (msg, "view", NULL, view) ;

	if (embed)
		e_gw_message_write_int_parameter (msg, "embed", NULL,1);

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

	if (!*item)
		return E_GW_CONNECTION_STATUS_INVALID_OBJECT;

	return E_GW_CONNECTION_STATUS_OK;
}

/* e_gw_connection_create_junk_entry :creates a junk entry in the list
 * @cnc
 * @value : to be added in the list
 * @match_type : "email"/"domain" default: email
 * @list_type : "junk"/"trust"/"block" default: junk
 * */

EGwConnectionStatus
e_gw_connection_create_junk_entry (EGwConnection *cnc, const char *value, const char *match_type, const char *list_type)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	g_return_val_if_fail (value != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "createJunkEntryRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	soup_soap_message_start_element (msg, "entry", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "match", NULL, value);
	e_gw_message_write_string_parameter (msg, "matchType", NULL, match_type);
	e_gw_message_write_string_parameter (msg, "listType", NULL, list_type);
	soup_soap_message_end_element (msg);
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

/*TODO: move to different file*/
static void
parse_junk_settings (SoupSoapParameter *param, int *use_junk, int *use_block, int *use_pab, int *persistence)
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	if (param == NULL)
		return ;
	else	{
		/* parse these parameters into junk settings*/
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "setting");
				subparam != NULL;
				subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {
			char *field = NULL;
			int val = 0;

			field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
			val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");

			if (field_param) {
				field = soup_soap_parameter_get_string_value (field_param);
				if (!field)
					continue;
			} else
				continue;

			if (!g_ascii_strcasecmp (field, "useJunkList")) {
				if (val_param)
					val = soup_soap_parameter_get_int_value (val_param);
				*use_junk = val;
			} else	if (!g_ascii_strcasecmp (field, "useBlockList")) {
				if (val_param)
					val = soup_soap_parameter_get_int_value (val_param);
				*use_block = val;
			} else if (!g_ascii_strcasecmp (field, "usePAB")) {
				if (val_param)
					val = soup_soap_parameter_get_int_value (val_param);
				*use_pab = val;
			} else if (!g_ascii_strcasecmp (field, "persistence")) {
				if (val_param)
					val = soup_soap_parameter_get_int_value (val_param);
				*persistence = val;
			}
			g_free (field);
		}
	}
}

/*
 * e_gw_connection_get_junk_settings: gets the junk settings
 * use_junk : returned value, whether junk list is being used
 * use_block: use block list
 * use_pab: returned value, whether personal addresbook is used
 * persistence:
 * */
EGwConnectionStatus
e_gw_connection_get_junk_settings (EGwConnection *cnc, int *use_junk, int *use_block, int *use_pab, int *persistence)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
	SoupSoapParameter *param;
        EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getJunkMailSettingsRequest");

	if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	e_gw_message_write_footer (msg);
	response =  e_gw_connection_send_message (cnc, msg);

	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);

	/* if status is OK - parse result. return the list */
	if (status == E_GW_CONNECTION_STATUS_OK) {
		param = soup_soap_response_get_first_parameter_by_name (response, "settings");
		parse_junk_settings (param, use_junk, use_block, use_pab, persistence);
	}
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;

}

static void
msg_add_settings (SoupSoapMessage *msg, char *field, int value)
{
	soup_soap_message_start_element (msg, "setting", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "field", NULL, field);
	e_gw_message_write_int_parameter (msg, "value", NULL, value);
	soup_soap_message_end_element (msg);
}

/* e_gw_connection_modify_junk_settings: creates/removes junk mail settings
 * @cnc
 * @use_junk 0 : disable spam learning from junk list 1: enable
 * @use_block: same for block list
 * use_pab 1: put messages except from personal add book in junk, 0 disable
 * @persistence :delete after
 * */

EGwConnectionStatus
e_gw_connection_modify_junk_settings (EGwConnection *cnc, int use_junk, int use_pab, int use_block, int persistence)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "modifyJunkMailSettingsRequest");
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return E_GW_CONNECTION_STATUS_UNKNOWN;
	}

	soup_soap_message_start_element (msg, "settings", NULL, NULL);

	msg_add_settings (msg, "useJunkList", use_junk);
	msg_add_settings (msg, "usePAB", use_pab);
	msg_add_settings (msg, "useBlockList", use_block);
	msg_add_settings (msg, "persistence", persistence);

	soup_soap_message_end_element (msg);
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

/*TODO:Move this code to some other file and make this function public*/
static EGwJunkEntry *
e_gw_junkentry_new_from_soap_parameter (SoupSoapParameter *param)
{
	EGwJunkEntry *junk_entry;
	SoupSoapParameter *subparam;

	g_return_val_if_fail (param != NULL, NULL);

	junk_entry = g_new0(EGwJunkEntry, 1);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "id");
	if (!subparam) {
		g_warning (G_STRLOC ": found junk entry with no name");
		return NULL;
	}
	junk_entry->id = soup_soap_parameter_get_string_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "match");
	if (!subparam) {
		g_warning (G_STRLOC ": found junk entry with no Match");
		return NULL;
	}
	junk_entry->match = soup_soap_parameter_get_string_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "matchType");
	if (!subparam) {
		g_warning (G_STRLOC ": found junk entry with no MatchType");
		return NULL;
	}
	junk_entry->matchType = soup_soap_parameter_get_string_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "lastUsed");
	if (subparam)
		junk_entry->lastUsed = soup_soap_parameter_get_string_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "version");
	if (subparam)
		junk_entry->version = soup_soap_parameter_get_int_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "modified");
	if (subparam)
		junk_entry->modified = soup_soap_parameter_get_string_value (subparam);

	return junk_entry;

}

/*TODO:Move this code to some other file and make this function public*/
static void
get_junk_list_from_soap_response (SoupSoapResponse *response, GList **entries)
{
	SoupSoapParameter *param, *subparam;

	param = soup_soap_response_get_first_parameter_by_name (response, "junk");
	if (param) {
		/* parse these parameters into junk entries*/
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "entry");
				subparam != NULL;
				subparam = soup_soap_parameter_get_next_child_by_name (subparam, "entry")) {
			EGwJunkEntry *junk_entry;

			junk_entry = e_gw_junkentry_new_from_soap_parameter (subparam);
			if (junk_entry)
				*entries = g_list_append (*entries, junk_entry);
		}
	}
	param = soup_soap_response_get_first_parameter_by_name (response, "block");
	if (param) {
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "entry");
				subparam != NULL;
				subparam = soup_soap_parameter_get_next_child_by_name (subparam, "entry")) {
			EGwJunkEntry *junk_entry;
			junk_entry = e_gw_junkentry_new_from_soap_parameter (subparam);
			if (junk_entry)
				*entries = g_list_append (*entries, junk_entry);
		}
	}
	param = soup_soap_response_get_first_parameter_by_name (response, "trust");
	if (param) {
		for (subparam = soup_soap_parameter_get_first_child_by_name (param, "entry");
				subparam != NULL;
				subparam = soup_soap_parameter_get_next_child_by_name (subparam, "entry")) {
			EGwJunkEntry *junk_entry;
			junk_entry = e_gw_junkentry_new_from_soap_parameter (subparam);
			if (junk_entry)
				*entries = g_list_append (*entries, junk_entry);
		}
	}
 }

/* Caller must free the entries*** TODO: have a function in the generic file to free these*/
EGwConnectionStatus
e_gw_connection_get_junk_entries (EGwConnection *cnc, GList **entries)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getJunkEntriesRequest");

	if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
        }
	e_gw_message_write_footer (msg);
	response =  e_gw_connection_send_message (cnc, msg);

	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_gw_connection_parse_response_status (response);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		reauthenticate (cnc);

	/* if status is OK - parse result. return the list */
	if (status == E_GW_CONNECTION_STATUS_OK)
		get_junk_list_from_soap_response (response, entries);

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
}

EGwConnectionStatus
e_gw_connection_remove_junk_entry (EGwConnection *cnc, const char *id)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (id != NULL, E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "removeJunkEntryRequest");

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
e_gw_connection_read_cal_ids (EGwConnection *cnc, const char *container, int cursor, gboolean forward, int count, const char *cursor_seek, GList **list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;
	SoupSoapParameter *param, *subparam;
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail ((container != NULL), E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "readCursorRequest");
	e_gw_message_write_int_parameter (msg, "cursor", NULL, cursor);
	*list = NULL;
	/* there is problem in read curosr if you set this, uncomment after the problem
	   is fixed in server */
	e_gw_message_write_string_parameter (msg, "position", NULL, cursor_seek);
	e_gw_message_write_string_parameter (msg, "forward", NULL, forward ? "true": "false");
	e_gw_message_write_string_parameter (msg, "container", NULL, container);
	e_gw_message_write_int_parameter (msg, "count", NULL, count);

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
		SoupSoapParameter *param_id;
		EGwItemCalId *calid = g_new0 (EGwItemCalId, 1);
		EGwItemType type;
		char *id = NULL, *item_type = NULL;

		item_type = soup_soap_parameter_get_property (subparam, "type");

		if (g_str_equal (item_type, "Appointment"))
				type  = E_GW_ITEM_TYPE_APPOINTMENT;
		else if  (g_str_equal (item_type, "Task"))
			type = E_GW_ITEM_TYPE_TASK;
		else {
			type = E_GW_ITEM_TYPE_NOTE;
		}
		g_free (item_type);

		param_id = soup_soap_parameter_get_first_child_by_name (subparam, "id");
		if (param_id) {
			id = soup_soap_parameter_get_string_value (param_id);
			if (!id) {
				e_gw_item_free_cal_id (calid);
				g_object_unref (response);
				g_object_unref (msg);
				return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
			}

			calid->item_id = id;
		} else {
			e_gw_item_free_cal_id (calid);
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}

		id = NULL;

		param_id = soup_soap_parameter_get_first_child_by_name (subparam, "recurrenceKey");
		if (param_id) {
			id = soup_soap_parameter_get_string_value (param_id);
		}

		if (id && !g_str_equal (id, "0")) {
			guint allday = 0;

			calid->recur_key = id;

			if (type == E_GW_ITEM_TYPE_APPOINTMENT) {
				param_id = soup_soap_parameter_get_first_child_by_name (subparam, "allDayEvent");
				if (param_id) {
					allday = soup_soap_parameter_get_int_value (param_id);
				}

			}

			if (allday)
				param_id = soup_soap_parameter_get_first_child_by_name (subparam, "startDay");
			else
				param_id = soup_soap_parameter_get_first_child_by_name (subparam, "startDate");

			if (param_id) {
				char *formatted_date;
				id = soup_soap_parameter_get_string_value (param_id);
				formatted_date = e_gw_connection_format_date_string (id);
				 /* store the date in calid for recurring events */
				calid->ical_id = formatted_date;
				g_free (id);
			}

		} else {
			g_free (id);
			id = NULL;

			param_id = soup_soap_parameter_get_first_child_by_name (subparam, "iCalId");
			if (!param_id) {
				if (*list) {
					g_list_foreach (*list, (GFunc) e_gw_item_free_cal_id, NULL);
					g_list_free (*list);
					*list = NULL;
				}

				e_gw_item_free_cal_id (calid);
				g_object_unref (response);
				g_object_unref (msg);
				return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
			}

			id = soup_soap_parameter_get_string_value (param_id);
			calid->ical_id = id;
		}

		*list = g_list_append (*list, calid);
	}

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);
	return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus
e_gw_connection_get_all_mail_uids (EGwConnection *cnc, const char *container, int cursor, gboolean forward, int count, const char *cursor_seek, GList **list)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;
	SoupSoapParameter *param, *subparam;
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	g_return_val_if_fail ((container != NULL), E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "readCursorRequest");
	e_gw_message_write_int_parameter (msg, "cursor", NULL, cursor);
	*list = NULL;
	/* there is problem in read curosr if you set this, uncomment after the problem
	   is fixed in server */
	e_gw_message_write_string_parameter (msg, "position", NULL, cursor_seek);
	e_gw_message_write_string_parameter (msg, "forward", NULL, forward ? "true": "false");
	e_gw_message_write_string_parameter (msg, "container", NULL, container);
	e_gw_message_write_int_parameter (msg, "count", NULL, count);

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
		SoupSoapParameter *param_id;
		char *id = NULL;

		param_id = soup_soap_parameter_get_first_child_by_name (subparam, "id");
		if (!param_id) {
			if (*list) {
				g_list_foreach (*list, (GFunc) g_free, NULL);
				g_list_free (*list);
				*list = NULL;
			}
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}

		id = soup_soap_parameter_get_string_value (param_id);
		if (id)
			*list =	g_list_prepend (*list, id);
		else {
			if (*list) {
				g_list_foreach (*list, (GFunc) g_free, NULL);
				g_list_free (*list);
				*list = NULL;
			}
			g_object_unref (response);
			g_object_unref (msg);
			return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
		}
	}
	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);
	return E_GW_CONNECTION_STATUS_OK;
}

EGwConnectionStatus
e_gw_connection_get_proxy_access_list (EGwConnection *cnc, GList **proxy_list)
{
	SoupSoapMessage *msg = NULL;
	SoupSoapResponse *response = NULL;
	EGwConnectionStatus status;
	SoupSoapParameter *param;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);

	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getProxyAccessListRequest");

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

	param = soup_soap_response_get_first_parameter_by_name (response, "accessRights");
	if (!param) {
		g_object_unref (response);
		return status;
	} else 	{
		e_gw_proxy_construct_proxy_access_list (param, proxy_list);
	}
	/* free memory */
	if (response)
		g_object_unref (response);
	if (msg)
		g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_add_proxy (EGwConnection *cnc, proxyHandler *new_proxy)
{
	SoupSoapMessage *msg = NULL;
	SoupSoapResponse *response = NULL;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc), e_gw_connection_get_session_id (cnc), "createProxyAccessRequest");

	e_gw_proxy_form_proxy_add_msg (msg, new_proxy);

	e_gw_message_write_footer (msg);
	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}
	status = e_gw_connection_parse_response_status (response);

	if (response)
	g_object_unref (response);

	if (msg)
	g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_remove_proxy (EGwConnection *cnc, proxyHandler *removeProxy)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);

	msg = e_gw_message_new_with_header (e_gw_connection_get_uri(cnc), e_gw_connection_get_session_id(cnc), "removeProxyAccessRequest");

	e_gw_proxy_form_proxy_remove_msg (msg, removeProxy);

	e_gw_message_write_footer (msg);

	response = e_gw_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}
	status = e_gw_connection_parse_response_status (response);
	g_object_unref (response);
	g_object_unref (msg);
	return E_GW_CONNECTION_STATUS_OK;

}

EGwConnectionStatus
e_gw_connection_modify_proxy (EGwConnection *cnc, proxyHandler *new_proxy)
{
	SoupSoapMessage *msg = NULL;
	SoupSoapResponse *response = NULL;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_UNKNOWN);
	msg = e_gw_message_new_with_header (e_gw_connection_get_uri (cnc), e_gw_connection_get_session_id (cnc), "modifyProxyAccessRequest");
	e_gw_message_write_string_parameter (msg, "id", NULL, new_proxy->uniqueid);

	e_gw_proxy_form_modify_proxy_msg (msg, new_proxy);

	e_gw_message_write_footer (msg);
	response = e_gw_connection_send_message (cnc, msg);

	if (!response) {
		g_object_unref (msg);
		return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
	}
	status = e_gw_connection_parse_response_status (response);

	if (response)
	g_object_unref (response);

	if (msg)
	g_object_unref (msg);
	return status;
}

EGwConnectionStatus
e_gw_connection_get_proxy_list (EGwConnection *cnc, GList **proxy_info)
{
	SoupSoapMessage *msg;
        SoupSoapResponse *response;
        EGwConnectionStatus status;
        SoupSoapParameter *param;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_OBJECT);
	/* build the SOAP message */
        msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getProxyListRequest");
        if (!msg) {
                g_warning (G_STRLOC ": Could not build SOAP message");
                return E_GW_CONNECTION_STATUS_UNKNOWN;
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
	}
	/* if status is OK - parse result. return the list */
	param = soup_soap_response_get_first_parameter_by_name (response, "proxies");
        e_gw_proxy_construct_proxy_list (param, proxy_info);
        if (!param) {
                g_object_unref (response);
                g_object_unref (msg);
                return E_GW_CONNECTION_STATUS_INVALID_RESPONSE;
        }

        g_object_unref (response);
	g_object_unref (msg);

        return E_GW_CONNECTION_STATUS_OK;
}

static SoupSoapMessage*
form_proxy_login_request (EGwConnection *cnc, const char* username, const char* password, const char *proxy)
{
	SoupSoapMessage *msg;
	/* build the SOAP message */
	msg = e_gw_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "loginRequest");
	soup_soap_message_start_element (msg, "auth", "types", NULL);
	soup_soap_message_add_attribute (msg, "type", "types:Proxy", "xsi",
					 "http://www.w3.org/2001/XMLSchema-instance");
	e_gw_message_write_string_parameter (msg, "username", "types", username);
	e_gw_message_write_string_parameter (msg, "password", "types", password);
	e_gw_message_write_string_parameter (msg, "proxy", "types", proxy);
	soup_soap_message_end_element (msg);
	e_gw_message_write_footer (msg);
	return msg;
}

EGwConnection *
e_gw_connection_get_proxy_connection (EGwConnection *parent_cnc, char *username, const char *password, const char *proxy, int  *permissions)
{
	EGwConnection *cnc;
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EGwConnectionStatus status;
	SoupSoapParameter *param;
	SoupSoapParameter *subparam;
	char *hash_key;
	char *name = NULL;
	int i;
	char *permissions_key = NULL;

	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;

	g_static_mutex_lock (&connecting);

	for (i=0; proxy[i]!='\0' && proxy[i]!='@'; i++);
	if (proxy[i]=='@')
		name = g_strndup(proxy, i);
	else
		name = g_strdup (proxy);
	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = g_strdup_printf ( "%s:%s@%s",
				name,
				"",
				parent_cnc->priv->uri);
		cnc = g_hash_table_lookup (loaded_connections_permissions, hash_key);
		permissions_key = g_strdup_printf ("%s:permissions", hash_key);

		if (E_IS_GW_CONNECTION (cnc)) {
			*permissions = GPOINTER_TO_INT (g_hash_table_lookup (loaded_connections_permissions, permissions_key));
			g_free (permissions_key);
			g_free (name);
			g_free (hash_key);
			g_object_ref (cnc);
			g_static_mutex_unlock (&connecting);
			return cnc;
		}
		g_free (permissions_key);
	}

	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_GW_CONNECTION, NULL);

	msg = form_proxy_login_request (parent_cnc, username, password, proxy);

	/* send message to server */
	response = e_gw_connection_send_message (parent_cnc, msg);

	if (!response) {
		g_object_unref (cnc);
		g_static_mutex_unlock (&connecting);
		g_object_unref (msg);
		return NULL;
	}

	status = e_gw_connection_parse_response_status (response);

	param = soup_soap_response_get_first_parameter_by_name (response, "session");
	if (!param) {
		g_object_unref (response);
		g_object_unref (msg);
		g_object_unref (cnc);
		g_static_mutex_unlock (&connecting);
		return NULL;
	}

	cnc->priv->uri = g_strdup (parent_cnc->priv->uri);
	cnc->priv->username = g_strdup (proxy);
	cnc->priv->password = NULL;
	cnc->priv->session_id = soup_soap_parameter_get_string_value (param);

	/* retrieve user information */
	param = soup_soap_response_get_first_parameter_by_name (response, "entry");

	if (param) {
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

		e_gw_proxy_parse_proxy_login_response (param, permissions);
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

	/* add the connection to the loaded_connections_permissions hash table */
	hash_key = g_strdup_printf ("%s:%s@%s",
			name,
			"",
			cnc->priv->uri);

	g_hash_table_insert (loaded_connections_permissions, hash_key, cnc);
	permissions_key = g_strdup_printf ("%s:permissions", hash_key);
	g_hash_table_insert (loaded_connections_permissions, permissions_key, GINT_TO_POINTER(*permissions));

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);
	g_free (name);
	g_static_mutex_unlock (&connecting);
	return cnc;
}
