/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>
#include <gdata-service-iface.h>
#include <gdata-google-service.h>

#include <libsoup/soup.h>
#include <string.h>

#define GDATA_GOOGLE_SERVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GDATA_TYPE_GOOGLE_SERVICE, GDataGoogleServicePrivate))

static void gdata_google_service_iface_init (GDataServiceIface *iface);

G_DEFINE_TYPE_EXTENDED (GDataGoogleService, gdata_google_service, G_TYPE_OBJECT, 0,
	G_IMPLEMENT_INTERFACE (GDATA_TYPE_SERVICE, gdata_google_service_iface_init))

static GDataEntry * gdata_google_service_update_entry_with_link (GDataService *service, GDataEntry *entry, const gchar *edit_link, GError **error);
static GDataEntry * gdata_google_service_insert_entry (GDataService *service, const gchar *feed_url, GDataEntry *entry, GError **error);
static gboolean     gdata_google_service_delete_entry (GDataService *service, GDataEntry *entry, GError **error);
static GDataFeed  * gdata_google_service_get_feed (GDataService *service, const gchar *feed_url, GError **error);
static GDataEntry * gdata_google_service_update_entry (GDataService *service, GDataEntry *entry, GError **error);
static void         gdata_google_service_set_credentials (GDataService *service, const gchar *username, const gchar *password);
static void         gdata_google_service_set_proxy (GDataService *service, SoupURI *proxy);

typedef struct _GDataGoogleServiceAuth GDataGoogleServiceAuth;
struct _GDataGoogleServiceAuth {
	/* Authentication Information */
	gchar *username;
	gchar *password;

	gchar *token;
};

struct _GDataGoogleServicePrivate {
	/* Session information */
	gchar *name;
	gchar *agent;

	SoupSession *soup_session;
	GDataGoogleServiceAuth *auth;

	gboolean dispose_has_run;

};

enum {
	PROP_0,
	PROP_NAME,
	PROP_AGENT
};

static const gchar *GOOGLE_CLIENT_LOGIN = "https://www.google.com/accounts/ClientLogin";

GQuark
gdata_google_error_quark (void)
{
	static GQuark error;
	return error ? error : (error = g_quark_from_static_string ("gdata_google_error_quark"));
}

static void
gdata_google_service_set_proxy (GDataService *service, SoupURI *proxy)
{
	GDataGoogleServicePrivate *priv;

	g_return_if_fail (service != NULL);
	g_return_if_fail (GDATA_IS_GOOGLE_SERVICE(service));

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(GDATA_GOOGLE_SERVICE(service));
	if (proxy && priv && priv->soup_session)
		g_object_set (priv->soup_session, SOUP_SESSION_PROXY_URI, proxy, NULL);
}

/* send a message without redirection and if it was required, then redirects itself */
static void
send_and_handle_google_redirection (SoupSession *soup_session, SoupMessage *msg)
{
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	soup_session_send_message (soup_session, msg);
	soup_message_set_flags (msg, 0);

	if (SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
		SoupURI *new_uri;
		const gchar *new_loc;

		new_loc = soup_message_headers_get (msg->response_headers, "Location");
		g_return_if_fail (new_loc != NULL);

		new_uri = soup_uri_new_with_base (soup_message_get_uri (msg), new_loc);
		if (!new_uri) {
			soup_message_set_status_full (msg,
						      SOUP_STATUS_MALFORMED,
						      "Invalid Redirect URL");
			return;
		}

		soup_message_set_uri (msg, new_uri);
		soup_uri_free (new_uri);

		soup_session_send_message (soup_session, msg);
	}
}

static void
gdata_google_service_set_credentials (GDataService *service, const gchar *username, const gchar *password)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;

	g_return_if_fail (service != NULL);
	g_return_if_fail (GDATA_IS_GOOGLE_SERVICE(service));

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(GDATA_GOOGLE_SERVICE(service));
	auth = (GDataGoogleServiceAuth *)priv->auth;

	auth->username = g_strdup(username);
	auth->password = g_strdup(password);
}

