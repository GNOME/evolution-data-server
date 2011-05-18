/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-session.c : Abstract class for an email session */

/*
 * Authors:
 *  Dan Winship <danw@ximian.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-debug.h"
#include "camel-file-utils.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-string-utils.h"
#include "camel-transport.h"
#include "camel-url.h"
#include "camel-folder.h"
#include "camel-mime-message.h"

#define d(x)

#define CAMEL_SESSION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SESSION, CamelSessionPrivate))

struct _CamelSessionPrivate {
	GMutex *lock;		/* for locking everything basically */
	GMutex *thread_lock;	/* locking threads */

	gint thread_id;
	GHashTable *thread_active;
	GThreadPool *thread_pool;

	GHashTable *thread_msg_op;
	GHashTable *junk_headers;

	gchar *socks_proxy_host;
	gint socks_proxy_port;

	guint check_junk        : 1;
	guint network_available : 1;
	guint online            : 1;
};

enum {
	PROP_0,
	PROP_CHECK_JUNK,
	PROP_NETWORK_AVAILABLE,
	PROP_ONLINE,
	PROP_STORAGE_PATH
};

G_DEFINE_TYPE (CamelSession, camel_session, CAMEL_TYPE_OBJECT)

static void
cs_thread_status (CamelOperation *op,
                  const gchar *what,
                  gint pc,
                  gpointer data)
{
	CamelSessionThreadMsg *msg = data;
	CamelSessionClass *class;

	class = CAMEL_SESSION_GET_CLASS (msg->session);
	g_return_if_fail (class->thread_status != NULL);

	class->thread_status (msg->session, msg, what, pc);
}

