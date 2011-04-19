/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-store.c : class for a pop3 store */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>

#include <glib/gi18n-lib.h>

#include "camel-pop3-folder.h"
#include "camel-pop3-store.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

/* Specified in RFC 1939 */
#define POP3_PORT  110
#define POP3S_PORT 995

/* defines the length of the server error message we can display in the error dialog */
#define POP3_ERROR_SIZE_LIMIT 60

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#ifdef CAMEL_HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static struct {
	const gchar *value;
	const gchar *serv;
	gint fallback_port;
	gint mode;
} ssl_options[] = {
	{ "",              "pop3s", POP3S_PORT, MODE_SSL   },  /* really old (1.x) */
	{ "always",        "pop3s", POP3S_PORT, MODE_SSL   },
	{ "when-possible", "pop3",  POP3_PORT,  MODE_TLS   },
	{ "never",         "pop3",  POP3_PORT,  MODE_CLEAR },
	{ NULL,            "pop3",  POP3_PORT,  MODE_CLEAR },
};

extern CamelServiceAuthType camel_pop3_password_authtype;
extern CamelServiceAuthType camel_pop3_apop_authtype;

G_DEFINE_TYPE (CamelPOP3Store, camel_pop3_store, CAMEL_TYPE_STORE)

/* returns error message with ': ' as prefix */
static gchar *
get_valid_utf8_error (const gchar *text)
{
	gchar *tmp = camel_utf8_make_valid (text);
	gchar *ret = NULL;

	/*TODO If the error message > size limit log it somewhere */
	if (!tmp || g_utf8_strlen (tmp, -1) > POP3_ERROR_SIZE_LIMIT) {
		g_free (tmp);
		return NULL;
	}

	/* Translators: This is the separator between an error and an explanation */
	ret = g_strconcat (_(": "), tmp, NULL);

	g_free (tmp);
	return ret;
}

static gboolean
connect_to_server (CamelService *service,
		   const gchar *host, const gchar *serv, gint fallback_port,
                   gint ssl_mode,
                   GError **error)
{
	CamelPOP3Store *store = CAMEL_POP3_STORE (service);
	CamelSession *session;
	gchar *socks_host;
	gint socks_port;
	CamelStream *tcp_stream;
	CamelPOP3Command *pc;
	guint32 flags = 0;
	gint clean_quit = TRUE;
	gint ret;
	const gchar *delete_days;

	if (ssl_mode != MODE_CLEAR) {
#ifdef CAMEL_HAVE_SSL
		if (ssl_mode == MODE_TLS) {
			tcp_stream = camel_tcp_stream_ssl_new_raw (service->session, service->url->host, STARTTLS_FLAGS);
		} else {
			tcp_stream = camel_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
		}
#else
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Could not connect to %s: %s"),
			service->url->host, _("SSL unavailable"));

		return FALSE;
#endif /* CAMEL_HAVE_SSL */
	} else
		tcp_stream = camel_tcp_stream_raw_new ();

	session = camel_service_get_session (service);
	camel_session_get_socks_proxy (session, &socks_host, &socks_port);

	if (socks_host) {
		camel_tcp_stream_set_socks_proxy ((CamelTcpStream *) tcp_stream, socks_host, socks_port);
		g_free (socks_host);
	}

	if (camel_tcp_stream_connect ((CamelTcpStream *) tcp_stream, host, serv, fallback_port, error) == -1) {
		g_object_unref (tcp_stream);
		return FALSE;
	}

	/* parent class connect initialization */
	if (CAMEL_SERVICE_CLASS (camel_pop3_store_parent_class)->connect (service, error) == FALSE) {
		g_object_unref (tcp_stream);
		return FALSE;
	}

	if (camel_url_get_param (service->url, "disable_extensions"))
		flags |= CAMEL_POP3_ENGINE_DISABLE_EXTENSIONS;

	if ((delete_days = (gchar *) camel_url_get_param(service->url,"delete_after")))
		store->delete_after =  atoi(delete_days);

	if (!(store->engine = camel_pop3_engine_new (tcp_stream, flags))) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to read a valid greeting from POP server %s"),
			service->url->host);
		g_object_unref (tcp_stream);
		return FALSE;
	}

	if (ssl_mode != MODE_TLS) {
		g_object_unref (tcp_stream);
		return TRUE;
	}