static gboolean
service_is_authenticated (GDataGoogleService *service)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(service);
	auth = (GDataGoogleServiceAuth *)priv->auth;

	if (auth->token == NULL)
		return FALSE;
	else
		return TRUE;
}

/**
 *
 * gdata_google_service_get_feed:
 * @service A GDataService Object
 * @feed_url Feed Url , the private url to send request to , needs authentication
 * @entry A GDataFeed Object
 * returns the newly inserted entry
 *
 **/

static GDataFeed *
gdata_google_service_get_feed (GDataService *service, const gchar *feed_url, GError **error)
{
	GDataFeed *feed = NULL;
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	SoupSession *soup_session;
	SoupMessage *msg;

	g_return_val_if_fail(service != NULL, NULL);
	g_return_val_if_fail(GDATA_IS_GOOGLE_SERVICE(service),NULL);

	if (!service_is_authenticated( GDATA_GOOGLE_SERVICE(service) )) {
		if (FALSE == gdata_google_service_authenticate(GDATA_GOOGLE_SERVICE(service), error))
			return NULL;
	}

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE( GDATA_GOOGLE_SERVICE(service) );
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;

	msg = soup_message_new(SOUP_METHOD_GET, feed_url);
	if (!msg)
		return NULL;

	soup_message_headers_append(msg->request_headers,
			"Authorization", (gchar *)g_strdup_printf("GoogleLogin auth=%s", auth->token));

	soup_session_send_message (soup_session, msg);
	if (msg->status_code != 200) {
		g_set_error (error, SOUP_HTTP_ERROR,
					 msg->status_code, "%s", msg->reason_phrase);

		g_object_unref (msg);
		return NULL;
	}

	if (msg->response_body->data && strlen (msg->response_body->data) > 0) {
		feed = gdata_feed_new_from_xml(msg->response_body->data,
					       msg->response_body->length);
	}

	if (NULL == feed) {
		g_set_error (error, GDATA_GOOGLE_ERROR,
					 -1, "GData protocol error");
	}

	g_object_unref(msg);

	return feed;
}

/**
 *
 * gdata_google_service_insert_entry:
 * @service A #GDataService Object
 * @feed_url Feed Url , this is the private url of the author which requires authentication
 * @entry A #GDataEntry Object
 * returns the newly inserted entry
 *
 **/
static GDataEntry *
gdata_google_service_insert_entry (GDataService *service, const gchar *feed_url, GDataEntry *entry, GError **error)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	GDataEntry *updated_entry = NULL;
	SoupSession *soup_session;
	SoupMessage *msg;
	gchar *entry_xml;

	g_return_val_if_fail(service != NULL, NULL);
	g_return_val_if_fail(GDATA_IS_GOOGLE_SERVICE(service), NULL);

	if (!service_is_authenticated( GDATA_GOOGLE_SERVICE(service) )) {
		if (FALSE == gdata_google_service_authenticate(GDATA_GOOGLE_SERVICE(service), error))
			return NULL;
	}

	entry_xml = gdata_entry_generate_xml (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(GDATA_GOOGLE_SERVICE(service));
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;

	msg = soup_message_new(SOUP_METHOD_POST, feed_url);
	soup_message_set_http_version (msg, SOUP_HTTP_1_0);

	soup_message_headers_append(msg->request_headers,
				    "Authorization",
				    (gchar *)g_strdup_printf("GoogleLogin auth=%s",
				    auth->token));

	soup_message_set_request (msg,
				"application/atom+xml",
				SOUP_MEMORY_COPY,
				entry_xml,
				strlen(entry_xml));

	/* Handle redirects ourself, since soup does not behave like google-api expects */
	send_and_handle_google_redirection (soup_session, msg);

	if (msg->status_code != 201) {
		g_set_error (error, SOUP_HTTP_ERROR,
					 msg->status_code, "%s", msg->reason_phrase);
		g_object_unref (msg);
		return NULL;
	}

	if (msg->response_body->data && strlen (msg->response_body->data) > 0) {
		updated_entry = gdata_entry_new_from_xml (msg->response_body->data);
	}

	if (NULL == updated_entry) {
		g_set_error (error, GDATA_GOOGLE_ERROR,
					 -1, "GData protocol error");
	}

	g_object_unref (msg);
	return updated_entry;
}

