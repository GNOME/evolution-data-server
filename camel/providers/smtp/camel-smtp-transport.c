/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-smtp-transport.c : class for a smtp transport */

/*
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-smtp-transport.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#undef MIN
#undef MAX

extern gint camel_verbose_debug;

#define d(x) (camel_verbose_debug ? (x) : 0)

/* Specified in RFC 821 */
#define SMTP_PORT  25
#define SMTPS_PORT 465

/* camel smtp transport class prototypes */
static gboolean smtp_send_to (CamelTransport *transport, CamelMimeMessage *message,
			      CamelAddress *from, CamelAddress *recipients, GError **error);

/* support prototypes */
static gboolean smtp_connect (CamelService *service, GError **error);
static gboolean smtp_disconnect (CamelService *service, gboolean clean, GError **error);
static GHashTable *esmtp_get_authtypes (const guchar *buffer);
static GList *query_auth_types (CamelService *service, GError **error);
static gchar *get_name (CamelService *service, gboolean brief);

static gboolean smtp_helo (CamelSmtpTransport *transport, GError **error);
static gboolean smtp_auth (CamelSmtpTransport *transport, const gchar *mech, GError **error);
static gboolean smtp_mail (CamelSmtpTransport *transport, const gchar *sender,
			   gboolean has_8bit_parts, GError **error);
static gboolean smtp_rcpt (CamelSmtpTransport *transport, const gchar *recipient, GError **error);
static gboolean smtp_data (CamelSmtpTransport *transport, CamelMimeMessage *message, GError **error);
static gboolean smtp_rset (CamelSmtpTransport *transport, GError **error);
static gboolean smtp_quit (CamelSmtpTransport *transport, GError **error);

static void smtp_set_error (CamelSmtpTransport *transport, const gchar *respbuf, GError **error);

G_DEFINE_TYPE (CamelSmtpTransport, camel_smtp_transport, CAMEL_TYPE_TRANSPORT)

static void
camel_smtp_transport_class_init (CamelSmtpTransportClass *class)
{
	CamelTransportClass *transport_class;
	CamelServiceClass *service_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->connect = smtp_connect;
	service_class->disconnect = smtp_disconnect;
	service_class->query_auth_types = query_auth_types;
	service_class->get_name = get_name;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to = smtp_send_to;
}

static void
camel_smtp_transport_init (CamelSmtpTransport *smtp)
{
	smtp->flags = 0;
	smtp->connected = FALSE;
}