static void
session_set_property (GObject *object,
                      guint property_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHECK_JUNK:
			camel_session_set_check_junk (
				CAMEL_SESSION (object),
				g_value_get_boolean (value));
			return;

		case PROP_NETWORK_AVAILABLE:
			camel_session_set_network_available (
				CAMEL_SESSION (object),
				g_value_get_boolean (value));
			return;

		case PROP_ONLINE:
			camel_session_set_online (
				CAMEL_SESSION (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
session_get_property (GObject *object,
                      guint property_id,
                      GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHECK_JUNK:
			g_value_set_boolean (
				value, camel_session_get_check_junk (
				CAMEL_SESSION (object)));
			return;

		case PROP_NETWORK_AVAILABLE:
			g_value_set_boolean (
				value, camel_session_get_network_available (
				CAMEL_SESSION (object)));
			return;

		case PROP_ONLINE:
			g_value_set_boolean (
				value, camel_session_get_online (
				CAMEL_SESSION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
session_finalize (GObject *object)
{
	CamelSession *session = CAMEL_SESSION (object);
	GThreadPool *thread_pool = session->priv->thread_pool;

	g_hash_table_destroy (session->priv->thread_active);

	if (thread_pool != NULL) {
		/* there should be no unprocessed tasks */
		g_assert (g_thread_pool_unprocessed (thread_pool) == 0);
		g_thread_pool_free (thread_pool, FALSE, FALSE);
	}

	g_free (session->storage_path);

	g_mutex_free (session->priv->lock);
	g_mutex_free (session->priv->thread_lock);

	if (session->priv->junk_headers) {
		g_hash_table_remove_all (session->priv->junk_headers);
		g_hash_table_destroy (session->priv->junk_headers);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_session_parent_class)->finalize (object);
}

static CamelService *
session_get_service (CamelSession *session,
                     const gchar *url_string,
                     CamelProviderType type,
                     GError **error)
{
	CamelURL *url;
	CamelProvider *provider;
	CamelService *service;

	url = camel_url_new (url_string, error);
	if (!url)
		return NULL;

	/* We need to look up the provider so we can then lookup
	   the service in the provider's cache */
	provider = camel_provider_get(url->protocol, error);
	if (provider && !provider->object_types[type]) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("No provider available for protocol '%s'"),
			url->protocol);
		provider = NULL;
	}

	if (!provider) {
		camel_url_free (url);
		return NULL;
	}

	/* If the provider doesn't use paths but the URL contains one,
	 * ignore it.
	 */
	if (url->path && !CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_PATH))
		camel_url_set_path (url, NULL);

	/* Now look up the service in the provider's cache */
	service = camel_object_bag_reserve(provider->service_cache[type], url);
	if (service == NULL) {
		service = g_object_new (provider->object_types[type], NULL);
		if (!camel_service_construct (service, session, provider, url, error)) {
			g_object_unref (service);
			service = NULL;
			camel_object_bag_abort(provider->service_cache[type], url);
		} else {
			camel_object_bag_add(provider->service_cache[type], url, service);
		}
	}

	camel_url_free (url);

	return service;
}

static gchar *
session_get_storage_path (CamelSession *session,
                          CamelService *service,
                          GError **error)
{
	gchar *service_path;
	gchar *storage_path;

	service_path = camel_service_get_path (service);
	storage_path = g_build_filename (
		session->storage_path, service_path, NULL);
	g_free (service_path);

	if (g_access (storage_path, F_OK) == 0)
		return storage_path;

	if (g_mkdir_with_parents (storage_path, S_IRWXU) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not create directory %s:\n%s"),
			storage_path, g_strerror (errno));
		g_free (storage_path);
		storage_path = NULL;
	}

	return storage_path;
}

static gpointer
session_thread_msg_new (CamelSession *session,
                        CamelSessionThreadOps *ops,
                        guint size)
{
	CamelSessionThreadMsg *m;

	m = g_malloc0(size);
	m->ops = ops;
	m->session = g_object_ref (session);
	m->op = camel_operation_new(cs_thread_status, m);
	camel_session_lock (session, CAMEL_SESSION_THREAD_LOCK);
	m->id = session->priv->thread_id++;
	g_hash_table_insert(session->priv->thread_active, GINT_TO_POINTER(m->id), m);
	camel_session_unlock (session, CAMEL_SESSION_THREAD_LOCK);

	return m;
}

static void
session_thread_msg_free (CamelSession *session,
                         CamelSessionThreadMsg *msg)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (msg != NULL && msg->ops != NULL);

	d(printf("free message %p session %p\n", msg, session));

	camel_session_lock (session, CAMEL_SESSION_THREAD_LOCK);
	g_hash_table_remove(session->priv->thread_active, GINT_TO_POINTER(msg->id));
	camel_session_unlock (session, CAMEL_SESSION_THREAD_LOCK);

	d(printf("free msg, ops->free = %p\n", msg->ops->free));

	if (msg->ops->free)
		msg->ops->free(session, msg);
	if (msg->op)
		camel_operation_unref(msg->op);
	g_clear_error (&msg->error);
	g_object_unref (msg->session);
	g_free(msg);
}

static void
session_thread_proxy (CamelSessionThreadMsg *msg,
                      CamelSession *session)
{
	if (msg->ops->receive) {
		CamelOperation *oldop;

		oldop = camel_operation_register(msg->op);
		msg->ops->receive(session, msg);
		camel_operation_register(oldop);
	}

	camel_session_thread_msg_free(session, msg);
}

static gint
session_thread_queue (CamelSession *session,
                      CamelSessionThreadMsg *msg,
                      gint flags)
{
	GThreadPool *thread_pool;
	gint id;

	camel_session_lock (session, CAMEL_SESSION_THREAD_LOCK);
	thread_pool = session->priv->thread_pool;
	if (thread_pool == NULL) {
		thread_pool = g_thread_pool_new (
			(GFunc) session_thread_proxy,
			session, 1, FALSE, NULL);
		session->priv->thread_pool = thread_pool;
	}
	camel_session_unlock (session, CAMEL_SESSION_THREAD_LOCK);

	id = msg->id;
	g_thread_pool_push(thread_pool, msg, NULL);

	return id;
}

static void
session_thread_wait (CamelSession *session,
                     gint id)
{
	gint wait;

	/* we just busy wait, only other alternative is to setup a reply port? */
	do {
		camel_session_lock (session, CAMEL_SESSION_THREAD_LOCK);
		wait = g_hash_table_lookup(session->priv->thread_active, GINT_TO_POINTER(id)) != NULL;
		camel_session_unlock (session, CAMEL_SESSION_THREAD_LOCK);
		if (wait) {
			g_usleep(20000);
		}
	} while (wait);
}

static void
session_thread_status (CamelSession *session,
                       CamelSessionThreadMsg *msg,
                       const gchar *text,
                       gint pc)
{
}

static void
camel_session_class_init (CamelSessionClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelSessionPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = session_set_property;
	object_class->get_property = session_get_property;
	object_class->finalize = session_finalize;

	class->get_service = session_get_service;
	class->get_storage_path = session_get_storage_path;
	class->thread_msg_new = session_thread_msg_new;
	class->thread_msg_free = session_thread_msg_free;
	class->thread_queue = session_thread_queue;
	class->thread_wait = session_thread_wait;
	class->thread_status = session_thread_status;

	g_object_class_install_property (
		object_class,
		PROP_CHECK_JUNK,
		g_param_spec_boolean (
			"check-junk",
			"Check Junk",
			"Check incoming messages for junk",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_NETWORK_AVAILABLE,
		g_param_spec_boolean (
			"network-available",
			"Network Available",
			"Whether the network is available",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			"Online",
			"Whether the shell is online",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));
}

static void
camel_session_init (CamelSession *session)
{
	session->priv = CAMEL_SESSION_GET_PRIVATE (session);

	session->priv->lock = g_mutex_new();
	session->priv->thread_lock = g_mutex_new();
	session->priv->thread_id = 1;
	session->priv->thread_active = g_hash_table_new(NULL, NULL);
	session->priv->thread_pool = NULL;
	session->priv->junk_headers = NULL;
}

/**
 * camel_session_construct:
 * @session: a #CamelSession object to construct
 * @storage_path: path to a directory the session can use for
 * persistent storage. (This directory must already exist.)
 *
 * Constructs @session.
 **/
void
camel_session_construct (CamelSession *session, const gchar *storage_path)
{
	session->storage_path = g_strdup (storage_path);
}

/**
 * camel_session_get_service:
 * @session: a #CamelSession object
 * @url_string: a #CamelURL describing the service to get
 * @type: the provider type (#CAMEL_PROVIDER_STORE or
 * #CAMEL_PROVIDER_TRANSPORT) to get, since some URLs may be able
 * to specify either type.
 * @error: return location for a #GError, or %NULL
 *
 * This resolves a #CamelURL into a #CamelService, including loading the
 * provider library for that service if it has not already been loaded.
 *
 * Services are cached, and asking for "the same" @url_string multiple
 * times will return the same CamelService (with its reference count
 * incremented by one each time). What constitutes "the same" URL
 * depends in part on the provider.
 *
 * Returns: the requested #CamelService, or %NULL
 **/
CamelService *
camel_session_get_service (CamelSession *session,
                           const gchar *url_string,
                           CamelProviderType type,
                           GError **error)
{
	CamelSessionClass *class;
	CamelService *service;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (url_string != NULL, NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->get_service != NULL, NULL);

	camel_session_lock (session, CAMEL_SESSION_SESSION_LOCK);

	service = class->get_service (session, url_string, type, error);
	CAMEL_CHECK_GERROR (session, get_service, service != NULL, error);

	camel_session_unlock (session, CAMEL_SESSION_SESSION_LOCK);

	return service;
}

/**
 * camel_session_get_service_connected:
 * @session: a #CamelSession object
 * @url_string: a #CamelURL describing the service to get
 * @type: the provider type
 * @error: return location for a #GError, or %NULL
 *
 * This works like #camel_session_get_service, but also ensures that
 * the returned service will have been successfully connected (via
 * #camel_service_connect.)
 *
 * Returns: the requested #CamelService, or %NULL
 **/
CamelService *
camel_session_get_service_connected (CamelSession *session,
                                     const gchar *url_string,
                                     CamelProviderType type,
                                     GError **error)
{
	CamelService *svc;

	svc = camel_session_get_service (session, url_string, type, error);
	if (svc == NULL)
		return NULL;

	if (svc->status != CAMEL_SERVICE_CONNECTED) {
		if (!camel_service_connect (svc, error)) {
			g_object_unref (svc);
			return NULL;
		}
	}

	return svc;
}

/**
 * camel_session_get_storage_path:
 * @session: a #CamelSession object
 * @service: a #CamelService
 * @error: return location for a #GError, or %NULL
 *
 * This returns the path to a directory which the service can use for
 * its own purposes. Data stored there will remain between Evolution
 * sessions. No code outside of that service should ever touch the
 * files in this directory. If the directory does not exist, it will
 * be created.
 *
 * Returns: the path (which the caller must free), or %NULL if an error
 * occurs.
 **/
gchar *
camel_session_get_storage_path (CamelSession *session,
                                CamelService *service,
                                GError **error)
{
	CamelSessionClass *class;
	gchar *storage_path;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->get_storage_path != NULL, NULL);

	storage_path = class->get_storage_path (session, service, error);
	CAMEL_CHECK_GERROR (session, get_storage_path, storage_path != NULL, error);

	return storage_path;
}

/**
 * camel_session_get_password:
 * @session: a #CamelSession object
 * @service: the #CamelService this query is being made by
 * @domain: domain of password request.  May be null to use the default.
 * @prompt: prompt to provide to user
 * @item: an identifier, unique within this service, for the information
 * @flags: %CAMEL_SESSION_PASSWORD_REPROMPT, the prompt should force a reprompt
 * %CAMEL_SESSION_PASSWORD_SECRET, whether the password is secret
 * %CAMEL_SESSION_PASSWORD_STATIC, the password is remembered externally
 * @error: return location for a #GError, or %NULL
 *
 * This function is used by a #CamelService to ask the application and
 * the user for a password or other authentication data.
 *
 * @service and @item together uniquely identify the piece of data the
 * caller is concerned with.
 *
 * @prompt is a question to ask the user (if the application doesn't
 * already have the answer cached). If %CAMEL_SESSION_PASSWORD_SECRET
 * is set, the user's input will not be echoed back.
 *
 * If %CAMEL_SESSION_PASSWORD_STATIC is set, it means the password returned
 * will be stored statically by the caller automatically, for the current
 * session.
 *
 * The authenticator should set @error to %G_IO_ERROR_CANCELLED if
 * the user did not provide the information. The caller must g_free()
 * the information returned when it is done with it.
 *
 * Returns: the authentication information or %NULL
 **/
gchar *
camel_session_get_password (CamelSession *session,
                            CamelService *service,
                            const gchar *domain,
                            const gchar *prompt,
                            const gchar *item,
                            guint32 flags,
                            GError **error)
{
	CamelSessionClass *class;
	gchar *password;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (prompt != NULL, NULL);
	g_return_val_if_fail (item != NULL, NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->get_password != NULL, NULL);

	password = class->get_password (
		session, service, domain, prompt, item, flags, error);
	CAMEL_CHECK_GERROR (session, get_password, password != NULL, error);

	return password;
}

/**
 * camel_session_forget_password:
 * @session: a #CamelSession object
 * @service: the #CamelService rejecting the password
 * @item: an identifier, unique within this service, for the information
 * @error: return location for a #GError, or %NULL
 *
 * This function is used by a #CamelService to tell the application
 * that the authentication information it provided via
 * #camel_session_get_password was rejected by the service. If the
 * application was caching this information, it should stop,
 * and if the service asks for it again, it should ask the user.
 *
 * @service and @item identify the rejected authentication information,
 * as with #camel_session_get_password.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_session_forget_password (CamelSession *session,
                               CamelService *service,
                               const gchar *domain,
                               const gchar *item,
                               GError **error)
{
	CamelSessionClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->forget_password, FALSE);

	success = class->forget_password (session, service, domain, item, error);
	CAMEL_CHECK_GERROR (session, forget_password, success, error);

	return success;
}

/**
 * camel_session_alert_user:
 * @session: a #CamelSession object
 * @type: the type of alert (info, warning, or error)
 * @prompt: the message for the user
 * @cancel: whether or not to provide a "Cancel" option in addition to
 * an "OK" option.
 *
 * Presents the given @prompt to the user, in the style indicated by
 * @type. If @cancel is %TRUE, the user will be able to accept or
 * cancel. Otherwise, the message is purely informational.
 *
 * Returns: %TRUE if the user accepts, %FALSE if they cancel.
 */
gboolean
camel_session_alert_user (CamelSession *session,
                          CamelSessionAlertType type,
                          const gchar *prompt,
                          gboolean cancel)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (prompt != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->alert_user != NULL, FALSE);

	return class->alert_user (session, type, prompt, cancel);
}

