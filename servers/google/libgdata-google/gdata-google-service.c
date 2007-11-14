/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
 *
 * Copyright 2007, Novell, Inc.
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

void gdata_google_service_update_entry_with_link (GDataService *service, GDataEntry *entry, gchar *edit_link);
GDataEntry * gdata_google_service_insert_entry (GDataService *service, const gchar *feed_url, GDataEntry *entry);
void gdata_google_service_delete_entry (GDataService *service, GDataEntry *entry);
GDataFeed * gdata_google_service_get_feed (GDataService *service, const gchar *feed_url);
void gdata_google_service_update_entry (GDataService *service, GDataEntry *entry);
void gdata_google_service_set_credentials (GDataService *service, const gchar *username, const gchar *password);

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
	PROP_AGENT,
};

static const gchar *GOOGLE_CLIENT_LOGIN = "https://www.google.com/accounts/ClientLogin";
static const gchar *GOOGLE_CLIENT_LOGIN_MSG = "Email=%s&Passwd=%s&service=%s&source=%s";

void
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

static gchar *
service_authenticate (GDataGoogleService *service)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	SoupMessage *msg;
	gchar *request_body;
	gchar *request_body_encoded;
	gchar *token = NULL;
	gchar *auth_begin = NULL;
	gchar *auth_end = NULL;

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(service);
	auth = (GDataGoogleServiceAuth *)priv->auth;

	msg = soup_message_new(SOUP_METHOD_POST, GOOGLE_CLIENT_LOGIN);
	request_body = g_strdup_printf(GOOGLE_CLIENT_LOGIN_MSG, auth->username,
			               auth->password,
				       priv->name,
				       priv->agent);

	request_body_encoded = soup_uri_encode(request_body,NULL);
	soup_message_set_http_version(msg, SOUP_HTTP_1_0);

	soup_message_set_request (msg, "application/x-www-form-urlencoded",
				  SOUP_BUFFER_USER_OWNED,
				  request_body_encoded,
				  strlen(request_body_encoded));

	soup_session_send_message(priv->soup_session, msg);

	if (msg->response.length) {
		auth_begin = strstr(msg->response.body, "Auth=");

		if (!auth_begin)
			return "FAILURE";

		auth_begin = auth_begin;
		auth_end  = strstr(auth_begin, "\n") - 5;

		if (auth_begin && strlen(auth_begin) > 5) {
			token = g_strndup(auth_begin + strlen("Auth="), auth_end - auth_begin);
		}
	}

	auth->token = token;
	if (!token)
		return "FAILURE";

	g_free(request_body);
	g_free(request_body_encoded);

	if(SOUP_IS_MESSAGE(msg))
		g_object_unref(msg);

	return "SUCCESS";
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