static const gchar *
smtp_error_string (gint error)
{
	/* SMTP error codes grabbed from rfc821 */
	switch (error) {
	case 0:
		/* looks like a read problem, check errno */
		if (errno)
			return g_strerror (errno);
		else
			return _("Unknown");
	case 500:
		return _("Syntax error, command unrecognized");
	case 501:
		return _("Syntax error in parameters or arguments");
	case 502:
		return _("Command not implemented");
	case 504:
		return _("Command parameter not implemented");
	case 211:
		return _("System status, or system help reply");
	case 214:
		return _("Help message");
	case 220:
		return _("Service ready");
	case 221:
		return _("Service closing transmission channel");
	case 421:
		return _("Service not available, closing transmission channel");
	case 250:
		return _("Requested mail action okay, completed");
	case 251:
		return _("User not local; will forward to <forward-path>");
	case 450:
		return _("Requested mail action not taken: mailbox unavailable");
	case 550:
		return _("Requested action not taken: mailbox unavailable");
	case 451:
		return _("Requested action aborted: error in processing");
	case 551:
		return _("User not local; please try <forward-path>");
	case 452:
		return _("Requested action not taken: insufficient system storage");
	case 552:
		return _("Requested mail action aborted: exceeded storage allocation");
	case 553:
		return _("Requested action not taken: mailbox name not allowed");
	case 354:
		return _("Start mail input; end with <CRLF>.<CRLF>");
	case 554:
		return _("Transaction failed");

	/* AUTH error codes: */
	case 432:
		return _("A password transition is needed");
	case 534:
		return _("Authentication mechanism is too weak");
	case 538:
		return _("Encryption required for requested authentication mechanism");
	case 454:
		return _("Temporary authentication failure");
	case 530:
		return _("Authentication required");

	default:
		return _("Unknown");
	}
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#ifdef CAMEL_HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static gboolean
connect_to_server (CamelService *service,
		   const gchar *host, const gchar *serv, gint fallback_port,
                   gint ssl_mode,
                   GError **error)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	CamelSession *session;
	gchar *socks_host;
	gint socks_port;
	CamelStream *tcp_stream;
	gchar *respbuf = NULL;

	if (!CAMEL_SERVICE_CLASS (camel_smtp_transport_parent_class)->connect (service, error))
		return FALSE;

	/* set some smtp transport defaults */
	transport->flags = 0;
	transport->authtypes = NULL;

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
	} else {
		tcp_stream = camel_tcp_stream_raw_new ();
	}

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

	transport->connected = TRUE;

	/* get the localaddr - needed later by smtp_helo */
	transport->localaddr = camel_tcp_stream_get_local_address (CAMEL_TCP_STREAM (tcp_stream), &transport->localaddrlen);

	transport->ostream = tcp_stream;
	transport->istream = camel_stream_buffer_new (tcp_stream, CAMEL_STREAM_BUFFER_READ);

	/* Read the greeting, note whether the server is ESMTP or not. */
	do {
		/* Check for "220" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (error, _("Welcome response error: "));
			transport->connected = FALSE;
			return FALSE;
		}
		if (strncmp (respbuf, "220", 3)) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("Welcome response error: "));
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "220-" then loop again */
	g_free (respbuf);

	/* Try sending EHLO */
	transport->flags |= CAMEL_SMTP_TRANSPORT_IS_ESMTP;
	if (!smtp_helo (transport, error)) {
		if (!transport->connected)
			return FALSE;

		/* Fall back to HELO */
		g_clear_error (error);
		transport->flags &= ~CAMEL_SMTP_TRANSPORT_IS_ESMTP;

		if (!smtp_helo (transport, error)) {
			camel_service_disconnect ((CamelService *) transport, TRUE, NULL);

			return FALSE;
		}
	}

	/* clear any EHLO/HELO exception and assume that any SMTP errors encountered were non-fatal */
	g_clear_error (error);

	if (ssl_mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}

#ifdef CAMEL_HAVE_SSL
	if (!(transport->flags & CAMEL_SMTP_TRANSPORT_STARTTLS)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to connect to SMTP server %s in secure mode: %s"),
			service->url->host, _("STARTTLS not supported"));

		goto exception_cleanup;
	}

	d(fprintf (stderr, "sending : STARTTLS\r\n"));
	if (camel_stream_write (tcp_stream, "STARTTLS\r\n", 10, error) == -1) {
		g_prefix_error (error, _("STARTTLS command failed: "));
		goto exception_cleanup;
	}

	respbuf = NULL;

	do {
		/* Check for "220 Ready for TLS" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (error, _("STARTTLS command failed: "));
			transport->connected = FALSE;
			goto exception_cleanup;
		}
		if (strncmp (respbuf, "220", 3) != 0) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("STARTTLS command failed: "));
			g_free (respbuf);
			goto exception_cleanup;
		}
	} while (*(respbuf+3) == '-'); /* if we got "220-" then loop again */

	/* Okay, now toggle SSL/TLS mode */
	if (camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream)) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Failed to connect to SMTP server %s in secure mode: %s"),
			service->url->host, g_strerror (errno));
		goto exception_cleanup;
	}
#else
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Failed to connect to SMTP server %s in secure mode: %s"),
		service->url->host, _("SSL is not available in this build"));
	goto exception_cleanup;
#endif /* CAMEL_HAVE_SSL */

	/* We are supposed to re-EHLO after a successful STARTTLS to
           re-fetch any supported extensions. */
	if (!smtp_helo (transport, error)) {
		camel_service_disconnect ((CamelService *) transport, TRUE, NULL);

		return FALSE;
	}

	return TRUE;

 exception_cleanup:

	g_object_unref (transport->istream);
	transport->istream = NULL;
	g_object_unref (transport->ostream);
	transport->ostream = NULL;

	transport->connected = FALSE;

	return FALSE;
}

static struct {
	const gchar *value;
	const gchar *serv;
	gint fallback_port;
	gint mode;
} ssl_options[] = {
	{ "",              "smtps", SMTPS_PORT, MODE_SSL   },  /* really old (1.x) */
	{ "always",        "smtps", SMTPS_PORT, MODE_SSL   },
	{ "when-possible", "smtp",  SMTP_PORT, MODE_TLS   },
	{ "never",         "smtp",  SMTP_PORT, MODE_CLEAR },
	{ NULL,            "smtp",  SMTP_PORT, MODE_CLEAR },
};

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
		serv = (gchar *) "smtp";
		fallback_port = SMTP_PORT;
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
		fallback_port = 0;
	}

	return connect_to_server (service, service->url->host, serv, fallback_port, mode, error);
}