/**
 * camel_session_lookup_addressbook:
 *
 * Since: 2.22
 **/
gboolean
camel_session_lookup_addressbook (CamelSession *session,
                                  const gchar *name)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->lookup_addressbook != NULL, FALSE);

	return class->lookup_addressbook (session, name);
}

/**
 * camel_session_build_password_prompt:
 * @type: account type (e.g. "IMAP")
 * @user: user name for the account
 * @host: host name for the account
 *
 * Constructs a localized password prompt from @type, @user and @host,
 * suitable for passing to camel_session_get_password().  The resulting
 * string contains markup tags.  Use g_free() to free it.
 *
 * Returns: a newly-allocated password prompt string
 *
 * Since: 2.22
 **/
gchar *
camel_session_build_password_prompt (const gchar *type,
                                     const gchar *user,
                                     const gchar *host)
{
	/* gchar *user_markup;*/
	/* gchar *host_markup;*/
	gchar *prompt;

	g_return_val_if_fail (type != NULL, NULL);
	g_return_val_if_fail (user != NULL, NULL);
	g_return_val_if_fail (host != NULL, NULL);

	/* Add bold tags to the "user" and "host" strings.  We use
	 * separate strings here to avoid putting markup tags in the
	 * translatable string below. */
	/* user_markup = g_markup_printf_escaped ("<b>%s</b>", user);
	host_markup = g_markup_printf_escaped ("<b>%s</b>", host); */

	/* Translators: The first argument is the account type
	 * (e.g. "IMAP"), the second is the user name, and the
	 * third is the host name. */
	prompt = g_strdup_printf (
		_("Please enter the %s password for %s on host %s."),
		type, user , host );

	/* g_free (user_markup);
	g_free (host_markup); */

	return prompt;
}