/**
 *
 * gdata_google_service_delete_entry:
 * @service A #GDataService Object
 * @feed_url Feed Url , this is the private url of the author which requires authentication
 * @entry A #GDataEntry Object
 * Removes the entry
 *
 **/
static gboolean
gdata_google_service_delete_entry (GDataService *service, GDataEntry *entry, GError **error)
{
	GDataGoogleServiceAuth *auth;
	GDataGoogleServicePrivate *priv;
	SoupSession *soup_session;
	SoupMessage *msg;
	const gchar *entry_edit_url;
	gboolean retval = FALSE;

	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (GDATA_IS_GOOGLE_SERVICE(service), FALSE);

	if (!service_is_authenticated( GDATA_GOOGLE_SERVICE(service) )) {
		if (FALSE == gdata_google_service_authenticate(GDATA_GOOGLE_SERVICE(service), error))
			return FALSE;
	}

	entry_edit_url = gdata_entry_get_edit_link (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE (GDATA_GOOGLE_SERVICE (service));
	auth = (GDataGoogleServiceAuth *) priv->auth;
	soup_session =	(SoupSession *)priv->soup_session;

	msg = soup_message_new (SOUP_METHOD_DELETE, entry_edit_url);
	soup_message_headers_append (msg->request_headers,
				     "Authorization",
				     (gchar *)g_strdup_printf ("GoogleLogin auth=%s",
				     auth->token));

	/* Handle redirects ourself */
	send_and_handle_google_redirection (soup_session, msg);

	if (msg->status_code != 200) {
		g_set_error (error, SOUP_HTTP_ERROR,
					 msg->status_code, "%s", msg->reason_phrase);
	} else {
		retval = TRUE;
	}

	g_object_unref (msg);

	return retval;
}

/**
 *
 * gdata_google_service_update_entry:
 * @service A GDataService Object
 * @feed_url Feed Url , this is the private url of the author which requires authentication
 * @entry A GDataEntry Object
 * updates the entry
 *
 **/
static GDataEntry*
gdata_google_service_update_entry (GDataService *service, GDataEntry *entry, GError **error)
{
	const gchar *entry_edit_url;

	g_return_val_if_fail (service != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_GOOGLE_SERVICE (service), NULL);

	entry_edit_url = gdata_entry_get_edit_link (entry);

	return gdata_google_service_update_entry_with_link (service, entry, entry_edit_url, error);
}

/**
 *
 * gdata_google_update_entry_with_link:
 * @service A #GDataService Object
 * @edit_link url of the edit link of the entry
 * @entry A #GDataEntry Object
 * Updates the entry
 *
 **/
static GDataEntry*
gdata_google_service_update_entry_with_link (GDataService *service, GDataEntry *entry, const gchar *edit_link, GError **error)
{
	GDataGoogleServiceAuth *auth;
	GDataGoogleServicePrivate *priv;
	SoupSession *soup_session;
	SoupMessage *msg;
	gchar *entry_xml;
	GDataEntry *updated_entry = NULL;

	g_return_val_if_fail (service != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_GOOGLE_SERVICE (service), NULL);
	g_return_val_if_fail (edit_link != NULL, NULL);

	if (!service_is_authenticated( GDATA_GOOGLE_SERVICE(service) )) {
		if (FALSE == gdata_google_service_authenticate(GDATA_GOOGLE_SERVICE(service), error))
			return NULL;
	}

	entry_xml = gdata_entry_generate_xml (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE (GDATA_GOOGLE_SERVICE(service));
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;

	msg = soup_message_new (SOUP_METHOD_PUT, edit_link);

	soup_message_headers_append (msg->request_headers,
				     "Authorization",
				     (gchar *)g_strdup_printf ("GoogleLogin auth=%s",
				     auth->token));

	soup_message_set_request (msg,
				"application/atom+xml",
				SOUP_MEMORY_COPY,
				entry_xml,
				strlen(entry_xml));

	/* Handle redirects ourself */
	send_and_handle_google_redirection (soup_session, msg);

	if (msg->status_code != 200) {
		g_set_error (error, SOUP_HTTP_ERROR,
					 msg->status_code, "%s", msg->reason_phrase);
		g_object_unref (msg);
		return updated_entry;
	}

	if (msg->response_body->data && strlen (msg->response_body->data) > 0) {
		updated_entry = gdata_entry_new_from_xml (msg->response_body->data);
	}

	if (NULL == updated_entry) {
		g_set_error (error, GDATA_GOOGLE_ERROR,
					 -1, "GData protocol error");
	}

	g_object_unref (msg);
	return updated_entry;
}

static void
gdata_google_service_iface_init (GDataServiceIface *iface)
{
	iface->set_proxy = gdata_google_service_set_proxy;
	iface->set_credentials = gdata_google_service_set_credentials;
	iface->get_feed = gdata_google_service_get_feed;
	iface->insert_entry = gdata_google_service_insert_entry;
	iface->delete_entry = gdata_google_service_delete_entry;
	iface->update_entry = gdata_google_service_update_entry;
	iface->update_entry_with_link = gdata_google_service_update_entry_with_link;
	return;
}

static void
gdata_google_service_init (GDataGoogleService *instance)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleService *self = instance;

	/* Private data set by g_type_class_add_private */
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(self);
	priv->dispose_has_run = FALSE;

	priv->name = NULL;
	priv->agent = NULL;

	priv->auth = g_new0(GDataGoogleServiceAuth,1);
	priv->auth->username = NULL;
	priv->auth->password = NULL;
	priv->auth->token = NULL;

	priv->soup_session = soup_session_sync_new();
}

static void gdata_google_service_dispose(GObject *obj)
{
	GObjectClass *parent_class;
	GDataGoogleServiceClass *klass;

	GDataGoogleService *self = (GDataGoogleService *)obj;
	GDataGoogleServicePrivate *priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(self);

	if (priv->dispose_has_run) {
		/* Don't run dispose twice */
		return;
	}
	priv->dispose_has_run = TRUE;

	if (priv->soup_session) {
		g_object_unref (priv->soup_session);
		priv->soup_session = NULL;
	}

	/* Chain up to the parent class */
	klass = GDATA_GOOGLE_SERVICE_CLASS(g_type_class_peek(GDATA_TYPE_GOOGLE_SERVICE));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	parent_class->dispose(obj);
}

static void gdata_google_service_finalize(GObject *obj)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	GDataGoogleService *self = GDATA_GOOGLE_SERVICE(obj);
	GObjectClass *parent_class;
	GDataGoogleServiceClass *klass;

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(self);
	auth = (GDataGoogleServiceAuth *)priv->auth;

	if (priv->name != NULL)
		g_free(priv->name);

	if (priv->agent != NULL)
		g_free(priv->agent);

	if (auth->username != NULL)
		g_free(auth->username);

	if (auth->password)
		g_free(auth->password);

	if (auth->token != NULL) {
		g_free(auth->token);
	}
	g_free(auth);

	/* Chain up to the parent class */
	klass = GDATA_GOOGLE_SERVICE_CLASS(g_type_class_peek(GDATA_TYPE_GOOGLE_SERVICE));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	parent_class->finalize(obj);
}