static gboolean
smtp_connect (CamelService *service, GError **error)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	gboolean has_authtypes;

	/* We (probably) need to check popb4smtp before we connect ... */
	if (service->url->authmech && !strcmp (service->url->authmech, "POPB4SMTP")) {
		gint truth;
		GByteArray *chal;
		CamelSasl *sasl;

		sasl = camel_sasl_new ("smtp", "POPB4SMTP", service);
		chal = camel_sasl_challenge (sasl, NULL, error);
		truth = camel_sasl_get_authenticated (sasl);
		if (chal)
			g_byte_array_free (chal, TRUE);
		g_object_unref (sasl);

		if (!truth)
			return FALSE;

		return connect_to_server_wrapper (service, error);
	}

	if (!connect_to_server_wrapper (service, error))
		return FALSE;

	/* check to see if AUTH is required, if so...then AUTH ourselves */
	has_authtypes = transport->authtypes ? g_hash_table_size (transport->authtypes) > 0 : FALSE;
	if (service->url->authmech && (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) && has_authtypes) {
		CamelSession *session = camel_service_get_session (service);
		CamelServiceAuthType *authtype;
		gboolean authenticated = FALSE;
		guint32 password_flags;
		gchar *errbuf = NULL;

		if (!g_hash_table_lookup (transport->authtypes, service->url->authmech)) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("SMTP server %s does not support "
				  "requested authentication type %s."),
				service->url->host, service->url->authmech);
			camel_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}

		authtype = camel_sasl_authtype (service->url->authmech);
		if (!authtype) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("No support for authentication type %s"),
				service->url->authmech);
			camel_service_disconnect (service, TRUE, NULL);
			return FALSE;
		}

		if (!authtype->need_password) {
			/* authentication mechanism doesn't need a password,
			   so if it fails there's nothing we can do */
			authenticated = smtp_auth (
				transport, authtype->authproto, error);
			if (!authenticated) {
				camel_service_disconnect (service, TRUE, NULL);
				return FALSE;
			}
		}

		password_flags = CAMEL_SESSION_PASSWORD_SECRET;

		/* keep trying to login until either we succeed or the user cancels */
		while (!authenticated) {
			GError *local_error = NULL;

			if (errbuf) {
				/* We need to un-cache the password before prompting again */
				g_free (service->url->passwd);
				service->url->passwd = NULL;
			}

			if (!service->url->passwd) {
				gchar *base_prompt;
				gchar *full_prompt;

				base_prompt = camel_session_build_password_prompt (
					"SMTP", service->url->user, service->url->host);

				if (errbuf != NULL)
					full_prompt = g_strconcat (errbuf, base_prompt, NULL);
				else
					full_prompt = g_strdup (base_prompt);

				service->url->passwd = camel_session_get_password (
					session, service, NULL, full_prompt,
					"password", password_flags, error);

				g_free (base_prompt);
				g_free (full_prompt);
				g_free (errbuf);
				errbuf = NULL;

				if (!service->url->passwd) {
					g_set_error (
						error, CAMEL_SERVICE_ERROR,
						CAMEL_SERVICE_ERROR_NEED_PASSWORD,
						_("Need password for authentication"));	
					camel_service_disconnect (service, TRUE, NULL);
					return FALSE;
				}
			}

			authenticated = smtp_auth (
				transport, authtype->authproto, &local_error);
			if (!authenticated) {
				if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
				    g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE)) {
					g_free (service->url->passwd);
					service->url->passwd = NULL;

					if (local_error)
						g_clear_error (&local_error);

					return FALSE;
				}

				errbuf = g_markup_printf_escaped (
					_("Unable to authenticate "
					  "to SMTP server.\n%s\n\n"),
					local_error ? local_error->message : _("Unknown error"));
				g_clear_error (&local_error);

				g_free (service->url->passwd);
				service->url->passwd = NULL;
			}

			/* Force a password prompt on the next pass, in
			 * case we have an invalid password cached.  This
			 * avoids repeated authentication attempts using
			 * the same invalid password. */
			password_flags |= CAMEL_SESSION_PASSWORD_REPROMPT;
		}
	}

	return TRUE;
}

static void
authtypes_free (gpointer key, gpointer value, gpointer data)
{
	g_free (value);
}

static gboolean
smtp_disconnect (CamelService *service,
                 gboolean clean,
                 GError **error)
{
	CamelServiceClass *service_class;
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);

	/*if (!service->connected)
	 *	return TRUE;
	 */

	if (transport->connected && clean) {
		/* send the QUIT command to the SMTP server */
		smtp_quit (transport, NULL);
	}

	/* Chain up to parent's disconnect() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_smtp_transport_parent_class);
	if (!service_class->disconnect (service, clean, error))
		return FALSE;

	if (transport->authtypes) {
		g_hash_table_foreach (transport->authtypes, authtypes_free, NULL);
		g_hash_table_destroy (transport->authtypes);
		transport->authtypes = NULL;
	}

	if (transport->istream) {
		g_object_unref (transport->istream);
		transport->istream = NULL;
	}

	if (transport->ostream) {
		g_object_unref (transport->ostream);
		transport->ostream = NULL;
	}

	g_free(transport->localaddr);
	transport->localaddr = NULL;

	transport->connected = FALSE;

	return TRUE;
}

static GHashTable *
esmtp_get_authtypes (const guchar *buffer)
{
	const guchar *start, *end;
	GHashTable *table = NULL;

	/* advance to the first token */
	start = buffer;
	while (isspace ((gint) *start) || *start == '=')
		start++;

	if (!*start)
		return NULL;

	table = g_hash_table_new (g_str_hash, g_str_equal);

	for (; *start; ) {
		gchar *type;

		/* advance to the end of the token */
		end = start;
		while (*end && !isspace ((gint) *end))
			end++;

		type = g_strndup ((gchar *) start, end - start);
		g_hash_table_insert (table, type, type);

		/* advance to the next token */
		start = end;
		while (isspace ((gint) *start))
			start++;
	}

	return table;
}