#ifdef CAMEL_HAVE_SSL
	/* as soon as we send a STLS command, all hope is lost of a clean QUIT if problems arise */
	clean_quit = FALSE;

	if (!(store->engine->capa & CAMEL_POP3_CAP_STLS)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to POP server %s in secure mode: %s"),
			service->url->host, _("STLS not supported by server"));
		goto stls_exception;
	}

	pc = camel_pop3_engine_command_new (store->engine, 0, NULL, NULL, "STLS\r\n");
	while (camel_pop3_engine_iterate (store->engine, NULL) > 0)
		;

	ret = pc->state == CAMEL_POP3_COMMAND_OK;
	camel_pop3_engine_command_free (store->engine, pc);

	if (ret == FALSE) {
		gchar *tmp;

		tmp = get_valid_utf8_error ((gchar *) store->engine->line);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			/* Translators: Last %s is an optional explanation beginning with ": " separator */
			_("Failed to connect to POP server %s in secure mode%s"),
			service->url->host, (tmp != NULL) ? tmp : "");
		g_free (tmp);
		goto stls_exception;
	}

	/* Okay, now toggle SSL/TLS mode */
	ret = camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream));

	if (ret == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to POP server %s in secure mode: %s"),
			service->url->host, _("TLS negotiations failed"));
		goto stls_exception;
	}
#else
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Failed to connect to POP server %s in secure mode: %s"),
		service->url->host, _("TLS is not available in this build"));
	goto stls_exception;
#endif /* CAMEL_HAVE_SSL */

	g_object_unref (tcp_stream);

	/* rfc2595, section 4 states that after a successful STLS
	   command, the client MUST discard prior CAPA responses */
	camel_pop3_engine_reget_capabilities (store->engine);

	return TRUE;

 stls_exception:
	if (clean_quit) {
		/* try to disconnect cleanly */
		pc = camel_pop3_engine_command_new (store->engine, 0, NULL, NULL, "QUIT\r\n");
		while (camel_pop3_engine_iterate (store->engine, NULL) > 0)
			;
		camel_pop3_engine_command_free (store->engine, pc);
	}

	g_object_unref (CAMEL_OBJECT (store->engine));
	g_object_unref (CAMEL_OBJECT (tcp_stream));
	store->engine = NULL;

	return FALSE;
}

static gboolean
connect_to_server_wrapper (CamelService *service,
                           GError **error)
{
	const gchar *ssl_mode;
	gint mode, i;
	gchar *serv;
	gint fallback_port;

	if ((ssl_mode = camel_url_get_param (service->url, "use_ssl"))) {
		for (i = 0; ssl_options[i].value; i++)
			if (!strcmp (ssl_options[i].value, ssl_mode))
				break;
		mode = ssl_options[i].mode;
		serv = (gchar *) ssl_options[i].serv;
		fallback_port = ssl_options[i].fallback_port;
	} else {
		mode = MODE_CLEAR;
		serv = (gchar *) "pop3";
		fallback_port = POP3S_PORT;
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
		fallback_port = 0;
	}

	return connect_to_server (service, service->url->host, serv, fallback_port, mode, error);
}