/**
 * camel_session_get_online:
 * @session: a #CamelSession
 *
 * Returns: whether or not @session is online
 **/
gboolean
camel_session_get_online (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	return session->priv->online;
}

/**
 * camel_session_set_online:
 * @session: a #CamelSession
 * @online: whether or not the session should be online
 *
 * Sets the online status of @session to @online.
 **/
void
camel_session_set_online (CamelSession *session,
                          gboolean online)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	session->priv->online = online;

	g_object_notify (G_OBJECT (session), "online");
}

/**
 * camel_session_get_filter_driver:
 * @session: a #CamelSession object
 * @type: the type of filter (eg, "incoming")
 * @error: return location for a #GError, or %NULL
 *
 * Returns: a filter driver, loaded with applicable rules
 **/
CamelFilterDriver *
camel_session_get_filter_driver (CamelSession *session,
                                 const gchar *type,
                                 GError **error)
{
	CamelSessionClass *class;
	CamelFilterDriver *driver;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (type != NULL, NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->get_filter_driver != NULL, NULL);

	driver = class->get_filter_driver (session, type, error);
	CAMEL_CHECK_GERROR (session, get_filter_driver, driver != NULL, error);

	return driver;
}

/**
 * camel_session_thread_msg_new:
 * @session: a #CamelSession object
 * @ops: thread operations
 * @size: number of bytes
 *
 * Create a new thread message, using ops as the receive/reply/free
 * ops, of @size bytes.
 *
 * @ops points to the operations used to recieve/process and finally
 * free the message.
 *
 * Returns: a new #CamelSessionThreadMsg
 **/