static void gdata_google_service_get_property (GObject *obj,
		guint    property_id,
		GValue  *value,
		GParamSpec *pspec)
{
	GDataGoogleServicePrivate *priv;

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(obj);

	switch (property_id) {
		case PROP_NAME:
			g_value_set_string(value, priv->name);
			break;
		case PROP_AGENT:
			g_value_set_string(value, priv->name);
			break;
		default:
			/* Invalid Property */
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, property_id, pspec);
			break;
	}
}

static void gdata_google_service_set_property (GObject *obj,
		guint    property_id,
		const GValue *value,
		GParamSpec   *pspec)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleService *self = (GDataGoogleService *) obj;

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(self);

	switch (property_id) {
		case PROP_NAME:
			if (priv->name != NULL)
				g_free(priv->name);
			priv->name = g_value_dup_string(value);
			break;
		case PROP_AGENT:
			if (priv->agent != NULL)
				g_free(priv->agent);
			priv->agent = g_value_dup_string(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, property_id, pspec);
			break;
	}
}

static void gdata_google_service_class_init (GDataGoogleServiceClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	g_type_class_add_private(klass, sizeof (GDataGoogleServicePrivate));

	gobject_class->set_property = gdata_google_service_set_property;
	gobject_class->get_property = gdata_google_service_get_property;
	gobject_class->dispose  = gdata_google_service_dispose;
	gobject_class->finalize = gdata_google_service_finalize;

	g_object_class_install_property(gobject_class, PROP_NAME,
			g_param_spec_string("name", "Name",
				"The name (e.g. 'cl') of the service",
				NULL,
				G_PARAM_READWRITE |
				G_PARAM_CONSTRUCT_ONLY |
				G_PARAM_STATIC_NAME |
				G_PARAM_STATIC_NICK |
				G_PARAM_STATIC_BLURB));

	g_object_class_install_property(gobject_class, PROP_AGENT,
			g_param_spec_string("agent", "Agent",
				"The agent (e.g 'evolution', 'tinymail') of the calling program",
				NULL,
				G_PARAM_READWRITE |
				G_PARAM_CONSTRUCT_ONLY |
				G_PARAM_STATIC_NAME |
				G_PARAM_STATIC_NICK |
				G_PARAM_STATIC_BLURB));

	return;
}