static gint
try_sasl (CamelPOP3Store *store,
          const gchar *mech,
          GError **error)
{
	CamelPOP3Stream *stream = store->engine->stream;
	guchar *line, *resp;
	CamelSasl *sasl;
	guint len;
	gint ret;

	sasl = camel_sasl_new("pop", mech, (CamelService *)store);
	if (sasl == NULL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Unable to connect to POP server %s: "
			  "No support for requested authentication mechanism."),
			CAMEL_SERVICE (store)->url->host);
		return -1;
	}

	if (camel_stream_printf((CamelStream *)stream, "AUTH %s\r\n", mech) == -1)
		goto ioerror;

	while (1) {
		if (camel_pop3_stream_line(stream, &line, &len) == -1)
			goto ioerror;
		if (strncmp((gchar *) line, "+OK", 3) == 0)
			break;
		if (strncmp((gchar *) line, "-ERR", 4) == 0) {
			gchar *tmp;

			tmp = get_valid_utf8_error (
				(gchar *) store->engine->line);
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				/* Translators: Last %s is an optional explanation beginning with ": " separator */
				_("SASL '%s' Login failed for POP server %s%s"),
				mech, CAMEL_SERVICE (store)->url->host,
				(tmp != NULL) ? tmp : "");
			g_free (tmp);

			goto done;
		}
		/* If we dont get continuation, or the sasl object's run out of work, or we dont get a challenge,
		   its a protocol error, so fail, and try reset the server */
		if (strncmp((gchar *) line, "+ ", 2) != 0
		    || camel_sasl_get_authenticated(sasl)
		    || (resp = (guchar *) camel_sasl_challenge_base64(sasl, (const gchar *) line+2, error)) == NULL) {
			camel_stream_printf((CamelStream *)stream, "*\r\n");
			camel_pop3_stream_line(stream, &line, &len);
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Cannot login to POP server %s: "
				  "SASL Protocol error"),
				CAMEL_SERVICE (store)->url->host);
			goto done;
		}

		ret = camel_stream_printf((CamelStream *)stream, "%s\r\n", resp);
		g_free(resp);
		if (ret == -1)
			goto ioerror;

	}
	g_object_unref (sasl);
	return 0;

 ioerror:
	g_prefix_error (
		error, _("Failed to authenticate on POP server %s: "),
		CAMEL_SERVICE (store)->url->host);

 done:
	g_object_unref (sasl);
	return -1;
}

static gint
pop3_try_authenticate (CamelService *service,
                       gboolean reprompt,
                       const gchar *errmsg,
                       GError **error)
{
	CamelPOP3Store *store = (CamelPOP3Store *)service;
	CamelPOP3Command *pcu = NULL, *pcp = NULL;
	gint status;

	/* override, testing only */
	/*printf("Forcing authmech to 'login'\n");
	service->url->authmech = g_strdup("LOGIN");*/

	if (!service->url->passwd) {
		gchar *base_prompt;
		gchar *full_prompt;
		guint32 flags = CAMEL_SESSION_PASSWORD_SECRET;

		if (reprompt)
			flags |= CAMEL_SESSION_PASSWORD_REPROMPT;

		base_prompt = camel_session_build_password_prompt (
			"POP", service->url->user, service->url->host);

		if (errmsg != NULL)
			full_prompt = g_strconcat (errmsg, base_prompt, NULL);
		else
			full_prompt = g_strdup (base_prompt);

		service->url->passwd = camel_session_get_password (
			camel_service_get_session (service), service,
			NULL, full_prompt, "password", flags, error);

		g_free (base_prompt);
		g_free (full_prompt);
		if (!service->url->passwd) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_NEED_PASSWORD,
				_("Need password for authentication"));
			return -1;
		}
	}

	if (!service->url->authmech) {
		/* pop engine will take care of pipelining ability */
		pcu = camel_pop3_engine_command_new(store->engine, 0, NULL, NULL, "USER %s\r\n", service->url->user);
		pcp = camel_pop3_engine_command_new(store->engine, 0, NULL, NULL, "PASS %s\r\n", service->url->passwd);
	} else if (strcmp(service->url->authmech, "+APOP") == 0 && store->engine->apop) {
		gchar *secret, *md5asc, *d;

		d = store->engine->apop;

		while (*d != '\0') {
			if (!isascii((gint)*d)) {

				/* README for Translators: The string APOP should not be translated */
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_URL_INVALID,
					_("Unable to connect to POP server %s:	"
					  "Invalid APOP ID received. Impersonation "
					  "attack suspected. Please contact your admin."),
					CAMEL_SERVICE (store)->url->host);

				return 0;
			}
			d++;
		}

		secret = g_alloca(strlen(store->engine->apop)+strlen(service->url->passwd)+1);
		sprintf(secret, "%s%s",  store->engine->apop, service->url->passwd);
		md5asc = g_compute_checksum_for_string (G_CHECKSUM_MD5, secret, -1);
		pcp = camel_pop3_engine_command_new(store->engine, 0, NULL, NULL, "APOP %s %s\r\n",
						    service->url->user, md5asc);
		g_free (md5asc);
	} else {
		CamelServiceAuthType *auth;
		GList *l;

		l = store->engine->auth;
		while (l) {
			auth = l->data;
			if (strcmp(auth->authproto, service->url->authmech) == 0)
				return try_sasl (store, service->url->authmech, error);
			l = l->next;
		}

		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Unable to connect to POP server %s: "
			  "No support for requested authentication mechanism."),
			CAMEL_SERVICE (store)->url->host);
		return 0;
	}

	while ((status = camel_pop3_engine_iterate(store->engine, pcp)) > 0)
		;

	if (status == -1) {
		if (errno == EINTR) {
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				_("Cancelled"));
		} else {
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Unable to connect to POP server %s.\n"
				  "Error sending password: %s"),
				CAMEL_SERVICE (store)->url->host, errno ?
				g_strerror (errno) : _("Unknown error"));
		}
	} else if (pcu && pcu->state != CAMEL_POP3_COMMAND_OK) {
		gchar *tmp;

		tmp = get_valid_utf8_error ((gchar *) store->engine->line);
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			/* Translators: Last %s is an optional explanation beginning with ": " separator */
			_("Unable to connect to POP server %s.\n"
			  "Error sending username%s"),
			CAMEL_SERVICE (store)->url->host,
			(tmp != NULL) ? tmp : "");
		g_free (tmp);
	} else if (pcp->state != CAMEL_POP3_COMMAND_OK) {
		gchar *tmp;

		tmp = get_valid_utf8_error ((gchar *) store->engine->line);
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			/* Translators: Last %s is an optional explanation beginning with ": " separator */
			_("Unable to connect to POP server %s.\n"
			  "Error sending password%s"),
			CAMEL_SERVICE (store)->url->host,
			(tmp != NULL) ? tmp : "");
		g_free (tmp);
	}

	camel_pop3_engine_command_free (store->engine, pcp);

	if (pcu)
		camel_pop3_engine_command_free(store->engine, pcu);

	return status;
}