GDataFeed *
gdata_google_service_get_feed (GDataService *service, const gchar *feed_url)
{
	GDataFeed *feed = NULL;
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	SoupSession *soup_session;
	SoupMessage *msg;
	gchar *status;

	g_return_val_if_fail(service != NULL, NULL);
	g_return_val_if_fail(GDATA_IS_GOOGLE_SERVICE(service),NULL);

	if (!service_is_authenticated( GDATA_GOOGLE_SERVICE(service) )) {
		status = service_authenticate(GDATA_GOOGLE_SERVICE(service));
		if (g_ascii_strcasecmp(status, "SUCCESS")) {
			return NULL;
		}
	}

	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE( GDATA_GOOGLE_SERVICE(service) );
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;
	msg = NULL;
	msg = soup_message_new(SOUP_METHOD_GET, feed_url);

	soup_message_add_header(msg->request_headers,
			"Authorization", (gchar *)g_strdup_printf("GoogleLogin auth=%s", auth->token));

	soup_session_send_message(soup_session, msg);
	if (msg->response.length) {
		feed = gdata_feed_new_from_xml(msg->response.body, msg->response.length);
	}

	if (SOUP_IS_MESSAGE(msg))
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
GDataEntry *
gdata_google_service_insert_entry (GDataService *service, const gchar *feed_url, GDataEntry *entry)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleServiceAuth *auth;
	GDataEntry *updated_entry;
	SoupSession *soup_session;
	SoupMessage *msg;
	gchar *status;
	gchar *entry_xml;

	g_return_val_if_fail(service != NULL, NULL);
	g_return_val_if_fail(GDATA_IS_GOOGLE_SERVICE(service), NULL);

	if (!service_is_authenticated(GDATA_GOOGLE_SERVICE(service))) {
		status = service_authenticate(GDATA_GOOGLE_SERVICE(service));

		if (g_ascii_strcasecmp(status,"SUCCESS"))
			return NULL;
	}

	entry_xml = gdata_entry_generate_xml (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE(GDATA_GOOGLE_SERVICE(service));
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;

	msg = soup_message_new(SOUP_METHOD_POST, feed_url);
	soup_message_set_http_version (msg, SOUP_HTTP_1_0);

	soup_message_add_header(msg->request_headers,
				"Authorization",
				(gchar *)g_strdup_printf("GoogleLogin auth=%s",
				auth->token));

	soup_message_set_request (msg,
				"application/atom+xml",
				SOUP_BUFFER_USER_OWNED,
				entry_xml,
				strlen(entry_xml));

	soup_session_send_message(soup_session, msg);

	if (!msg->response.length) {
		g_message ("\n %s, %s, Response Length NULL when inserting entry", G_STRLOC, G_STRFUNC);
		return NULL;
	}

	updated_entry = gdata_entry_new_from_xml (msg->response.body);
	if (!GDATA_IS_ENTRY(entry)) {
		g_critical ("\n %s, %s, Error During Insert Entry ", G_STRLOC, G_STRFUNC);
		return NULL;
	}

	if (SOUP_IS_MESSAGE(msg))
		g_object_unref (msg);

	if (entry_xml)
		g_free (entry_xml);

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
void
gdata_google_service_delete_entry (GDataService *service, GDataEntry *entry)
{
	GDataGoogleServiceAuth *auth;
	GDataGoogleServicePrivate *priv;
	SoupSession *soup_session;
	SoupMessage *msg;
	const gchar *entry_edit_url;
	xmlChar *status;

	g_return_if_fail (service !=NULL);
	g_return_if_fail (GDATA_IS_GOOGLE_SERVICE(service));

	if (!service_is_authenticated (GDATA_GOOGLE_SERVICE(service))) {
		status = (xmlChar *)service_authenticate (GDATA_GOOGLE_SERVICE(service));
		if (g_ascii_strcasecmp((gchar *)status, "SUCCESS"))
			return ;
	}

	entry_edit_url = gdata_entry_get_edit_link (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE (GDATA_GOOGLE_SERVICE (service));
	auth = (GDataGoogleServiceAuth *) priv->auth;
	soup_session = 	(SoupSession *)priv->soup_session;

	msg = soup_message_new (SOUP_METHOD_DELETE, entry_edit_url);
	soup_message_add_header (msg->request_headers,
				"Authorization",
				(gchar *)g_strdup_printf ("GoogleLogin auth=%s",
				auth->token));
	soup_session_send_message (soup_session, msg);

	if (SOUP_IS_MESSAGE(msg))
		g_object_unref (msg);
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
void
gdata_google_service_update_entry (GDataService *service, GDataEntry *entry)
{
	GDataGoogleServiceAuth *auth;
	GDataGoogleServicePrivate *priv;
	SoupSession *soup_session;
	SoupMessage *msg;
	gchar *status;
	gchar *entry_xml;
	const gchar *entry_edit_url;

	g_return_if_fail (service !=NULL);
	g_return_if_fail (GDATA_IS_GOOGLE_SERVICE (service));

	if (!service_is_authenticated (GDATA_GOOGLE_SERVICE (service))) {
		status = service_authenticate (GDATA_GOOGLE_SERVICE (service));
		if (g_ascii_strcasecmp (status, "SUCCESS"))
			return;
	}

	entry_xml = gdata_entry_generate_xml (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE (GDATA_GOOGLE_SERVICE (service));
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;

	entry_edit_url = g_strdup (gdata_entry_get_edit_link (entry));
	msg = soup_message_new (SOUP_METHOD_PUT, entry_edit_url);

	if (!msg) {
		g_message ("\n MSG Fails %s", G_STRLOC);
		return;
	}

	soup_message_add_header (msg->request_headers,
				"Authorization",
				(gchar *)g_strdup_printf ("GoogleLogin auth=%s",
				auth->token));
	soup_message_set_request (msg,
			"application/atom+xml",
			SOUP_BUFFER_USER_OWNED,
			entry_xml,
			strlen(entry_xml));

	soup_session_send_message (soup_session, msg);

	if (SOUP_IS_MESSAGE(msg))
		g_object_unref (msg);
	if (entry_xml)
		g_free (entry_xml);
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
void
gdata_google_service_update_entry_with_link (GDataService *service, GDataEntry *entry, gchar *edit_link)
{
	GDataGoogleServiceAuth *auth;
	GDataGoogleServicePrivate *priv;
	SoupSession *soup_session;
	SoupMessage *msg;
	gchar *status;
	gchar *entry_xml;

	g_return_if_fail (service !=NULL);
	g_return_if_fail (GDATA_IS_GOOGLE_SERVICE (service));

	if (!service_is_authenticated (GDATA_GOOGLE_SERVICE(service))) {
		status = service_authenticate (GDATA_GOOGLE_SERVICE(service));
		if (g_ascii_strcasecmp (status, "SUCCESS"))
			return;
	}

	entry_xml = gdata_entry_generate_xml (entry);
	priv = GDATA_GOOGLE_SERVICE_GET_PRIVATE (GDATA_GOOGLE_SERVICE(service));
	auth = (GDataGoogleServiceAuth *)priv->auth;
	soup_session = (SoupSession *)priv->soup_session;

	msg = soup_message_new (SOUP_METHOD_PUT, edit_link);

	if (!msg) {
		g_message ("\n Message Corrupt %s", G_STRLOC);
		return;
	}

	soup_message_add_header (msg->request_headers,
				"Authorization",
				(gchar *)g_strdup_printf ("GoogleLogin auth=%s",
				auth->token));

	soup_message_set_request (msg,
				"application/atom+xml",
				SOUP_BUFFER_USER_OWNED,
				entry_xml,
				strlen(entry_xml));

	soup_session_send_message (soup_session, msg);

	if (SOUP_IS_MESSAGE(msg))
		g_object_unref (msg);
	if (entry_xml)
		g_free (entry_xml);
}

static void gdata_google_service_iface_init(gpointer  g_iface, gpointer iface_data)
{
	GDataServiceIface *iface = (GDataServiceIface *)g_iface;

	iface->set_credentials = gdata_google_service_set_credentials;
	iface->get_feed = gdata_google_service_get_feed;
	iface->insert_entry = gdata_google_service_insert_entry;
	iface->delete_entry = gdata_google_service_delete_entry;
	iface->update_entry = gdata_google_service_update_entry;
	iface->update_entry_with_link = gdata_google_service_update_entry_with_link;
	return;
}

static void gdata_google_service_instance_init(GTypeInstance *instance,
		gpointer      g_class)
{
	GDataGoogleServicePrivate *priv;
	GDataGoogleService *self = (GDataGoogleService *)instance;

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


static void gdata_google_service_class_init(gpointer g_class,
		gpointer g_class_data)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
	GDataGoogleServiceClass *klass = GDATA_GOOGLE_SERVICE_CLASS(g_class);

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


GType  gdata_google_service_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0))
	{
		static const GTypeInfo info =
		{
			sizeof (GDataGoogleServiceClass),
			NULL,   /* base_init */
			NULL,   /* base_finalize */
			(GClassInitFunc) gdata_google_service_class_init, /* class_init */
			NULL,   /* class_finalize */
			NULL,   /* class_data */
			sizeof (GDataGoogleService),
			0,      /* n_preallocs */
			gdata_google_service_instance_init    /* instance_init */
		};

		static const GInterfaceInfo gdata_google_service_iface_info =
		{
			(GInterfaceInitFunc) gdata_google_service_iface_init, /* interface_init */
			NULL,         /* interface_finalize */
			NULL          /* interface_data */
		};

		type = g_type_register_static (G_TYPE_OBJECT,
				"GDataGoogleServiceType",
				&info, 0);

		g_type_add_interface_static (type, GDATA_TYPE_SERVICE,
				&gdata_google_service_iface_info);

	}

	return type;
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