static GList *
query_auth_types (CamelService *service, GError **error)
{
	CamelSmtpTransport *transport = CAMEL_SMTP_TRANSPORT (service);
	CamelServiceAuthType *authtype;
	GList *types, *t, *next;

	if (!connect_to_server_wrapper (service, error))
		return NULL;

	if (!transport->authtypes) {
		smtp_disconnect (service, TRUE, NULL);
		return NULL;
	}

	types = g_list_copy (service->provider->authtypes);
	for (t = types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!g_hash_table_lookup (transport->authtypes, authtype->authproto)) {
			types = g_list_remove_link (types, t);
			g_list_free_1 (t);
		}
	}

	smtp_disconnect (service, TRUE, NULL);

	return types;
}

static gchar *
get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf (_("SMTP server %s"), service->url->host);
	else {
		return g_strdup_printf (_("SMTP mail delivery via %s"),
					service->url->host);
	}
}

static gboolean
smtp_send_to (CamelTransport *transport, CamelMimeMessage *message,
	      CamelAddress *from, CamelAddress *recipients,
	      GError **error)
{
	CamelSmtpTransport *smtp_transport = CAMEL_SMTP_TRANSPORT (transport);
	CamelInternetAddress *cia;
	gboolean has_8bit_parts;
	const gchar *addr;
	gint i, len;

	if (!smtp_transport->connected) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Cannot send message: service not connected."));
		return FALSE;
	}

	if (!camel_internet_address_get (CAMEL_INTERNET_ADDRESS (from), 0, NULL, &addr)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot send message: sender address not valid."));
		return FALSE;
	}

	camel_operation_start (NULL, _("Sending message"));

	/* find out if the message has 8bit mime parts */
	has_8bit_parts = camel_mime_message_has_8bit_parts (message);

	/* rfc1652 (8BITMIME) requires that you notify the ESMTP daemon that
	   you'll be sending an 8bit mime message at "MAIL FROM:" time. */
	if (!smtp_mail (smtp_transport, addr, has_8bit_parts, error)) {
		camel_operation_end (NULL);
		return FALSE;
	}

	len = camel_address_length (recipients);
	if (len == 0) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot send message: no recipients defined."));
		camel_operation_end (NULL);
		return FALSE;
	}

	cia = CAMEL_INTERNET_ADDRESS (recipients);
	for (i = 0; i < len; i++) {
		gchar *enc;

		if (!camel_internet_address_get (cia, i, NULL, &addr)) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot send message: "
				  "one or more invalid recipients"));
			camel_operation_end (NULL);
			return FALSE;
		}

		enc = camel_internet_address_encode_address(NULL, NULL, addr);
		if (!smtp_rcpt (smtp_transport, enc, error)) {
			g_free(enc);
			camel_operation_end (NULL);
			return FALSE;
		}
		g_free(enc);
	}

	if (!smtp_data (smtp_transport, message, error)) {
		camel_operation_end (NULL);
		return FALSE;
	}

	/* reset the service for our next transfer session */
	smtp_rset (smtp_transport, NULL);

	camel_operation_end (NULL);

	return TRUE;
}

static const gchar *
smtp_next_token (const gchar *buf)
{
	const guchar *token;

	token = (const guchar *) buf;
	while (*token && !isspace ((gint) *token))
		token++;

	while (*token && isspace ((gint) *token))
		token++;

	return (const gchar *) token;
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : (c) - 'A' + 10)

/*
 * example (rfc2034):
 * 5.1.1 Mailbox "nosuchuser" does not exist
 *
 * The human-readable status code is what we want. Since this text
 * could possibly be encoded, we must decode it.
 *
 * "xtext" is formally defined as follows:
 *
 *   xtext = *( xchar / hexchar / linear-white-space / comment )
 *
 *   xchar = any ASCII CHAR between "!" (33) and "~" (126) inclusive,
 *        except for "+", "\" and "(".
 *
 * "hexchar"s are intended to encode octets that cannot be represented
 * as plain text, either because they are reserved, or because they are
 * non-printable.  However, any octet value may be represented by a
 * "hexchar".
 *
 *   hexchar = ASCII "+" immediately followed by two upper case
 *        hexadecimal digits
 */