gpointer
camel_session_thread_msg_new (CamelSession *session,
                              CamelSessionThreadOps *ops,
                              guint size)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (ops != NULL, NULL);
	g_return_val_if_fail (size >= sizeof (CamelSessionThreadMsg), NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->thread_msg_new != NULL, NULL);

	return class->thread_msg_new (session, ops, size);
}

/**
 * camel_session_thread_msg_free:
 * @session: a #CamelSession object
 * @msg: a #CamelSessionThreadMsg
 *
 * Free a @msg.  Note that the message must have been allocated using
 * msg_new, and must nto have been submitted to any queue function.
 **/
void
camel_session_thread_msg_free (CamelSession *session,
                               CamelSessionThreadMsg *msg)
{
	CamelSessionClass *class;

	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (msg != NULL && msg->ops != NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_if_fail (class->thread_msg_free != NULL);

	class->thread_msg_free (session, msg);
}

/**
 * camel_session_thread_queue:
 * @session: a #CamelSession object
 * @msg: a #CamelSessionThreadMsg
 * @flags: queue type flags, currently 0.
 *
 * Queue a thread message in another thread for processing.
 * The operation should be (but needn't) run in a queued manner
 * with other operations queued in this manner.
 *
 * Returns: the id of the operation queued
 **/
gint
camel_session_thread_queue (CamelSession *session,
                            CamelSessionThreadMsg *msg,
                            gint flags)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), -1);
	g_return_val_if_fail (msg != NULL, -1);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->thread_queue != NULL, -1);

	return class->thread_queue (session, msg, flags);
}