static void
pop3_store_finalize (GObject *object)
{
	CamelPOP3Store *pop3_store = CAMEL_POP3_STORE (object);

	/* force disconnect so we dont have it run later, after we've cleaned up some stuff */
	/* SIGH */

	camel_service_disconnect((CamelService *)pop3_store, TRUE, NULL);

	if (pop3_store->engine)
		g_object_unref (pop3_store->engine);
	if (pop3_store->cache)
		g_object_unref (pop3_store->cache);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_pop3_store_parent_class)->finalize (object);
}

static gboolean
pop3_store_connect (CamelService *service,
                    GError **error)
{
	CamelPOP3Store *store = (CamelPOP3Store *)service;
	gboolean reprompt = FALSE;
	CamelSession *session;
	gchar *errbuf = NULL;
	gint status;
	GError *local_error = NULL;

	session = camel_service_get_session (service);

	if (store->cache == NULL) {
		gchar *root;

		root = camel_session_get_storage_path (session, service, error);
		if (root) {
			store->cache = camel_data_cache_new (root, error);
			g_free(root);
			if (store->cache) {
				/* Ensure cache will never expire, otherwise it causes redownload of messages */
				camel_data_cache_set_expire_age (store->cache, -1);
				camel_data_cache_set_expire_access (store->cache, -1);
			}
		}
	}

	if (!connect_to_server_wrapper (service, error))
		return FALSE;

	while (1) {
		status = pop3_try_authenticate (
			service, reprompt, errbuf, &local_error);
		g_free (errbuf);
		errbuf = NULL;

		/* we only re-prompt if we failed to authenticate,
		 * any other error and we just abort */
		if (g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE)) {
			gchar *tmp = camel_utf8_make_valid (local_error->message);
			errbuf = g_markup_printf_escaped ("%s\n\n", tmp);
			g_free (tmp);

			g_clear_error (&local_error);

			g_free (service->url->passwd);
			service->url->passwd = NULL;
			reprompt = TRUE;
		} else
			break;
	}

	g_free (errbuf);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		camel_service_disconnect(service, TRUE, NULL);
		return FALSE;
	}

	/* Now that we are in the TRANSACTION state,
	 * try regetting the capabilities */
	store->engine->state = CAMEL_POP3_ENGINE_TRANSACTION;
	camel_pop3_engine_reget_capabilities (store->engine);

	return TRUE;
}