/*********API******* */

/**
 *
 * gdata_google_service_new:
 * @service_name
 * @agent
 * Returns a new #GDataGoogleService Object
 *
 **/
GDataGoogleService *
gdata_google_service_new(const gchar *service_name, const gchar *agent)
{
	return g_object_new(GDATA_TYPE_GOOGLE_SERVICE,
			"name", service_name,
			"agent",agent,
			NULL);
}

gboolean
gdata_google_service_authenticate (GDataGoogleService *service, GError **error)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	SoupMessage *msg;
	GHashTable *request_form;
	gchar *request_body;
	gchar *token = NULL;

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(service);
	auth = (GDataGoogleServiceAuth *)priv->auth;

	request_form = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (request_form, (gpointer) "Email", auth->username);
	g_hash_table_insert (request_form, (gpointer) "Passwd", auth->password);
	g_hash_table_insert (request_form, (gpointer) "service", priv->name);
	g_hash_table_insert (request_form, (gpointer) "source", priv->agent);
	g_hash_table_insert (request_form, (gpointer) "accountType", (gpointer) "HOSTED_OR_GOOGLE");
	request_body = soup_form_encode_hash (request_form);
	g_hash_table_destroy (request_form);

	msg = soup_message_new(SOUP_METHOD_POST, GOOGLE_CLIENT_LOGIN);
	soup_message_set_http_version(msg, SOUP_HTTP_1_0);
	soup_message_set_request (msg, "application/x-www-form-urlencoded",
				  SOUP_MEMORY_TAKE,
				  request_body, strlen(request_body));

	soup_session_send_message (priv->soup_session, msg);

	if (msg->status_code != 200) {
		g_set_error (error, SOUP_HTTP_ERROR,
					 msg->status_code, "%s", msg->reason_phrase);
		g_object_unref(msg);
		return (NULL != token);
	}
	if (msg->response_body->data && strlen (msg->response_body->data) > 0) {
		gchar *auth_begin = NULL;
		gchar *auth_end = NULL;

		auth_begin = strstr(msg->response_body->data, "Auth=");

		if (!auth_begin) {
			return (NULL != token);
		}

		auth_end  = strstr(auth_begin, "\n") - 5;

		if (auth_begin && strlen(auth_begin) > 5) {
			token = g_strndup(auth_begin + strlen("Auth="), auth_end - auth_begin);
		}
	}

	auth->token = token;
	if (NULL == token) {
		g_set_error (error, GDATA_GOOGLE_ERROR,
					 -1, "GData protocol error");
	}

	return (NULL != token);
}