static gchar *
smtp_decode_status_code (const gchar *in, gsize len)
{
	guchar *inptr, *outptr;
	const guchar *inend;
	gchar *outbuf;

	outbuf = (gchar *) g_malloc (len + 1);
	outptr = (guchar *) outbuf;

	inptr = (guchar *) in;
	inend = inptr + len;
	while (inptr < inend) {
		if (*inptr == '+') {
			if (isxdigit (inptr[1]) && isxdigit (inptr[2])) {
				*outptr++ = HEXVAL (inptr[1]) * 16 + HEXVAL (inptr[2]);
				inptr += 3;
			} else
				*outptr++ = *inptr++;
		} else
			*outptr++ = *inptr++;
	}

	*outptr = '\0';

	return outbuf;
}

/* converts string str to local encoding, thinking it's in utf8.
   If fails, then converts all character greater than 127 to hex values.
   Also those under 32, other than \n, \r, \t.
   Note that the c is signed character, so all characters above 127 have
   negative value.
*/
static void
convert_to_local (GString *str)
{
	gchar *buf;

	buf = g_locale_from_utf8 (str->str, str->len, NULL, NULL, NULL);

	if (!buf) {
		gint i;
		gchar c;
		GString *s = g_string_new_len (str->str, str->len);

		g_string_truncate (str, 0);

		for (i = 0; i < s->len; i++) {
			c = s->str[i];

			if (c < 32 && c != '\n' && c != '\r' && c != '\t')
				g_string_append_printf (str, "<%X%X>", (c >> 4) & 0xF, c & 0xF);
			else
				g_string_append_c (str, c);
		}

		g_string_free (s, TRUE);
	} else {
		g_string_truncate (str, 0);
		g_string_append (str, buf);

		g_free (buf);
	}
}

static void
smtp_set_error (CamelSmtpTransport *transport,
                const gchar *respbuf,
                GError **error)
{
	const gchar *token, *rbuf = respbuf;
	gchar *buffer = NULL;
	GString *string;
	gint errnum;

	if (!respbuf) {
	fake_status_code:
		errnum = respbuf ? atoi (respbuf) : 0;
		if (errnum == 0 && errno == EINTR)
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				"%s", smtp_error_string (errnum));
		else
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", smtp_error_string (errnum));
	} else {
		string = g_string_new ("");
		do {
			if (transport->flags & CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES)
				token = smtp_next_token (rbuf + 4);
			else
				token = rbuf + 4;

			if (*token == '\0') {
				g_free (buffer);
				g_string_free (string, TRUE);
				goto fake_status_code;
			}

			g_string_append (string, token);
			if (*(rbuf + 3) == '-') {
				g_free (buffer);
				buffer = camel_stream_buffer_read_line (
					CAMEL_STREAM_BUFFER (transport->istream), NULL);
				g_string_append_c (string, '\n');
			} else {
				g_free (buffer);
				buffer = NULL;
			}

			rbuf = buffer;
		} while (rbuf);

		convert_to_local (string);
		if (!(transport->flags & CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES) && string->len) {
			string->str = g_strstrip (string->str);
			string->len = strlen (string->str);

			if (!string->len) {
				g_string_free (string, TRUE);
				goto fake_status_code;
			}

			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", string->str);

			g_string_free (string, TRUE);
		} else {
			buffer = smtp_decode_status_code (string->str, string->len);
			g_string_free (string, TRUE);
			if (!buffer)
				goto fake_status_code;

			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				"%s", buffer);

			g_free (buffer);
		}
	}
}