/**
 * camel_session_thread_wait:
 * @session: a #CamelSession object
 * @id: id of the operation to wait on
 *
 * Wait on an operation to complete (by id).
 **/
void
camel_session_thread_wait (CamelSession *session,
                           gint id)
{
	CamelSessionClass *class;

	g_return_if_fail (CAMEL_IS_SESSION (session));

	if (id == -1)
		return;

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_if_fail (class->thread_wait != NULL);

	class->thread_wait (session, id);
}

/**
 * camel_session_get_check_junk:
 * @session: a #CamelSession
 *
 * Do we have to check incoming messages to be junk?
 *
 * Returns: whether or not we are checking incoming messages for junk
 **/
gboolean
camel_session_get_check_junk (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	return session->priv->check_junk;
}

/**
 * camel_session_set_check_junk:
 * @session: a #CamelSession
 * @check_junk: whether to check incoming messages for junk
 *
 * Set check_junk flag, if set, incoming mail will be checked for being junk.
 **/
void
camel_session_set_check_junk (CamelSession *session,
                              gboolean check_junk)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	session->priv->check_junk = check_junk;

	g_object_notify (G_OBJECT (session), "check-junk");
}

/**
 * camel_session_get_network_available:
 * @session: a #CamelSession
 *
 * Since: 2.32
 **/
gboolean
camel_session_get_network_available (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	return session->priv->network_available;
}

/**
 * camel_session_set_network_available:
 * @session: a #CamelSession
 * @network_available: whether a network is available
 *
 * Since: 2.32
 **/
void
camel_session_set_network_available (CamelSession *session,
                                     gboolean network_available)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	session->priv->network_available = network_available;

	g_object_notify (G_OBJECT (session), "network-available");
}

/**
 * camel_session_set_junk_headers:
 *
 * Since: 2.22
 **/