static gboolean
pop3_store_disconnect (CamelService *service,
                       gboolean clean,
                       GError **error)
{
	CamelServiceClass *service_class;
	CamelPOP3Store *store = CAMEL_POP3_STORE (service);

	if (clean) {
		CamelPOP3Command *pc;

		pc = camel_pop3_engine_command_new(store->engine, 0, NULL, NULL, "QUIT\r\n");
		while (camel_pop3_engine_iterate(store->engine, NULL) > 0)
			;
		camel_pop3_engine_command_free(store->engine, pc);
	}

	/* Chain up to parent's disconnect() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_pop3_store_parent_class);
	if (!service_class->disconnect (service, clean, error))
		return FALSE;

	g_object_unref (store->engine);
	store->engine = NULL;

	return TRUE;
}

static GList *
pop3_store_query_auth_types (CamelService *service,
                             GError **error)
{
	CamelServiceClass *service_class;
	CamelPOP3Store *store = CAMEL_POP3_STORE (service);
	GList *types = NULL;
	GError *local_error = NULL;

	/* Chain up to parent's query_auth_types() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_pop3_store_parent_class);
	types = service_class->query_auth_types (service, &local_error);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return NULL;
	}

	if (connect_to_server_wrapper (service, NULL)) {
		types = g_list_concat(types, g_list_copy(store->engine->auth));
		pop3_store_disconnect (service, TRUE, NULL);
	} else {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Could not connect to POP server %s"),
			service->url->host);
	}

	return types;
}

static gchar *
pop3_store_get_name (CamelService *service,
                     gboolean brief)
{
	if (brief)
		return g_strdup_printf (
			_("POP3 server %s"),
			service->url->host);
	else
		return g_strdup_printf (
			_("POP3 server for %s on %s"),
			service->url->user,
			service->url->host);
}

static CamelFolder *
pop3_store_get_folder (CamelStore *store,
                       const gchar *folder_name,
                       guint32 flags,
                       GError **error)
{
	if (g_ascii_strcasecmp (folder_name, "inbox") != 0) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID,
			_("No such folder '%s'."), folder_name);
		return NULL;
	}

	return camel_pop3_folder_new (store, error);
}

static CamelFolder *
pop3_store_get_trash (CamelStore *store,
                      GError **error)
{
	/* no-op */
	return NULL;
}

static CamelFolderInfo *
pop3_store_get_folder_info (CamelStore *store,
                            const gchar *top,
                            guint32 flags,
                            GError **error)
{
	g_set_error (
		error, CAMEL_STORE_ERROR,
		CAMEL_STORE_ERROR_NO_FOLDER,
		_("POP3 stores have no folder hierarchy"));

	return NULL;
}

static gboolean
pop3_store_can_refresh_folder (CamelStore *store,
                               CamelFolderInfo *info,
                               GError **error)
{
	/* any pop3 folder can be refreshed */
	return TRUE;
}

static void
camel_pop3_store_class_init (CamelPOP3StoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = pop3_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->connect = pop3_store_connect;
	service_class->disconnect = pop3_store_disconnect;
	service_class->query_auth_types = pop3_store_query_auth_types;
	service_class->get_name = pop3_store_get_name;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder = pop3_store_get_folder;
	store_class->get_trash = pop3_store_get_trash;
	store_class->get_folder_info = pop3_store_get_folder_info;
	store_class->can_refresh_folder = pop3_store_can_refresh_folder;
}

static void
camel_pop3_store_init (CamelPOP3Store *pop3_store)
{
}

/**
 * camel_pop3_store_expunge:
 * @store: the store
 * @error: return location for a #GError, or %NULL
 *
 * Expunge messages from the store. This will result in the connection
 * being closed, which may cause later commands to fail if they can't
 * reconnect.
 **/
void
camel_pop3_store_expunge (CamelPOP3Store *store,
                          GError **error)
{
	CamelPOP3Command *pc;

	pc = camel_pop3_engine_command_new (
		store->engine, 0, NULL, NULL, "QUIT\r\n");

	while (camel_pop3_engine_iterate(store->engine, NULL) > 0)
		;

	camel_pop3_engine_command_free(store->engine, pc);

	camel_service_disconnect (CAMEL_SERVICE (store), FALSE, error);
}