static gboolean
smtp_helo (CamelSmtpTransport *transport, GError **error)
{
	gchar *name = NULL, *cmdbuf = NULL, *respbuf = NULL;
	const gchar *token, *numeric = NULL;
	struct sockaddr *addr;
	socklen_t addrlen;

	/* these are flags that we set, so unset them in case we
	   are being called a second time (ie, after a STARTTLS) */
	transport->flags &= ~(CAMEL_SMTP_TRANSPORT_8BITMIME |
			      CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES |
			      CAMEL_SMTP_TRANSPORT_STARTTLS);

	if (transport->authtypes) {
		g_hash_table_foreach (transport->authtypes, authtypes_free, NULL);
		g_hash_table_destroy (transport->authtypes);
		transport->authtypes = NULL;
	}

	camel_operation_start_transient (NULL, _("SMTP Greeting"));

	addr = transport->localaddr;
	addrlen = transport->localaddrlen;

	if (camel_getnameinfo (addr, addrlen, &name, NULL, NI_NUMERICHOST, NULL) != 0) {
		name = g_strdup ("localhost.localdomain");
	} else {
		if (addr->sa_family == AF_INET6)
			numeric = "IPv6:";
		else
			numeric = "";
	}

	token = (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) ? "EHLO" : "HELO";
	if (numeric)
		cmdbuf = g_strdup_printf("%s [%s%s]\r\n", token, numeric, name);
	else
		cmdbuf = g_strdup_printf("%s %s\r\n", token, name);
	g_free (name);

	d(fprintf (stderr, "sending : %s", cmdbuf));
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("HELO command failed: "));
		camel_operation_end (NULL);

		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (error, _("HELO command failed: "));
			transport->connected = FALSE;
			camel_operation_end (NULL);
			return FALSE;
		}
		if (strncmp (respbuf, "250", 3)) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("HELO command failed: "));
			camel_operation_end (NULL);
			g_free (respbuf);
			return FALSE;
		}

		token = respbuf + 4;

		if (transport->flags & CAMEL_SMTP_TRANSPORT_IS_ESMTP) {
			if (!strncmp (token, "8BITMIME", 8)) {
				transport->flags |= CAMEL_SMTP_TRANSPORT_8BITMIME;
			} else if (!strncmp (token, "ENHANCEDSTATUSCODES", 19)) {
				transport->flags |= CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES;
			} else if (!strncmp (token, "STARTTLS", 8)) {
				transport->flags |= CAMEL_SMTP_TRANSPORT_STARTTLS;
			} else if (!strncmp (token, "AUTH", 4)) {
				if (!transport->authtypes || transport->flags & CAMEL_SMTP_TRANSPORT_AUTH_EQUAL) {
					/* Don't bother parsing any authtypes if we already have a list.
					 * Some servers will list AUTH twice, once the standard way and
					 * once the way Microsoft Outlook requires them to be:
					 *
					 * 250-AUTH LOGIN PLAIN DIGEST-MD5 CRAM-MD5
					 * 250-AUTH=LOGIN PLAIN DIGEST-MD5 CRAM-MD5
					 *
					 * Since they can come in any order, parse each list that we get
					 * until we parse an authtype list that does not use the AUTH=
					 * format. We want to let the standard way have priority over the
					 * broken way.
					 **/

					if (token[4] == '=')
						transport->flags |= CAMEL_SMTP_TRANSPORT_AUTH_EQUAL;
					else
						transport->flags &= ~CAMEL_SMTP_TRANSPORT_AUTH_EQUAL;

					/* parse for supported AUTH types */
					token += 5;

					if (transport->authtypes) {
						g_hash_table_foreach (transport->authtypes, authtypes_free, NULL);
						g_hash_table_destroy (transport->authtypes);
					}

					transport->authtypes = esmtp_get_authtypes ((const guchar *) token);
				}
			}
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	camel_operation_end (NULL);

	return TRUE;
}

static gboolean
smtp_auth (CamelSmtpTransport *transport,
           const gchar *mech,
           GError **error)
{
	CamelService *service;
	gchar *cmdbuf, *respbuf = NULL, *challenge;
	gboolean auth_challenge = FALSE;
	CamelSasl *sasl = NULL;

	service = CAMEL_SERVICE (transport);

	camel_operation_start_transient (NULL, _("SMTP Authentication"));

	sasl = camel_sasl_new ("smtp", mech, service);
	if (!sasl) {
		camel_operation_end (NULL);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Error creating SASL authentication object."));
		return FALSE;
	}

	challenge = camel_sasl_challenge_base64 (sasl, NULL, error);
	if (challenge) {
		auth_challenge = TRUE;
		cmdbuf = g_strdup_printf ("AUTH %s %s\r\n", mech, challenge);
		g_free (challenge);
	} else {
		cmdbuf = g_strdup_printf ("AUTH %s\r\n", mech);
	}

	d(fprintf (stderr, "sending : %s", cmdbuf));
	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("AUTH command failed: "));
		goto lose;
	}
	g_free (cmdbuf);

	respbuf = camel_stream_buffer_read_line (
		CAMEL_STREAM_BUFFER (transport->istream), error);

	while (!camel_sasl_get_authenticated (sasl)) {
		if (!respbuf) {
			g_prefix_error (error, _("AUTH command failed: "));
			transport->connected = FALSE;
			goto lose;
		}

		/* the server challenge/response should follow a 334 code */
		if (strncmp (respbuf, "334", 3) != 0) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("AUTH command failed: "));
			goto lose;
		}

		if (FALSE) {
		broken_smtp_server:
			d(fprintf (stderr, "Your SMTP server's implementation of the %s SASL\n"
				   "authentication mechanism is broken. Please report this to the\n"
				   "appropriate vendor and suggest that they re-read rfc2554 again\n"
				   "for the first time (specifically Section 4).\n",
				   mech));
		}

		/* eat whtspc */
		for (challenge = respbuf + 4; isspace (*challenge); challenge++);

		challenge = camel_sasl_challenge_base64 (sasl, challenge, error);
		if (challenge == NULL)
			goto break_and_lose;

		g_free (respbuf);

		/* send our challenge */
		cmdbuf = g_strdup_printf ("%s\r\n", challenge);
		g_free (challenge);
		d(fprintf (stderr, "sending : %s", cmdbuf));
		if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
			g_free (cmdbuf);
			goto lose;
		}
		g_free (cmdbuf);

		/* get the server's response */
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
	}

	if (respbuf == NULL)
		goto lose;

	/* Work around broken SASL implementations. */
	if (auth_challenge && strncmp (respbuf, "334", 3) == 0)
		goto broken_smtp_server;

	/* If our authentication data was rejected, destroy the
	 * password so that the user gets prompted to try again. */
	if (strncmp (respbuf, "535", 3) == 0) {
		g_free (service->url->passwd);
		service->url->passwd = NULL;
	}

	/* Catch any other errors. */
	if (strncmp (respbuf, "235", 3) != 0) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Bad authentication response from server."));
		goto lose;
	}

	g_object_unref (sasl);
	camel_operation_end (NULL);

	g_free (respbuf);

	return TRUE;

 break_and_lose:
	/* Get the server out of "waiting for continuation data" mode. */
	d(fprintf (stderr, "sending : *\n"));
	camel_stream_write (transport->ostream, "*\r\n", 3, NULL);
	respbuf = camel_stream_buffer_read_line (
		CAMEL_STREAM_BUFFER (transport->istream), NULL);
	d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));

 lose:
	g_object_unref (sasl);
	camel_operation_end (NULL);

	g_free (respbuf);

	return FALSE;
}