void
camel_session_set_junk_headers (CamelSession *session,
                                const gchar **headers,
                                const gchar **values,
                                gint len)
{
	gint i;

	g_return_if_fail (CAMEL_IS_SESSION (session));

	if (session->priv->junk_headers) {
		g_hash_table_remove_all (session->priv->junk_headers);
		g_hash_table_destroy (session->priv->junk_headers);
	}

	session->priv->junk_headers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	for (i=0; i<len; i++) {
		g_hash_table_insert (session->priv->junk_headers, g_strdup (headers[i]), g_strdup(values[i]));
	}
}

/**
 * camel_session_get_junk_headers:
 *
 * Since: 2.22
 **/
const GHashTable *
camel_session_get_junk_headers (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	return session->priv->junk_headers;
}

/**
 * camel_session_forward_to:
 * Forwards message to some address(es) in a given type. The meaning of the forward_type defines session itself.
 * @session #CameSession.
 * @folder #CamelFolder where is @message located.
 * @message Message to forward.
 * @address Where forward to.
 * @ex Exception.
 *
 * Since: 2.26
 **/
gboolean
camel_session_forward_to (CamelSession *session,
                          CamelFolder *folder,
                          CamelMimeMessage *message,
                          const gchar *address,
                          GError **error)
{
	CamelSessionClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	g_return_val_if_fail (address != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->forward_to != NULL, FALSE);

	success = class->forward_to (session, folder, message, address, error);
	CAMEL_CHECK_GERROR (session, forward_to, success, error);

	return success;
}

/**
 * camel_session_lock:
 * @session: a #CamelSession
 * @lock: lock type to lock
 *
 * Locks #session's #lock. Unlock it with camel_session_unlock().
 *
 * Since: 2.32
 **/
void
camel_session_lock (CamelSession *session,
                    CamelSessionLock lock)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	switch (lock) {
		case CAMEL_SESSION_SESSION_LOCK:
			g_mutex_lock (session->priv->lock);
			break;
		case CAMEL_SESSION_THREAD_LOCK:
			g_mutex_lock (session->priv->thread_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_session_unlock:
 * @session: a #CamelSession
 * @lock: lock type to unlock
 *
 * Unlocks #session's #lock, previously locked with camel_session_lock().
 *
 * Since: 2.32
 **/
void
camel_session_unlock (CamelSession *session,
                      CamelSessionLock lock)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	switch (lock) {
		case CAMEL_SESSION_SESSION_LOCK:
			g_mutex_unlock (session->priv->lock);
			break;
		case CAMEL_SESSION_THREAD_LOCK:
			g_mutex_unlock (session->priv->thread_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_session_set_socks_proxy:
 * @session: A #CamelSession
 * @socks_host: Hostname of the SOCKS proxy, or #NULL for none.
 * @socks_port: Port number of the SOCKS proxy
 *
 * Sets a SOCKS proxy that will be used throughout the @session for
 * TCP connections.
 *
 * Since: 2.32
 */
void
camel_session_set_socks_proxy (CamelSession *session, const gchar *socks_host, gint socks_port)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	if (session->priv->socks_proxy_host)
		g_free (session->priv->socks_proxy_host);

	if (socks_host && socks_host[0] != '\0') {
		session->priv->socks_proxy_host = g_strdup (socks_host);
		session->priv->socks_proxy_port = socks_port;
	} else {
		session->priv->socks_proxy_host = NULL;
		session->priv->socks_proxy_port = 0;
	}
}

/**
 * camel_session_get_socks_proxy:
 * @session: A #CamelSession
 * @host_ret: Location to return the SOCKS proxy hostname
 * @port_ret: Location to return the SOCKS proxy port
 *
 * Queries the SOCKS proxy that is configured for a @session.  This will
 * put #NULL in @hosts_ret if there is no proxy configured.
 *
 * Since: 2.32
 */
void
camel_session_get_socks_proxy (CamelSession *session, gchar **host_ret, gint *port_ret)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (host_ret != NULL);
	g_return_if_fail (port_ret != NULL);

	*host_ret = g_strdup (session->priv->socks_proxy_host);
	*port_ret = session->priv->socks_proxy_port;
}