static gboolean
smtp_mail (CamelSmtpTransport *transport, const gchar *sender, gboolean has_8bit_parts, GError **error)
{
	/* we gotta tell the smtp server who we are. (our email addy) */
	gchar *cmdbuf, *respbuf = NULL;

	if (transport->flags & CAMEL_SMTP_TRANSPORT_8BITMIME && has_8bit_parts)
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s> BODY=8BITMIME\r\n", sender);
	else
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s>\r\n", sender);

	d(fprintf (stderr, "sending : %s", cmdbuf));

	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("MAIL FROM command failed: "));
		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);
		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250 Sender OK..." */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (error, _("MAIL FROM command failed: "));
			camel_service_disconnect (
				CAMEL_SERVICE (transport), FALSE, NULL);
			return FALSE;
		}
		if (strncmp (respbuf, "250", 3)) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("MAIL FROM command failed: "));
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_rcpt (CamelSmtpTransport *transport, const gchar *recipient, GError **error)
{
	/* we gotta tell the smtp server who we are going to be sending
	 * our email to */
	gchar *cmdbuf, *respbuf = NULL;

	cmdbuf = g_strdup_printf ("RCPT TO:<%s>\r\n", recipient);

	d(fprintf (stderr, "sending : %s", cmdbuf));

	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("RCPT TO command failed: "));
		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);

		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250 Recipient OK..." */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (
				error, _("RCPT TO <%s> failed: "), recipient);
			camel_service_disconnect (
				CAMEL_SERVICE (transport), FALSE, NULL);
			return FALSE;
		}
		if (strncmp (respbuf, "250", 3)) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (
				error, _("RCPT TO <%s> failed: "), recipient);
			g_free (respbuf);

			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_data (CamelSmtpTransport *transport, CamelMimeMessage *message, GError **error)
{
	struct _camel_header_raw *header, *savedbcc, *n, *tail;
	CamelBestencEncoding enctype = CAMEL_BESTENC_8BIT;
	CamelStream *filtered_stream;
	gchar *cmdbuf, *respbuf = NULL;
	CamelMimeFilter *filter;
	CamelStreamNull *null;
	gint ret;

	/* If the server doesn't support 8BITMIME, set our required encoding to be 7bit */
	if (!(transport->flags & CAMEL_SMTP_TRANSPORT_8BITMIME))
		enctype = CAMEL_BESTENC_7BIT;

	/* FIXME: should we get the best charset too?? */
	/* Changes the encoding of all mime parts to fit within our required
	   encoding type and also force any text parts with long lines (longer
	   than 998 octets) to wrap by QP or base64 encoding them. */
	camel_mime_message_set_best_encoding (message, CAMEL_BESTENC_GET_ENCODING, enctype);

	cmdbuf = g_strdup ("DATA\r\n");

	d(fprintf (stderr, "sending : %s", cmdbuf));

	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("DATA command failed: "));
		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);
		return FALSE;
	}
	g_free (cmdbuf);

	respbuf = camel_stream_buffer_read_line (
		CAMEL_STREAM_BUFFER (transport->istream), error);
	if (respbuf == NULL) {
		g_prefix_error (error, _("DATA command failed: "));
		camel_service_disconnect (
			CAMEL_SERVICE (transport), FALSE, NULL);
		return FALSE;
	}
	if (strncmp (respbuf, "354", 3) != 0) {
		/* We should have gotten instructions on how to use the DATA
		 * command: 354 Enter mail, end with "." on a line by itself
		 */
		smtp_set_error (transport, respbuf, error);
		g_prefix_error (error, _("DATA command failed: "));
		g_free (respbuf);
		return FALSE;
	}

	g_free (respbuf);
	respbuf = NULL;

	/* unlink the bcc headers */
	savedbcc = NULL;
	tail = (struct _camel_header_raw *) &savedbcc;

	header = (struct _camel_header_raw *) &CAMEL_MIME_PART (message)->headers;
	n = header->next;
	while (n != NULL) {
		if (!g_ascii_strcasecmp (n->name, "Bcc")) {
			header->next = n->next;
			tail->next = n;
			n->next = NULL;
			tail = n;
		} else {
			header = n;
		}

		n = header->next;
	}

	/* find out how large the message is... */
	null = CAMEL_STREAM_NULL (camel_stream_null_new ());
	camel_data_wrapper_write_to_stream (
		CAMEL_DATA_WRAPPER (message), CAMEL_STREAM (null), NULL);

	filtered_stream = camel_stream_filter_new (transport->ostream);

	/* setup progress reporting for message sending... */
	filter = camel_mime_filter_progress_new (NULL, null->written);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), filter);
	g_object_unref (filter);
	g_object_unref (null);

	/* setup LF->CRLF conversion */
	filter = camel_mime_filter_crlf_new (
		CAMEL_MIME_FILTER_CRLF_ENCODE,
		CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), filter);
	g_object_unref (filter);

	/* write the message */
	ret = camel_data_wrapper_write_to_stream (
		CAMEL_DATA_WRAPPER (message), filtered_stream, error);

	/* restore the bcc headers */
	header->next = savedbcc;

	if (ret == -1) {
		g_prefix_error (error, _("DATA command failed: "));

		g_object_unref (filtered_stream);

		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);
		return FALSE;
	}

	camel_stream_flush (filtered_stream, NULL);
	g_object_unref (filtered_stream);

	/* terminate the message body */

	d(fprintf (stderr, "sending : \\r\\n.\\r\\n\n"));

	if (camel_stream_write (transport->ostream, "\r\n.\r\n", 5, error) == -1) {
		g_prefix_error (error, _("DATA command failed: "));
		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);
		return FALSE;
	}

	do {
		/* Check for "250 Sender OK..." */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (error, _("DATA command failed: "));
			camel_service_disconnect (
				CAMEL_SERVICE (transport), FALSE, NULL);
			return FALSE;
		}
		if (strncmp (respbuf, "250", 3) != 0) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("DATA command failed: "));
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_rset (CamelSmtpTransport *transport, GError **error)
{
	/* we are going to reset the smtp server (just to be nice) */
	gchar *cmdbuf, *respbuf = NULL;

	cmdbuf = g_strdup ("RSET\r\n");

	d(fprintf (stderr, "sending : %s", cmdbuf));

	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("RSET command failed: "));
		camel_service_disconnect ((CamelService *) transport, FALSE, NULL);
		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "250" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);
		if (respbuf == NULL) {
			g_prefix_error (error, _("RSET command failed: "));
			camel_service_disconnect (
				CAMEL_SERVICE (transport), FALSE, NULL);
			return FALSE;
		}
		if (strncmp (respbuf, "250", 3) != 0) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("RSET command failed: "));
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "250-" then loop again */
	g_free (respbuf);

	return TRUE;
}

static gboolean
smtp_quit (CamelSmtpTransport *transport, GError **error)
{
	/* we are going to reset the smtp server (just to be nice) */
	gchar *cmdbuf, *respbuf = NULL;

	cmdbuf = g_strdup ("QUIT\r\n");

	d(fprintf (stderr, "sending : %s", cmdbuf));

	if (camel_stream_write (transport->ostream, cmdbuf, strlen (cmdbuf), error) == -1) {
		g_free (cmdbuf);
		g_prefix_error (error, _("QUIT command failed: "));
		return FALSE;
	}
	g_free (cmdbuf);

	do {
		/* Check for "221" */
		g_free (respbuf);
		respbuf = camel_stream_buffer_read_line (
			CAMEL_STREAM_BUFFER (transport->istream), error);

		d(fprintf (stderr, "received: %s\n", respbuf ? respbuf : "(null)"));
		if (respbuf == NULL) {
			g_prefix_error (error, _("QUIT command failed: "));
			transport->connected = FALSE;
			return FALSE;
		}
		if (strncmp (respbuf, "221", 3) != 0) {
			smtp_set_error (transport, respbuf, error);
			g_prefix_error (error, _("QUIT command failed: "));
			g_free (respbuf);
			return FALSE;
		}
	} while (*(respbuf+3) == '-'); /* if we got "221-" then loop again */

	g_free (respbuf);

	return TRUE;
}
